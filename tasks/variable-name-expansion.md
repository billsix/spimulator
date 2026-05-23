# Variable name expansion plan

Rename abbreviated identifiers in the spimulator source to be more
descriptive.  Targets non-MIPS-spec abbreviations only — names that
are inherited terseness, not load-bearing convention from the MIPS
architecture reference.

## Naming policy

Per the discussion that produced this plan:

- **Names that match the MIPS reference manual** (`RS`, `RT`, `RD`,
  `SHAMT`, `IMM`, `HI`, `LO`, `PC`, `EPC`, the CP0 Status/Cause
  field names, `FCSR`, `FIR`, `FCC`, etc.) **stay abbreviated**.
  Changing them would hurt anyone reading the MIPS manual alongside
  the code.

- **Rarely-used names** (typedef members, globals accessed a few
  dozen times) **get fully spelled**.  Visual noise is cheap when
  the name only appears occasionally; clarity at the use site
  wins.

- **High-frequency names** (locals in hot loops, the giant
  `switch (OPCODE(inst))` in `run.c`, GPR array accesses) **get
  the moderate form** if they get renamed at all.
  `gpr[rs_val]` is calmer than
  `general_purpose_registers[rs_value]`.

## Verification gate (every phase)

After each phase commit:

1. `ninja -C builddir` clean — zero warnings.
2. `meson test -C builddir` — 22/22 green.
3. The flaky `core` test under parallelism is OK on a clean
   rerun; not a regression.

Same gate the C23 sweep used.

## Phase 1 — Trivial macro renames (low blast radius)

Two macro names that are inconsistent with the rest of the
codebase's naming.

- **`BIN_SA(V)` → `BIN_SHAMT(V)`** in `include/inst.h:183`.
  The rest of the codebase uses `SHAMT` for the shift-amount
  field; `BIN_SA` breaks the pattern.  ~2 call sites.

- **`IDISP(INST)` → `BRANCH_OFFSET(INST)`** in
  `include/inst.h:94`.  "I-displacement" reads as "what is an
  I-displacement?"  The macro computes the byte-offset of a
  branch target — `BRANCH_OFFSET` matches the concept directly.
  ~20 call sites in `src/run.c` and `src/explain.c`.

**Effort:** ~10 minutes.  **Risk:** none.

## Phase 2 — `lab` → `label`

`include/sym-tbl.h` declares `struct lab`, `struct lab_use`, and
parameters / locals named `lab`, `l`.  Saving 2 characters
wasn't worth the cognitive double-take when the same file also
uses `label*` (the typedef).

