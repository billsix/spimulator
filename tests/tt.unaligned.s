# SPIM S20 MIPS simulator.
# Test that a runtime exception (here: an unaligned word load,
# ExcCode 4 = AdEL) produces a non-zero shell exit status.
#
# Historical regression: spim's default exception handler
# printed "Exception 4 [Address error in inst/data fetch]
# occurred and ignored" and then continued past the faulting
# instruction; main() returned spim_return_value = 0, so a
# Makefile/CI couldn't tell that the program had crashed.
#
# Convention used: 128 + ExcCode, mirroring the shell's
# "128 + signum" for signal-killed processes.  An unaligned
# load is ExcCode 4, so expected exit status = 132.
#
# Driver (from a shell, not from this file):
#
#   $ spimulator -f tt.unaligned.s > /dev/null 2>&1 ; echo $?
#   132

        .text
        .globl main
main:
        lw $t0, 1($zero)      # misaligned load — must trigger AdEL
        li $a0, 0
        li $v0, 17
        syscall
