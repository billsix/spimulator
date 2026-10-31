# (MIT license abbreviated)


# C source — see uniq.c
#
#     Collapse adjacent duplicate lines.  stdin-or-file argv shape.


# Invocations:
#   spimulator -f uniq.asm             # reads stdin
#   spimulator -f uniq.asm -           # reads stdin (explicit "-")
#   spimulator -f uniq.asm /etc/motd   # reads the file
#   printf 'a\na\nb\nb\nc\n' | spimulator -f uniq.asm   # => a b c


#PURPOSE:  uniq.  Maintain a `prevLine` buffer; compare each new line to it.
#          Emit line + '\n' if it differs from prev, then copy current into prev.
#
#          STACK-VISIBLE STYLE: main's C locals fd and the argv[1] path live in
#          stack slots.  The line buffers (curLine/prevLine) and their lengths
#          (curLen/prevLen) are file-scope state, so they stay in .data — shared
#          between main and the comparison helper.  main makes `jal emit_if_new`,
#          so it saves $ra.  emit_if_new is a leaf helper (syscalls only) that
#          works on the .data globals with transient scratch registers.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr      | register   | C type     | bytes | role / notes
#  ------------+------------------+------------+------------+-------+---------------------------
#  fd          | -12($fp)         | $t0 (copy) | int        |    4  | STDIN (0) or opened file descriptor
#  argv1       | -16($fp)         | $t0 (copy) | char *     |    4  | argv[1]; "-"/FILE, kept for error msg
#  curLine/    | *Line (.data)    | —          | char[1024] | 1024  | file-scope current/previous line buffers
#   prevLine   |                  |            |            |       |
#  curLen/     | *Len (.data)     | —          | int        |    4  | file-scope current/previous lengths
#   prevLen    |                  |            |            |       |  (prevLen = -1 means "no previous line")
#  (saved $ra) | -4($fp)          | —          | code addr  |    4  | jal emit_if_new
#  (saved $fp) | -8($fp)          | —          | frame ptr  |    4  | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/uniq/uniq.input
#  stdout:
#  | a
#  | b
#  | c

        .data
usageMsg:   .asciiz "usage: uniq [FILE|-]\n"
errMsg:     .asciiz "uniq: cannot open "
nlMsg:      .asciiz "\n"
prevLine:   .space 1024
curLine:    .space 1024
oneByte:    .space 1
prevLen:    .word -1                 # -1 = no previous line yet
curLen:     .word 0

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # -> -4($fp)
        sw $fp, 8($sp)               # -> -8($fp)
        addiu $fp, $sp, 16

        li $t0, 0
        sw $t0, -12($fp)             # fd = STDIN (0)
        sw $zero, -16($fp)           # argv1 = 0

        li $t0, 1
        beq $a0, $t0, read_setup
        li $t0, 2
        bne $a0, $t0, usage
        lw $t0, 4($a1)               # argv[1]
        sw $t0, -16($fp)             # argv1
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_setup

do_open:
        lw $a0, -16($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -12($fp)             # fd

read_setup:
read_loop:
        li $v0, 14
        lw $a0, -12($fp)             # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append

        # newline: emit_if_new; curLen = 0
        jal emit_if_new
        la $t1, curLen
        sw $0, ($t1)
        j read_loop

append:
        # if (cur_len < 1024) cur_line[cur_len++] = c
        la $t1, curLen
        lw $t2, ($t1)
        li $t3, 1024
        bge $t2, $t3, read_loop
        la $t3, curLine
        add $t3, $t3, $t2
        sb $t0, ($t3)
        addi $t2, $t2, 1
        sw $t2, ($t1)
        j read_loop

after_read:
        # if (cur_len > 0) emit_if_new
        la $t1, curLen
        lw $t2, ($t1)
        blez $t2, close_and_exit
        jal emit_if_new

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
        addiu $sp, $sp, 16
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        lw $a0, -16($fp)             # argv1
        syscall
        li $v0, 4
        la $a0, nlMsg
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


# ---------- emit_if_new (leaf helper) ----------
# Compare curLine[0..curLen] to prevLine[0..prevLen].  If they differ (or there
# is no prev yet), write the current line + '\n' and copy current into prev.
# Operates entirely on the .data globals with transient scratch registers; no
# frame needed (no jal; $ra is untouched).
emit_if_new:
        # First-line case: prevLen == -1 -> always emit
        la $t0, prevLen
        lw $t1, ($t0)
        bltz $t1, do_emit

        # Compare lengths
        la $t0, curLen
        lw $t2, ($t0)
        bne $t1, $t2, do_emit

        # Lengths equal; compare bytes
        la $t3, prevLine
        la $t4, curLine
        li $t5, 0                    # index
cmp_loop:
        bge $t5, $t1, lines_match
        add $t6, $t3, $t5
        add $t7, $t4, $t5
        lb $t8, ($t6)
        lb $t9, ($t7)
        bne $t8, $t9, do_emit
        addi $t5, $t5, 1
        j cmp_loop

lines_match:
        jr $ra                       # same as prev — do nothing

do_emit:
        # write(STDOUT, curLine, curLen)
        li $v0, 15
        li $a0, 1
        la $a1, curLine
        la $t0, curLen
        lw $a2, ($t0)
        syscall

        # write(STDOUT, "\n", 1)
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

        # Copy curLine to prevLine; prevLen = curLen
        la $t0, curLen
        lw $t1, ($t0)
        la $t0, prevLen
        sw $t1, ($t0)

        la $t3, curLine
        la $t4, prevLine
        li $t5, 0
copy_loop:
        bge $t5, $t1, copy_done
        add $t6, $t3, $t5
        add $t7, $t4, $t5
        lb $t8, ($t6)
        sb $t8, ($t7)
        addi $t5, $t5, 1
        j copy_loop
copy_done:
        jr $ra
