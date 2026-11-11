# Porting the simulator engine to another language — which language, and how to prove compatibility

**Reference document.** States what is true about porting spimulator's engine (`src/` + `include/`,
~17K lines of C23) to Python, Rust, Java, C#, Go or GHC Haskell: the recommendation, what each
language does to compatibility and readability, the concrete answer to "does Python's lack of
fixed-width integers hurt?", the differential-test plan, and where the old grammar lives. Written
2026-11-10 from `tasks/archive/2026/11/10/engine-port-language-investigation.md`; the behavior
inventory it relies on is the frozen audit record `tasks/review/2026/11/10/repo-review.md` (§1, §5–§7).
Goal order, set by the maintainer (William Emerison Six <billsix@gmail.com>, 2026-11-10):
**compatibility with today's C first, readability for an upper-level undergraduate second.**

## Recommendation

**Python** (3.12+, fully type-annotated, `ty`-checked, `match` dispatch, `dataclass` state), with one
small "machine arithmetic" module that owns every 32-bit and IEEE-single conversion. Nothing in the
engine needs speed (§ "Speed"), every C semantic the engine depends on has an exact Python spelling
(§ "Fixed-width integers"), and Python is the most readable of the six for the target reader. The
compatibility risk is not the language; it is the dozens of C-specific output details in §5 of the
audit record, which every port must reproduce the same way, and which the differential harness below
catches regardless of language.

**Rust is the strong alternative** if the maintainer wants the compiler to enforce the widths (no
discipline needed: `u32`/`i32`/`f32` and `wrapping_*`/`checked_*` say it in the types) or wants the
port to replace the C binary for speed. Its cost is readability for an undergraduate who has not met
ownership and lifetimes; the engine's global mutable machine state (registers, memory segments, the
REPL's redo state) fights the borrow checker more than it helps.

**Not recommended as the first port:** Java and Haskell. Both format `%.18g` wrongly: they print
`0.300000000000000040` where C prints `...044` (measured), so `print_double` needs a hand-written
formatter. Haskell's purity also makes the stateful machine the least direct transcription. The
maintainer chose to port to Haskell and Rust **as well**, as further implementations
(`tasks/engine-ports.md`; `tasks/engine-port-rust.md`),
and its formatter problem is solved (below, "Haskell's float formatting"). Go and C# are
workable (both format correctly, both have fixed-width ints) but offer nothing over Python for
readability or over Rust for checked widths.

| | Compatibility risk | Readability (undergrad) | Width safety | Rough effort |
|---|---|---|---|---|
| **Python** | low with the arithmetic module + harness | best | by discipline (masking helpers); `ty` + tests check it | 6–9 weeks |
| **Rust** | low | medium (ownership; global state awkward) | compiler-enforced | 8–12 weeks |
| Go | low | good | types (`int32`/`uint32` wrap) | 7–10 weeks |
| C# | low | good | types (`unchecked`) | 7–10 weeks |
| Java | medium: no unsigned types; `%g` wrong | good | `int` wraps; unsigned via `Integer.*Unsigned` | 7–10 weeks |
| Haskell | medium: `printf %g` wrong (solved: exact-`Rational` formatter, below); state threading | low | `Data.Int`/`Data.Word` | 10–14 weeks |

The effort column is one experienced person, full-time, reaching parity with today's test suites; it
is an estimate from line counts per subsystem (below), not a measurement, and is mostly the same
across languages because the work is understanding the C, not typing the port.

## Does a language without fixed-width integers hurt? (Python)

**No, provided every result is normalized at the point it is produced, in one module.** The spikes
under `tasks/adhoc/engine-port-language-investigation/` (deleted with the task archive; see the
archived task doc for what they contained) reproduced, byte-for-byte against a C reference, every
integer and float semantic the engine uses:

