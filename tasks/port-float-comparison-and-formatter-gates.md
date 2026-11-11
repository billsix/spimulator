# Compare port floats numerically, and gate the ports' float formatters

**Status:** proposed — needs go-ahead
**Priority:** 2
**Difficulty:** 5
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §5.1, §5.2, §1.2 F2

## BLUF

The harness lets real float errors through. A port printing `0.6` where C prints `0.5` gets only a
WARN, and a WARN passes. Nothing gates the ports' float formatters or `atof`. Done means:
- the harness tolerates only a numeric difference of a few ULP;
- each port's formatter has an oracle test that a meson suite runs;
- the float-format checks live in `tools/` (the maintainer's answer to the record's question 3);
- `tasks/adhoc/integer-conversion-hardening/` is gone;
- the reference docs describe what is actually checked.

## Context

- **The tolerance.** `port/harness/compare_engines.py:118-140` (`last_digit_unit`, `close`), also
  used by `port/harness/compare_all_programs.py`, forgives one unit in the last printed digit. C prints `%g`,
  which drops trailing zeros, so that unit can be large. Reproduce, from `port/harness/`:
  `python3 -c "import compare_engines as c; print(c.compare_text('stdout','0.5\n','0.6\n'))"` gives
  `('WARN', ...)`; `1e+10`→`2e+10` and `3.14159`→`3.1416` give WARN; `0.5`→`0.7` gives FAIL.
- **No formatter gate.**
  - `port/haskell/tests/check-format.sh` and `port/haskell/tests/check-strtod.sh` are not registered in meson.
  - Nothing runs `cargo test`, so `port/rust/tests/cformat_vs_c.rs` and the `port/rust/src/cformat.rs` doctests never
    run. `cformat_vs_c.rs:96-99` also reports "skipped" on any compile failure.
  - The Python `port/python/spimulator_py/cformat.py` has no oracle test.
- **The docs claim otherwise.** `tasks/reference/engine-port-language-choice.md:201,291` say the
  formatting is "checked by" those scripts and `cargo test`. Its two double counts (158,029 vs
  58,028) also disagree.
- **The ad-hoc checks.** `tasks/adhoc/engine-ports-float-format/` holds the checks those docs cite
  (`engine-port-language-choice.md:181,198-200`, `engine-ports-design.md:30`). The maintainer
  decided (record's dated note) to promote them to `tools/`. Also remove
  `tasks/adhoc/integer-conversion-hardening/` (the `-Wconversion` gate replaced it) and its citation in
  `tasks/reference/simulator-integer-types.md:55`.

## Plan

1. Replace the last-digit rule: parse both tokens as doubles, and pass only when they are equal or
   within a few ULP. Keep a WARN only for equal values with different text (`1e+10` vs
   `10000000000`). Add the reproductions above as unit tests of `compare_text`.
2. Promote `tasks/adhoc/engine-ports-float-format/` to `tools/`, cleaned up per the Python
   standard, and register it as a meson test per port. Make it test each port's formatter against
   glibc's `%g`/`%f` over a fixed corpus of values (subnormals, NaN, inf, ties, large exponents).
3. Register `port/haskell/tests/check-format.sh` and `port/haskell/tests/check-strtod.sh` as meson tests, and run
   `cargo test --offline` as one. Make `port/rust/tests/cformat_vs_c.rs` fail, not skip, when a compiler is present
   but the compile fails.
4. `git rm` `tasks/adhoc/integer-conversion-hardening/`, and update the reference docs.
5. Gate: `make image`. If the strict comparison now fails a case, that is a real port bug: fix it in
   the port.

## Open questions

None. The record's questions 2 and 3 were answered as recommended.
