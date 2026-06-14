# Variable name expansion — what landed

## Status — closed (May 2026)

All eleven planned phases shipped on the `renameVariables`
branch (now merged to master).  Renamed ~2500 identifier
sites and 9 source files across the codebase.

The work targeted non-MIPS-spec abbreviations only — names
that were inherited 80s-era terseness, not load-bearing
convention from the MIPS architecture reference.

## Naming policy used

- **Names that match the MIPS reference manual** (`RS`, `RT`,
  `RD`, `SHAMT`, `IMM`, `HI`, `LO`, `PC`, `EPC`, the CP0
  Status/Cause field names, `FCSR`, `FIR`, `FCC`, etc.) stayed
  abbreviated.  Changing them would hurt anyone reading the
  MIPS manual alongside the code.

- **Rarely-used names** (typedef members, globals accessed a
  few dozen times) got fully spelled — clarity at the use site
  beats the small visual-noise cost.

- **High-frequency names** (locals in hot loops, the giant
  `switch (OPCODE(inst))` in `run.c`, GPR array accesses) got
  the moderate form: `gpr[rs_val]` rather than the verbose
  `general_purpose_registers[rs_value]`.

## What landed, by phase

### Phase 1 — Trivial macro renames

- `BIN_SA(V)` → `BIN_SHAMT(V)` in `include/instruction.h` —
  brings the binary-extraction macro in line with `SHAMT(inst)`
  used everywhere else.
- `IDISP(INST)` → `BRANCH_OFFSET(INST)` — "I-displacement" was
  opaque; the macro computes the byte-offset of a branch
  target.

### Phase 2 — `lab` → `label`

Struct tags `struct lab` / `struct lab_use` renamed to
`struct label` / `struct label_use` in `include/symbol-table.h`
(matching the typedef'd names that callers already used).
Local variables named `lab` renamed per-context to
descriptive names (`cursor`, `new_entry`, `looked_up`) — can't
just use `label` because it shadows the typedef.

### Phase 3 — Multiply-intermediate locals

`src/run.c` had `reg_word lo = LO, hi = HI;` in the
`TOK_MADD_OPCODE`/`TOK_MSUB_OPCODE` blocks — local variables
named identically to the global `HI`/`LO` register pair
modulo case.  Real footgun when scanning.  Renamed to
`product_low` / `product_high`.

### Phase 4 — `vs`/`vt`/`imm` arithmetic operands (no-op)

Target sites (the `vs = R[RS(inst)]; vt = R[RT(inst)]`
captures) were already eliminated as a side-effect of the
C23 `<stdckdint.h>` migration — `ckd_add` takes the
register accesses directly, no intermediate locals needed.
Phase kept in the plan for traceability.

### Phase 5 — Coprocessor register arrays

`CPR` → `coprocessor_registers`, `CCR` →
`coprocessor_control_registers` in `include/registers.h`.
~16 use sites; the CP0_* accessor macros (CP0_Status,
CP0_Cause, etc.) get the renamed storage in their bodies but
keep their public names.

### Phase 6 — FP register-view pointers

`FPR` → `fp_double_view`, `FGR` → `fp_single_view`, `FWR` →
`fp_int_view`.  Each is a typed alias into the same backing
storage at different widths.  The new names make the
type-pun aliasing visible at every use site:
`fp_single_view = (float*)fp_double_view;`

The macros that wrap them (`FPR_S`, `FPR_D`, `FPR_W`, `SET_*`,
`FPR_LENGTH`, `FGR_LENGTH`) kept their original names — they
name the *operation* (single/double/word), not the storage.

### Phase 6.5 — TOK_* suffix expansion

Token enumerators carry a category suffix:

| Old | New |
|---|---|
| `TOK_*_OP` (opcode) | `TOK_*_OPCODE` |
| `TOK_*_DIR` (directive) | `TOK_*_DIRECTIVE` |
| `TOK_*_POP` (pseudo-op) | `TOK_*_PSEUDO_OP` |

~1125 site renames across the codebase, expanded via three
word-boundary sed sweeps.  The MIPS mnemonic in the middle
(`CFC0`, `ADD`, `BEQ`, etc.) stays — it's the literal
assembler keyword.  The `TOK_` prefix also stays — pure
namespace marker.

The win: `case TOK_CFC0_OPCODE:` reads as "the case for the
cfc0 instruction's opcode" instead of the categoryless
`TOK_CFC0_OP`.

### Phase 7 — General-purpose register array

`R` → `gpr` in `include/registers.h`.  ~400 sites in `run.c`
alone, ~30 in `explain.c`, ~20 elsewhere.

Approach: targeted `R[` → `gpr[` sed (catches the array
indexing form without hitting `R` in unrelated contexts:
comments mentioning "R-type", format strings `"R%-2d ..."`,
the `REGS(R, O)` macro parameter).  Three special-case sites
handled manually: `memset(R, 0, ...)`, `memcpy(snap_R, R,
...)`, and a comment mentioning "the live R".

### Phase 8 — `K` → `kilo` and `mega`

`include/spim.h` previously defined `constexpr int K = 1024;`
as a single-letter multiplier.  Renamed to `kilo`, added
`constexpr int mega = kilo * kilo;` as a derived constant.

Result reads as English: `STACK_SIZE = 64 * kilo`,
`DATA_LIMIT = mega`, `addr > stack_bot - 16 * mega` in
`memory.c`.

### Phase 9 — `inst` → `instruction` + type → `mips_instruction`

Two-part rename.  Variables and parameters named `inst`
became `instruction` (~641 sites).  But to avoid shadowing
the typedef of the same name — and to fix a subtler
correctness bug where `sizeof(instruction)` would have
referred to the local variable (pointer = 8 bytes) instead
of the struct type — the typedef itself was renamed
`instruction` → `mips_instruction`.

After:
```c
mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));
```
Type and variable have distinct names; sizeof unambiguously
refers to the type.

`#include "inst.h"` references were restored after the sed
sweep, since the file wasn't renamed yet (that came in Phase
10).

