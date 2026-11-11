# Give the xc1/prefx rows their real COP1X encodings

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.3 F11

## BLUF

Six encodings in `include/opcodes.tsv` belong to two rows each. The table's decode column (`*`) now
picks one on purpose. For three of them it picks the wrong one: 0x46000008/09/0f decode to
`swxc1`/`sdxc1`/`prefx`, while the real COP1 instructions at those encodings are
`round.l.s`/`trunc.l.s`/`floor.w.s`. The xc1/prefx instructions are COP1X (major opcode 0x13,
`0x4c…`), so their rows are wrong. Done means:
- each row has its MIPS32 encoding, checked against GNU `as`;
- the COP1X decode ORs in funct;
- no duplicates remain and the stars are gone;
- all four engines and tree-sitter are regenerated.

## Context

- The baseline review's §3.9 and F8 (`tasks/review/2026/11/10/repo-review.md`) listed the six:
  swxc1/round.l.s 0x46000008, sdxc1/trunc.l.s 0x46000009, suxc1/trunc.w.s 0x4600000d,
  prefx/floor.w.s 0x4600000f, lwxc1/madd.s 0x4c000000, ldxc1/madd.d 0x4c000001.
  `cut -f4 include/opcodes.tsv | sort | uniq -d` still lists six.
- Symptom: store a `floor.w.s $f0,$f2` word into text and execute it. It prints "Unknown instruction
  type: 532", a fatal error.
- `src/instruction.c` `inst_decode`, `case 0x4c000000`, ORs in only bits 21-25, not funct.
- Oracle: `mips64-linux-gnu-as -mabi=32 -mips32r2 -mfp64 -EB`, then `mips64-linux-gnu-objdump -d`
  (`binutils-mips64-linux-gnu`).
- Method: `tasks/reference/opcode-table-generator.md`. Gate with `BUILD_TREE_SITTER=1` (`CLAUDE.md`).

## Plan

1. Look up each of the 12 instructions with GNU `as`; fix the rows; drop the duplicate stars.
2. Fix the COP1X decode in `src/instruction.c` and in each port's counterpart.
3. Add a regression test that stores each word into text and disassembles or executes it.
4. `python3 tools/gen_opcodes.py` (or `make generate`, once `tasks/archive/2026/11/11/opcode-generate-workflow.md` is
   done), then `make image` with `BUILD_TREE_SITTER=1`.

## Open questions

None.
