# Rust port cleanups from the 2026-11-11 review

**Status:** proposed — needs go-ahead
**Priority:** 8
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.5.3, §4.5.4, §4.5.5 F21

## BLUF

Code-quality items in `port/rust/` against the Rust standard (the rust-coding-standard.md reference
doc from github.com/billsix/runClaudeInContainer). None changes behavior. Done means each is fixed,
clippy is clean, and `port-rust` is green.

## Context

- **Unannotated bindings** (§4.5.3; the house rule overrides Google's inference default):
  `cli.rs:560,889`, `machine.rs:303`, `scanner.rs:551`, `parser.rs:183`,
  `instruction.rs:608,722,835,888,906,920`, `spim_utils.rs:32,245`; and closures with no return type
  at `ast.rs:308` and `display.rs:25`.
- **`_ =>` hiding a named case** (§4.5.4): in `run.rs:598` (traps) the `_` is TLTU/TLTIU. Name them,
  and make `_` fatal, as C does. The lane arms in `run.rs:627-659` and `memory.rs:404,454,470` are
  total over `& 3` and can stay.
- **Dead public functions** (§4.5.5): `Machine::report` (`parser.rs:1757`) and `data_bot`
  (`data.rs:218`). rustc doesn't warn because they are `pub` in the library.

## Plan

1. Fix each item.
2. Gate: `cargo clippy --offline --all-targets`, `cargo test --offline`, `meson test --suite port-rust`.

## Open questions

None.