| C behavior the engine relies on | Python spelling | Trap if done naively |
|---|---|---|
| `addu` wraps mod 2^32 | `(a + b) & 0xFFFFFFFF` | unbounded int: `0x7fffffff + 1` is `0x80000000`, never negative, never wraps |
| `add` traps on signed overflow (`ckd_add`) | compare the unbounded sum against `[-2**31, 2**31-1]` — simpler than C | — |
| registers hold bit patterns, read signed or unsigned | store `u32`; `s32(x) = x - 2**32 if x & 0x80000000 else x` at signed uses | mixing signed and unsigned ints in one register file |
| `sra` arithmetic, `srl` logical | `s32(x) >> n` and `x >> n` (Python `>>` is arithmetic on negatives) | `>>` on a stored `u32` is logical — right for `srl`, wrong for `sra` |
| shift amounts masked to 5 bits | `n & 31` | — |
| `mult`/`multu` HI/LO | full product in an unbounded int, then `>> 32` and `& M32` | none — simpler than C's 16-bit-limb `unsigned_multiply` |
| `div`: quotient truncates toward zero | `q = abs(a) // abs(b)`, sign fixed up; `r = a - q*b` | **`-7 // 2 == -4` and `-7 % 2 == 1` in Python; C gives -3 and -1** |
| div by zero / INT_MIN÷-1 leave HI/LO | explicit guard (as C) | — |
| `lb` sign-extends a byte, `lbu` zero-extends | `b - 0x100 if b & 0x80 else b` | — |
| FP register file is raw bits (`mtc1`/`mfc1`) | `struct.pack('<I')`/`unpack('<f')` | — |
| single-precision arithmetic (`add.s`) | round every result through `struct.pack('<f')` | **computing in binary64 prints `0.30000000` instead of spim's `0.30000001` for 0.1f+0.2f** |
| `cvt.w.s` truncates; spim's `round.w` is `(int)(x+0.5)` | `int(x)`, `int(x + 0.5)` | — |
| `print_float` `%.8f`, `print_double` `%.18g` | `f"{x:.8f}"`, `f"{x:.18g}"` match glibc, except a negative NaN (`nan`, not glibc's `-nan`) | — |

Cost of masking everywhere: in the spike, the decode+execute loop ran ~9M instructions in 1.8 s in
CPython 3.15 (C: 0.01 s; Rust 0.01 s; Java 0.02 s). `ctypes`/`numpy` fixed-width scalars are
**not** recommended: they are slower than masking in CPython, `numpy` integer overflow semantics
emit warnings and differ between scalar and array operations, and both hide the arithmetic from
the student the engine is meant to teach. A typed `U32 = int` alias plus five helpers (`u32`, `s32`,
`s16`, `f32`, bit casts) keep the widths visible.

Three C behaviors are **undefined in C** and so currently follow the host compiler; a port must pick
the value explicitly (audit record §3.22, §7): out-of-range or NaN FP→int conversion (x86 gives
0x80000000; MIPS gives 0x7fffffff; Rust and Java saturate), the NaN bit pattern produced by 0/0
(x86 `0xffc00000`, which `print_float` shows as `-nan`), and FP results that depend on the host's
rounding mode. Pin these with tests before porting.

## Speed

Not a constraint. Every regression and examples test runs in ≤ 0.21 s under the C engine
(`tt.core.s`, the biggest, in 0.09 s, mostly its wall-clock timer spin), so a 100× slower Python
engine finishes each in seconds. Interactive teaching use (`-explain`, `step`) is human-paced.

## Subsystem map — what each part becomes

| C subsystem (lines) | What it does | Port notes |
|---|---|---|
| `scanner.c` + `parser.c` + `ast.c` + `pseudo-op.c` (~6.4K) | hand-written recursive descent → AST → emit | port as recursive descent (the C is already that shape), or a parsing library (Lark in Python, `chumsky`/`nom` in Rust, ANTLR for Java/C#). The C's quirks (§5.4: `,` as whitespace and as FP decimal separator, constants substituted at scan time, only the first unresolved symbol kept) must be reproduced on purpose; a grammar generator makes accidental "fixes" easy, so keep the AST-parity goldens as the gate |
| `symbol-table.c` (~400) | hash table + forward references | the **dump order** depends on the C hash (`h = len; h = h*613 + (unsigned)c` …, §5.4); a port must reimplement the hash, not use a dict's order |
| `run.c` + `memory.c` (~2.3K) | fetch/decode/execute, segments | `match` on opcode; segments as `bytearray` + base address; keep the delay-slot and delayed-load state explicit (§5.5) |
| `instruction.c` + `opcodes.h` (~2K) | codec, disassembly, the X-macro table | generate the opcode table from `opcodes.h` (it is data); keep the listing format of §5.7 byte-exact |
| `syscall.c` (~220) | syscalls 1–17 | `open` passes **host** flag values through (§5.6): a port must map them the way the C does on Linux |
| `exceptions.s` | trap handler (MIPS asm, `#embed`ed) | reuse the same file unchanged; the AST-parity dumps depend on its addresses |
| `spim.c` + `spim-utils.c` + `display-utils.c` (~2.5K) | CLI, REPL, register display | `setjmp`/`longjmp` (Ctrl-C, `run_error`) become exceptions — cleaner in every candidate language; libedit becomes Python `readline`/`cmd`, Rust `rustyline` |
| `explain.c` (~2.6K) | teaching narration | port last; its known bugs (`tasks/explain-mode-correctness-bugs.md`, audit §3.6) should be fixed in C first so the oracle is right |

## Differential test plan

Method: the porting-with-an-output-oracle.md reference doc from github.com/billsix/runClaudeInContainer
— the C engine is the oracle; one process per case; byte identity on stdout, and exit status
compared, under a fixed environment (`env -i`, because stack addresses in the explain golden depend
on the environment).

1. **Fix the oracle first** (audit record §7, F5): bugs where today's C is wrong (divu guard, forward
   `.word lab+N`, c.cond/movf decode, branch reach, explain-mode exception leaks) are fixed in C with
   regression tests before the port copies them. Otherwise the port either reproduces a bug or
   "fails" against a wrong oracle.
2. **Turn self-checking tests into full-output goldens.** The sentinel tests (`bare core le dir …`)
   only check the last line; capture the C engine's full stdout+stderr+status for each as a pinned
   golden (audit §6.4), so formatting and exception messages are compared too.
3. **Corpus**, in the order a port reaches it:
   - `tests/` regression suite (25 tests, `tests/run-test.sh`) plus the two never-executed upstream
     tests `tt.alu.bare.s` and `tt.fpu.bare.s` run with `-bare -noexception` (audit §6.1).
   - The AST-parity goldens (`tests/golden.ast_parity*.txt`) — assembler output bytes, independent of
     execution; the first milestone for the parser.
   - **Historical tests from git** (audit §1.5): upstream `Tests/` is byte-identical to today's
     `tests/`; the extra oracles are the SPIM 6.4/6.5 `.OK` transcripts and `allinsts.s/.in/.ok`
     restorable with `git show f570a87^:BoneYard/spiminst/dos/build/Tests/allinsts.s` (and `.in`,
     `.ok`) — usable for instruction semantics, not for banners.
   - **The example programs**, which the maintainer named as the acceptance test: the 34 golden-pinned
     demos in `examples/tests/run-demo.sh`, plus every documented spimulator command in the
     examples' BUILD AND RUN headers (`tools/check_example_commands.py` runs them), plus the PGU book
     programs under `pgu/src/` (run through both engines and diff).
   - Generated cases: a small random-instruction generator for the ALU/FPU (operands drawn from edge
     values: 0, ±1, INT_MIN, INT_MAX, 0x8000, 0xffff, NaN, ±inf, denormals), assembled once, executed
     by both engines with the register file dumped — the cheapest way to find width bugs.
4. **Harness shape:** one new driver (to be written by the port task), e.g. `tools/diff_engines.py OLD NEW [CASES…]`, running each case through
   both binaries with the same flags/stdin/argv and reporting the first differing line; wired as a
   meson suite so it runs in the image gate.
5. **Nondeterministic cases** are excluded or controlled: `tt.core.s`'s timer test spins on CP0 Count
   driven by a 10 ms wall clock (audit §6 oracle notes), `time.s` is a benchmark, and mapped-IO
   latency is counted in instruction chunks; give the port a deterministic timer and exclude those.

## Where the old grammar is

The flex/bison grammar the hand-written parser replaced is a useful second specification of the
syntax, especially if the port uses a parser generator:

- upstream SPIM 9.1.24: `git show 6cb6a08:CPU/parser.y` (2967 lines), `git show 6cb6a08:CPU/scanner.l` (733)
- last fork version (adds the octal-escape fix and clang-tidy edits): `git show 6bafa31^:src/parser.y`
  (2987), `git show 6bafa31^:src/scanner.l` (742); deleted in `6bafa31` (2026-05-18)
- design and history of the replacement: `tasks/reference/parser-ast-emit.md`

## Licensing

Upstream SPIM is 3-clause BSD from `c75531a` (2010), "Copyright (c) 1990-2021, James R. Larus"
(audit §1.3). A translation is a derivative work: carry the BSD notice and the no-endorsement clause
into the port.

## Spike results (2026-11-10)

All four implementations printed identical output for the edge-case table and the loop:

| | loop of ~9M instructions | notes |
|---|---|---|
| C (`gcc -O2`) | 0.008 s | reference |
| Rust 1.99 (release) | 0.011 s | no `%g` in `std::fmt`: `print_double` needs a small `%.18g` formatter (written 2026-11-10; see "Float formatting across ports") |
| Java 25 | 0.021 s | `String.format("%.18g")` prints `0.300000000000000040`; `BigDecimal` rounding fixes it |
| Python 3.15 | 1.73 s | `%`/f-string formatting matches glibc; `//` and `%` floor, so `div` needs truncation |

Formatting probes, no spike: Go's `%.18g` and C#'s `"G18"` match C (Go only when the sum is computed
at run time: untyped constant arithmetic is exact); GHC's `Text.Printf` `%.18g` prints
`0.300000000000000040`, like Java.

## Float formatting across ports (2026-11-10)

**Formatting does not gate a port.** The maintainer's rule (William Emerison Six
<billsix@gmail.com>, 2026-11-10): a port must match the C engine functionally. A float printed with
different digits, when the values are the same or very close, is reported as a warning, not a
failure. The tolerance and how the harness detects it: `tasks/engine-ports.md`. Matching glibc
exactly is still the goal where it is cheap, and it is cheap in all three languages measured.
`bash tasks/adhoc/engine-ports-float-format/run.sh` checks each one against glibc `printf` on 158,029
doubles, in all five formats the C engine prints (`%.8f`, `%.18g`, `%.17g`, `%g`, `%#-13.6g`):

