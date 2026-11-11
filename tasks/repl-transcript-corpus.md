# Compare full REPL transcripts across the engines

**Status:** proposed — needs go-ahead
**Priority:** 4
**Difficulty:** 6
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §6.1, §10.2 F7

## BLUF

Most REPL commands have no test on any engine. A port's REPL can diverge from C without failing
anything. Done means a harness corpus of REPL sessions that runs each one on C and on every port and
compares the full transcript (stdout, stderr, exit status), covering every command in
`src/spim.c`'s command table. The one-shot REPL differs in `tasks/adhoc/` are promoted or removed.

## Context

- The commands are at `src/spim.c:75-80`. The REPL inputs in `tests/run-test.sh` use only load,
  read, disasm, args, run, quit, exit, print, print_all_regs, `.` and a bare breakpoint, and those
  tests only grep for a line or two.
- Never exercised: step, continue, delete, list, dump, dumpnative, help, print_symbols,
  reinitialize, and a breakpoint that actually fires.
- `port/harness/corpus.txt:13-14` excludes the REPL on purpose.
- Starting points, one-shot scripts from the port tasks: `tasks/adhoc/engine-port-python/diff_repl.sh`
  and `tasks/adhoc/engine-port-haskell/repl.py`. They are still tracked (the record's §10.2).
- Known divergence to expect: `print_all_regs` followed by `print_all_regs hex` leaves three stale
  lines in C (a C bug, `ss_clear`; `tasks/archive/2026/11/11/c-undefined-behavior-fixes-then-ports.md`). The Haskell
  port's output is the correct one.
- Ports must be run with the same environment (the stack holds the environment), as the harness
  already does with `env -i`.

## Plan

1. Write `port/harness/repl_corpus/*.in` sessions: one per command, plus breakpoint + continue +
   step + delete, redo of each redo-able command, and `reinitialize` between loads.
2. Add a `compare_repl.py` (or a mode of `port/harness/compare_engines.py`) that feeds each session to C and a
   port and compares the whole transcript. Register it per port in `port/meson.build`.
3. Fix or record each divergence it finds. A C bug goes to C first.
4. `git rm` the one-shot `tasks/adhoc/engine-port-*` REPL scripts once the corpus covers them.
5. Gate: `make image`.

## Open questions

None.
