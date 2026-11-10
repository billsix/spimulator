# Ported the simulator engine to Rust, equivalent to the C engine

**Created:** 2026-11-10 · **Completed:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/archive/2026/11/10/engine-ports.md`

## Summary

`port/rust/spimulator-rust` (a launcher that builds `port/rust/` with
`cargo build --release --offline`; standard library only, about 10,800 lines) is a Rust
implementation of the engine without the teaching mode. It behaves the same as the C engine, the
standard, everywhere tested:
- the `port-rust` meson suite: 70 tests;
- `port/harness/compare_engines.py`: 23 cases;
- `port/harness/compare_all_programs.py`: 1,214 cases, plus four skipped by name because the C side
  times out (below).

There were no float-formatting warnings: `cformat.rs` is in the port. `cargo fmt`, `clippy -D
warnings` and `cargo test` are clean. Every example's BUILD AND RUN header shows the
`spimulator-rust` run. The port was written in parallel with the other two, in a separate git
worktree, by an agent from a written brief. The coordinating session merged it last and registered
it. The port found all six C bugs fixed in `a0eefb8`, independently, and three more, fixed in
`6c27911`. It is the fastest engine of the four (`time.s` and `timer.s` against a debug C build).
The few differences not observable in the tests (no line editing, Ctrl-C ends the run) are listed
below.

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

## Decisions

- Port to Rust as well, the third implementation (William Emerison Six <billsix@gmail.com>,
  2026-11-10).
- Standard library only; no crates (the agent's call, for offline builds). Revisit for line editing
  only.
- Go-ahead (William Emerison Six <billsix@gmail.com>, 2026-11-10). The teaching mode (`-explain`) is out of scope for now (`tasks/archive/2026/11/10/engine-ports.md`).
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
- **Where C crashed, the port died of the same signal** (follows "match C"; commit `1091420`),
  until C was fixed in `6c27911` (C engine bugs found); the port now follows the fix.
- **`-explain` (any level 1-4, `-explain`, `-x`) prints "spimulator-rust: the teaching mode (-explain)
  is not supported by this port" and exits 2**; `-explain=0`, `-explain 0` and `-noexplain` are
  accepted (teaching mode off), and an invalid level gets C's message and usage.

## C engine bugs found

Logged, not fixed here (the port's rule); the coordinator fixed each in C, and the port follows.
The bugs the Python and Haskell ports found (REPL operands, REPL `.`, code before the first `.data`,
a byte load from an empty text word, a run-time error while loading, `expand_k_data`) were fixed in C
by `a0eefb8` while this port was being written; this port found them too. The three this port
found were fixed in C by `6c27911`:

1. **An assembler expression `-2147483648 / -1` killed the assembler with SIGFPE.** `parse_term`
   guarded division by zero but not the one overflowing quotient (and `*`, `+`, `-` overflowed as
   undefined behavior). Reproducer: `.data` / `x: .word -2147483648 / -1` / `.text` / `main: jr $ra`;
   `spimulator -f x.s` died of SIGFPE (status 136). Fixed in `6c27911`: the constant arithmetic wraps
   at 32 bits (`INT_MIN / -1` is `INT_MIN`; `/ 0` still gives 0); test `engine_fixes` case 10. The
   port, which had died of the same signal (`1091420`), now wraps (`wrapping_div`; `*`, `+`, `-`
   already wrapped).
2. **A numeric flag as the last argument crashed.** `-stext`, `-sdata`, `-ldata`, `-sstack`,
   `-lstack`, `-sktext`, `-skdata`, `-lkdata` called `atoi(argv[++i])`, `atoi(NULL)` at the end of the
   command line: `spimulator -sdata </dev/null` died of SIGSEGV (status 139).
3. **`-exception_file` (or `-trap_file`) as the last argument silently loaded no exception handler.**
   `spimulator -exception_file </dev/null` started the REPL without the `Loaded:` line.

   Bugs 2 and 3 fixed in `6c27911`: each of the ten value-taking flags given last prints
   `spimulator: FLAG requires a value` (the flag as typed) and the usage text, and parsing goes on,
   as for a bad `-explain` level; test `missing_flag_value`. The port does the same
   (`flag_value_missing` in `src/cli.rs`); its `die_of_signal` is gone.

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

### 2026-11-10 — following the C fixes of `6c27911`

- The coordinator merged this branch into `engine-ports` (`0427cf2`), registered the port (`b965190`)
  and fixed the three C bugs this port found (`6c27911`). The worktree branch was fast-forwarded to
  `engine-ports` at `b965190` and the port changed to match: constant arithmetic wraps (`/ -1`
  included); the ten value-taking flags given last report "requires a value" and print the usage.
  This was done in the worktree, not the main checkout: the agent's sandbox refuses git commands
  outside its worktree, so the branch is to be merged (fast-forward) into `engine-ports`.
- Gate: a fresh build of this tree (`meson setup /tmp/claude-0/rsport-main --buildtype=debug`),
  `meson test --suite port-rust`: 70 OK, 0 failed. `cargo fmt --check`, clippy `-D warnings` and
  `cargo test` clean.
