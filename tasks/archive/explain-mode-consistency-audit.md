# Explain-mode per-instruction consistency audit — what landed

## Status — closed (May 2026)

All five planned phases shipped on the `explainModeConsistency`
branch, plus an L1 extension that grew out of Phase D.  Each
phase is a separate commit; together they touch
`src/explain.c` and regenerate `tests/tt.explain.expected`.

The audit identified seven concrete inconsistencies in the
per-instruction narration at `-explain=1` through `-explain=4`.
Six were addressed; the seventh (broader cross-mnemonic
template-shape normalization) was deferred as a larger
refactor.

## How the audit was collected

```sh
ninja -C builddir
for level in 1 2 3 4; do
  ./builddir/spimulator -explain=$level -f tests/tt.explain.s \
      > /tmp/explain_L$level.txt 2>&1
done
```

Per-mnemonic line counts measured by extracting blocks between
consecutive `Stepped at PC = ...` headers.

## What landed

### Phase A — Mnemonic-prefix consistency + always-show offset

- **`j` opcode template** had been emitting `Jumped unconditionally
  to X.` while every other instruction used the `Mnemonic Name —
  verb-phrase` pattern.  Added the `Jump —` prefix.
- **`syscall` template** similarly lacked the prefix.  Now reads
  `System Call — invoked the kernel. The call number was in
  $v0; arguments were in $a0..$a3...`
- **Load/store offset line** in the Inputs block was gated on
  `off != 0`.  Removed the guard — `offset = 0 (0x0000)` now
  shows unconditionally for every load/store, giving consistent
  input-line count across all instances regardless of source
  offset.

### Phase B — Modifier rebalance + `l` modifier addition

- **Width-modifier descriptions** (`b`, `h`, `w`) were each one
  line; other modifiers like `u`/`i`/`v` had 3-4 line descriptions.
  Expanded the width descriptions to match the deeper depth —
  each now explains the bit width, alignment requirement, and
  (for narrow loads) sign-extension behavior.
- **`MOD_L_LIKELY` modifier added** to the enum, letter table,
  and description tables.  Documents the delay-slot-nullify
  behavior of branch-likely instructions.
- **Classification cases added** for the 6 plain branch-likely
  opcodes (`TOK_BEQL_OPCODE` etc.) and 2 branch-and-link-likely
  (`TOK_BGEZALL_OPCODE`, `TOK_BLTZALL_OPCODE`).  Previously these
  fell through to the default and emitted no category/modifier
  preamble at all.

### Phase C — First-encounter logic removed (always show)

The narration system had `category_seen[]` / `modifier_seen[]`
flag arrays that showed full descriptions only on the *first*
appearance of each category/modifier per session.  This
created a reading-order trap: a student landing on the 50th
load saw a different (shorter) block than the 1st.

Removed the flags; every instruction now shows the full
category description and full modifier descriptions every
time.  Also deleted the now-dead `modifier_short[]` table
that supplied the short forms.

Cost: +18% L2 line count, +13% L3, +9% L4.  Within the
30-50% estimate from the original audit.

### Phase D — Try-it-yourself baseline + bug fixes

- **3-line minimum on the "Try it yourself" block.**  Added a
  static line counter (`try_lines_emitted`) and a
  `say_try_finish()` function called from the dispatch wrapper.
  If the operand-specific suggestions don't reach 3 lines (e.g.
  `j` with 0 register operands, `mfhi` with 1), the function
  pads with `print_all_regs`, `step`, `continue`.  Loads/stores
  already at 4+ lines skip the padding cleanly.
- **`$rs`/`$rt`/`$rd` placeholder legend.**  Pseudo-op
  descriptions in `pseudo_ops[]` contain literal `$rs`, `$rt`,
  `$rd` placeholders (per the C23 phase-8 design — they describe
  the generic pseudo-op pattern, not a specific instance).
  Added a "Placeholder notation" section to the once-per-session
  legend explaining what those identifiers mean.
