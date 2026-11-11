# Use the shared dispatch checker instead of spimulator's own

**Status:** in progress (branch `dispatch-follow-ups`)
**Priority:** 4
**Difficulty:** 1
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)

## BLUF

`tools/dispatch_candidates.py` was generalized into a SHARED, VENDORED tool,
`c_dispatch_check.py`. Its canonical copy is in github.com/billsix/runClaudeInContainer
`tasks/reference/`; that repo's record is `tasks/archive/2026/11/11/shared-dispatch-checker.md`.
Vendor it into `tools/` and delete the local one. Mark `run_spim`'s fetch chain with the accept
comment (`dispatch-check: ok`, with the reason), so the tree checks clean (exit 0). Update the
docs that name the old tool. Done means `python3 tools/c_dispatch_check.py BUILDDIR src` exits 0.

## Context

- The vendored copy must stay byte-identical to the canonical. Edit it there, never here.
- The fetch chain, in `src/run.c`, is the one deliberate leave-alone recorded in
  `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`.
- Not wired into a gate. Doing that would mean a clang build inside the image; propose it, don't
  do it.
