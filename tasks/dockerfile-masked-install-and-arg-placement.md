# Fix the Dockerfile's masked base-install status, and declare each ARG at first use

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 1
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §8.1, §8.3, §8.2 F14

## BLUF

The base-install `RUN` exits 0 even when `entrypoint/01-install-base.sh` fails, and every `ARG` is declared at
the top, so toggling one re-runs every slow layer below it. Done means a failed group script fails its
`RUN`, each `ARG` sits just before the first layer that reads it, and the two stale comment paths point
at the archive.

## Context

- `Dockerfile:60-68`: `01-install-base.sh && echo … ; if [ "$USE_EMACS" = "1" ]; then …; fi ;`. The
  `;` makes the `RUN`'s status the `if`'s. Reproduce:
  `sh -c 'false && echo x ; if [ 0 = 1 ]; then :; fi'; echo $?` prints 0.
- `ARG`s at `Dockerfile:3-26`; `BUILD_TREE_SITTER` is first read at :195, `RUN_SANITIZERS` at :150.
  The container template's rule: a build-arg's value is part of every later layer's cache key, so
  declare it just before its first use (the personal overlay's Dockerfile contract;
  github.com/billsix/geometricalgebra is the worked example).
- Stale comments: `Dockerfile:175` cites `examples/tasks/PLAN-asm-listings-makefile.md` (now
  `tasks/archive/2026/05/24/PLAN-asm-listings-makefile.md`); `:180` cites
  `tasks/examples-makefile-debug-build.md` (now `tasks/archive/2026/10/31/`).

## Plan

1. Chain the base-install `RUN` with `&&` throughout, so the `if` runs only after success.
2. Move each `ARG` to just before its first `RUN`.
3. Fix the two comment paths.
4. Gate: `make image`, then toggle `BUILD_TREE_SITTER` and confirm the base layers come from cache.

## Open questions

None.