- **Spurious blank-line bug fix.**  `render_dispatch` had an
  unconditional `write_output("\n")` at function exit that
  landed *between* the `Try it yourself:` header and the
  `say_try_finish` pad lines.  Removed; `explain_after`'s
  trailing newline still gives proper inter-instruction spacing.

### Phase D extension — L1 pseudo-op header compressed

After Phase E landed, L1 still had a 2× spread because the
pseudo-op header was a 4-line block.  Compressed at L1 to a
single line: `Pseudo-op: \`li\` (first real instruction)`.
Preserves the "you wrote a pseudo-op" pedagogical signal
without the verbose description; L2+ still gets the full
4-line block.  L1 spread dropped from 2.0× to **1.5×** —
hitting the audit's target.

### Phase E — Store memory snapshot in Inputs

Loads' Inputs section showed `memory at addr = X` (the value
being read).  Stores didn't show a corresponding "memory at
addr (before)" — asymmetric.  Used the existing `snap_mem_addr`/
`snap_mem_val` machinery (already populated for the after-step
memory diff) to emit the prior memory value, masked to the
store width.

Reads symmetrically now: a student sees what value is being
overwritten right next to what's overwriting it.

## What didn't land

### Seventh item — broader cross-mnemonic template normalization

The audit's per-level spread targets:

| Level | Pre-audit | Post-phase-E | Audit target |
|---|---|---|---|
| L1 | 2.0× | **1.50×** | 1.50× ✓ |
| L2 | 1.94× | 1.80× | 1.50× (not met) |
| L3 | ~2× | 2.10× | 1.50× (not met) |
| L4 | ~3× | 2.76× | 1.50× (not met) |

L1 hit the target cleanly.  L2-L4 still have ~2× spread
because of *structural* differences between instruction
templates — jumps don't have memory inspection lines;
loads/stores do; `mfhi`/`mflo` have only HI/LO inputs;
syscall has up to 4 register inputs.  Tightening further
would require either:

- Padding every block to a fixed line count (overkill;
  pedagogically uninformative).
- Shape-normalizing the per-template emission code, which is
  the larger refactor the audit explicitly flagged as out of
  scope.

## Verification

Each phase:

1. `ninja -C builddir`
2. `meson test -C builddir` — 22/22 green
3. Regenerated `tests/tt.explain.expected` via
   `env -i ./builddir/spimulator -exception_file
   src/exceptions.s -explain=2 -f tests/tt.explain.s >
   tests/tt.explain.expected 2>&1`
4. Per-level distribution rechecked.

Final state: clean build, 22/22 regression tests green, L1
spread at the 1.50 target, L2-L4 modestly improved.

## Files touched

- `src/explain.c` — the entire audit body lives here:
  - Phase A: J, syscall, load/store templates.
  - Phase B: modifier tables, classification cases for
    branch-likely.
  - Phase C: removal of first-encounter flag arrays + the
    `modifier_short` table.
  - Phase D: `say_try_finish` + counter machinery; legend
    update for `$rs`/`$rt`/`$rd`; render_dispatch trailing-
    blank fix.
  - Phase D extension: L1 short-form pseudo-op header.
  - Phase E: store-side memory snapshot in Inputs.

- `tests/tt.explain.expected` — golden output for the
  `explain` regression test, regenerated after each phase.

## Followup items worth filing

- **Cross-mnemonic shape normalization** — the deferred 7th
  item.  Per-template shape audit + pad strategy to bring
  L2-L4 spread under 1.50× without bloating the output.  Would
  need a design decision on whether the goal is "every block
  same shape" (rigid) or "every block reaches N suggestions"
  (more pedagogical).

- **Branch-likely template coverage in `tests/tt.explain.s`**
  — Phase B added classification for `beql`/`bnel`/`bgezl`/etc.
  but the test file doesn't exercise them, so the new modifier
  output is unverified.  Adding even one branch-likely to the
  test would surface any regression in the modifier emission.
