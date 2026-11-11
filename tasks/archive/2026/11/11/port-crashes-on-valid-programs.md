# Fix the port crashes on valid programs (sbrk, closed stdout, delay-slot recursion, Rust overflows)

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §4.2, §4.5.1, §4.5.2, §6.3 F3

## Outcome

On each of these programs the C engine exits cleanly, or with its own fatal message, and each port
now gives the same stdout, stderr and exit status. The harness pins all four
(`port/harness/corpus.txt`; the programs are in `tests/`).

| Case | C | Before the fix |
|---|---|---|
| `big_sbrk`: an sbrk of about 2 GB | 255, "realloc failed in expand_data" | Python `ValueError`, Haskell index error, Rust panic (101) |
| `big_stack`: `-sstack 1073741824 -lstack 2000000000`, touch below the stack | 255, "Out of memory at request for 18446744071562067968 bytes." | crashes |
| `close_stdout`: close fd 1, then print | 0 | Python `OSError`, Haskell exit 1 |
| `delay_chain`: 400 chained `j` under `-delayed_branches` | 0 | Python `RecursionError` |

- **Segment growth** (all three ports): the sizes are C ints. A growth that wraps negative passes the
  limit check, and C's allocation then fails. The ports wrap the same way and call `fatal_error`
  with C's messages, for the data, kernel-data and stack segments.
- **Closed descriptors** (Python, Haskell): a write to a descriptor the program closed is dropped,
  as C's stdio drops it. Haskell now writes stdout and stderr with `write(2)` (each write was flushed
  anyway).
- **Python's recursion limit** covers C's delay-slot nesting. C handles 10,000 chained jumps and
  segfaults by 15,000; Python's default limit stopped at about 330.
- **Rust:** `overflow-checks = true` in the release profile (the decision below). The FP-pair
  `+ 1` sites and the REPL step count use `wrapping_*`, as C's ints wrap.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. The Rust overflow gate is `overflow-checks = true` in `[profile.release]`, not a separate
   debug-build run.

## Found and fixed on the way

- **Haskell ignored `SIGPIPE`.** The GHC runtime ignores it, so `spimulator-haskell … | head`
  exited 0 where C dies with 141. `setupHandles` now restores the default action, as the Python port
  already did.
- **Rust lost the end of a `-listing` file on a fatal exit.** `fatal_error`'s `process::exit` runs no
  destructors, so the `BufWriter` lost its tail (260 lines from C, 142 from Rust), where C's `exit()`
  flushes stdio. The listing is now a `LineWriter`. The new `big_sbrk` case found this, through the
  all-programs comparison's listing mode.

## Moved to another task

An `fp_reg_overflow` case (`li.d $f2147483647`) was tried. Python and Haskell crash on it, but C
passes it only by writing out of bounds (undefined behavior), so it belongs with the scanner range
check in `tasks/archive/2026/11/11/c-undefined-behavior-fixes-then-ports.md` (item 6). The Rust `wrapping_add` changes
stay.

## Verification

The regression suite plus port-python, port-haskell and port-rust: 259/259, including each port's
`compare` and `all-programs` cases. ruff, `ty` and clippy are clean.
