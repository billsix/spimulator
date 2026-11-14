# Spell out the abbreviated names in the Rust port (`port/rust/`)

**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Done:** 2026-11-14, on branch `spelled-out-names-c-core` (commit `ee67ae5`)
**Depended on:** `tasks/archive/2026/11/14/spelled-out-names-c-core.md`

## Summary

The Rust port's own names were spelled out to match the C core's, in Rust's casing: 334 names and
3,994 tokens. `gpr` became `general_purpose_registers`, `ImmExpr` became `ImmediateExpression`,
and `ExcCode::AdEL` became `ExceptionCode::AddressErrorLoad`. `cargo fmt` was applied,
`port/rust/lint.sh` (clippy and rustfmt) passes, and all 86 port-rust meson tests pass, including
the parity runs against the C engine and `rust-cargo-test`.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Once that's done for the C task, I want a follow up task for the python port, haskell port, and
rust port to do the same." The go-ahead to run the ports with no stop for review: "you should
be able to start on the ports without asking me anything".

## How

- **The tool.** The shared `port_names.py` (see
  `tasks/archive/2026/11/14/spelled-out-names-python-port.md`) found the port's declared names
  by declaration patterns.
- **What it leaves alone:**
  - the keywords and primitive types;
  - std's prelude;
  - every name a `use` imports from outside the crate, and every name after `std::`;
  - the `macro_rules!` fragment specifiers (`expr`, `ident`);
  - the module names, which are file names.
- **Method calls.** After a single `.`, a name is renamed only when the port declares it as a
  function or a field. So std's `.len()` stays, while a local `len` becomes `length`. A `..`
  range or struct update is not a member access.
- **Format strings.** Their inline arguments (`"fd={fd}"`) are renamed with their variables;
  the printed text is not.

## Port-only decisions (`data/rust.manual.tsv`)

- **Renames:**
  - `read_le32` became `read_little_endian_32`: here `le` is little-endian, not "less or equal".
  - `LoadDest::Gpr`/`Fpr` became `LoadDestination::GeneralPurposeRegister`/`FloatingPointRegister`,
    singular, as each holds one register number.
  - `REG_RA` became `RETURN_ADDRESS_REGISTER` (C decision 2), and `AsmState` became
    `AssemblerState`.
  - `join_hi_lo`/`split_hi_lo` became `join_multiply_divide`/`split_multiply_divide`.
- **The `ExceptionCode` variants** were renamed by `fix_rust_after_apply.py`, because `Int` is
  also a `ScanValue` variant. Each variant's doc comment starts with the manual's mnemonic
  (`/// AdEL: Address error on a load …`).
- **`console.rs`'s `fd`** stays a POSIX file descriptor.
- **Kept:**
  - the R/I/J format letters (`emit_r`, `i_type`);
  - the grammar shapes (`parse_b2`, `parse_r3`);
  - the syscall names (`print_int`, `read_int`);
  - libc's `PollFd` and the `*_fd` helpers;
  - the numeric helpers (`exponent_i32`, `round_64k`);
  - English-word names (`at_eof`, `text_in_kernel`).
- **Haskell alignment.** The Haskell port's `DirKData`/`DirKText` became `DirectiveKdata` and
  `DirectiveKtext`, matching the other three engines, which keep the `.kdata`/`.ktext` directive's
  own name (commit `ced3497`).

## The ad-hoc scripts

This was the last of the three port tasks, so its archive commit removed
`tasks/adhoc/spelled-out-names-c-core/` and `tasks/adhoc/spelled-out-names-python-port/`. Both are
in the history at commit `ced3497`, for the shared-layer follow-up below.

## Follow-ups

- **Harvesting the method into the shared layer** (github.com/billsix/runClaudeInContainer and
  runCrushInContainer), so the maintainer can ask for the same sweep in another repo in one line.
  - The checklist is in `tasks/archive/2026/11/14/spelled-out-names-c-core.md`, "Follow-ups".
  - The port tasks add these lessons:
    - Find a language's own names as "every identifier except the libraries' exports" (ghci
      `:browse`), rather than by declaration patterns.
    - Check what the output prints from names (Python's `Kind.name`, Rust format arguments).
    - Watch for names that shadow a language's prelude (`length`, `error`).
    - Handle a spelling that means two things, which a whole-tree token rename cannot split, by
      keeping it in the token pass and renaming it in a targeted pass.
  - The maintainer decides the shape at the time.
