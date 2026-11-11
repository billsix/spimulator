# The paired-single compares (`c.cond.ps`) assemble to the wrong encoding and cannot run

**Status:** in progress (branch `dispatch-follow-ups`)
**Priority:** 6
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`, while adding a `default` to
`r_cond_type_inst`'s switch (`src/instruction.c`)

## BLUF

The 16 `c.cond.ps` rows of `include/opcodes.tsv` encode format field 0x13. MIPS32 Release 2 puts
paired-single at format 22 (0x16), so `c.eq.ps $f0, $f2` should encode as `0x46c20032`, and spim
gives `0x46620032`. The engine also can't execute them: running one prints `Unknown instruction
type: 357`. Done means one of two outcomes, the maintainer's choice:
- (a) the encodings are fixed and the compares run, in the C engine and all three ports, with
  regression tests;
- (b) the rows are removed, if paired-single is out of scope for a teaching simulator.

## Context

- **Oracle:** GNU binutils for MIPS (`binutils-mips64-linux-gnu`; installed 2026-11-11, and added to
  both sandboxes' base lists). `mips64-linux-gnu-as -mabi=32 -mips32r2 -mfp64 -EB` then
  `mips64-linux-gnu-objdump -d` gives `46c20032 c.eq.ps $f0,$f2` and `46c6203c c.lt.ps $f4,$f6`.
  For `c.eq.s` and `c.eq.d` it gives `46020032` and `46220032`, which match spim. LLVM's
  `llvm-mc` doesn't support the paired-single compares at all.
- **The table row:** `c.eq.ps	TOK_C_EQ_PS_OPCODE	FP_CMP_FS_FT_INST	0x46600032		MIPS32 Rev 2`.
  The other 15 `.ps` compares follow the same pattern (`0x4660003X`).
- **Assembly path:** these rows are of type `FP_CMP_FS_FT_INST`, so the parser emits them through
  `emit_fp_compare` → `r_cond_type_inst`. Its switch sets the `COND` field only for the 32 `.s`/`.d`
  compares; its `default` documents that the `.ps` compares reach it with no case.
- **Execution:** `run.c` has no case for them, so executing one reaches the unknown-instruction
  path.
- Any change to `opcodes.tsv` must be gated with `BUILD_TREE_SITTER=1` (`CLAUDE.md`), and regenerated
  with `make generate`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. **Remove the 16 `c.cond.ps` rows** (option (b)). Nothing in `examples/` or `tests/` uses
   paired-single, the engine can't execute them, and their encodings are wrong. `make generate`
   regenerates every table: the C engine's, the ports' and tree-sitter's. The image gate runs with
   `BUILD_TREE_SITTER=1`. After the change, a `c.eq.ps` line is an unknown instruction, as any
   unsupported mnemonic is.
2. `r_cond_type_inst`'s `default` then covers only impossible values. It becomes fatal, like the
   task's other exhaustive switches.
