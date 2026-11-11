# Compare port floats numerically, and gate the ports' float formatters

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §5.1, §5.2, §1.2 F2

## Outcome

- **The harness's float comparison is numeric.** Two printed floats pass only when they are the same
  value, within 4 ULP (`ULP_TOLERANCE`, `port/harness/compare_engines.py` `close`). A WARN is left
  only for the same value printed differently (`1e+10` vs `10000000000`). The earlier rule forgave
  one unit in the last printed digit; C prints `%g`, which drops trailing zeros, so `0.5`→`0.6`,
  `1e+10`→`2e+10` and `3.14159`→`3.1416` were warnings, and a warning passes. Those cases are
  doctests in `close`, run by the new meson test `harness-doctests`.
- **Each port's float formatter is gated against glibc's printf,** in the C engine's five formats:
  - `python-float-format`: the new `tools/check_float_format.py`, 158,029 doubles, against
    `port/python/spimulator_py/cformat.py`. Python had no check before.
  - `haskell-float-format` and `haskell-strtod`: `port/haskell/tests/check-format.sh` and
    `check-strtod.sh`, which existed but were never registered.
  - `rust-cargo-test`: `cargo test --offline`, the `port/rust/src/cformat.rs` doctests and
    `port/rust/tests/cformat_vs_c.rs`, which never ran in any gate. `cformat_vs_c.rs` now fails when a
    C compiler runs but the oracle doesn't compile; it skips only when there is no `cc`.
- **The ad-hoc folders are gone.** `tasks/adhoc/engine-ports-float-format/` held early copies of the
  formatters, now in the ports; its glibc oracle is now `tools/float_format_oracle.c`.
  `tasks/adhoc/integer-conversion-hardening/` was a one-shot scan that the `-Wconversion` gate
  replaced; it is in git history.
- **The docs cite the live checks:** `tasks/reference/engine-port-language-choice.md` (the formatter
  table names each check), `tasks/reference/engine-ports-design.md` (the new tolerance),
  `tasks/reference/simulator-integer-types.md`, and the comments in `cformat.py` and `CFormat.hs`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

- The harness compares floats numerically, within a few ULP, and the printed-digit rule goes (the
  review's question 2).
- The float-format checks move to `tools/`, and the integer-conversion scan is removed (question 3).

## Verification

- The five new tests pass: `python-float-format`, `haskell-float-format`, `haskell-strtod`,
  `rust-cargo-test` and `harness-doctests`.
- Under the strict rule, each port's `compare` (28 cases) and `all-programs` (1,256) pass, with 0
  warnings and 0 failures.
