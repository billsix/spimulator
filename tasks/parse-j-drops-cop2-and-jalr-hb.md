# `cop2` and `jalr.hb` assemble to nothing, silently

**Status:** in progress (branch `dispatch-follow-ups`)
**Priority:** 4
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`, while converting `parse_j`'s chains
(`src/parser.c`) to `switch` statements

## BLUF

`parse_j` handles `j`, `jr`, `jal` and `jalr`, but two more opcodes reach it, and for both it emits
no instruction and reports no error:
- `cop2`, whose row in `include/opcodes.tsv` has type `J_TYPE_INST`;
- `jalr.hb`, whose row has type `RD_RS_INST`, which `parse_asm_code` also sends to `parse_j`.

A program using either one assembles one instruction short. Done means both assemble to the
encoding GNU `as` gives (or report an error, if the maintainer prefers that for `cop2`), in the C
engine and all three ports, with regression tests.

## Context

- **Reproduce:** assemble `jalr.hb $t0`, `jalr.hb $s0, $t0` and `cop2 0x10`, then `disasm`. Only the
  instructions after them appear.
- **Oracle:** GNU binutils for MIPS (`binutils-mips64-linux-gnu`, in both sandboxes' base lists):
  `mips64-linux-gnu-as -mabi=32 -mips32r2 -EB`, then `mips64-linux-gnu-objdump -d`. It gives
  `0100fc09 jalr.hb t0`, `01008409 jalr.hb s0,t0` and `4a000010 c2 0x10`.
- `jalr.hb` is `jalr` with the hazard-barrier bit (bit 10) set. `cop2`'s operand is a 25-bit
  coprocessor function field, not a jump target, so `J_TYPE_INST` is the wrong operand shape for it.
- **Where it happens:** the `default` branches of `parse_j`'s three switches name these two
  opcodes. The ports follow the C engine, so they drop them too. A fix must change all four
  engines, or the port suites fail.
- Any change to `opcodes.tsv` must be gated with `BUILD_TREE_SITTER=1` (`CLAUDE.md`).

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. **`cop2` is rejected with an error** (option (b)): spim has no coprocessor 2. A teaching
   simulator should say it doesn't support an instruction, not drop it. The error names the
   instruction and the reason.
2. **`jalr.hb` assembles** to the encoding GNU `as` gives: `jalr` with the hazard-barrier bit
   (bit 10) set, in both the one-register and two-register forms.
3. All four engines change together: C, then the Python, Haskell and Rust ports. Each case gets a
   regression test, in the C suite and therefore in every port suite.
