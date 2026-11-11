# Gate each port's lint, type-check and unit tests

**Status:** proposed — needs go-ahead
**Priority:** 4
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §5.3 F6

## BLUF

No gate runs the ports' tier-(a) tools: ruff, `ty`, `check_local_annotations` and the doctests for
Python; clippy, `cargo fmt` and `cargo test` for Rust; `hlint` for Haskell. They all pass when run by
hand today, but only because whoever edits the ports runs them. Done means a meson test per port that
`make image` runs, so a lint or type error fails the image.

## Context

- `grep -rn "ruff\|ty check\|clippy\|cargo fmt\|cargo test\|hlint" Makefile Dockerfile meson.build
  port/meson.build entrypoint/*.sh` finds nothing.
- Haskell builds with `-Wall -Werror` (`port/haskell/build.sh:17`), so its compiler tier is gated
  already.
- The standards: the python-, rust- and haskell-coding-standard.md reference docs from
  github.com/billsix/runClaudeInContainer. Their tier (a) is what these tools enforce.
- Python: `port/python/pyproject.toml` configures ruff and `ty`. Run `ty` from `port/python/`; run
  from the repo root, it ignores the port's config. `check_local_annotations.py` is a SHARED,
  VENDORED tool: vendor it into `tools/` (canonical: runClaudeInContainer
  `tasks/reference/check_local_annotations.py`). `ruff --select RUF100` finds 10 unused `noqa`
  comments (see `tasks/python-port-cleanups.md`).
- The image must have each tool offline: ruff and `ty` (dnf or the venv), clippy and rustfmt
  (`rust-clippy`, `rustfmt`), and `hlint`. Check `entrypoint/05-install-ports.sh`.

## Plan

1. Install the missing tools in `entrypoint/05-install-ports.sh`.
2. Add meson tests `port-python-lint` (ruff check, ruff format --check, `ty`, the annotation check,
   `python -m doctest` or pytest `--doctest-modules` if adopted), `port-rust-lint` (`cargo fmt
   --check`, `cargo clippy --offline --all-targets -- -D warnings`, `cargo test --offline`) and
   `port-haskell-lint` (`hlint`; see question 1).
3. Gate: `make image`. Introduce one lint error per port to confirm each test fails, then revert.

## Open questions

1. `hlint` reports 7 and 12 advisory hints in `port/haskell/src/Parser.hs` and `port/haskell/src/Run.hs` today. Should it be (a) a gate
   at zero hints, after fixing or suppressing them, or (b) advisory, run but never failing?
   Recommend (b), as the Haskell standard says, unless the maintainer wants it strict.
