# Gate each port's lint, type-check and unit tests

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §5.3 F6

## Outcome

Each port has a lint script, `port/<port>/lint.sh`, which a meson test in its suite runs. Every step
runs, and the script fails if any failed (the gate-script rule).
- `port-python-lint` (`port/python/lint.sh`): `ruff check`, `ruff format --check`, `ty` (run from
  `port/python/` so it reads the port's `pyproject.toml`), `tools/check_local_annotations.py`, and the
  doctests through `port/python/run_doctests.py` (36 today).
  - The annotation checker is the SHARED, VENDORED copy of github.com/billsix/runClaudeInContainer
    `tasks/reference/check_local_annotations.py`; edit the canonical and re-copy it.
  - The doctest runner exists because the modules use relative imports, so `python -m doctest FILE`
    can't load them.
- `port-rust-lint` (`port/rust/lint.sh`): `cargo fmt --check` and `cargo clippy --all-targets -- -D
  warnings`. `cargo test` is `rust-cargo-test`, from `port-float-comparison-and-formatter-gates`.
- `port-haskell-hlint` (`port/haskell/lint.sh`): `hlint`, advisory. It prints its hints (51 today)
  and always passes. The compiler tier is already a gate: `-Wall -Werror`.

`entrypoint/05-install-ports.sh` installs `ruff`, `ty`, `hlint`, `clippy` and `rustfmt`, all Fedora
packages. `port/README.md` lists everything each port suite runs.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `hlint` is advisory: run, never failing.

## Verification

- All three lint tests pass in meson.
- An unused import in a Python module makes `port/python/lint.sh` exit 1, and a misformatted Rust
  function makes `port/rust/lint.sh` exit 1. Both probes were reverted.
