# Close the harness holes: double timeouts, empty selections, missing pgu, stale Rust binary

**Status:** proposed — needs go-ahead
**Priority:** 4
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §5.4, §5.5, §5.6 F9

## BLUF

Four ways the port comparison can pass without comparing anything. Done means each one fails, or is
fixed, and is covered by a self-test of the harness.

## Context

- **Both engines time out → pass.** `port/harness/compare_engines.py:103-104` and
  `compare_all_programs.py:100-105` return the same placeholder for each side.
  `compare_engines.py --reference hang --engine hang --timeout 0.2 core le` prints "2 passed", exit 0.
- **An empty selection → pass.** A case name that matches nothing runs zero cases and exits 0.
- **pgu is skipped in the image.** `compare_all_programs.py:53` globs `pgu/**/*.s` (62 files, 7 modes
  each), but the Dockerfile copies no `pgu/`, so inside `make image` those cases don't exist. The
  script's docstring says pgu is covered.
- **The Rust launcher never rebuilds.** `port/rust/spimulator-rust:13` builds only when the binary is
  missing; `port/haskell/spimulator-haskell` checks source timestamps. In a dev shell, an edit to
  `port/rust/src`, or a regenerated `port/rust/src/opcodes_table.rs`, leaves the suites testing the old binary.
  The image is safe because `.dockerignore` drops `target/`.

## Plan

1. A C-side timeout is a FAIL. A selection of zero cases is a FAIL.
2. `COPY pgu/` into the image, or just its `.s` files and what they include. Make
   `port/harness/compare_all_programs.py` assert a minimum number of sources (the tracked count) so a missing tree
   can't pass.
3. Make the Rust launcher run `cargo build --release --offline --quiet` every time; it is a no-op when
   up to date.
4. Gate: `make image`; check that each hole now fails, using a scratch "hang" engine.

## Open questions

1. Copy all of `pgu/` into the image, or only what the comparison needs? Recommend only the `.s`
   sources and their includes, to keep the engine layers' cache independent of the book.
