# SPIM S20 MIPS simulator.
# Test that read_int (syscall 5) reads space-separated ints from one
# piped line, one int per call (scanf-style), instead of discarding
# everything after the first int on the line.
#
# Driven via piped stdin mixing separators, with a negative value:
#   "43 3 12\n-5 8\n"
#
# The program reads ints in a loop, accumulating sum and count; on
# $a3 = 1 (EOF) it stops.  Expected count 5, sum 43+3+12-5+8 = 61.
#
# Invoke:
#   printf '43 3 12\n-5 8\n' | spimulator -f tt.read_int_space_sep.s
#
# Expects "Passed all tests".  Fails on any mismatch in count or sum.

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

        # $a3 == 1 -> EOF / no more numbers
        bnez $a3, eof

        add $s1, $s1, $v0            # sum += value
        addi $s2, $s2, 1
        j loop

eof:
        # Expect: count == 5, sum == 61.
        li $t0, 5
        bne $s2, $t0, fail
        li $t0, 61
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
