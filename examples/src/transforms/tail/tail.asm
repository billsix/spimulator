# (MIT license abbreviated)


# C source — see tail.c
#
#     Ring buffer of N line slots (max N=64, line max 1024).
#     Write-index modulo N keeps the last N complete lines.


# Invocations:
#   spimulator -f tail.asm                       # stdin, last 10 lines
#   spimulator -f tail.asm -n 3                  # stdin, last 3 lines
#   spimulator -f tail.asm -n 5 /etc/services    # file, last 5 lines
#   seq 1 100 | spimulator -f tail.asm -n 4      # pipe in, last 4 lines


#PURPOSE:  tail -n N [FILE|-] via a ring buffer.
#
#          STACK-VISIBLE STYLE: all of main's C locals — fd, the line count n,
#          the file_arg pointer, the argv vector, the ring write index `slot`,
#          the `total` line count, and the `cur_line_len` — live in stack slots;
#          a scratch register holds only a working COPY.  Homing them also frees
#          the emit phase's count/start/i to be plain transients (the old version
#          had to juggle $s* registers there).  The ring buffer, its per-slot
#          lengths, and the current-line buffer are file-scope, so they stay in
#          .data.  main makes jals (str_eq, atoi), so it saves $ra; those are
#          leaf helpers.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable    | memory addr    | register   | C type    | bytes | role / notes
#  --------------+----------------+------------+-----------+-------+---------------------------
#  fd            | -12($fp)       | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  n             | -16($fp)       | $t1 (copy) | int       |   4   | number of trailing lines (1..64)
#  file_arg      | -20($fp)       | $t0 (copy) | char *    |   4   | NULL, "-", or FILE from argv
#  argv          | -24($fp)       | $t0 (copy) | char **   |   4   | entry $a1, kept across the jals
#  slot          | -28($fp)       | $t7 (copy) | int       |   4   | ring write index (mod n)
#  total         | -32($fp)       | $t7 (copy) | int       |   4   | total lines committed
#  cur_line_len  | -36($fp)       | $t8 (copy) | int       |   4   | bytes in the current (unterminated) line
#  ring/ringLen/ | .data          | —          | buffers   |       | file-scope ring of lines + per-slot lengths
#   curLine      |                |            |           |       |
#  (saved $ra)   | -4($fp)        | —          | code addr |   4   | jal str_eq / jal atoi
#  (saved $fp)   | -8($fp)        | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: -n 2
#  stdin: transforms/tail/tail.input
#  stdout:
#  | 4
#  | 5

        .data
usageMsg:   .asciiz "usage: tail [-n N] [FILE|-]\n"
badNMsg:    .asciiz "tail: N must be 1..64\n"
errMsg:     .asciiz "tail: cannot open "
nlMsg:      .asciiz "\n"
flagN:      .asciiz "-n"
ring:       .space 65536            # 64 lines * 1024 bytes
ringLen:    .space 256              # 64 ints
oneByte:    .space 1
curLine:    .space 1024

        .text
        .globl main
main:
        addiu $sp, $sp, -40
        sw $ra, 36($sp)              # -> -4($fp)
        sw $fp, 32($sp)              # -> -8($fp)
        addiu $fp, $sp, 40

        li $t0, 0
        sw $t0, -12($fp)             # fd = STDIN (0)
        li $t0, 10
        sw $t0, -16($fp)             # n = 10
        sw $zero, -20($fp)           # file_arg = NULL
        sw $a1, -24($fp)             # argv

        li $t0, 1
        beq $a0, $t0, after_argv
        li $t0, 2
        beq $a0, $t0, argc_2
        li $t0, 3
        beq $a0, $t0, argc_3
        li $t0, 4
        beq $a0, $t0, argc_4
        j usage

argc_2:
        lw $t0, -24($fp)
        lw $t0, 4($t0)
        sw $t0, -20($fp)             # file_arg = argv[1]
        j after_argv

argc_3:
        # tail -n N (no file)
        lw $t0, -24($fp)
        lw $a0, 4($t0)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $t0, -24($fp)
        lw $a0, 8($t0)
        jal atoi
        sw $v0, -16($fp)             # n
        j after_argv

argc_4:
        lw $t0, -24($fp)
        lw $a0, 4($t0)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $t0, -24($fp)
        lw $a0, 8($t0)
        jal atoi
        sw $v0, -16($fp)             # n
        lw $t0, -24($fp)
        lw $t0, 12($t0)
        sw $t0, -20($fp)             # file_arg = argv[3]

