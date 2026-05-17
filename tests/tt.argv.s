# SPIM S20 MIPS simulator.
# Test for command-line argv handling (see
# tasks/argv-command-line-handling.md).
#
# Invoke (after `meson install`):
#   spimulator -f tt.argv.s alpha beta gamma
#
# Expects:
#   argc          == 4              ($a0 on entry to main)
#   argv[1][0]    == 'a'            (first letter of "alpha")
#   argv[2][0]    == 'b'            (first letter of "beta")
#   argv[3][0]    == 'g'            (first letter of "gamma")
#
# Any mismatch prints "Failed test" and exits; all-pass prints
# "Passed all tests" so the docker test driver can grep for it.

        .data
passMsg:    .asciiz "Passed all tests\n"
failMsg:    .asciiz "Failed test\n"

        .text
        .globl main
main:
        # Park the runtime's return address and the entry-time
        # argc/argv into $s registers so the syscalls below don't
        # clobber them.  ($a0/$a1 carry argc/argv per the
        # exceptions.s startup.)
        move $s0, $ra
        move $s1, $a0                # argc
        move $s2, $a1                # argv

        # ---- Test 1: argc == 4 ----
        li $t0, 4
        bne $s1, $t0, fail

        # ---- Test 2: argv[1][0] == 'a' ----
        lw $t0, 4($s2)               # argv[1]  (each pointer is 4 bytes)
        lb $t1, ($t0)                # *argv[1]
        bne $t1, 'a', fail

        # ---- Test 3: argv[2][0] == 'b' ----
        lw $t0, 8($s2)               # argv[2]
        lb $t1, ($t0)
        bne $t1, 'b', fail

        # ---- Test 4: argv[3][0] == 'g' ----
        lw $t0, 12($s2)              # argv[3]
        lb $t1, ($t0)
        bne $t1, 'g', fail

        # All passed.
        li $v0, 4                    # syscall 4 = print_string
        la $a0, passMsg
        syscall

        move $ra, $s0
        jr $ra                       # return to the runtime

fail:
        li $v0, 4
        la $a0, failMsg
        syscall

        li $v0, 10                   # exit
        syscall
