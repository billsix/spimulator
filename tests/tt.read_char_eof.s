# SPIM S20 MIPS simulator.
# Test that read_char (syscall 12) returns -1 on EOF.
#
# Driven via piped stdin with exactly 3 bytes: "abc"
# (no newline, no trailing characters).  The program reads
# bytes in a loop; after the third byte the next read_char
# should return -1.
#
# Invoke:
#   echo -n "abc" | spimulator -f tt.read_char_eof.s
#
# Expected stdout (then a "Passed all tests" line):
#   abc
#   Passed all tests

        .data
passMsg:    .asciiz "Passed all tests\n"
failMsg:    .asciiz "Failed test\n"

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # byte count

loop:
        li $v0, 12                   # read_char
        syscall

        # If $v0 == -1, EOF.
        bltz $v0, eof

        # Echo the byte to stdout.
        move $a0, $v0
        li $v0, 11                   # print_char
        syscall

        addi $s1, $s1, 1             # count++
        j loop

eof:
        # Expect exactly 3 bytes consumed before EOF.
        li $t0, 3
        bne $s1, $t0, fail

        # Print the success sentinel for the docker test driver.
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
