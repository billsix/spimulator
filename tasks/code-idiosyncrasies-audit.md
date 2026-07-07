# Audit the code for inherited idiosyncrasies

**Status:** proposed — not started
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

Double-check a lot of the code for weird idiosyncrasies — the observed example
being a "random `void` argc"-style oddity (nonsensical parameter
types/usages).

**Scope (clarified by Bill, 2026-07-07): the example code too, not necessarily
spimulator source itself.** So the sweep covers, in priority order:
1. `examples/src` — the demo C sources (freestanding, hand-written for
   readability — exactly where a nonsense parameter survives review because
   nothing warns on it) and the hand-written `.asm`.
2. `pgu/src` — the book's C ports and asm.
3. `src/`+`include/` (the simulator) — lower priority; it's been through
   several modernization passes, but the same greps are cheap to run over it.

## What to sweep for

Concrete patterns, each greppable or clang-tidy-able:

- **Nonsense signatures / parameters**: `void`-typed or unused parameters that
  exist only for a dead historical reason (`(void)x;` casts hiding a parameter
  that should be removed; `argc`/`argv` threaded into functions that ignore
  them; find the specific "void argc" Bill saw).
- **K&R-era residue**: implicit-int habits, old-style casts where C23 idioms
  exist, `register`/`extern` noise, `char*` used for byte buffers that should
  be `uint8_t*`.
- **Signature/typedef mismatches**: e.g. functions taking `int` where every
  caller passes a `mem_addr`/`reg_word`; boolean-ish `int`s not yet `bool`.
- **Dead parameters and always-constant arguments**: parameters that every
  caller passes the same literal for.
- **Inconsistent conventions** already flagged in `codebase-cleanup-plan.md`
  Tier B (`read_mem_*`/`set_mem_*` asymmetry, `str_copy` vs `strdup`,
  `*_inst` suffix) — fold those in rather than duplicating them here.
- **Comment/code drift**: comments describing behavior the code no longer has
  (the "op.h" self-references in `opcodes.h`/`opcode-types.h` are one known
  case — tracked in `opcode-types-descriptive-names.md`).

## Method

1. One pass with tooling over the C: `clang-tidy` (readability-*,
   misc-unused-parameters, bugprone-*) over `examples/src`, `pgu/src/c`, and
   `src/`+`include/`, and triage — the image already ships clang-tidy via
   `lint.sh`. Note the demos compile `-nostdlib -ffreestanding`; pass those
   flags so tidy sees them the way the build does.
2. One pass by eye — the `.asm` files have no tooling, so the demo asm gets
   read directly (weird register choices, dead stores, copy-paste residue
   from a neighboring demo). Log findings as a table in this doc (site →
   what's weird → proposed fix → risk).
3. Get a go-ahead on the findings table, then fix in small mechanical commits
   with the full `meson test` suite (and `make image`'s sanitizer gate) as the
   verification gate.

## Relation to other tasks

Overlaps deliberately bounded: naming consistency lives in
`codebase-cleanup-plan.md` Tier B; header hygiene in Tier C; opcode-table
naming in `opcode-types-descriptive-names.md`. This task is the
catch-the-rest sweep for *semantic* oddities, not naming style.
