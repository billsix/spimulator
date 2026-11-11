# Fix the C bugs and undefined behavior the ports exposed, then follow in every port

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 6
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.7, §4.3.1, §4.3.2, §4.5.6 F12

## BLUF

The port readers found places where C itself is wrong or undefined, so a port can't match it. The
maintainer decided (the record's question 1): fix C first, then make each port follow. Done means each
item below is fixed in C with a regression test, and the three ports give the same result.

## Context

| # | Item | Evidence |
|---|---|---|
| 1 | `print_all_regs` after `print_all_regs hex` leaves three stale lines | `src/spim.c` `print_all_regs`: its static `str_stream` is cleared by `ss_clear`, which rewinds the `open_memstream` without truncating. Haskell's output is the correct one |
| 2 | `-skt 0` loops forever on "Invalid address (0x80000180) for instruction" | both C and the ports; C used 3.5 GB before it was killed |
| 3 | `ld $31`, `sd $31`, and `mfc1.d`/`mtc1.d` with register 31 write `gpr[32]` | C undefined behavior (it lands in HI); Python raises `IndexError` (`parser.py:790-822`). Reject the last or odd register for the pair forms |
| 4 | `.align N` for N ≥ 32 | `src/data.c:78,82` and `src/instruction.c:99,103` shift unbounded (`1u << 40` is UB); Python computes mask 0 (`data.py:67-72`). The 2026-11-10 review's §3.14. Range-check 0..31 |
| 5 | `-assemble` leaves a trailing `", "` when the data segment doesn't end on a word | C's separator test `addr == end-4`; Python's `", ".join` never does (`spim.py:420-425`) |
| 6 | out-of-range `$fN` (`print $f33`, `print $f4294967294`) | C reads past the array; Python prints `FP reg 1`, Rust "Odd FP double register number" (`cli.rs:539-551`). Range-check `$fN` in the scanner |

- Every C fix changes behavior, so each needs its port changes in the same change, or the port
  suites fail (`CLAUDE.md`).

## Plan

1. For each item: add a regression test (`tests/run-test.sh` plus `meson.build`) that fails today, fix
   C, then fix Python, Haskell and Rust until the port suites pass.
2. Order: 4 and 6 (range checks), then 3, 1, 5, 2.
3. Gate: `make image` (UBSan and ASan included).

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. A text segment too small for the exception handler (`-skt 0`) is a startup error (option (a)).
