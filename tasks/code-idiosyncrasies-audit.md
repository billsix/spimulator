# Audit the code for inherited idiosyncrasies

**Status:** proposed — not started
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

Double-check a lot of the code for weird idiosyncrasies — the observed example
being a "random `void` argc"-style oddity (nonsensical parameter
types/usages). The codebase is a 1990s fork that's been through several
modernization passes; leftovers from upstream SPIM and from mechanical sweeps
can survive in odd corners.

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

1. One pass with tooling: crank `clang-tidy` checks up
   (readability-*, misc-unused-parameters, bugprone-*) over `src/` +
   `include/` and triage the output — the image already ships clang-tidy via
   `lint.sh`.
2. One pass by eye, file by file (~14k LOC), noting anything a new reader
   would trip over. Log findings as a table in this doc (site → what's weird →
   proposed fix → risk).
3. Get a go-ahead on the findings table, then fix in small mechanical commits
   with the full `meson test` suite (and `make image`'s sanitizer gate) as the
   verification gate.

## Relation to other tasks

Overlaps deliberately bounded: naming consistency lives in
`codebase-cleanup-plan.md` Tier B; header hygiene in Tier C; opcode-table
naming in `opcode-types-descriptive-names.md`. This task is the
catch-the-rest sweep for *semantic* oddities, not naming style.
