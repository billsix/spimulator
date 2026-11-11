# Fix the port crashes on valid programs (sbrk, closed stdout, delay-slot recursion, Rust overflows)

**Status:** proposed — needs go-ahead
**Priority:** 2
**Difficulty:** 5
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.2, §4.5.1, §4.5.2, §6.3 F3

## BLUF

On these valid programs the C engine exits cleanly, or with its own fatal message, while a port
crashes. Done means each port gives C's stdout, stderr and exit status for each one, with a
`port/harness/corpus.txt` case per program so the comparison keeps them that way.

## Context

All re-run on 2026-11-11 against the C engine (`builddir/spimulator`) and the three ports.

| Input | C | Python | Haskell | Rust |
|---|---|---|---|---|
| `li $a0,0x7ffff000; li $v0,9; syscall; lw $a0,0x10000000` | 255, "realloc failed in expand_data" | 1, `ValueError: negative count` (`memory.py:106-107`) | 1, `Ix{Int}.index` (`Memory.hs:113-129`); other sizes pass silently | 101, panic (`memory.rs:241-291`), in release too |
| close fd 1 (`li $a0,1; li $v0,16; syscall`), then print_string | 0 | 1, `OSError: [Errno 9]` (`console.py:43-55`) | 1 | 0 |
| 400 chained `j` under `-delayed_branches` | 0 | 1, `RecursionError` (`run.py:220-226`) | 0 | 0 |
| `li.d $f2147483647, 1.0`, `mtc1.d $t0, $f2147483647` | 0 | — | — | debug build panics: `+ 1` (`parser.rs:804,823`) |
| REPL `step -2147483648` after a breakpoint | 0 | — | — | debug build panics: `steps -= 1` (`spim_utils.rs:227`) |

- The sbrk fix in each port is to treat a negative new size as C does: `fatal_error` with C's
  messages, "realloc failed in expand_data\n", "realloc failed in expand_k_data\n", and
  "Out of memory at request for N bytes.\n" for the stack. C's `src/memory.c` `expand_data`,
  `expand_k_data` and `expand_stack` are the reference; in Haskell, also compute `delta` in `Int32`,
  as C does.
- A write to a closed fd fails silently in C's stdio. The ports should ignore `EBADF` on output.
- The Python delay slot is a recursive `run_spim` call per nested slot. Run it iteratively.
- Rust's release profile has `overflow-checks` off, so the last two panics show only in debug
  builds. Use `wrapping_add` and `wrapping_sub`.

## Plan

1. Add each program to `port/harness/corpus.txt` (with its flags), and confirm each fails today.
2. Fix them one engine at a time, starting with sbrk in all three ports.
3. Add a debug-build Rust run of the harness, or set `overflow-checks = true` in `[profile.release]`
   (and spell every intended wrap `wrapping_*`). See question 1.
4. Gate: `make image` (all port suites and `<port>-compare`).

## Open questions

1. For the Rust overflow gate: (a) set `overflow-checks = true` in the release profile, or (b) add
   a debug-build harness run? Recommend (a): one build, and every intended wrap becomes explicit.
