# Gave the xc1/prefx and madd/msub rows their real COP1X encodings

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.3 F11

## Summary

`include/opcodes.tsv` had six encodings shared by two rows each, inherited from upstream SPIM's
table. GNU `as` (`mips64-linux-gnu-as -mabi=32 -mips32r2 -mfp64 -EB`, then `objdump -d`) gave the
real words for all 24 COP1/COP1X instructions involved:

- `swxc1`/`sdxc1`/`suxc1`/`prefx` are COP1X with funct 0x08/0x09/0x0d/0x0f (`0x4c00000…`); the
  table had given them the COP1 words of `round.l.s`/`trunc.l.s`/`trunc.w.s`/`floor.w.s`.
- The oracle also showed that `madd.s`/`.d`/`.ps` are funct 0x20/0x21/0x26 and `msub.*`
  0x28/0x29/0x2e. The table had madd at 0x00/0x01/0x06, which collided with `lwxc1`/`ldxc1`, and
  msub at madd's values. Those rows were fixed too.
- `lwxc1`, `ldxc1`, `luxc1`, `nmadd.*`, `nmsub.*` and the four COP1 conversions were already right.

`inst_decode` keyed every `0x4c…` word by its rs field, so all COP1X words collapsed together. Now
it keys COP1X by funct. The change is in `src/instruction.c` and the same branch in each port
(`port/python/spimulator_py/instruction.py`, `port/haskell/src/Instruction.hs`,
`port/rust/src/instruction.rs`). No encoding is shared any more, so the six `*` marks are gone.
`tools/gen_opcodes.py` regenerated every table; the tree-sitter keyword lists didn't change.

The Python doctest in `port/python/spimulator_py/opcodes.py` pinned the old answer (`prefx` for
0x4600000f) and now expects `floor.w.s`.
`tasks/reference/opcode-table-generator.md` ("The decode column") and
`tasks/reference/engine-port-language-choice.md` were updated to match.

## Test

`tests/tt.cop1x_decode.s` (regression case `cop1x_decode`, also in `port/harness/corpus.txt`)
stores the four COP1 words over nops in text. It runs `trunc.w.s` and `floor.w.s` on -2.75, which
must print -2 and -3, and then disassembles all four words with the REPL. On an unfixed engine it
stops with "Unknown instruction type: 548". With the fix it passed on the gcc and clang builds, and
the whole local suite passed (310/310: regression, examples and the three port suites).

## Left as found

The COP1X instructions themselves are still not usable: the parser rejects `madd.s` etc. ("not yet
supported"), the xc1/prefx rows have operand types that don't match their `index(base)` syntax, and
none of them has an execute case or a disassembly shape (`FP_FD_FR_FS_FT_INST` has no decode case).
That was true before this change and is unchanged by it; supporting them would be a feature, in
every engine, and no task tracks it.
