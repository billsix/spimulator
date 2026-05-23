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

## Phase 4 — Local `vs`/`vt` in arithmetic operands

Inside the `switch (OPCODE(inst))` dispatch in `src/run.c`, the
common pattern is:

```c
case TOK_ADD_OP: {
  reg_word vs = R[RS(inst)], vt = R[RT(inst)];
  ...
}
```

`vs` = "value of RS", `vt` = "value of RT".  The abbreviation
is non-obvious without context.  Moderate-form rename:

- `vs` → `rs_val`
- `vt` → `rt_val`
- `imm` (when paired with vs/vt) → `imm_val`

Scope: ~40 sites in `src/run.c`, all inside switch case bodies.

**Effort:** ~30 minutes.  **Risk:** very low.

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

## Phase 8 — `K` (kilo)

`include/spim.h:54`:

```c
constexpr int K = 1024;
```

Used in sizing constants: `STACK_SIZE = 64 * K`, etc.

Options:

- **`K` → `KIB`** (preserves the multiplier pattern).
- **Inline the value** (`64 * 1024`) at the few call sites
  and delete the constant.

Recommendation: inline.  `K` is only referenced in ~8 sizing
constants in `spim.h` itself.  Eliminating the indirection
makes each constant self-documenting.

Scope: 8 sites, all in `include/spim.h`.

**Effort:** ~10 minutes.  **Risk:** none.

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
decide); Phase 8 is cosmetic; Phase 9 is optional.

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
| 4 | local `vs`/`vt` → `rs_val`/`rt_val` | 30 min | very low |
| 5 | `CPR`/`CCR` → spelled-out | 1 hour | low |
| 6 | `FPR`/`FGR`/`FWR` → `fp_*_view` | 1 hour | low |
| 7 | `R` → `gpr` | 1 hour | low |
| 8 | `K` → inline `1024` | 10 min | none |
| 9 | `inst` → `instruction` (optional) | 1-2 hours | low |

**Total (phases 1-8): roughly a half-day of work.**
