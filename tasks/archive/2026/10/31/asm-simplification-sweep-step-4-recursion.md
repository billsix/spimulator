# Step 4 — asm simplification rollout: recursion/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 5 — `extras/`

**Status:** done + archived 2026-10-31 — 3 demos converted, all byte-identical to
pre-change output; fibonacci + hanoi checker-green, queens gated by before/after equivalence.
**Priority:** 5
**Difficulty:** 5

## BLUF

Convert the 3 non-exempt `recursion/` demos to the stack-visible style. `recursion/` has 5
`.asm`; `get-char-from-user/-1` and `-2` are the exempt deliberate bug/fix pair. The recursion
demos already use per-call frames (the topic's whole point); the conversion regularizes them
to the `$fp` convention and moves the remaining register-held locals onto the stack. "Done" =
each demo byte-identical to its pre-change output; the step archives.

## Context

Cite `tasks/reference/mips-asm-teaching-style.md`. These used the old "$s-parking" idiom for
`main`/`fib_iter`, and `$sp`-relative per-call frames for the recursive routines.

## The 3 files and what each needed

- **`fibonacci/fibonacci.asm`** — `main` homes `n` and saves `$ra`. `fib_iter` (iterative,
  no jal) now keeps `a`, `b`, `i` in stack slots (the sum `t = a+b` stays transient in a
  register). `fib_rec` (recursive) regularized to the `$fp` convention: saved `$ra`, saved
  `$fp`, `n`, and the `fib(n-1)` intermediate all at fixed `-N($fp)` offsets in each call's
  own 16-byte frame.
- **`hanoi/hanoi.asm`** — the deepest-frame demo. `hanoi` regularized to `$fp`; all four C
  parameters (`n`, `src`, `dst`, `tmp`) + saved `$ra`/`$fp` live in each call's 24-byte frame,
  reloaded from slots between the two recursive calls.
- **`queens/queens.asm`** — `solve` (recursive) gets `row` + the column loop counter `col` on
  its 16-byte frame (the backtracking lesson). The leaf helpers `safe` and `print_solution`
  (no `jal`) get small frames homing their loop index `i`. `columns[]` and `N_global` are C
  GLOBALS and correctly stay in `.data`.

### `#CANONICAL` coverage

- `fibonacci` (`args: 10` → `iter: 55` / `rec:  55`) and `hanoi` (`args: 3` → the 7 moves):
  both green.
- `queens` has NO `#CANONICAL`: each solution line ends with a trailing space (a space is
  printed after every column, including the last), which a comment-based anchor cannot carry
  robustly. Gated by before/after equivalence instead; noted in-file.

## Verification (done)

- Full before/after output equivalence, all 3 byte-identical (fib 0/1/2/5/10/15/no-arg; hanoi
  1/2/3/4/no-arg; queens 1/4/6/default-8-linecount=92).
- `tools/check_asm_canonical.py`: fibonacci + hanoi PASS; queens skipped (no block).
- No golden tests for any of these (confirmed). `make image` is the batch "done" bar.

## Exempt (unchanged)

- `recursion/get-char-from-user/get-char-from-user-1.asm` and `-2.asm` — deliberate bug/fix pair.
