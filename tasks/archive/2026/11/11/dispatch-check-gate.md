# Make `parse_j` pass the dispatch check, and run the checker in meson

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.1, §2.3 F5

## Outcome

- `parse_j` (`src/parser.c`) is one `if`/`else if`/`else` chain. It runs: cop2 error; register forms;
  `jalr.hb` with a label is an error; otherwise the label form. The run of early returns that the
  cop2 fix (`625b30b`) had introduced is gone. No behavior changed.
- A new regression test, `dispatch_check` (`meson.build`), runs `tools/c_dispatch_check.py` on this
  build's `compile_commands.json` whenever `clang` is installed, so the tree can't drift from the
  C standard's Dispatch rules again. `CLAUDE.md` names the test.

## Verification

- `python3 tools/c_dispatch_check.py BUILDDIR src`: flagged 0 (accepted 1, `run_spim`'s fetch chain).
- With the old `parse_j` restored, `dispatch_check` fails (exit 1); with the new one it passes.
- The regression suite passes, 39/39 including the new test.
