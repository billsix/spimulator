# Step 7 — asm simplification rollout: lib/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 8 — `lang/`

**Status:** done + archived 2026-10-31 — 7 driver demos converted, all byte-identical to
pre-change output (incl. exit statuses); the 4 library files left register-based, untouched.
**Priority:** 5
**Difficulty:** 6

## BLUF

Convert the `lib/` **driver** demos (`*-demo`) to the stack-visible style; leave the library
routine files (`libctype`, `libctype-defensive`, `libstdlib`, `libstr`) register-based and
untouched. `lib/` has 11 `.asm` = 4 library + 7 drivers. "Done" = each driver byte-identical to
its pre-change output; the step archives.

## Context

Maintainer decision (William Emerison Six <billsix@gmail.com>, 2026-10-31): "convert the drivers
only for lib/". The library routines are exactly the **shared leaf helpers** the style doc keeps
register-based, so they are NOT converted. The drivers all call into the library via `jal`.

## The 7 drivers

- **atoi-demo, abs-demo** — loop over a `.word` table; the loop index `i` and the current value
  move from `$s*` onto stack slots (the current value needs a home because it is used across
  several `jal`s). `_ps`/`_pi` kept as leaf print helpers.
- **ctype-demo** — the loop variable `c` moves onto a stack slot; each ctype call reloads `c`
  into `$a0`. `_ps`/`_pi`/`_pc` kept as leaf helpers.
- **str-demo** — a straight-line sequence of libstr calls with NO persistent C locals (each
  test's PASS/FAIL is transient in `$a1`); only the frame was regularized to the `$fp`
  convention. `report` kept as a leaf helper.
- **exit-demo** — no C locals (prints a string, tail-calls `_Exit`); light conversion (style
  note + empty SYMBOL TABLE); no frame.
- **atexit-demo** — no C locals (passes 3 handler addresses to `atexit`, then `exit`); frame
  regularized to the `$fp` convention; handlers kept as leaf functions.
- **bsearch-demo** — the keys cursor/end/key/result move onto `-N($fp)` slots, but the O32
  **outgoing-argument area stays at `$sp+0..16`** (the 5th arg, the cmp pointer, must sit at
  `16($sp)` where bsearch reads it) — a deliberate calling-convention teaching point, preserved.
  `int_cmp` kept as a leaf comparator.

### No `#CANONICAL` for any lib driver

Each driver `jal`s into its library (`atoi`, `isalpha`, `bsearch`, `strlen`, …), so the
single-file value-checker (`spim -f <one file>`) cannot assemble it standalone. All 7 are
**golden-tested** via `run-demo.sh` (which loads the library `.asm` alongside), including the
exit statuses for exit-demo/atexit-demo — a stronger gate than a single `#CANONICAL` line.

## Verification (done)

- Before/after output equivalence, loading the same library files as `run-demo.sh`: all 7
  byte-identical in stdout AND exit status (exit-demo/atexit-demo = 42).
- All 7 have pinned goldens; the examples suite stays green. `make image` is the batch bar.

## Not converted (per the maintainer's "drivers only")

`lib/libctype/libctype.asm`, `lib/libctype/libctype-defensive.asm`, `lib/libstdlib/libstdlib.asm`,
`lib/libstr/libstr.asm` — the shared library routines, intentionally register-based.
