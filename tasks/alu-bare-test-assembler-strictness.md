# Make upstream's ALU torture test `tt.alu.bare.s` run: loosen the assembler or edit the test

**Status:** proposed — needs go-ahead (one decision, Open questions)
**Priority:** 6
**Difficulty:** 3
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/10/repo-review.md §6.1 (F7)

## BLUF

`tests/tt.alu.bare.s` is James Larus's ALU torture test for bare-machine mode. Upstream ran it as
`spim -bare -noexception -file tt.alu.bare.s` (`git show 6cb6a08:spim/Makefile`, lines 160–165). In
the fork, 19 of its lines do not assemble, so it has never run. Done means it runs in the regression
suite under `-bare -noexception` and prints `Passed all tests`.

## Context

The 19 errors, from `spimulator -bare -noexception -f tests/tt.alu.bare.s`, have two causes:
- **Immediates out of range (9 lines).** Examples: `addi $2, $3, 65432` on lines 414–426, and
  `ori`/`xori` with `-1` on lines 588, 818, 1439, 1443 and 1445. Upstream's assembler silently
  truncated these to 16 bits; the fork reports `immediate value (65432) out of range`. The fork's
  check is right for a teaching assembler, which should say when a constant does not fit.
- **Coprocessor 0, 2 and 3 instructions the fork does not have (10 lines).** `mtc3`, `mfc3`,
  `ctc3`, `lwc3`, `swc3` and `swc0`, around lines 1601–1779. These are MIPS-I instructions that
  MIPS32 removed (coprocessor 3 is gone, and `lwc3` became `pref`), and the fork's opcode table
  follows MIPS32 (`include/opcodes.h` has `mtc2`/`lwc2`/`swc2`, not the c3 forms). The parser
  reports `Instruction not yet supported`.

`tt.fpu.bare.s` (the FPU twin) passes and runs as the `fpu_bare` test since 2026-11-10.

## Open questions

1. How should `tt.alu.bare.s` be made to run?
   - (a) Edit the test: write the immediates in range (`-104` for `65432`, `0xffff` for `-1`) and
     delete or rewrite the coprocessor-3 sections, with a header comment saying what changed from
     upstream and why.
   - (b) Loosen the assembler to accept and truncate out-of-range immediates under `-bare`, and add
     the MIPS-I coprocessor-3 instructions.

   Recommendation: (a). The fork's range check is a teaching feature, MIPS32 has no coprocessor 3,
   and the test file's upstream origin is recorded in git (`30efcf3`).
