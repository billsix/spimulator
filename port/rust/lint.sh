#!/usr/bin/env bash
# The Rust port's tier-(a) gate (the rust-coding-standard.md reference doc from
# github.com/billsix/runClaudeInContainer): rustfmt and clippy, with warnings as
# errors.  (cargo test is the meson test rust-cargo-test.)  Every step runs, and
# the script fails if any failed.  The meson test port-rust-lint runs it.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0
cargo fmt --manifest-path "$here/Cargo.toml" --check || status=1
cargo clippy --offline --quiet --manifest-path "$here/Cargo.toml" --all-targets -- -D warnings || status=1
exit $status