| Language | Formatter | Result |
|---|---|---|
| Python | `cformat.py`: the `%` operator; adds the `-nan` sign Python drops | identical |
| Rust | `cformat.rs`, about 60 lines, std only: `{:.N}` and `{:.Ne}` are already correctly rounded, so it applies C's `%g` rules (fixed vs exponential, stripping zeros, `#`, the two-digit exponent) and `nan`/`inf` | identical |
| GHC Haskell | `CFormat.hs`, about 90 lines: rounds the exact `Rational` value (below) | identical |

Java was not re-tested; `BigDecimal` rounding fixed its one measured case.

### Haskell's float formatting (solved 2026-11-10)

GHC's `Text.Printf` and `Numeric.showFFloat` format from the shortest round-trip digits
(`floatToDigits`) and pad with zeros. glibc formats the exact binary value. A Double's exact value is
`toRational x`, so a pure-Haskell formatter rounds it with integer arithmetic: exact decimal
exponent, ties-to-even rounding, the `%g` fixed/exponential rule, `#`, width, `inf`/`nan` by the sign
bit. `tasks/adhoc/engine-ports-float-format/CFormat.hs` implements all five formats the C engine uses
(`%.8f`, `%.18g`, `%.17g`, `%g`, `%#-13.6g`). It is byte-identical to glibc on 158,029 values
(`bash tasks/adhoc/engine-ports-float-format/run.sh`). Detail: `tasks/archive/2026/11/10/engine-port-haskell.md`.
The port's copy is `port/haskell/src/CFormat.hs`, checked by `port/haskell/tests/check-format.sh`.

