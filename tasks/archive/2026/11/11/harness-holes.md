# Close the harness holes: double timeouts, empty selections, missing pgu, stale Rust binary

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §5.4, §5.5, §5.6 F9

## Outcome

- **A hung reference fails.** In `port/harness/compare_engines.py` and `compare_all_programs.py`, a
  case where the C engine times out is a FAIL. Before, both engines timing out compared equal and
  passed (`--reference hang --engine hang --timeout 0.2 core le` printed "2 passed").
- **An empty selection fails.** A case name that isn't in the corpus, or a filter that matches
  nothing, exits 1 instead of running zero cases and passing.
- **pgu is compared, in the image too.** The comparison's `pgu/**/*.s` pattern matched only the
  upstream x86 sources, so the 11 MIPS PGU programs (`pgu/src/*.asm`) were never compared anywhere.
  It now also globs `pgu/**/*.asm`. The image copies just those sources (`pgu/src/*.asm` and the
  upstream `.s` files); before, it copied no `pgu/`, so every pgu case was silently missing in
  `make image`. Each tracked-source pattern must match at least one file, or the run stops naming
  it. `examples/**/*.s`, the native listings the examples build writes, stays optional.
- **The Rust launcher rebuilds a stale binary.** `port/rust/spimulator-rust` rebuilds when a source
  file or `Cargo.toml` is newer than the binary, as the Haskell launcher does. Before, it built only
  when the binary was missing.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. The image copies only the pgu `.s`/`.asm` sources, not the book.

## Verification

- A scratch engine that sleeps: `compare_engines.py --reference hang --engine hang --timeout 0.2`
  now reports both cases FAIL ("the C engine timed out").
- `compare_engines.py … nosuchcase` and `compare_all_programs.py … zzznomatch` exit 1.
- The 77 new PGU cases (11 programs, 7 modes) pass in all three ports.
- After touching `port/rust/src/lib.rs`, the launcher rebuilt the binary; an up-to-date call took
  10 ms.
