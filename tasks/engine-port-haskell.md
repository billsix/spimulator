# Port the simulator engine to GHC Haskell, equivalent to the C engine

**Status:** in progress: the engine is written and passes every check (Log, 2026-11-10); owed: the
coordinator registers it in `port/meson.build` and adds the example-header lines ("What registration
needs" below)
**Priority:** 7
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/engine-ports.md`
**Depends on:** `tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md` (done); the shared harness from `tasks/engine-port-python.md` (Plan step 2)

## BLUF

Write a GHC Haskell implementation of the spimulator engine that behaves equivalently to the C
engine, the standard: the same stdout, stderr and exit status on the whole shared corpus, as
checked by the differential harness (`tasks/engine-ports.md`). The maintainer wants to see how a
pure functional language handles a stateful machine. The language study's blocker, GHC printing
`%.18g` wrongly, would not gate the port anyway, since float-to-text differences are warnings
(`tasks/engine-ports.md`). It is also solved and measured (Findings). Done means:
- the harness reports zero differences for the Haskell engine (float-to-text warnings allowed);
- every example's header shows how to run its `.asm` through `spimulator-haskell`;
- it builds offline in the image with GHC's own libraries only.

## Context

- `tasks/engine-ports.md` — the shared rules: the C engine is the standard, what equivalent means,
  the layout (`port/haskell/`), the milestone order.
- `tasks/reference/engine-port-language-choice.md` — why the study did not recommend Haskell
  (formatting, state threading), and the measured `%.18g` fix.
- `tasks/review/2026/11/10/repo-review.md` §5 — the behavior inventory to match.
- `tasks/adhoc/engine-ports-float-format/` — the formatter proof (run `bash tasks/adhoc/engine-ports-float-format/run.sh`).

## Findings (2026-11-10)

**The printing issue, and the fix.** GHC's `Text.Printf` `%.18g` prints `0.300000000000000040` for
`0.1 + 0.2`, where glibc prints `0.300000000000000044` (re-measured with GHC 9.10.3). GHC formats
from the *shortest* digits that read back as the same Double (`floatToDigits`:
`0.30000000000000004`) and pads them with zeros. glibc formats the Double's *exact* binary value,
rounded to the requested digits. `Numeric.showFFloat (Just n)` starts from the same shortest digits,
so it has the same problem.

The fix is a small pure-Haskell formatter. `toRational` turns a Double into its exact value, a
`Rational`. From there, `%.Pg` and `%.Pf` are integer arithmetic:
1. Find the decimal exponent `X` by exact comparison with powers of ten.
2. Round `|x| / 10^(X-P+1)` to an integer, ties to even (glibc's default rounding mode, applied to
   the exact value).
3. Carry into the next power of ten when the rounding overflows.
4. Choose fixed or exponential notation by C's rule (`X < -4 || X >= P`), strip trailing zeros, and
   print a two-digit-minimum exponent.

`inf`/`-inf`, and `nan`/`-nan` by the sign bit, are special-cased. The `#` flag keeps trailing zeros
and the point, and a width pads on the right.

The C engine prints floats in five formats:
- `%.8f` and `%.18g`: the `print_float`/`print_double` syscalls (`src/syscall.c`);
- `%.17g` and `%g`: the AST dump (`src/ast.c`);
- `%.18g` and `%g`: the data listing and the REPL `print` (`src/spim.c`);
- `%#-13.6g`: the FP register display (`src/display-utils.c`).

`tasks/adhoc/engine-ports-float-format/CFormat.hs` (about 90 lines) implements all five. Rust and Python formatters sit beside it, tested the same way. On 158,029
values it is **byte-identical to glibc `printf`** in every one of them. The values were random
64-bit patterns, values near every decimal exponent from 1e-30 to 1e30, subnormals, the extremes,
±0, ±inf, ±nan, eighths and single-precision values.

**Other risks, with the planned answer:**
- **Fixed-width integers:** `Data.Int`/`Data.Word` (`Int32`, `Word32`, …) wrap like C's unsigned
  types. Use `quot`/`rem`, which truncate like C, never `div`/`mod`, which floor.
  `Data.Bits.shiftR` on `Int32` is arithmetic and on `Word32` logical, which matches MIPS `sra`/`srl`
  when the type is chosen per instruction.
- **Single precision:** GHC's `Float` is real IEEE single, so `add.s` rounds like C without
  emulation. This is an advantage over Python. Bit casts are `castWord32ToFloat` /
  `castFloatToWord32` (`GHC.Float`), and widening is `float2Double`.
- **What C leaves undefined:** FP→int conversion of NaN or an out-of-range value is undefined in C
  (x86 gives `0x80000000`; review §3.22), and so is GHC's `truncate`. Match the C engine's observed
  value explicitly, as the Python port must.
- **Mutable machine state:** registers and memory segments live in `IOUArray`s (`Data.Array.IO`),
  the scalar state in a record of `IORef`s, and the executor runs in `IO`. This is the clearest
  transcription of a stateful machine. The pure parts are pure functions: decoding, the assembler's
  parse to an AST, the formatter, the arithmetic. That pure/stateful split is the lesson this port
  can teach that the Python one cannot.
- **Parser:** `parsec` (it ships with GHC), mirroring `parser.c`'s grammar and error messages.
  Message text must match C byte for byte, so error paths come straight from the C code, not
  parsec's default messages.
- **REPL:** `haskeline` (it ships with GHC) for line editing, the counterpart of `-Dline_editing`.
  The harness drives the REPL without a TTY, so the non-interactive path must match C exactly.
- **Speed:** not a goal. The harness corpus must still finish inside the meson test timeout.

GHC 9.10.3 (Fedora `ghc-compiler`) ships `base`, `array`, `containers`, `bytestring`, `text`,
`mtl`, `transformers`, `parsec`, `haskeline`, `process`, `directory` and `filepath`. The port uses
only these, so it builds offline with `ghc --make` or `cabal build --offline` and needs no Hackage
download.

## Plan

1. [x] Image: `ghc` comes from the unconditional `entrypoint/05-install-ports.sh` (it exists).
2. [x] `port/haskell/`: built with plain `ghc --make` (`port/haskell/build.sh`), GHC's own libraries
   only; a `.cabal` file sits beside it for editors (Decisions). [ ] `port/meson.build` registers it
   (the coordinator; "What registration needs").
3. [x] Milestones in the umbrella's order, each gated by the harness: arithmetic (with `CFormat` promoted
   from the ad-hoc proof, plus its C-vs-Haskell check as a test) → assembler (AST-parity bytes) →
   executor + syscalls → CLI/REPL. [ ] Teaching mode (out of scope for now).
