# Port the simulator engine to Rust, equivalent to the C engine

**Status:** go-ahead given; prerequisites done; waits on the Python port's harness milestone
**Priority:** 7
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/engine-ports.md`
**Depends on:** `tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md` (done); the shared harness from `tasks/engine-port-python.md` (Plan step 2)

## BLUF

Write a Rust implementation of the spimulator engine that behaves equivalently to the C engine, the
standard: the same stdout, stderr and exit status on the whole shared corpus, checked by the
differential harness. Float-to-text differences are warnings only (`tasks/engine-ports.md`). The
language study rated Rust the strong alternative to Python: the compiler checks the integer widths.
Done means:
- the harness reports zero functional differences for the Rust engine;
- every example's header shows how to run its `.asm` through `spimulator-rust`;
- it builds offline in the image with the standard library only.

## Context

- `tasks/engine-ports.md`: the shared rules (C is the standard, functional equivalence, the
  examples rule, the unconditional image toolchains, the milestone order, the layout `port/rust/`).
- `tasks/reference/engine-port-language-choice.md`: Rust's row, the spike (0.011 s for the ~9M
  instruction loop, against C's 0.008 s), and "Float formatting across ports".
- `tasks/review/2026/11/10/repo-review.md` §5: the behavior inventory to match.
- `tasks/adhoc/engine-ports-float-format/cformat.rs`: the tested `%g`/`%f` formatter, byte-identical
  to glibc in all five of the C engine's float formats. Promote it into the port.
- The shared Rust standard: the `rust-coding-standard.md` reference doc in
  github.com/billsix/runClaudeInContainer. It covers rustfmt, clippy, annotating every binding and
  `// SAFETY:`. Read it first.

## Findings (2026-11-10)

Rust's traps against the C engine, with the planned answer:
- **Overflow panics.** Debug builds panic on integer overflow, and `i32::MIN / -1` panics in any
  build. Use `wrapping_add`/`wrapping_sub`/`wrapping_mul` where MIPS wraps (`addu`). Use
  `checked_add` where MIPS traps (`add`, raising the overflow exception the C engine raises). Write
  the `div` overflow case out explicitly, matching the C engine's guard.
- **Shift amounts.** A shift by 32 or more panics. MIPS uses the low five bits, so mask with `& 31`,
  as the hardware does, or call `wrapping_shl`. `i32 >>` is arithmetic and `u32 >>` logical, which
  matches `sra`/`srl` when the type is chosen per instruction.
- **Float→int.** `as` saturates and sends NaN to 0, but x86 C gives `0x80000000` (review §3.22).
  Match the C engine's observed value with an explicit conversion function.
- **Single precision.** `f32` is real IEEE single, like C's `float`. Bit casts are
  `f32::from_bits`/`to_bits`, and `f32 as f64` widens exactly.
- **Division** truncates toward zero, as in C; `%` is the C remainder.
- **Machine state.** One `Machine` struct (registers, memory segments, the REPL's redo state) passed
  as `&mut`, with no globals. The study's worry about the borrow checker applies to global mutable
  state, which this design avoids.
- **Parser.** Hand-written recursive descent mirroring `src/parser.c`, so the error messages match
  byte for byte. No parser crate.
- **REPL line editing.** `rustyline` would need a crates.io download, so the port uses std only and
  reads lines plainly. The harness drives the REPL without a TTY, which is the path that must match C.
  Line editing is a later, optional addition if a vendored crate is ever wanted.
- **No dependencies.** `cargo build --offline` with an empty `[dependencies]` builds in the image
  with no network, as the self-contained-image rule requires.

## Plan

1. `port/rust/`: a cargo workspace with no dependencies. `meson.build` builds it with
   `cargo build --release --offline` and registers `spimulator-rust` with the shared harness.
2. Milestones in the umbrella's order, each gated by the harness: machine arithmetic (with `cformat`
   promoted and its check against C as a test) → assembler (AST-parity bytes) → executor + syscalls →
   CLI/REPL → teaching mode.
3. Gates: `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, and `cargo test`
   (doctests included, per the doctests-by-default rule).
4. When the harness is clean: the examples step from `tasks/engine-ports.md`, a `spimulator-rust`
   line in every example header, checked by `tools/check_example_commands.py`.

## Decisions

- Port to Rust as well, the third implementation (William Emerison Six <billsix@gmail.com>,
  2026-11-10).
- Standard library only; no crates (the agent's call, for offline builds). Revisit for line editing
  only.
- Go-ahead (William Emerison Six <billsix@gmail.com>, 2026-11-10). The teaching mode (`-explain`) is out of scope for now (`tasks/engine-ports.md`).
