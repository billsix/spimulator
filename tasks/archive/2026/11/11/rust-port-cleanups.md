# Cleaned up the Rust port against the Rust standard

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.5.3, §4.5.4, §4.5.5 F21

## Summary

No behavior changed. A fresh subagent did the work with git forbidden, and the coordinator reviewed
and committed it.

- **Annotations** (the house rule overrides Google's inference default): every listed binding now
  has a type (`&Registers`, `&mut Options`, `&Label`, `Ref<'_, Instruction>`/`RefMut<'_, Instruction>`
  at the six borrow sites, the panic hook's `Box<dyn Fn …>`). A grep of `port/rust/src` found no
  other unannotated `let` outside the generated `opcodes_table.rs`. The two unit closures
  (`ast.rs` `mnemonic`, `display.rs` `gpr`) got `-> ()`. A closure binding can't take a type in
  stable Rust, so closures carry parameter and return types only, as the rest of the crate does.
- **Trap dispatch** (`run.rs`, `execute`): the `_` arm that stood for TLTU/TLTIU names them, and a
  new `_` arm is a fatal "internal error: run_spim: trap opcode N", commented as never reached. C has
  no message to match: each trap is its own case of the main switch there. The `& 3` lane arms in
  `run.rs` and `memory.rs` were total and stayed.
- **Dead functions:** `Machine::report` (`parser.rs`) and `Machine::data_bot` (`data.rs`) had no
  callers in src, tests or the launcher, so they were removed along with the imports they left
  unused.

Gates: `cargo fmt --check`; `port/rust/lint.sh` (clippy `--all-targets -D warnings`); `cargo test
--offline`; `meson test --suite port-rust`, 82/82 (comparisons with C included).