### What the Haskell port taught (2026-11-10)

Measured while writing `port/haskell/` (`tasks/archive/2026/11/10/engine-port-haskell.md`, Log). Every check in the
corpus matched the C engine on the port's first full run; the risks the study named were real, but
each had a direct answer.

- **State threading was not the obstacle the study expected.** One record of `IORef`s and
  `IOUArray`s (`port/haskell/src/State.hs`), passed as the first argument, transcribes the C globals
  one to one. The pure parts stay pure: encoding, decoding, the opcode tables, the AST printers, the
  float formatter. The scanner and parser cannot be pure without changing behavior: the C scanner
  classifies a word (keyword, constant, identifier) when it is scanned, through a 3-token
  look-ahead, against a symbol table the parse changes.
- **The C struct's unions matter.** `mips_instruction` overlays the register bytes, the 16-bit
  immediate and the 26-bit target, and the engine relies on it (FD lives in the shamt byte). The
  port keeps one `Word32` with byte accessors.
- **The C library's searches are part of the output.** Six encodings sit on two opcode rows each;
  `bsearch` over a `qsort`ed table picks one. Reproducing glibc's stable merge sort and its exact
  `bsearch` loop reproduces the choice (disassembling 0x4600000f gives `prefx`, not `floor.w.s`).
  The other ports need the same.
