# More descriptive names for the opcode-table headers ("op.h")

**Status:** DONE — archived 2026-07-07.  All 21 terse tags renamed to
operand-order-as-written names (`I2a_TYPE_INST` → `RT_ADDR_INST` for
`lw rt, address`; `R3_TYPE_INST` → `RD_RS_RT_INST` for `add rd, rs, rt`;
etc.), with a naming-scheme legend and per-tag example comments in
`opcode-types.h`.  Names were derived from the parser's per-tag `parse_*`
code (ground truth), not guessed from the old abbreviations —
notably `R2td`/`FP_R2ts` are really REG,COPREG shapes (`RT_COPREG_INST` /
`FP_RT_COPREG_INST`).  `J_TYPE_INST`/`NOARG_TYPE_INST`/`MOVC`/`FP_MOVC`
kept (already legible).  All ~290 uses swept across src/, include/, and the
tree-sitter extractor comment; every stale "op.h" self-reference now says
`opcodes.h` (the extractor keys only on the mnemonic column, so the
grammar is unaffected).  Suite green.
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

"Give more descriptive names to things in op.h."

There is no `op.h` today — the files are `include/opcodes.h` (the X-macro
master list) and `include/opcode-types.h` (the operand-shape enum). Note the
comments *inside* both files still say "op.h" (e.g. opcode-types.h: "type tags
used by op.h's X-macro entries"), suggesting the file was renamed without
updating its self-references. Two sub-tasks fall out:

## 1. Rename the terse `op_type` tags in `opcode-types.h`

The current names are dense abbreviations that need the comment block to
decode:

```
BC_TYPE_INST, B1_TYPE_INST, I1s_TYPE_INST, I1t_TYPE_INST, I2_TYPE_INST,
B2_TYPE_INST, I2a_TYPE_INST, R1s_TYPE_INST, R1d_TYPE_INST, R2st_TYPE_INST,
R2ds_TYPE_INST, R2td_TYPE_INST, R2sh_TYPE_INST, R3_TYPE_INST, R3sh_TYPE_INST,
FP_* variants, J_TYPE_INST, NOARG_TYPE_INST
```

Proposal: spell out what each shape means, e.g.
`I1s_TYPE_INST` → something like `IMM_RS_ONLY_INST` /
`ITYPE_ONE_SRC_REG_INST` — exact scheme to be designed so the name states
(a) encoding family (R/I/J/FP), (b) which operands appear and their roles
(s/t/d = rs/rt/rd, sh = shamt, a = address, C = condition). Whatever scheme is
chosen, document it once at the top of the enum and make every name
self-consistent with it.

Ripple: the consumers keying on these tags — `i_opcode_tbl` in
`src/instruction.c`, `keyword_tbl` in `src/scanner.c`, `op_type_table` in
`src/parser.c` — plus anywhere else `grep -rn 'TYPE_INST' src include` hits.
Mechanical rename; do it with the full test suite as the gate.

## 2. Clean up the `opcodes.h` self-references

Either rename the file back to `op.h` (matching all its internal prose) or —
better — fix the ~half-dozen comment references from "op.h" to "opcodes.h" in
`opcodes.h`, `opcode-types.h`, and any `src/` comments that mention it.

## Constraints

- `opcodes.h` is the single source of truth for the tree-sitter grammar too
  (`scripts/extract-keywords.py`). Check whether the script reads the type-tag
  column; if it does, keep the script in sync in the same change.
- The X-macro column *order* and the OP() row format should not change —
  this is a naming pass, not a restructuring (see codebase-cleanup-plan G2:
  the X-macro pattern stays).

## Verification

Full `meson test` suite green; tree-sitter grammar regenerates without diff
(or with only the expected renames); `make image` green.
