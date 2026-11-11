# Fix the tool defects the 2026-11-11 review found

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §5.7, §5.8, §5.9, §5.10 F22

## BLUF

Small defects in the repo's tools. Done means each is fixed with a test or a demonstrated failing
input.

## Context

- **`tools/c_dispatch_check.py`** (§5.9): `flagged()` counts constants by splitting `detail` on ",",
  so a `','` character constant counts as two, which can make a false `switch-chain`. The file is a
  SHARED, VENDORED copy: fix the canonical, github.com/billsix/runClaudeInContainer
  `tasks/reference/c_dispatch_check.py` (and its runCrushInContainer mirror), then re-copy it here.
  Keep the constants as a list in `Finding` instead of a joined string.
- **`tools/const_candidates.py`** (§5.8):
  - 43 of 73 lines print `:?:` for the line, because clang omits a repeated `loc.line`; track
    locations as `tools/c_dispatch_check.py`'s `LocationTracker` does;
  - a FILE argument resolves against the repo root, so a wrong path checks nothing and exits 0;
  - a pointer passed through a cast, or as `&p[i]`, is classified param-pointee where it should be
    param-cascade.
- **`tools/gen_opcodes.py`** (§5.7):
  - duplicate `value`s in `include/opcode-types.tsv` aren't rejected (Python's `IntEnum` would make the
    second an alias);
  - names aren't escaped in emitted string literals;
  - `FIRST_KEYWORD_TOKEN = 264` (line 36) must be changed by hand with the structural tokens in
    `tokens_h`;
  - the decode table holds a dead `(-1, remu)` row.
- **`tools/check_asm_canonical.py`** (§5.10) exits 0 when it finds no block, and ignores spim's exit
  status.
- **`port/rust/tests/cformat_vs_c.rs:96-99`** "skips" on any compile failure. Covered by
  `tasks/port-float-comparison-and-formatter-gates.md`.
- **`tests/run-test.sh:352`** pipes spim into `grep -v`, losing its status. Covered by
  `tasks/tests-check-exit-status.md`.

## Plan

1. Fix each tool, adding a check that fails before the fix (a mutation of the input).
2. For `tools/c_dispatch_check.py`: change the canonical copy, mirror it, re-vendor, and confirm `cmp`.
3. Gate: `python3 tools/gen_opcodes.py --check`, `meson test`.

## Open questions

None.