- **`struct lab` → `struct label_info`** (the typedef'd `label`
  name stays — that's already what callers use).  Or just
  collapse the typedef and struct tag to both be `label`.
- **`struct lab_use` → `struct label_use`**.
- All locals named `lab` → `label`.

Scope: `include/sym-tbl.h`, `src/sym-tbl.c`, a handful of
references in `src/parser.c` and `src/explain.c` (~30 sites
total).

**Effort:** ~30 minutes.  **Risk:** none.

## Phase 3 — Local `hi`/`lo` in multiply intermediates

`src/run.c` uses lowercase `hi` and `lo` as local variables in
the multiply/divide case bodies — these are partial products
during a 64-bit multiplication.  They **shadow the global
`HI`/`LO` register pair** by case only.  That's a real footgun
when scanning quickly.

- `reg_word hi` → `reg_word product_high`
- `reg_word lo` → `reg_word product_low`

Scope: `src/run.c` `TOK_MULT_OP`, `TOK_MULTU_OP`, `TOK_MADD_OP`,
`TOK_MADDU_OP`, `TOK_MSUB_OP`, `TOK_MSUBU_OP` case bodies
(~30 sites in tightly-scoped blocks).

**Effort:** ~15 minutes.  **Risk:** very low.  The blocks are
small and each one is self-contained.

## Phase 4 — Local `vs`/`vt` in arithmetic operands (no-op)

Originally targeted the `vs` / `vt` / `imm` locals in the
`TOK_ADD_OP`, `TOK_ADDI_OP`, `TOK_SUB_OP` case bodies — short
identifiers whose meaning ("value of RS", "value of RT")
required context to decode.

That work is already done as a side-effect of the C23
`<stdckdint.h>` migration (C23 phase 10).  The overflow-trapping
arithmetic instructions were rewritten to pass
`R[RS(inst)]` / `R[RT(inst)]` directly into `ckd_add` / `ckd_sub`
rather than capturing them into named locals first:

```c
case TOK_ADD_OP: {
  reg_word sum;
  if (ckd_add(&sum, R[RS(inst)], R[RT(inst)]))
    RAISE_EXCEPTION(ExcCode_Ov, break);
  R[RD(inst)] = sum;
  break;
}
```

No `vs` / `vt` / `imm` locals remain in `src/run.c`.

The other "value extracted from a register" locals in the file
(`val`, `reg`, `word`, `tmp` in store/shift/branch case bodies)
have descriptive-enough names already — leaving alone.

**Effort:** zero, work already done.  Phase kept in the plan
for traceability.

## Phase 5 — `CPR` and `CCR` (coprocessor register arrays)

`include/reg.h:48`:

```c
extern reg_word CCR[4][32], CPR[4][32];
```

Two three-letter acronyms doing different things side-by-side:
`CPR` = Coprocessor Registers, `CCR` = Coprocessor Control
Registers.  Combined with the `CP0_*` constants threaded
through them, the result reads like alphabet soup
(`CPR[0][CP0_Status_Reg]`).

Fully-spelled rename (rarely-used → favor clarity):

- `CPR` → `coprocessor_registers`
- `CCR` → `coprocessor_control_registers`

Side-effect: the `CP0_BadVAddr`, `CP0_Count`, `CP0_Compare`,
`CP0_Status`, `CP0_Cause`, `CP0_EPC`, `CP0_Config` accessor
macros in `include/reg.h:54-108` are defined in terms of
`CPR[0][...]`; the macro bodies update, the names stay.

Scope: ~150 use sites across `src/run.c`, `src/syscall.c`,
`src/explain.c`.

**Effort:** ~1 hour.  **Risk:** low; mechanical rename with
sed plus rebuild.

## Phase 6 — `FPR`, `FGR`, `FWR` (floating-point register views)

`include/reg.h:127-129`:

```c
extern double* FPR;  /* FP Register, double-precision view */
extern float* FGR;   /* FP General Register, single-precision view */
extern int* FWR;     /* FP Word Register, integer-word view */
```

Three aliased pointers into the same backing storage at three
different widths.  The current names tell you nothing about
which precision; you have to read the comments.

Fully-spelled rename:

- `FPR` → `fp_double_view`
- `FGR` → `fp_single_view`
- `FWR` → `fp_int_view`

(Or `floating_point_*_view` if you prefer the longer form.
`fp_` as a prefix is a near-universal short for floating-point;
this plan picks the moderate-form name as the default and the
fully-spelled form as an upgrade option.)

The `FPR_S(REGNO)`, `FPR_D(REGNO)`, `FPR_W(REGNO)`,
`SET_FPR_S/D/W` macros stay — those refer to the *operation*
(single/double/word), not the *storage*.  Their bodies update
to use the new storage names.

Scope: ~80 use sites across `src/run.c` (FP instruction
dispatch), `src/mem.c` (init/zero), `src/spim-utils.c`
(register init/format).

**Effort:** ~1 hour.  **Risk:** low.  Worth a careful look at
the macro bodies to make sure the type-pun aliasing pointers
remain consistent.

## Phase 7 — `R` (the GPR array)

`include/reg.h:19`:

```c
extern reg_word R[R_LENGTH];
```

`R[]` is the general-purpose register file.  Modern MIPS32
reference manuals call this `GPR[]`; the original H&P uses
`R[]`.  Pedagogically, the longer name is clearer; mechanically,
every register access is `R[RS(inst)]`-shaped and changing it
adds visible bulk to ~400 sites.

Recommended moderate-form rename:

- `R` → `gpr`

(Not `general_purpose_registers` — that's too much pixel cost
per access in run.c's hot dispatch.)

If after seeing the diff this still feels too dense at the use
site, revert — there's nothing wrong with `R[]` semantically.

Scope: ~400 sites in `src/run.c`, ~30 in `src/explain.c`,
~20 elsewhere.

**Effort:** ~1 hour for the rename; the diff is what tells you
whether to keep it.  **Risk:** low.  Mechanical.

## Phase 8 — `K` (kilo) → `kilo` and `mega`

`include/spim.h` previously defined `constexpr int K = 1024;`
as a single-letter multiplier shortcut.  Per the rename
theme, this expands to spelled-out lowercase names that
read as English at the use site:

- `constexpr int kilo = 1024;`
- `constexpr int mega = kilo * kilo;`  (= 1 MB)

After: `STACK_SIZE = 64 * kilo`, `DATA_LIMIT = mega`,
`addr > stack_bot - 16 * mega` (in `mem.c`).  Each constant
reads as plain unit prose.

Scope: 16 sites total — 8 in `include/spim.h`, 6 in
`src/data.c`, 2 in `src/mem.c`.

**Effort:** ~15 minutes.  **Risk:** none.

## Phase 9 — `inst` → `instruction`

Optional / borderline.

`inst` is universal across the spim source (~250 sites in
`run.c`, `explain.c`, `inst.c`, `parser.c`, `ast.c`).  It
abbreviates a long word, but it's *consistently* abbreviated
and recognizable to anyone who's worked in spim for more than
five minutes.

Recommendation: **skip unless you specifically want it**.
The cost-benefit is poor — heavy churn for marginal clarity
gain on a name that already reads cleanly to the audience.

If proceeding, default to `instruction` (fully spelled).

## Phase 9.5 — Token suffix expansion

Currently the token enumerators look like `TOK_CFC0_OP`,
`TOK_ASCII_DIR`, `TOK_LA_POP`.  The middle component is the
MIPS assembler mnemonic (stays — MIPS-spec).  The trailing
suffix encodes a real semantic category but in opaque
abbreviation:

| Current | Means | Proposed |
|---|---|---|
| `_OP` | machine opcode | `_OPCODE` |
| `_DIR` | assembler directive (`.align`, `.ascii`) | `_DIRECTIVE` |
| `_POP` | pseudo-instruction (`la`, `li`, `b`, etc.) | `_PSEUDO_OP` |

After: `TOK_CFC0_OPCODE`, `TOK_ASCII_DIRECTIVE`,
`TOK_LA_PSEUDO_OP`.  Each token name carries its category at
the call site without needing to consult `op.h`.

**The `TOK_` prefix stays as `TOK_`** — pure namespace marker,
expanding it ("token") adds 3 chars per case label across ~400
sites for zero conceptual gain.

### Mechanics

Three independent sed sweeps, in order:

1. `_OP` (~380 sites in op.h + ~250 case labels in run.c +
   scattered uses) → `_OPCODE`
2. `_DIR` (~25 sites) → `_DIRECTIVE`
3. `_POP` (~40 sites in op.h + several call sites) →
   `_PSEUDO_OP`

Care: `_OP` is a common substring, but `\bTOK_[A-Z0-9_]+_OP\b`
(word-boundary regex) catches exactly the right names.  Same
for `_DIR` and `_POP`.  Build between each sweep so any missed
site fails the compile loudly.

### Scope

Files touched: `include/op.h`, `include/tokens.h` (via X-macro
expansion, no direct change), `src/run.c`, `src/inst.c`,
`src/parser.c`, `src/explain.c`, `src/pseudo_op.c`,
`src/scanner.c`.  Total ~1000 use-site changes (mechanical).

### Why placed here

Phase 9.5 because the change is independent of every earlier
phase (it touches different identifiers).  Could land any time;
sandwiched between phase 9 and phase 10 mostly because phase 10
(file renames) is the natural cleanup-last operation.

**Effort:** ~45 minutes.  **Risk:** very low; pure rename, the
compiler will fail loudly on any missed site.

## Phase 10 — Source file name expansion

Rename the source files whose names are abbreviated.  Touches
the meson source list and every `#include` of the affected
headers across the rest of the tree.

### Renames

| Current | Proposed | Reason |
|---|---|---|
| `src/sym-tbl.{c,h}` | `src/symbol-table.{c,h}` | Two words abbreviated: `sym` + `tbl`.  Saves no real space; loses readability. |
| `src/inst.{c,h}` | `src/instruction.{c,h}` | Aligns with Phase 9 if taken.  Skip this rename if Phase 9 is skipped. |
| `src/reg.{c,h}` | `src/registers.{c,h}` | `reg` is fine in code; less so as a filename. |
| `src/mem.{c,h}` | `src/memory.{c,h}` | Same shape. |
| `include/op.h` | `include/opcodes.h` | "op" is ambiguous (operation? operand? opcode?).  Spelling clarifies. |
| `include/op-types.h` | `include/opcode-types.h` | Same. |
| `src/asm_event.{c,h}` | `src/assembler-event.{c,h}` | Also normalizes `_` → `-` to match the project's predominant separator (see "Style note" below). |
| `src/pseudo_op.{c,h}` | `src/pseudo-op.{c,h}` | Separator normalization only — `pseudo-op` reads fine. |
| `src/dump_ops.c` | `src/dump-opcodes.c` | Both the word expansion and separator normalization.  This file is a standalone utility not in the regular build, so the rename touches only one file. |

### Style note: `-` vs `_` in filenames

The project currently mixes both:

- `-`: `display-utils`, `op-types`, `spim-syscall`, `spim-utils`, `string-stream`, `sym-tbl` (6 files)
- `_`: `asm_event`, `pseudo_op`, `dump_ops` (3 files)

Project leans `-`.  Phase 10 normalizes the `_` files to `-`
alongside their word expansion.

### Files left alone

These are already single-word and clear:

- `ast`, `data`, `explain`, `parser`, `run`, `scanner`, `spim`,
  `syscall`, `tokens`, `version`
- The `-` files that already use full words:
  `display-utils`, `string-stream`, `spim-syscall`, `spim-utils`

### Mechanics

Per file:

1. `git mv old.c new.c` (and the `.h` partner) — preserves git
   history detection.
2. Update `meson.build`'s `source_files = files(...)` list.
3. `grep -rln '#include "old.h"' src include` then sed-rename
   every `#include` site.
4. Build, test.
5. If the renamed file is a header that consumers reference by
   a relative path (e.g. inside `tasks/archive/` doc snippets),
   update those too — minor.

Order within the phase: do one rename at a time and rebuild
between each, so any missed `#include` site fails the build
loudly rather than getting buried in a multi-file mass rename.

### Scope

- 9 file pairs to rename = ~14 actual files (some are .c-only).
- Each rename touches the meson list + 2-8 `#include` sites.
- Total `#include` rewrites: ~40-50.

### Effort

~2 hours for the mechanical work + verification.  Could be
faster (one big sed) but the one-at-a-time pace makes
catching missed sites trivial.

**Risk:** low — the build immediately fails on any missed
`#include`, no silent breakage possible.

### Pairs with Phase 9

If Phase 9 (`inst` → `instruction`) is skipped, also skip the
`src/inst.{c,h}` → `src/instruction.{c,h}` line of Phase 10.
The other 8 file renames stand on their own.

## What's intentionally left alone

These names look abbreviated but are actually MIPS-standard;
changing them would diverge from the architecture reference:

- **`RS`, `RT`, `RD`, `SHAMT`, `IMM`** — R/I-type instruction
  field names per the MIPS manual.
- **`HI`, `LO`** — the multiply/divide result registers.  The
  `mfhi` / `mflo` instructions literally name them.
- **`PC`, `nPC`** — universal CPU terms.
- **`CP0_*` accessor macros and `_Reg` field names** — MIPS
  Coprocessor 0 register naming.
- **`EPC`, `EXL`, `IE`, `IM`, `IP`, `BD`, `CU`, `UM`** — MIPS
  Status/Cause register bit names.
- **`FCSR`, `FIR`, `FCC`, `CC0_bit`, `CC1_bit`** — MIPS
  FP control-register field names.
- **`BASE`** in load/store macros — MIPS standard for the
  base-address register field.

## Suggested ordering

Do phases in the order listed.  Phase 1-2 are trivial warmups;
Phase 3-4 surface the multiply-shadow footgun which is the
single most concrete win in the plan; Phase 5-6 are the
biggest readability wins (CPR/CCR/FPR-family alphabet soup);
Phase 7 is the judgment call (commit, look at the diff,
decide); Phase 8 is cosmetic; Phase 9 is optional; Phase 10
(file renames) lands last so it can incorporate any Phase 9
outcome.

Each phase is one commit on a single rename branch (say
`variable-name-expansion`), matching the per-phase-commit
pattern from the C23 sweep.  After all phases land green,
merge to master.

## Effort summary

| Phase | What | Time | Risk |
|---|---|---|---|
| 1 | `BIN_SA`, `IDISP` macro renames | 10 min | none |
| 2 | `lab` → `label` | 30 min | none |
| 3 | local `hi`/`lo` → `product_high`/`product_low` | 15 min | very low |
| 4 | local `vs`/`vt` → `rs_val`/`rt_val` | done by C23 | none |
| 5 | `CPR`/`CCR` → spelled-out | 1 hour | low |
| 6 | `FPR`/`FGR`/`FWR` → `fp_*_view` | 1 hour | low |
| 7 | `R` → `gpr` | 1 hour | low |
| 8 | `K` → `kilo` (+ `mega = kilo * kilo`) | 15 min | none |
| 9.5 | `TOK_*_OP` → `_OPCODE`, `_DIR` → `_DIRECTIVE`, `_POP` → `_PSEUDO_OP` | 45 min | very low |
| 10 | source file names expanded | 2 hours | low |
| 9 | `inst` → `instruction` (optional) | 1-2 hours | low |

**Total (phases 1-8): roughly a half-day of work.**
