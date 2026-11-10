# Port the simulator engine to Rust, equivalent to the C engine

**Status:** engine done (milestones 1-4): zero differences on the harness, the regression suite (less `-explain`), the demos and the documented commands; waits on the coordinator to register it in `port/meson.build` and to add the examples step (see "What registration needs")
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
- **Milestones 2-4 landed as one commit** (the agent's call, 2026-11-10). Options weighed: (a) a commit
  per milestone, each with a reduced crate; (b) one commit for the engine, with each milestone's gate
  checked in order. The C assembler and executor share memory, the exception registers and the symbol
  table (storing an instruction into a bad text address raises a bus error; resolving a label reads
  the instruction before it), so the Rust modules that mirror them only build together, and a
  "milestone 2 only" crate would have been an invented intermediate state. Chose (b). Milestone 1
  (arithmetic, formatter, opcode table) stands alone and has its own commit.
- **One `Machine` struct for the C globals; `run_error` is an unwind** (the agent's call). The C
  engine's `run_error` `longjmp`s to the top level from deep inside a memory access or a syscall.
  Options: (a) a `Result` threaded through every memory access and the 200-case executor; (b) a panic
  with a private payload, caught (`catch_run_error`) exactly where C calls `setjmp`, with a panic hook
  that stays silent for that payload. (b) keeps the executor a straight transcription of `run.c`;
  real panics still print. Chose (b).
- **Instructions are `Rc<RefCell<Instruction>>`** (the agent's call). A forward reference in C holds a
  pointer to the instruction in the text segment and patches it in place; the breakpoint instruction
  is one shared object compared by address. Options: (a) uses that remember the address and look the
  slot up; (b) shared ownership. (a) differs from C when the store failed or the slot was rewritten;
  (b) is C's semantics exactly (`Rc::ptr_eq` for the breakpoint). Chose (b).
- **The instruction fields keep C's unions** (`Instruction::fields`, four bytes with accessors):
  writing the 26-bit target overwrites rs/rt/imm, as in C, and the floating-point names are aliases
  (`fs` is `rd`, `fd` is `shamt`).
- **Input is read with the C library's `read(2)` through `extern "C"`**, not `std::io::stdin` (which
  buffers): the REPL, `read_int` and a program's own `read` syscall share descriptor 0 byte by byte, as
  in C. `open`/`read`/`write`/`close`/`poll`/`signal`/`raise` are the only `unsafe` code, each with a
  `// SAFETY:` comment (`src/console.rs`).
- **The REPL follows the C engine built with libedit** (`-Dline_editing`, the image's build): with
  input that is not a terminal it prints no prompt, takes one line at a time, and prints `"\n"` at end
  of input. The non-libedit `(spim) ` prompt is not reproduced.
- **The opcode table is generated, not typed**: `tools/gen_opcodes.sh` compiles `tools/gen_opcodes.c`
  against `include/` and writes `src/opcodes_table.rs` (committed). It also records which row the C
  engine's `qsort` + `bsearch` pick for each shared encoding, by running them.
- **Where C crashes, the port dies of the same signal** (follows "match C"): `.word -2147483648 / -1`
  raises SIGFPE and a numeric flag with no value (`-stext` last) SIGSEGV, as in C (`die_of_signal`).
  The Python port exits 139 instead; a signal death is closer to C (Python's `returncode` is -8/-11
  for both C and this port).
- **`-explain` (any level 1-4, `-explain`, `-x`) prints "spimulator-rust: the teaching mode (-explain)
  is not supported by this port" and exits 2**; `-explain=0`, `-explain 0` and `-noexplain` are
  accepted (teaching mode off), and an invalid level gets C's message and usage.

## C engine bugs found

Logged, not fixed (the port's rule). The bugs the Python and Haskell ports found (REPL operands, REPL
`.`, code before the first `.data`, a byte load from an empty text word, a run-time error while
loading, `expand_k_data`) were fixed in C by `a0eefb8` while this port was being written; this port
found them too and follows the fixed behavior. Not yet fixed in C:

1. **An assembler expression `-2147483648 / -1` kills the assembler with SIGFPE.** `parse_term` guards
   division by zero but not the one overflowing quotient. Reproducer: a file with
   `.data` / `x: .word -2147483648 / -1` / `.text` / `main: jr $ra`; `spimulator -f x.s` prints
   `Loaded: …` and dies of SIGFPE (shell status 136). The port dies the same way.
2. **A numeric flag as the last argument crashes.** `-stext`, `-sdata`, `-ldata`, `-sstack`, `-lstack`,
   `-sktext`, `-skdata`, `-lkdata` call `atoi(argv[++i])`, which is `atoi(NULL)` at the end of the
   command line. Reproducer: `spimulator -sdata </dev/null` dies of SIGSEGV (status 139). The port dies
   the same way.
3. **`-exception_file` (or `-trap_file`) as the last argument loads no exception handler and no `main`
   declaration**, silently: `exception_file_name` becomes `argv[argc]`, NULL. Reproducer:
   `spimulator -exception_file </dev/null` starts the REPL without the `Loaded:` line. The port does the
   same.

## Deliberate differences (not observable in the tests)

- The REPL does not read or write `~/.spimulator_history` (libedit's history file), and has no line
  editing or Tab completion (std only).
- No SIGINT handler: Ctrl-C ends the port instead of printing "Execution interrupted" and returning
  to the REPL.
- `-mapped_io` does not put the terminal in raw mode (`tcsetattr`); with input that is not a terminal
  (every test) that is what C does too.
- After a blank line repeats `args` or `.`, the C engine reads the next command from the stdio
  `stdin` stream (it buffers ahead); the port reads it from its line source, so it sees end of input.
- `print_string` (and `open`'s path) of an address in the text segment: C prints the bytes of its
  instruction pointers; the port prints nothing. A string running off the end of its segment stops at
  the end.
- `read_float`/`read_double` of `nan(n)`: glibc keeps the payload `n`; the port reads plain NaN.
- Floating-point registers past 31 (`$f40`, which the assembler accepts): C reads and writes past its
  array; the port reads 0 and drops the write.

## What registration needs (for the coordinator)

- **Required before the `rust-all-programs` test can pass:** `port/harness/compare_all_programs.py`
  must exclude (or bound) `tests/timer.s` and `tests/tt.io.s` in the `run` and `-listing` modes. Both
  spin forever without `-mapped_io`; each engine stops after 2^31 - 1 instructions, and whether that
  happens inside the 120 s timeout is a speed race the Rust port wins (43 s) and the debug C build
  loses, so the case reads FAIL though the behavior is the same (Log). A slower port passes only
  because both sides time out. Suggested: exclude them by name with that reason, as `corpus.txt`
  does for its exclusions. (Not changed here: the harness is outside this task's files.)
- `port/meson.build`: add `['rust', 'port/rust/spimulator-rust']` to `ports`. That is all the tests
  need: the launcher builds the binary on its first call if it is missing
  (`cargo build --release --offline` with an empty `[dependencies]`; about 25 s from scratch, quiet
  unless it fails, works in an empty environment), and cargo's own lock makes concurrent first runs
  wait.
- **Recommended**, because the launcher only builds when the binary is missing (a stale binary is not
  rebuilt): build before the tests, e.g. in `port/meson.build`
  `run_command('cargo', 'build', '--release', '--offline', '--manifest-path', meson.current_source_dir() / 'rust' / 'Cargo.toml', check: true)`,
  or in the Dockerfile after the source `COPY`: `cargo build --release --offline --manifest-path port/rust/Cargo.toml`.
  The image already installs `rust` and `cargo` (`entrypoint/05-install-ports.sh`).
- Optional, the port's own checks as a test: `test('rust-cargo-test', find_program('cargo'), args: ['test', '--offline', '--manifest-path', meson.current_source_dir() / 'rust' / 'Cargo.toml'], suite: 'port-rust', timeout: 600)`
  (doctests plus the glibc formatter comparison, which compiles a C oracle with `cc`).
- `.dockerignore` (and `.gitignore` already, via `port/rust/.gitignore`): `port/rust/target/`, so a
  host build is not copied into the image.
- The examples step (`tasks/engine-ports.md`): a `spimulator-rust -f X.asm ...` line in each example
  header, and `tools/check_example_commands.py` taught to run `spimulator-rust`.
  `python3 tools/check_example_commands.py --spimulator $PWD/port/rust/spimulator-rust` already passes
  74/74 on the `spimulator` lines.
- Regenerating the opcode table after a change to `include/opcodes.h`: `port/rust/tools/gen_opcodes.sh`
  (needs `cc` and `rustfmt`), then commit `port/rust/src/opcodes_table.rs`.

## Log

### 2026-11-10 — engine written and checked (agent session in a worktree)

- Worktree branch `worktree-agent-a422e3aa545bb8872`, started on `master`, fast-forwarded to
  `engine-ports` at `bd20729` (the harness and the C prerequisite fixes).
- Read every C file of the engine except `explain.c`, then wrote the port module by module in C
  order (the module map is in `port/rust/src/lib.rs`). It built after one warning and passed the
  whole harness (23/23) on its first run.
- Milestone 1, commit `6454bcb`: `cformat.rs` (promoted), `arith.rs`, the generated opcode table.
  `tests/cformat_vs_c.rs`: identical to glibc on 58,028 doubles in all five formats.
- Milestones 2-4, commit `91449f1` (one commit; Decisions). Bugs found while testing, before that
  commit: the harness runs engines from `tests/`, so the launcher needs an absolute path; bash adds
  `PWD` and `SHLVL` to its children's environment, which the engine copies onto the stack, so the
  launcher re-execs with `/proc/$$/environ` (`env -i`); `check_example_commands.py` calls the engine
  through a symlink (`readlink -f`); `spimulator -f yes.asm | head -3` never ended because Rust starts
  with SIGPIPE ignored (restored to the default); the C oracle test deadlocked writing to a pipe the
  oracle was blocked writing (values now go through a file).
- Merged `engine-ports` at `a0eefb8` (the coordinator's six C fixes) before that commit and followed
  them: emit starts in the segment current when the parse began and emits only the nodes the call
  parsed; `print_all_regs`/`breakpoint`/`delete` with no operand do not read on, and a missing
  breakpoint address sets nothing; a run-time error while setting up or loading exits 1; byte loads
  from an empty text word read 0.
- Results against the C engine built from the merged tree (`meson setup /tmp/claude-0/rsport-ref
  --buildtype=debug`):
  - `port/harness/compare_engines.py`: 23 passed, 0 warned, 0 failed.
  - `tests/run-test.sh`, all 32 regression tests but `explain`: 32 passed
    (`tasks/adhoc/engine-port-rust/run-regression.sh`).
  - `examples/tests/run-demo.sh`: 34 of 34 (`tasks/adhoc/engine-port-rust/run-demos.sh`).
  - `tools/check_example_commands.py --spimulator port/rust/spimulator-rust`: 74/74 agree.
  - `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`: clean; `cargo test`: 17
    doctests and the formatter comparison pass.
  - A wider sweep of my own (`tasks/adhoc/engine-port-rust/diff_more.py`): every `.s`/`.asm` of
    `tests/`, `examples/` and `pgu/` through `-dump`, `-full_dump`, `-print-ast`, `-print-ast-json`,
    `-show-expansion`, `-listing -`, `-assemble`, `-bare` and a run with arguments and stdin, plus 17
    REPL sessions (help, redo, disasm, registers, symbols, step, print forms, breakpoints, reinitialize,
    dump, `.`, args, errors) and 14 flag combinations (`-mapped_io`, sizes, delayed branches/loads,
    `-nopseudo`, `-quiet`, a `;`-list of exception files, ...), comparing stdout, stderr, status and
    every file written: 1138 of 1138 identical.
  - After merging `engine-ports` at `a7753a3`: `port/harness/compare_all_programs.py --jobs 8`:
    1214 passed, 0 warned, 4 failed. The four are `tests/timer.s` and `tests/tt.io.s`, run and
    `-listing`: programs that spin forever without `-mapped_io` (no interrupt is taken, the device is
    never ready). Both engines stop after `DEFAULT_RUN_STEPS` (2^31 - 1) instructions with status 0;
    the port gets there in 43 s, inside the harness's 120 s timeout, and the debug C build does not,
    so the C side reads "timed out". Not a functional difference: run to the end, the C engine gives
    the same output and status 0 on `timer.s` after 13 min 55 s.
- Crash parity, commit `1091420`: where C dies of a signal (C engine bugs 1 and 2), the port now dies
  of the same one (status 136 and 139 from both).
- Speed: `tt.core.s` (with its 10 ms timer spin) runs in 0.11 s, against 0.10 s for the debug C
  build.
