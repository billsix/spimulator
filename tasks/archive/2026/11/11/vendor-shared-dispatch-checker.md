# Use the shared dispatch checker instead of spimulator's own

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `dispatch-follow-ups`

## Outcome

`tools/c_dispatch_check.py` is the vendored, byte-identical copy of
github.com/billsix/runClaudeInContainer `tasks/reference/c_dispatch_check.py`, and it replaced
`tools/dispatch_candidates.py`. That repo's record is
`tasks/archive/2026/11/11/shared-dispatch-checker.md`. Edit the canonical, then re-copy it here.

`run_spim`'s instruction-fetch chain (`src/run.c`), the deliberate leave-alone from
`tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`, carries the accept comment. Its reason
is on the line above, so the marker line sits directly above the `if`, where the tool looks for
it. `python3 tools/c_dispatch_check.py BUILDDIR src` exits 0: 17 units, 0 flagged, 1 accepted.
`CLAUDE.md` names the command.

The vendored copy is clean under this repo's ruff config, so `make format` leaves it byte-identical.
It isn't wired into a gate, because that would need a clang build inside the image.
