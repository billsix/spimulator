# Fixed the tool defects the 2026-11-11 review found

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §5.7, §5.8, §5.9, §5.10 F22

## Summary

Each fix was shown against an input that failed before it.

- **`tools/c_dispatch_check.py`** (shared and vendored): `Finding` keeps the chain's constants as a
  tuple, so a `','` character constant counts once. Before the fix, a two-test chain
  `if (c == ',') … else if (c == 'a') … else` was a false `switch-chain` (exit 1); after it, exit 0.
  Doctests on `flagged` cover both cases. The change was made in the canonical copy
  (github.com/billsix/runClaudeInContainer `tasks/reference/c_dispatch_check.py`, branch
  `c-dispatch-check-comma-constant`), mirrored to github.com/billsix/runCrushInContainer on a branch of
  the same name, and re-vendored here byte-identical (`cmp`). gpg signing was turned off in both
  repos for the commits and back on afterwards.
- **`tools/const_candidates.py`**: lines come from a walk in clang's printing order (a
  `LocationTracker`, as in the dispatch checker), so all 72 candidates print a line where 43 printed
  `?`. A FILE argument is relative to the working directory, and an unmatched one exits 2 (it used
  to check nothing and exit 0). A pointer passed through a cast or as `&p[i]`/`&p->f` counts as
  passed to a call, which makes it param-cascade (`argument_id`, with doctests). The candidate set on
  today's tree is unchanged.
- **`tools/gen_opcodes.py`**: it now rejects a repeated type value and a name that isn't printable
  ASCII, emits names through `quoted()`, derives `FIRST_KEYWORD_TOKEN` from the same
  `STRUCTURAL_TOKENS` list `tokens.h` is written from, and leaves the `-1` rows out of the decode
  tables. That removed `remu`'s star and the dead `(-1, remu)` row from every engine. Each check
  rejected a mutated copy of the data files; `--check` stayed clean.
- **`tools/check_asm_canonical.py`**: it compares spim's exit status and fails when no block is found.
  A block can pin `status: N`; the error-path runs of `cp.asm` and `touch.asm` pin 1. `--all`
  gave 47 PASS. `tasks/reference/mips-asm-teaching-style.md` documents the field.
- The `cformat_vs_c.rs` and `run-test.sh` items were already done by
  `tasks/archive/2026/11/11/port-float-comparison-and-formatter-gates.md` and
  `tasks/archive/2026/11/11/tests-check-exit-status.md`.

Gates: `meson test` 330/330 (gcc build), `dispatch_check` on the clang build.
