# Make the Haskell port's register writes strict, and its sbrk non-quadratic

**Status:** proposed — needs go-ahead
**Priority:** 3
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.4.1, §4.4.2 F4

## BLUF

The Haskell port leaks memory. HI/LO are lazy `IORef`s, so a `madd`/`msub` loop that never reads them
builds an ever-longer thunk chain. Its sbrk also copies the whole data segment on every call. Done
means:
- a 3,000,000-iteration `madd` loop runs in roughly constant memory;
- 20,000 `sbrk(16)` calls take well under a second;
- the port suites are green.

## Context

- **The leak.** `port/haskell/src/State.hs:46` `set = writeIORef`, used by `Run.hs:252-262`
  (`setHiLo`, `multiplyAdd`). Measured 2026-11-11: a 3M-iteration `madd $t1, $t2` loop peaked at
  488 MB RSS in Haskell (sampled, so a lower bound; the reviewer saw 910 MB) and at 4 MB in C.
  - The program: `li $t0, 3000000; li $t1, 3; li $t2, 5; loop: madd $t1, $t2; addiu $t0, $t0, -1;
    bnez $t0, loop; li $v0, 10; syscall`.
  - Measure with `/proc/<pid>/status` `VmHWM`, since `/usr/bin/time` isn't installed.
  - The Haskell standard's "strict where state accumulates" rule names exactly this case
    (the haskell-coding-standard.md reference doc from github.com/billsix/runClaudeInContainer).
- **The quadratic sbrk.** `Memory.hs:104-109` `growBytes` allocates a new array and copies the
  segment one boxed element at a time, on every sbrk. 20,000 `sbrk(16)` calls take 0.04 s in C and
  8.9 s in Haskell. Programs that malloc through `examples/lib` hit this.

## Plan

1. Make `set` strict (`set r v = writeIORef r $! v`), which covers every long-lived IORef, and check
   the other state helpers for the same pattern (`modify`).
2. Grow the data segment geometrically and keep a separate logical top, or copy with a bulk
   primitive.
3. Measure both programs before and after, and record the numbers here.
4. Gate: the `port-haskell` suite (with `haskell-annotations`) and `make image`.

## Open questions

None.
