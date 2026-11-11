# The "immediate value out of range" error named the line after the bad one

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Found by:** `tasks/archive/2026/11/11/alu-bare-test-assembler-strictness.md`

## Outcome

The range error now names the instruction's own line, in the C engine and in all three ports.

**The cause:** `check_imm_range` and `check_uimm_range` (`src/pseudo-op.c`) printed the scanner's
`line_no`. Reading past the immediate scans the newline that ends the instruction, and scanning a
newline advances `line_no`, so the error named the next line. At the end of a file with no
trailing newline it happened to be right.

**The fix:** both checks take a `line` argument. `parse_imm16`, `parse_uimm16` and the `ror`/`rol`
immediate path (`src/parser.c`) capture `line_no` before parsing the immediate, and pass it. The
Python (`pseudo_op.py`, `parser.py`), Haskell (`Parser.hs`) and Rust (`pseudo_op.rs`, `parser.rs`)
ports make the same change.

## Verification

- The new regression test `imm_range` (`tests/tt.imm_range.s`, `addi $t0, $zero, 0xff98` on line 7)
  expects "… out of range (-32768 .. 32767) on line 7", and exit 0. It fails on the old C code.
- It runs against each port too. Regression plus the three port suites: 271/271.
