# Check the exit status in every sentinel and demo test

**Status:** proposed — needs go-ahead
**Priority:** 4
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §6.2 F8

## BLUF

About 45 tests check only output. A port, or the C engine, that prints the right lines and then
crashes passes them, although `port/README.md` says ports must match the exit status. Done means
every test in `tests/run-test.sh` and `examples/tests/run-demo.sh` checks the status it expects.

## Context

- `tests/run-test.sh:39-113`: the sentinel tests call `expect_sentinel` and never look at `$?`.
  `stderr_split` (around :234) doesn't either.
- `examples/tests/run-demo.sh:270-276` checks the status only for `exit-demo` and `atexit-demo`
  (`<demo>.expected-status`).
- `tests/run-test.sh:352` pipes spim into `grep -v`, which loses spim's exit status (§5.10).
- These tests also run against every port (`port/meson.build`), so the fix covers all four engines
  at once.

## Plan

1. In `tests/run-test.sh`, capture `rc` for each sentinel test and `expect_exit` the status the program
   returns (most return main's `$v0`; look each up by running C). Use `${PIPESTATUS[0]}` at :352.
2. In `examples/tests/run-demo.sh`, compare the C binary's status with the asm run's status for every demo, not only
   the two with `.expected-status` files.
3. Gate: `meson test` (regression, examples, and the port suites) and `make image`.

## Open questions

None.
