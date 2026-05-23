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
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        li $s2, 10                   # n = 10
        move $s3, $0                 # file_arg = NULL
        move $s4, $a1                # argv

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
        lw $s3, 4($s4)
        j after_argv

argc_3:
        # head -n N (no file)
        lw $a0, 4($s4)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $a0, 8($s4)
        jal atoi
        move $s2, $v0
        j after_argv

argc_4:
        lw $a0, 4($s4)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $a0, 8($s4)
        jal atoi
        move $s2, $v0
        lw $s3, 12($s4)

after_argv:
        # bounds: 1 <= n <= 64
        li $t0, 1
        blt $s2, $t0, badn
        li $t0, 64
        bgt $s2, $t0, badn

        # if file_arg && !is_dash open
        beqz $s3, read_setup
        lb $t0, 0($s3)
        bne $t0, '-', open_file
        lb $t0, 1($s3)
        beq $t0, $0, read_setup

open_file:
        move $a0, $s3
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_setup:
        li $s5, 0                    # slot = 0
        li $s6, 0                    # total = 0
        li $s7, 0                    # cur_line_len = 0

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        lb $t0, oneByte
        bne $t0, '\n', append_char

        # newline: commit cur_line to ring[slot]
        # ring[slot] = ring + slot * 1024
        sll $t1, $s5, 10             # slot * 1024
        la $t2, ring
        add $t2, $t2, $t1            # &ring[slot]

        # Copy cur_line[0..cur_line_len] into ring[slot]
        la $t3, curLine
        li $t4, 0                    # i
copy_in:
        bge $t4, $s7, copy_in_done
        add $t5, $t3, $t4
        lb $t6, ($t5)
        add $t5, $t2, $t4
        sb $t6, ($t5)
        addi $t4, $t4, 1
        j copy_in
copy_in_done:
        # ringLen[slot] = cur_line_len
        sll $t1, $s5, 2
        la $t2, ringLen
        add $t2, $t2, $t1
        sw $s7, ($t2)

        # slot = (slot + 1) % n
        addi $s5, $s5, 1
        bne $s5, $s2, slot_ok
        li $s5, 0
slot_ok:
        addi $s6, $s6, 1             # total++
        li $s7, 0                    # cur_line_len = 0
        j read_loop

append_char:
        # if (cur_line_len < 1024) cur_line[cur_line_len++] = c
        li $t1, 1024
        bge $s7, $t1, read_loop
        la $t1, curLine
        add $t1, $t1, $s7
        sb $t0, ($t1)
        addi $s7, $s7, 1
        j read_loop

after_read:
        # count = min(total, n);  start = (total <= n) ? 0 : slot
        ble $s6, $s2, full_history
        move $t0, $s2                # count = n
        move $t1, $s5                # start = slot
        j emit_loop_setup

full_history:
        move $t0, $s6                # count = total
        li $t1, 0                    # start = 0

emit_loop_setup:
        # Use $s4 = count, $s5 = start (clobbering argv since we're done with it)
        # No wait, $s5 = slot is already used.  Use new regs.
        # Actually $s4 (argv) we still need? No — we're past argv parsing.
        # Reuse $s4 = count, $s5 = i_loop... wait that clobbers slot.
        # OK use $t* — we don't need $t* after this point.
        # Let $t2 = count, $t3 = start, $t4 = i.  $s* preserved.
        move $t2, $t0                # count
        move $t3, $t1                # start
        li $t4, 0                    # i = 0

emit_loop:
        bge $t4, $t2, after_ring_emit

        # s = (start + i) % n
        add $t5, $t3, $t4
        rem $t5, $t5, $s2            # s = (start+i) % n

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
        blez $s7, close_and_exit
        li $v0, 15
        li $a0, 1
        la $a1, curLine
        move $a2, $s7
        syscall
        li $v0, 15
        li $a0, 1
        la $a1, nlMsg
        li $a2, 1
        syscall

close_and_exit:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s3
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
