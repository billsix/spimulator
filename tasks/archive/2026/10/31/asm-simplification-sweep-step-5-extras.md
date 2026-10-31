# Step 5 — asm simplification rollout: extras/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 6 — `fileio/` (first of the large topics)

**Status:** done + archived 2026-10-31 — 3 demos converted, all byte-identical to
pre-change output; comma + testStrings checker-green, print-out-ascii gated by equivalence.
**Priority:** 5
**Difficulty:** 4

## BLUF

Convert the 3 non-exempt `extras/` demos to the stack-visible style. `extras/` has 5 `.asm`;
`subrountines/subrountines-1.asm` and `-2.asm` are the exempt register-vs-stack contrast pair.
"Done" = each demo byte-identical to its pre-change output; the step archives.

## Context

Cite `tasks/reference/mips-asm-teaching-style.md`. These used the old register-held-locals
idiom.

## The 3 files and what each needed

- **`commaAndPeriodCounter/commaAndPeriodCounter.asm`** — no `jal`; the three C locals
  (`comma_count`, `period_count`, `this_char`) move from `$t0`/`$t1`/`$t2` onto the stack
  (16-byte frame, saved `$fp` only). `#CANONICAL` reads a new stdin file
  `commaAndPeriodCounter.input`.
- **`print-out-ascii/print-out-ascii-1.asm`** — no `jal`; the counter `c` moves onto the stack
  (8-byte frame). NO `#CANONICAL`: the output is all 256 values -128..127 — too long for a
  comment anchor; gated by before/after equivalence instead.
- **`testStringsForEquality/testStringsForEquality-1.asm`** — `main` makes three `jal streq`,
  so it saves `$ra`. `streq` stays a register-passing leaf helper. **The documented intentional
  bug is PRESERVED**, not silently fixed: main's C never initializes its return value yet reads
  it on exit — rendered here as a `return_value` slot at `-12($fp)` that is never written and is
  read into `$v0` at the epilogue (surfaces 0 on a fresh spim stack; affects only exit status,
  not stdout). The inverse-return semantics (0 = equal) are also preserved.

### Judgment call flagged for the maintainer

The "preserve intentional bugs" discipline (from `PLAN-asm-comments.md`) was honored for
testStringsForEquality-1 by reproducing the uninitialized-return-value read within the standard
`$fp` frame (a never-written `return_value` slot), rather than cleaning it up to `li $v0, 0`.
If the maintainer prefers the clean exit, it is a one-line change.

## Verification (done)

- Before/after output equivalence: all 3 byte-identical (comma: sample input + empty; ascii:
  256 lines, -128 first / 127 last; testStrings: 1/0/1).
- `tools/check_asm_canonical.py`: comma + testStrings PASS; print-out-ascii skipped (no block).
- No golden tests for any of these (confirmed). `make image` is the batch "done" bar.

## Exempt (unchanged)

- `extras/subrountines/subrountines-1.asm` (longhand stack) and `-2.asm` (idiomatic register
  calling convention) — the register-vs-stack contrast IS the demo.
