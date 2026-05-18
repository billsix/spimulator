# (MIT license abbreviated)


# C source — see 33-nl.c
#
#     for each line: print "     N\t<line>" with N right-aligned
#     in 6 chars, then \t, then the line itself.


#PURPOSE:  Number lines.  New asm pattern: width-padded int
#          output via a `print_padded` helper that counts
#          digits and emits leading spaces.

        .data
usageMsg:   .asciiz "usage: nl [FILE|-]\n"
errMsg:     .asciiz "nl: cannot open "
nlMsg:      .asciiz "\n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s4, $0
        li $s2, 1                    # line_num = 1
        li $s3, 0                    # line_started = 0

        li $t0, 1
        beq $a0, $t0, read_loop
        li $t0, 2
        bne $a0, $t0, usage

        lw $s4, 4($a1)
        lb $t0, 0($s4)
        bne $t0, '-', do_open
        lb $t0, 1($s4)
        beq $t0, $0, read_loop

do_open:
        move $a0, $s4
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, after_read

        # if (!line_started) print padded N + '\t'
        bnez $s3, emit_char
        move $a0, $s2
        li $a1, 6
        jal print_padded
        li $a0, '\t'
        li $v0, 11
        syscall
        li $s3, 1

emit_char:
        lb $a0, oneByte
        li $v0, 11
        syscall
        bne $a0, '\n', read_loop
        addi $s2, $s2, 1
        li $s3, 0
        j read_loop

after_read:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        move $a0, $s4
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


# ---------- print_padded(int n, int width) --------------------------
# Count digits, emit (width - digits) leading spaces, then
# print_int(n).
# Input:  $a0 = n, $a1 = width
# Trashes: $a0, $v0, $t0, $t1, $t2
print_padded:
        move $t0, $a0                # save n
        move $t1, $a1                # save width

        # Count digits in $t0
        li $t2, 1                    # digits = 1
        move $a0, $t0                # work value
pp_digit_loop:
        li $a1, 10
        bge $a0, $a1, pp_more_digits
        j pp_have_digits
pp_more_digits:
        div $a0, $a1
        mflo $a0
        addi $t2, $t2, 1
        j pp_digit_loop

pp_have_digits:
        # spaces = width - digits
        sub $t1, $t1, $t2
pp_space_loop:
        blez $t1, pp_print_n
        li $a0, ' '
        li $v0, 11
        syscall
        addi $t1, $t1, -1
        j pp_space_loop

pp_print_n:
        move $a0, $t0
        li $v0, 1
        syscall
        jr $ra
