# The other paired-single (`.ps`) instructions assemble but can't run

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** `tasks/archive/2026/11/11/paired-single-compare-encoding.md`, which removed the 16
`c.cond.ps` compares

## BLUF

`include/opcodes.tsv` still has 22 paired-single rows that aren't compares: 19 ending `.ps`
(`abs.ps`, `add.ps`, `alnv.ps`, `madd.ps`, `mov.ps`, `movf.ps`, `movn.ps`, `movt.ps`, `movz.ps`,
`msub.ps`, `mul.ps`, `neg.ps`, `nmadd.ps`, `nmsub.ps`, `pll.ps`, `plu.ps`, `pul.ps`, `puu.ps`,
`sub.ps`) and 3 conversions (`cvt.ps.s`, `cvt.s.pl`, `cvt.s.pu`).
- **All of them assemble, and none of them run.** `run.c` has no case for any of them, so running
  `add.ps $f0, $f2, $f4` prints `Unknown instruction type: 311`.
- **Three are also encoded wrongly.** `abs.ps`, `add.ps` and `sub.ps` put format field 0x13 in
  their rows (`0x4660000X`), where GNU `as` puts paired-single at 0x16
  (`46c41000 add.ps $f0,$f2,$f4`). The other 19 match GNU `as`.

Done means one of two outcomes, the maintainer's choice:
- (a) remove the 22 rows, as was done for the compares, so they are rejected at assembly time like
  any unsupported instruction ("Instruction not yet supported");
- (b) implement paired-single execution and fix the three encodings.

Either way, all four engines and the tree-sitter grammar change together.

## Context

- **The compares were removed for the same reasons:** the engine can't run them, and nothing in
  `examples/` or `tests/` uses paired-single.
  `tasks/archive/2026/11/11/paired-single-compare-encoding.md` has the method: delete the rows,
  regenerate with `python3 tools/gen_opcodes.py` (what `make generate` runs) or `make generate`,
  and gate with `BUILD_TREE_SITTER=1`.
- **Oracle:** GNU binutils for MIPS, `mips64-linux-gnu-as -mabi=32 -mips32r2 -mfp64 -EB`, then
  `mips64-linux-gnu-objdump -d`.

## Open questions

1. **Remove them or implement them?** (a) Remove the 22 rows, or (b) implement paired-single
   execution and fix the three encodings. Recommend (a): it matches the decision for the compares,
   and a teaching simulator gains little from paired-single.
