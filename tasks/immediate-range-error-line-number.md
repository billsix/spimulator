# The "immediate value out of range" error names the line after the bad one

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** `tasks/archive/2026/11/11/alu-bare-test-assembler-strictness.md`

## BLUF

When an instruction's immediate does not fit in 16 bits, the assembler reports the error one line
too late. Done means the message names the instruction's own line, in the C engine and in all
three ports, with a regression test that pins the line number.

## Context

- Reproduce: a file whose line 3 is `addi $4 $0 0xff98` (after `.text` and `main:`) prints
  `spim: (parser) immediate value (65432) out of range (-32768 .. 32767) on line 4`. The same
  happens when line 3 is the last line of the file. `Instruction not yet supported` errors name
  the right line, so the off-by-one is specific to this check.
- The message is built in `src/pseudo-op.c` (search `out of range (%d .. %d)`). It probably reads
  the scanner's current line after the parser has consumed the instruction's newline, but this has
  not been checked.
- The ports match C (`tasks/reference/engine-ports-design.md`), so each reports the same wrong line
  and must change with C, or the port suites fail.

## Plan

1. Find where the line number comes from and pass the instruction's own line.
2. Add a regression case that checks the reported line number.
3. Make the same change in `port/python/`, `port/haskell/` and `port/rust/`; run `meson test`.