4. [ ] When the harness is clean (it is): the examples step from `tasks/engine-ports.md`, a `spimulator-haskell`
   line in every example header, checked by `tools/check_example_commands.py`.
5. Style: `-Wall -Werror`, every top-level binding with a type signature, `hlint` if it can run
   offline. No Haskell coding-standard doc exists in github.com/billsix/runClaudeInContainer yet;
   write the port's conventions in `port/haskell/README.md`, and propose a shared standard doc only if
   a second Haskell project needs it.

## Decisions

- Port to GHC Haskell as well as Python; both must behave equivalently to the C engine, the standard
  (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- Print floats with the exact-`Rational` formatter (Findings), not `Text.Printf` or a C FFI call to
  `snprintf`. FFI would match trivially, but the port would then depend on the C library for the
  output it is being compared on. The agent's call; revisit only if the formatter shows a difference.
- Go-ahead (William Emerison Six <billsix@gmail.com>, 2026-11-10). The teaching mode (`-explain`) is out of scope for now (`tasks/engine-ports.md`).
- Build with plain `ghc --make`, not `cabal build --offline` (the agent's call, 2026-11-10). Measured:
  with an empty cabal home, `cabal build --offline` stops with "The package list for
  'hackage.haskell.org' does not exist. Run 'cabal update'", although every dependency ships with
  GHC; the image has no index and must build offline. `ghc --make -isrc` needs nothing else (about 15 s
  from scratch). `port/haskell/spimulator-haskell.cabal` is kept, not used by the build, for editors and
  for anyone with an index.
- No parsec (the agent's call). The C parser classifies each word when it is scanned, through a 3-token
  look-ahead buffer and a one-shot "next word is an identifier" flag, against a symbol table that the
  parse itself changes; its error texts, the lines they name and its recovery are output to match.
  Parsec's backtracking and messages do not map onto that, so the scanner and parser are a
  statement-by-statement transcription in `IO`.
- Machine state in one record of `IORef`s and `IOUArray`s passed as the first argument
  (`port/haskell/src/State.hs`), not a `ReaderT`/`StateT` stack: the plainest reading for a student who
  knows the C globals, and every function's type says whether it touches the machine. Only the scanner
  runs in `StateT ScanState IO`, since its state is one value the REPL saves and restores whole.
- Instructions keep the C struct's overlapping union as one `Word32` with byte accessors
  (`port/haskell/src/Types.hs`): the engine relies on the overlap (FD lives in the shamt byte, a jump
  target overwrites the register fields).
- Console input through the C library's `read` on fd 0, one byte at a time (FFI), and the program's
  open/read/write/close system calls through the FFI as well; no `unix` package (not in the list of
  allowed libraries). A GHC `Handle` would buffer input that the REPL and the program's system calls
  must share.
- The launcher passes on exactly the environment it received (`/proc/$$/environ` + `env -i`): the engine
  copies the environment onto the simulated stack, so bash's additions (PWD, SHLVL, `_`) would move `$sp`
  and every stack address. Checked: under `env -i FOO=bar` both engines show the same `$a1`, `$a2`,
  `$sp`. The binary is linked `-rtsopts=ignoreAll`, so `+RTS` in a program's arguments reaches the
  program (checked with the echo example).
- On a terminal the REPL uses haskeline with history in `~/.spimulator-haskell_history`, not the C
  engine's `~/.spimulator_history` (libedit's file format differs; sharing it would corrupt the C
  engine's history). Without a terminal it reads lines itself, byte by byte, and prints no prompt, as
  the C engine does with libedit.
- With no `-exception_file`, the binary walks up from its own directory to the first `src/exceptions.s`
  and calls it `<embedded exceptions.s>`, as the C engine calls its built-in copy.
- Milestones 2-4 landed in one commit: the command line is what runs the assembler and the executor, so
  neither could be checked without it; milestone 1 (tables, formatter, `atof`) is its own commit.

## C engine bugs found

Logged, not fixed (the port's rule). The port follows C unless noted.

1. **REPL `print_all_regs`, `breakpoint` and `delete` with no operand end the session.** Each reads one
   token for its operand (here the newline), then `flush_to_newline` reads on past the end of the line's
   input, and `read_token` exits with status 0. Reproducer:
   `printf 'print_all_regs\nprint $sp\n' | spimulator` prints the registers and stops; `print $sp` never
   runs. `printf 'breakpoint\nprint $sp\n' | spimulator -noexception` prints nothing at all. The port
   does the same.
2. **REPL `.` (assemble this line) assembles the last loaded file again.** `parse_file` appends the line
   to the tree of the last file, which was already emitted, and `emit_ast` emits the whole tree again.
   Reproducer: a 5-instruction file `x.s` with `main:` on its first instruction;
   `printf 'load "x.s"\n. addiu $t0,$zero,7\nprint_symbols\n' | spimulator -noexception` shows `main`
   at 0x00400014, the second copy. The port does the same.
3. **A byte or half-word load from an empty text-segment word crashes.** `bad_mem_read` reads
   `ENCODING(text_seg[...])` without the null check its word case has. Reproducer: `lui $t1, 0x40`,
   `ori $t1, $t1, 0x1000`, `lb $t0, 0($t1)` (past the code): the C engine segfaults (status 139). The
   port reads 0 and carries on; it cannot crash the same way.
4. **A run-time error while assembling, in batch mode, `longjmp`s through a `jmp_buf` that was never
   set.** E.g. `spimulator -noexception -stext 8 -f prog.s`: "Can't expand text segment", then a segfault
   (status 139). The port prints the same message and exits 1.
5. **`expand_k_data` zeroes the wrong range** (from `k_data_seg_b + old_size / BYTES_PER_WORD`), so kernel
   data grown past its initial 64 KB may hold `realloc` garbage. Not observable deterministically; the
   port zeroes the new memory.

## What registration needs (for the coordinator)

- `port/meson.build`: add `['haskell', 'port/haskell/spimulator-haskell']` to `ports`. Nothing else is
  required: the first test that runs the launcher builds the binary (`port/haskell/build.sh`: plain
  `ghc --make`, offline, about 15 s; concurrent first runs wait on a `flock`), and later runs reuse it.
  The 600 s per-test timeout covers the build.
- Optional, to build before the tests rather than inside the first one: in `port/meson.build`,
  `run_command(meson.current_source_dir() / 'haskell' / 'build.sh', check: true)` (adds the 15 s to
  `meson setup`).
- Optional, the port's own checks as tests of its suite (they need gcc, python3 and ghc, all in the
  image):
  `test('haskell-format', find_program('bash'), args: [meson.current_source_dir() / 'haskell' / 'tests' / 'check-format.sh'], suite: 'port-haskell')`,
  and the same for `check-strtod.sh` (about 5 s and 2 s).
- `.dockerignore`: add `port/haskell/build/`, so a host build is not copied into the image.
- The examples step (`tasks/engine-ports.md`): a `spimulator-haskell -f X.asm ...` line in each example
  header, and `tools/check_example_commands.py` taught to run `spimulator-haskell`.
  `python3 tools/check_example_commands.py --spimulator $PWD/port/haskell/spimulator-haskell` already
  passes 74/74 on the `spimulator` lines.
- The harness needs an absolute engine path (it runs from `tests/`); `port/meson.build` already passes one.

## Log

### 2026-11-10 — engine written and checked (agent session in a worktree)

- Worktree branch `worktree-agent-a57d66e8781a2f9c4`, based on `engine-ports` at `b0e455a` (the harness
  and the C prerequisite fixes); the worktree started on `master` and was fast-forwarded to it.
- Read every C file of the engine except `explain.c`, then wrote the port module by module in C order
  (`port/haskell/README.md` has the map). It compiled under `-Wall -Werror` after a handful of fixes
  and passed the whole harness on its first full run.
- Milestone 1, commit `373baa9`: `port/haskell/tools/GenOpcodes.hs` generates `src/Opcodes.hs` from
  `include/opcodes.h`; `OpcodeTables` reproduces glibc's `qsort` + `bsearch` (six encodings sit on two
  rows; both engines decode 0x46000008/09/0d/0f as swxc1, sdxc1, trunc.w.s, prefx); `CFormat` promoted,
  plus `cStrtod` (C's `atof`). `tests/check-format.sh`: identical to glibc on 158,029 doubles in all five
  formats. `tests/check-strtod.sh`: identical on 60,035 strings; it first caught `nan` read as negative
  and without its payload (GHC's `0/0` is the negative NaN), fixed to glibc's quiet NaN.
- Milestones 2-4, commit `8699546` (one commit; Decisions). Against the C engine built from this tree
  (`meson setup … --buildtype=debug`):
  - `port/harness/compare_engines.py`: 23 passed, 0 warned, 0 failed.
  - `tests/run-test.sh`: all 28 regression tests except `explain` pass.
  - `examples/tests/run-demo.sh`: all 34 demos pass.
  - `tools/check_example_commands.py --spimulator <port>`: 74/74.
  - `tasks/adhoc/engine-port-haskell/differential.py`: 185 programs × 14 modes = 2,590 cases, all
    identical except `tests/time.s`, a 10-million-iteration loop the debug C build cannot finish in the
    10 s limit (identical with a 300 s limit; 1.8 s in the port, 11.7 s in C).
  - `tasks/adhoc/engine-port-haskell/repl.py`: 24 REPL scripts, all identical.
  - `allinsts.s` (from `f570a87^`, `BoneYard/spiminst/dos/build/Tests/`) in the three AST views and a
    bare `-dump`: identical (it has 20 parse errors in the fork, so it never runs).
- The one difference the wide test found: `-assemble` left out the `, ` after the last word when the
  data segment's end is not word-aligned; C prints one after every word except the word at `end - 4`.
  Fixed in `src/Main.hs` before the commit.
- Surprise: the port runs faster than the debug C build (`time.s` above); the C engine calls
  `getitimer` on every instruction for the CP0 timer, the port reads GHC's monotonic clock.
- Not checked: the terminal REPL beyond one pty session (prompt, `run` output and the history file
  were right); Ctrl-C (the C engine longjmps back to the prompt; the port is stopped by GHC's default
  handler); `-mapped_io` on a real terminal.
- `hlint` is not installed, so the Plan's "hlint if it can run offline" was skipped.
