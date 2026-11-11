# Port the teaching mode (`-explain`) to the Python, Haskell and Rust engines

**Status:** proposed — needs go-ahead
**Depends on:** the C teaching mode's correctness fixes and redesign: `tasks/archive/2026/11/11/explain-mode-correctness-bugs.md` and `tasks/explain-levels-redesign.md`.
**Priority:** 8
**Difficulty:** 7
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Follows:** `tasks/archive/2026/11/10/engine-ports.md`

## BLUF

The three ports (`port/python/`, `port/haskell/`, `port/rust/`) implement everything the C engine
does except the teaching mode: given `-explain`, each says it is not supported and exits 2. The
maintainer decided to port the teaching mode only after the C version is fixed and redesigned,
because porting it first would port output that is about to change. Done means each port runs
`-explain` at every level with output identical to C, and the port suites stop skipping the
`explain` test (`port_skipped_tests` in `port/meson.build`).

## Context

- `src/explain.c` (about 2,600 lines) is the C implementation; the levels and the learner model are
  in `tasks/reference/teaching-mode-learner-model.md`.
- The ports' structure and the harness: `port/README.md`, `tasks/archive/2026/11/10/engine-ports.md`.
- A parity test belongs with the C fixes (`tasks/archive/2026/11/11/explain-mode-correctness-bugs.md`, "Note from the
  engine-port prerequisites"): a program's stdout and exit status must not change with `-explain`.

## Plan

1. When the C tasks are done, add explain cases to `port/harness/corpus.txt` (small programs; the
   narration of `tests/tt.core.s` is about 45 million lines a minute).
2. Port the teaching mode to each engine, in the order Python, Haskell, Rust, each gated by the port
   suite with `explain` no longer skipped.