### Phase 10 — Source file name expansion

9 file pair/single renames.  `git mv` preserved git's
rename-detection metadata.  meson.build and `#include`
directives updated.

| Old | New |
|---|---|
| `src/inst.{c,h}` | `src/instruction.{c,h}` |
| `src/mem.{c,h}` | `src/memory.{c,h}` |
| `include/reg.h` | `include/registers.h` |
| `src/sym-tbl.{c,h}` | `src/symbol-table.{c,h}` |
| `include/op.h` | `include/opcodes.h` |
| `include/op-types.h` | `include/opcode-types.h` |
| `src/asm_event.{c,h}` | `src/assembler-event.{c,h}` |
| `src/pseudo_op.{c,h}` | `src/pseudo-op.{c,h}` |
| `src/dump_ops.c` | `src/dump-opcodes.c` |

Three files normalized `_` separator to `-` (matching the
project's predominant style).  Followup: the tree-sitter
Makefile, the keyword-extraction Python scripts, and the
emacs elisp tree-sitter mode all had `op.h` references that
needed updating; the docker build broke on the first attempt
because the tree-sitter Makefile still pointed at
`../include/op.h`.

## Verification gate

After each phase commit:

1. `ninja -C builddir` — zero warnings.
2. `meson test -C builddir` — 22/22 green.

The flaky `core` test under parallelism was acceptable on
rerun; not introduced by any phase here.

## Files left alone

MIPS-spec names that look abbreviated but are load-bearing
convention from the architecture reference:

- `RS`, `RT`, `RD`, `SHAMT`, `IMM` — R/I-type instruction
  field names.
- `HI`, `LO` — multiply/divide result registers (`mfhi`/`mflo`
  literally name them).
- `PC`, `nPC` — universal CPU terms.
- `CP0_*` accessor macros and `_Reg` field names.
- `EPC`, `EXL`, `IE`, `IM`, `IP`, `BD`, `CU`, `UM` — MIPS
  Status/Cause bit names.
- `FCSR`, `FIR`, `FCC`, `CC0_bit`, `CC1_bit` — MIPS FP
  control-register field names.
- `BASE` in load/store macros.

## Followup items worth filing

None directly arising from this work.  See the separate task
docs in `tasks/` for the pre-existing UBSan findings and the
deferred `string-stream → open_memstream` modernization.
