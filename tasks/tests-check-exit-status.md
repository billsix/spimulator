# Check the exit status in every sentinel and demo test

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §6.2 F8

## Outcome

- **`tests/run-test.sh`:** 22 spim runs checked only their output. Each now ends `; rc=$?` and
  `expect_exit N "$rc"`, with N the status the C engine gives. Most programs return main's `$v0`,
  which is 4 after a last `print_str`; the REPL sessions exit 0.
- **The `explain` test** pipes spim into `grep -v`; it now reads spim's status through
  `${PIPESTATUS[0]}`.
- **`examples/tests/run-demo.sh`** compares the asm run's status with the C binary's for every demo,
  not only the two with a pinned `.expected-status` file.

These tests also run against each port, so a port that prints the right output and then crashes now
fails.

## Found on the way

- **`tests/tt.explain.s` exits 132 under `-explain=1` and `-explain=2`, and 0 without `-explain`.**
  The teaching mode's memory peeks raise an exception the program never does: explain-mode bug 1,
  the 2026-11-10 review's §3.6. The explain golden had been pinning that run. The test pins 132,
  with a comment, until `tasks/explain-mode-correctness-bugs.md` fixes it; then it becomes 0.
- **`examples/src/intro/yes/yes.asm`** prints `y` forever, so `harness-holes`' new rule (a C-side
  timeout fails) failed its run and listing modes. They are now skipped by name, with the reason,
  in `port/harness/compare_all_programs.py`.

## How

The expected statuses were read from the C engine, not written by hand. A placeholder
`expect_exit __RC__` went after each run; a scratch copy of the runner, whose `expect_exit` printed
the status and line at each placeholder, ran each test against C; and the 22 values were filled in.

## Verification

Regression 40/40, examples 35/35, and port-python, port-haskell and port-rust all pass.