- **Reading numbers matters as much as printing them.** `atof` is in the assembler (FP literals) and
  in read_float/read_double; an exact `Rational` conversion rounded once matches glibc on 60,035
  strings, once NaN is glibc's positive quiet NaN (GHC's `0/0` is negative) and keeps a `nan(n)`
  payload (`port/haskell/tests/check-strtod.sh`).
- **The environment is on the simulated stack.** The engine copies `environ` onto the stack, so any
  wrapper (a launcher script, an interpreter that sets variables) moves `$sp`. The Haskell launcher
  re-execs with the exact environment it received (`/proc/$$/environ`, `env -i`). Python run in
  an empty environment adds `LC_CTYPE=C.UTF-8` to `os.environ` (PEP 538 locale coercion; measured:
  `env -i python3 -c 'import os; print(dict(os.environ))'`, also with `-I`), a trap for a Python
  engine or launcher that copies `os.environ` onto the stack.
- **Input must not be buffered.** The C engine reads fd 0 one byte at a time, and a program run from
  the REPL reads the lines after `run`; a buffered reader (a GHC `Handle`) would swallow them. The
  port calls the C library's `read` through the FFI.
- **cabal cannot build offline without a package index**, even when every dependency ships with
  GHC; plain `ghc --make` can (about 15 s for the port from scratch).
- **Speed is no problem**: the port runs `tests/time.s` (10 million loop iterations) in 1.8 s, against
  11.7 s for the debug C build, which calls `getitimer` on every instruction.

## What the Python port found (2026-11-10)

The port (`port/python/`, `tasks/archive/2026/11/10/engine-port-python.md`) reached zero differences on the corpus.
Traps beyond the table above, each one a byte-level difference until handled:

- **Speed is fine if each instruction is decoded once.** Compiling an instruction on first
  execution into a closure with its fields bound (`run._compile`) makes `tt.core.s` run in 0.2 s
  and every test and demo in under 1.5 s under CPython 3.15; a re-decoding `match` per step would
  be several times slower.
- **Python edits its own environment at start-up** (PEP 538 adds `LC_CTYPE=C.UTF-8` under the C
  locale). The engine copies the environment onto the MIPS stack, so `$sp` and any program that
  reads uninitialised stack moved; read `/proc/self/environ` instead of `os.environ`.
- **Python ignores SIGPIPE.** `spimulator ... | head` must end when `head` does; restore
  `SIG_DFL` and do not catch `BrokenPipeError`.
- **Python raises where x86 computes**: `x / 0.0`, `math.sqrt(-1)`, `int(nan)`, `math.ceil(inf)`,
  `struct.pack('<f', 1e300)` (OverflowError, where C's `(float)` gives infinity).
- **glibc's `qsort` + `bsearch` decide duplicate keys**: six encodings belong to two mnemonics
  each, and the disassembler's pick depends on the stable sort and the exact halving loop.
- **Bytes, not text**: source lines, MIPS strings and file names may hold any byte; a `str`
  holding one character per byte (Latin-1) round-trips them.
- **C's undefined behavior has to be reproduced, crash included**, where a test can see it: the
  port exits 139 where the C engine dies of SIGSEGV (`tasks/archive/2026/11/10/engine-port-python.md`, "C engine bugs
  found").
- **Module-level globals and import cycles**: the C files include each other's headers freely;
  Python modules that import each other must only use each other's attributes at call time, and
  the package `__init__` imports the entry module first so any module can be imported alone.

## Cross-links

- `tasks/review/2026/11/10/repo-review.md` — §5 behavior inventory, §6 test oracle quality, §7 which
  oracle to trust.
- `tasks/reference/simulator-integer-types.md` — the C engine's integer types.
- `tasks/reference/inherited-idiosyncrasies.md` — upstream quirks.
- `tasks/archive/2026/11/10/engine-port-python.md` — the Python port's task and log.
