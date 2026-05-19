# SPIM S20 MIPS simulator.
# Test that main's $v0 return value becomes the shell exit
# status, mirroring `int main() { return N; }` on real Unix.
#
# Historical regression: exceptions.s' __start used to issue
# `syscall 10` after main returned, discarding $v0.  Any C-on-
# MIPS demo doing `return 42` exited 0.  Fix sets $a0 = $v0
# and uses syscall 17 (exit2) instead.
#
# This test sets $v0 = 42 and `jr $ra`s.  Expected shell exit
# status: 42.
#
# Driver (from a shell, not from this file):
#
#   $ spimulator -f tt.return_value.s ; echo $?
#   42
#
# Also test status=0 separately by running a program that
# `jr $ra`s with $v0=0 — see tt.return_value_zero.s if you
# split this.

        .text
        .globl main
main:
        li $v0, 42
        jr $ra
