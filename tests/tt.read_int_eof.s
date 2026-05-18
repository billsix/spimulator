# SPIM S20 MIPS simulator.
# Test that read_int (syscall 5) sets $a3 = 1 on EOF.
#
# Driven via piped stdin with two ints followed by EOF:
#   "5\n7\n"
#
# The program reads ints in a loop, accumulating the sum;
# on EOF (the third read should set $a3) it stops.  Expected
# sum is 12.
#
# Invoke:
#   printf '5\n7\n' | spimulator -f tt.read_int_eof.s
#
# Expects "Passed all tests".  Fails on any mismatch in
# $a3-signal or sum.

        .data
passMsg:    .asciiz "Passed all tests\n"
failMsg:    .asciiz "Failed test\n"

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # running sum
        li $s2, 0                    # count of ints read

loop:
        li $v0, 5                    # read_int
        syscall

        # $a3 == 1 -> EOF
        bnez $a3, eof

        add $s1, $s1, $v0            # sum += value
        addi $s2, $s2, 1
        j loop

eof:
        # Expect: count == 2, sum == 12.
        li $t0, 2
        bne $s2, $t0, fail
        li $t0, 12
        bne $s1, $t0, fail

        li $v0, 4
        la $a0, passMsg
        syscall

        move $ra, $s0
        jr $ra

fail:
        li $v0, 4
        la $a0, failMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall
