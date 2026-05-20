# SPIM S20 MIPS simulator.
# Test that integer divide-by-zero raises ExcCode 9 (Bp, Breakpoint)
# and the default exception handler reports + continues.
#
# spim's `div` pseudo-op expands to: bne $rt, $0, +3; nop; trap;
# div $rd, $rs, $rt; mflo $rd.  The `trap` macro encodes a `break`
# instruction with the divide-by-zero pattern; the default handler
# (src/exceptions.s) treats Bp (ExcCode 9) as informational —
# prints "Exception 9 [Breakpoint] occurred and ignored" to stderr
# and resumes after the offending instruction.
#
# So the expected end-to-end behavior is:
#   - stderr contains "Exception 9".
#   - main reaches the exit syscall and returns 0.
#
# Driver: see tests/run-test.sh divide_by_zero case.

        .text
        .globl main
main:
        li $t0, 10
        li $t1, 0
        div $t2, $t0, $t1    # 10 / 0 → trap → ExcCode 9 → "ignored"
        # Resume here after the handler returns.  Exit cleanly.
        li $a0, 0
        li $v0, 17
        syscall
