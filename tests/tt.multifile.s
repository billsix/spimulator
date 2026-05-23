# Multi-`-f` CLI regression test (see
# tasks/cli-multi-file-load.md).
#
# Calls helper_double (defined in tt.multifile.helper.s).  The
# test driver invokes spim with both -f flags in both orders:
#
#   spimulator -f tt.multifile.helper.s -f tt.multifile.s
#   spimulator -f tt.multifile.s        -f tt.multifile.helper.s
#
# Both must succeed — load order shouldn't matter because the
# parser does forward-reference resolution across files.
#
# Verifies:
#   helper_double(21) == 42
#
# Prints "Passed all tests\n" on success so the test driver's
# expect_sentinel can grep for it.

        .data
passMsg:    .asciiz "Passed all tests\n"
failMsg:    .asciiz "Failed test\n"

        .text
        .globl  main
main:
        # Save $ra across the jal — the simulator's exception
        # handler called us, so we have to return to it cleanly.
        move    $s0, $ra

        li      $a0, 21
        jal     helper_double       # cross-file call resolves at parse time
        li      $t0, 42
        bne     $v0, $t0, fail

        li      $v0, 4              # syscall 4 = print_string
        la      $a0, passMsg
        syscall

        li      $v0, 0              # main returns 0 → shell exit 0
        move    $ra, $s0
        jr      $ra

fail:
        li      $v0, 4
        la      $a0, failMsg
        syscall
        li      $v0, 17             # syscall 17 = exit2 with status in $a0
        li      $a0, 1
        syscall
