# SPIM S20 MIPS simulator.
# Test that a program lacking a `main` symbol — the entry point
# the default exception handler jumps to — exits with a non-zero
# status rather than 0.
#
# Historical regression: spim would print "The following
# symbols are undefined: main", then try to jump there anyway,
# print a runtime-error message, and exit 0.
#
# Driver (from a shell, not from this file):
#
#   $ spimulator -f tt.missing_main.s > /dev/null 2>&1 ; echo $?
#   1
#
# This file has valid asm but no `main` label.

        .text
        .globl somewhere_else
somewhere_else:
        li $v0, 17
        li $a0, 0
        syscall
