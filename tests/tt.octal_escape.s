# SPIM S20 MIPS simulator.
# Test that .asciiz "\ooo" octal escapes decode correctly.
#
# Historical bug: scanner.l copy_str shifted the high octal digit
# by 3 bits instead of 6, so "\134" (intended: backslash, 0x5C)
# produced 0x24 ('$') instead.  This test guards against
# regression of that fix.
#
# Invoke:
#   spimulator -f tt.octal_escape.s
#
# Expected stdout:
#   \$P
#   Passed all tests

        .data
s134:   .asciiz "\134"               # 0x5C  '\'
s044:   .asciiz "\044"               # 0x24  '$'
s120:   .asciiz "\120"               # 0x50  'P'
nl:     .asciiz "\n"
passMsg:    .asciiz "Passed all tests\n"
failMsg:    .asciiz "Failed test\n"

        .text
        .globl main
main:
        move $s0, $ra

        # Print the three decoded chars (visual sanity check).
        la $a0, s134
        li $v0, 4
        syscall
        la $a0, s044
        li $v0, 4
        syscall
        la $a0, s120
        li $v0, 4
        syscall
        la $a0, nl
        li $v0, 4
        syscall

        # Verify s134 byte 0 == 0x5C
        lbu $t0, s134
        li $t1, 0x5C
        bne $t0, $t1, fail

        # Verify s044 byte 0 == 0x24
        lbu $t0, s044
        li $t1, 0x24
        bne $t0, $t1, fail

        # Verify s120 byte 0 == 0x50
        lbu $t0, s120
        li $t1, 0x50
        bne $t0, $t1, fail

        la $a0, passMsg
        li $v0, 4
        syscall
        move $ra, $s0
        jr $ra

fail:
        la $a0, failMsg
        li $v0, 4
        syscall
        li $a0, 1
        li $v0, 17
        syscall
