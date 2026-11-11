# Ported the simulator engine to GHC Haskell, equivalent to the C engine

**Created:** 2026-11-10 · **Completed:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/engine-ports.md`

## Summary

`port/haskell/spimulator-haskell` (a launcher that builds `port/haskell/` with `ghc --make`, using
only the libraries that ship with GHC) is a Haskell implementation of the engine without the teaching
mode. It behaves the same as the C engine, the standard, everywhere tested:
- the `port-haskell` meson suite: 69 tests;
- `port/harness/compare_engines.py`: 23 cases;
- `port/harness/compare_all_programs.py`: 1,218 cases.

There were no float-formatting warnings: the exact-`Rational` formatter from the language study
(Findings) went into the port. Every example's BUILD AND RUN header shows the `spimulator-haskell`
run. The port was written in parallel with the Python one, in a separate git worktree, by an agent
from a written brief. The coordinating session merged it after the Python port, as
`tasks/engine-ports.md` planned, and registered it. The port found five C engine bugs; three were
fixed in C in `a0eefb8` (below).

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

**All fixed in C in `a0eefb8`** (with the Python port's finding, code before the first `.data` with
no `.text` assembled into the data segment; tests `text_before_data`, `repl_dot`, `repl_no_operand`,
`load_error`, `engine_fixes` case 9). The port now matches the fixed engine (Log, second entry):
`parseFile` emits only the statements the call parsed, starting in the segment current when the
parse began (bugs 2 and the data-segment one); `print_all_regs`, `breakpoint` and `delete` do not
flush past a missing operand, and a missing breakpoint address prints "Must supply an address for
breakpoint" and sets nothing (bug 1). Bugs 3-5 needed no change: the port already read 0, exited 1
and zeroed the new memory. As first found (before the fix):

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
- A final run from a clean `port/haskell/build/` failed every harness case: the harness runs engines
  with an empty environment, the launcher's `PATH` was not exported, so ghc could not find gcc, and the
  compiler's progress went to the program's stderr. Fixed in the launcher (export `PATH` for the build,
  build into `build/build.log`, show it only on failure); then, from a clean build directory, the harness
  passed 23/23, and six concurrent first runs under `env -i` all built once and ran correctly.

### 2026-11-10 — matched to the C fixes of `a0eefb8`

- The coordinator merged this branch into `engine-ports` (`eff193a`) and fixed in C the bugs both
  ports found (`a0eefb8`). Merged `engine-ports` back here and rebuilt the C reference.
- `port/haskell/src/Parser.hs` `parseFile`: remembers how many statements the tree already holds and
  the segment flags (`dataDir`/`textDir`) when the call starts, then emits only the new statements,
  starting from those flags. This fixes code before the first `.data` with no `.text` (it went to
  the data segment) and the REPL `.` command (it emitted the loaded file again).
- `port/haskell/src/Main.hs`: `print_all_regs`, `breakpoint` and `delete` skip the flush when the
  operand token was the newline; a missing breakpoint address prints "Must supply an address for
  breakpoint" and returns without setting anything (nor recording the command for a blank-line redo,
  as in C).
- The pointer to the prerequisite task already read
  `tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md` after the merge; nothing left to repoint.
- Gate, all passing: `tests/run-test.sh` 32 regression tests (every one but `explain`, including the
  new `text_before_data`, `load_error`, `repl_no_operand`, `repl_dot`, and `engine_fixes` case 9);
  34 demos; `tools/check_example_commands.py --spimulator <C> --port haskell=<port>` 74/74;
  `port/harness/compare_engines.py` 23/23; `port/harness/compare_all_programs.py` 1218 passed, 0
  warned, 0 failed; `tasks/adhoc/engine-port-haskell/repl.py` 24/24.