after_argv:
        # bounds: 1 <= n <= 64
        lw $t1, -16($fp)             # n
        li $t0, 1
        blt $t1, $t0, badn
        li $t0, 64
        bgt $t1, $t0, badn

        # if file_arg && !is_dash open
        lw $t0, -20($fp)             # file_arg
        beqz $t0, read_setup
        lb $t1, 0($t0)
        bne $t1, '-', open_file
        lb $t1, 1($t0)
        beq $t1, $0, read_setup

open_file:
        lw $a0, -20($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -12($fp)             # fd

read_setup:
        li $t0, 0
        sw $t0, -28($fp)             # slot = 0
        sw $t0, -32($fp)             # total = 0
        sw $t0, -36($fp)             # cur_line_len = 0

read_loop:
        li $v0, 14
        lw $a0, -12($fp)             # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append_char

        # newline: commit cur_line to ring[slot]
        lw $t7, -28($fp)             # slot
        sll $t1, $t7, 10             # slot * 1024
        la $t2, ring
        add $t2, $t2, $t1            # &ring[slot]
        la $t3, curLine
        li $t4, 0                    # i
        lw $t8, -36($fp)             # cur_line_len
copy_in:
        bge $t4, $t8, copy_in_done
        add $t5, $t3, $t4
        lb $t6, ($t5)
        add $t5, $t2, $t4
        sb $t6, ($t5)
        addi $t4, $t4, 1
        j copy_in
copy_in_done:
        # ringLen[slot] = cur_line_len
        lw $t7, -28($fp)             # slot
        sll $t1, $t7, 2
        la $t2, ringLen
        add $t2, $t2, $t1
        lw $t8, -36($fp)             # cur_line_len
        sw $t8, ($t2)

        # slot = (slot + 1) % n
        lw $t7, -28($fp)
        addi $t7, $t7, 1
        lw $t1, -16($fp)             # n
        bne $t7, $t1, slot_ok
        li $t7, 0
slot_ok:
        sw $t7, -28($fp)             # slot
        lw $t7, -32($fp)             # total
        addi $t7, $t7, 1
        sw $t7, -32($fp)             # total++
        sw $zero, -36($fp)           # cur_line_len = 0
        j read_loop

append_char:
        # if (cur_line_len < 1024) cur_line[cur_line_len++] = c
        lw $t8, -36($fp)             # cur_line_len
        li $t1, 1024
        bge $t8, $t1, read_loop
        la $t1, curLine
        add $t1, $t1, $t8
        sb $t0, ($t1)
        addi $t8, $t8, 1
        sw $t8, -36($fp)             # cur_line_len++
        j read_loop

after_read:
        # count = min(total, n);  start = (total <= n) ? 0 : slot
        lw $t6, -32($fp)             # total
        lw $t7, -16($fp)             # n
        ble $t6, $t7, full_history
        move $t2, $t7                # count = n
        lw $t3, -28($fp)             # start = slot
        j emit_loop_setup
full_history:
        move $t2, $t6                # count = total
        li $t3, 0                    # start = 0

emit_loop_setup:
        li $t4, 0                    # i = 0  ($t2=count, $t3=start, $t4=i transient)

emit_loop:
        bge $t4, $t2, after_ring_emit

        # s = (start + i) % n
        add $t5, $t3, $t4
        lw $t7, -16($fp)             # n
        rem $t5, $t5, $t7

        # write ring[s], ringLen[s] bytes
        sll $t6, $t5, 10
        la $t7, ring
        add $t7, $t7, $t6            # &ring[s]
        sll $t6, $t5, 2
        la $t8, ringLen
        add $t8, $t8, $t6
        lw $t9, ($t8)                # len

        li $v0, 15
        li $a0, 1
        move $a1, $t7
        move $a2, $t9
        syscall

        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

        addi $t4, $t4, 1
        j emit_loop

after_ring_emit:
        # Trailing partial line if any
        lw $t8, -36($fp)             # cur_line_len
        blez $t8, close_and_exit
        li $v0, 15
        li $a0, 1
        la $a1, curLine
        lw $a2, -36($fp)             # cur_line_len
        syscall
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

close_and_exit:
        lw $t0, -12($fp)             # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -12($fp)
        syscall
exit_ok:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 40
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        lw $a0, -20($fp)             # file_arg
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

badn:
        li $v0, 4
        la $a0, badNMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- str_eq (leaf helper) — $v0 = 1 if equal, 0 if not --------
str_eq:
        lb $t0, ($a0)
        lb $t1, ($a1)
        bne $t0, $t1, str_eq_no
        beq $t0, $0, str_eq_yes
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j str_eq
str_eq_yes:
        li $v0, 1
        jr $ra
str_eq_no:
        li $v0, 0
        jr $ra


# ---------- atoi (leaf helper) — parse decimal int from $a0 ----------
atoi:
        li $v0, 0
        li $t1, 1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
