# Port the simulator engine to Python, proven against the C engine

**Status:** milestones 1–6 done on branch `engine-ports` (see Log); the teaching mode waits on the C fixes (`tasks/engine-ports.md`)
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (from `tasks/archive/2026/11/10/engine-port-language-investigation.md`)
**Part of:** `tasks/engine-ports.md` (the C engine is the standard; `tasks/engine-port-haskell.md` is the sibling port)
**Source:** tasks/review/2026/11/10/repo-review.md §5, §6, §7 F2

## BLUF

Write a Python implementation of the spimulator engine (assembler, executor, syscalls, REPL, and
last the teaching mode) that behaves equivalently to the C engine, the standard: the same stdout, stderr and exit status on
the shared corpus, float-to-text differences being warnings only (`tasks/engine-ports.md`), checked by a differential harness in the
meson gate. This port also builds that harness, for every engine. Done means: the harness reports
zero functional differences on the whole corpus, and every example's
header shows how to run its `.asm` through `spimulator-python`.

## Context

- Language choice, the fixed-width-integer answer, subsystem map and the test plan:
  `tasks/reference/engine-port-language-choice.md` (read first). The language is Python (Decisions).
- What to match: audit record `tasks/review/2026/11/10/repo-review.md` §5 (behavior inventory) and §7
  (which oracle to trust).
- Old grammar, as a second syntax spec: `git show 6cb6a08:CPU/parser.y`, `git show 6bafa31^:src/parser.y`.

## Plan

1. **Prerequisites in C** (`tasks/engine-port-prereq-c-fixes.md`, before porting): fix the wrong-value bugs the record lists
   in §3 (F5) and pin the self-checking tests' full output as goldens (§6.4, F7), so the oracle is
   right and complete.
2. Harness first, shared by every port (`port/harness/`): a driver that runs each case through the
   C engine and each registered port (one command line per engine) and diffs stdout, stderr and exit
   status, wired as a meson suite. Corpus and rules: `tasks/engine-ports.md`.
3. Port in this order, each milestone gated by the harness: machine-arithmetic module → assembler
   (gate: AST-parity bytes) → executor + syscalls (gate: regression + examples) → CLI/REPL → explain.
4. Package layout, typing and test gate per the python-coding-standard.md reference doc from
   github.com/billsix/runClaudeInContainer (`ty`, ruff, doctests). The command is `spimulator-python`.
   Its interpreter comes from the unconditional `entrypoint/0N-install-ports.sh`
   (`tasks/engine-ports.md`); this port starts first, so it adds that script, listing every language.
5. When the harness is clean: the examples step from `tasks/engine-ports.md`, a `spimulator-python`
   line in every example header, checked by `tools/check_example_commands.py`.

## Findings

- **Python prints a negative NaN as `nan`; glibc prints `-nan`** (measured 2026-11-10 for
  `0xfff8000000000000` in `%.18g`, `%.8f` and `%#-13.6g`). On x86, `0.0 / 0.0` produces that NaN, so a
  MIPS program that divides zero by zero prints `-nan` in C. This difference would only be a
  warning (float-to-text does not gate a port; `tasks/engine-ports.md`). The fix is cheap and done:
  `tasks/adhoc/engine-ports-float-format/cformat.py`, the `%` operator plus a `-nan` sign fix, matches
  glibc exactly in all five of the C engine's float formats on 158,029 values. Promote it into the port.

## Decisions

- The port is in **Python** (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The C engine's wrong-value bugs (`tasks/review/2026/11/10/repo-review.md` §3, F5) are **fixed before
  the port**, so the port is compared against correct output (William Emerison Six <billsix@gmail.com>, 2026-11-10). Plan step 1.
- `,` as a floating-point decimal separator is **dropped** from the C scanner before the port (it
  makes `.word 1,2` a syntax error); the maintainer left the call to the agent, who recommended
  dropping it (William Emerison Six <billsix@gmail.com>, 2026-11-10). Tracked with the other
  step 1 prerequisites in `tasks/engine-port-prereq-c-fixes.md`.
- The port lives **in this repo**, under `port/`, so the harness builds the C oracle in the same
  image ("for now"; William Emerison Six <billsix@gmail.com>, 2026-11-10).
- Go-ahead (William Emerison Six <billsix@gmail.com>, 2026-11-10). The teaching mode (`-explain`) is out of scope for now (`tasks/engine-ports.md`).

## Log

### 2026-11-10 — milestone 1: machine arithmetic and float formatting

- `port/python/spimulator_py/machine.py` holds every C width and IEEE behavior the engine
  relies on (`u32`/`s32`/`s16`/`s8`, `ckd_add`-style overflow tests, truncating `c_div`/`c_rem`,
  leading zeros/ones, float/double bit casts, `f32` rounding, hardware-style `f_div`/`f_sqrt`,
  x86's `(int32_t)` conversion, C's `atof`), each with doctests.
- `cformat.py` is `tasks/adhoc/engine-ports-float-format/cformat.py` promoted (the `%`
  operator plus glibc's `-nan`).
- **Decision: registers and memory words are held signed** (`-2**31 .. 2**31-1`), the C
  engine's `reg_word`. Options weighed: (a) unsigned `u32` everywhere, as the language study
  sketched; (b) signed, as C. Chose (b): the port is a transliteration of the C, and every
  `%d` the engine prints is of a signed word, so (b) needs a conversion only where C casts to
  unsigned, which is where a reader expects one.
- **Decision: text is a `str` with one character per byte** (decoded as Latin-1) everywhere
  inside the port, encoded back on output. Options: `bytes` throughout (awkward with `%`
  formatting and f-strings), UTF-8 `str` (changes byte counts and breaks on invalid bytes), or
  Latin-1 `str`. Latin-1 round-trips every byte, so a source line or MIPS string with any
  bytes in it comes out exactly as it went in.
- **Decision: x86's answers where C is undefined.** Python raises where C quietly computes
  (`1.0/0.0`, `math.sqrt(-1)`, `int(nan)`, packing an overflowing double into a float); the
  helpers return what the x86 hardware returns (IEEE infinities, the default NaN
  `0xfff8...`, `INT_MIN` from `cvttsd2si`), which is what the C engine inherits.

### 2026-11-10 — milestones 2, 3 and 4: assembler, executor + syscalls, CLI/REPL

Landed in one commit: the modules import one another as the C files include one another's
headers (the assembler stores into memory, memory raises exceptions, the REPL drives both), so
no earlier subset runs on its own. Each milestone's gate was run separately:

- **Assembler (milestone 2):** `tests/golden.ast_parity.txt` and `golden.ast_parity_all.txt`
  match (`ast_parity`, `ast_parity_all`), the harness's `core_dump` case passes, and
  `tasks/adhoc/engine-port-python/diff_engines.py` finds the C engine and the port identical on
  all 172 `.s`/`.asm` files under `tests/`, `examples/` and `pgu/` in `-dump`, `-full_dump`,
  `-print-ast`, `-print-ast-json`, `-show-expansion` and `-listing` (1032 cases: stdout,
  stderr, exit status and every file written).
- **Executor and syscalls (milestone 3):** all 28 non-explain regression tests pass through
  `tests/run-test.sh`, all 34 examples demos through `examples/tests/run-demo.sh`, the harness
  reports 23 passed, 0 warned, 0 failed, and `diff_engines.py --run` finds every one of the
  172 programs identical when run (stdin empty).
- **CLI and REPL (milestone 4):** `args_cmd`, `disasm`, `listing`, `print_ast`,
  `show_expansion` pass; `tasks/adhoc/engine-port-python/diff_repl.sh` runs 15 REPL sessions
  (every command, breakpoints, step/continue, redo with a blank line, `.` assembly, dump files)
  and 18 command lines (bad flags, missing files, size flags, `-assemble`, `-full_dump`) under
  `env -i`: 33 identical.

Design and decisions:

- **Module per C file**: `scanner.py`, `parser.py` (parser + emitter), `ast_nodes.py`,
  `pseudo_op.py`, `symbol_table.py`, `data.py`, `instruction.py` (builders, codec,
  disassembler) with `inst.py` (the instruction record), `memory.py`, `run.py`, `syscall.py`,
  `traps.py` (`raise_exception`), `registers.py`, `spim_utils.py`, `display.py`,
  `assembler_event.py`, `spim.py` (CLI and REPL), `console.py` (I/O, `run_error`),
  `options.py`/`memory_layout.py` (spim.h's globals and constants), `opcodes.py`.
- **The opcode table is read from `include/opcodes.h` at import** (a regex over the `OP(...)`
  rows, plus `opcode-types.h` for the shape tags); token numbers follow `tokens.h`, so
  `-print-ast`'s `op=` numbers match. Six encodings are shared by two mnemonics
  (`prefx`/`floor.w.s`, ...); the C disassembler's choice comes from glibc's stable `qsort`
  and its `bsearch` path, so `opcodes._bsearch` is written as glibc's loop (checked against a C
  program that sorts the real table).
- **Global state stays module-level, as in C** (`parser.data_dir`, `scanner.line_no`, ...),
  read as `module.name` so writes are seen everywhere. Options weighed: (a) one `Simulator`
  object threaded through every call; (b) module globals mirroring the C statics. Chose (b):
  a student can put a C function and its Python version side by side.
- **The instruction record keeps C's union overlap**: four bytes (rs, rt, rd, shamt) with
  `imm` and `target` recomputed as views whenever a byte is set; the engine depends on it
  (FP compares keep their condition in the rs byte).
- **Speed: decode once.** The executor compiles an instruction, the first time it runs, into a
  closure with its fields bound (`run._compile`, one `@_executes` factory per C `case`),
  cached on the instruction and dropped when a field changes. `tt.core.s` runs in 0.2 s;
  every regression test and demo in under 1.5 s. The CP0 timer is polled every 64 steps
  instead of every step (the C calls `getitimer` per instruction: `tests/time.s` takes 11 s in
  C and 8 s in the port).
- **The environment on the MIPS stack** comes from `/proc/self/environ`, not `os.environ`:
  Python adds `LC_CTYPE=C.UTF-8` to its own environment at start-up (PEP 538), which moved
  `$sp` by 20 bytes and changed two programs' leftover registers (found by `diff_engines.py`).
- **The REPL reads stdin one byte at a time** when it is not a terminal, as libedit does, so
  a program run from the REPL reads the rest of stdin (`args_cmd`); on a terminal it uses
  `input()` with the `(spim) ` prompt.
- **`-explain`/`-x` (any form)** prints `spimulator-python: the teaching mode (-explain) is not
  supported by this port` and exits 2.
- **The default exception handler** is read from `src/exceptions.s` next to the package; its
  `Loaded: <embedded exceptions.s>` line matches the C binary's.

## C engine bugs found

The port matches the C engine in each case (the C engine is the standard); none is fixed here.

1. **A file whose code comes before its first `.data`, with no `.text`, is assembled into the
   data segment.** `emit_ast` starts with `data_dir`/`text_dir` as the parse left them (the
   file's last segment), not as `parser_init` set them, so the leading instructions are stored
   with `store_word` into user data. Reproducer:

       printf 'main: li $v0 1\n li $a0 7\n syscall\n li $v0 10\n syscall\n .data\nx: .word 1\n' > t.s
       spimulator -dump -f t.s    # data.asm holds 0x34020001 0x34040007 0x0000000c ...

   Running it prints `Target of jump differs in high-order 4 bits` and loops on bad-PC
   exceptions. Fix (in C): reset `data_dir = false; text_dir = true` at the start of
   `emit_ast`.
2. **A run-time error while the `-f` files load crashes with SIGSEGV** (exit 139): `run_error`
   longjmps to `spim_top_level_env` before any `setjmp` filled it. Reproducer:
   `spimulator -stext 64 -f tests/tt.le.s` prints `Can't expand text segment`, then dumps core.
   The port prints the same and exits 139.
3. **A byte or halfword load from an empty user-text word crashes with SIGSEGV**:
   `bad_mem_read` reads `ENCODING(text_seg[...])` without checking for a null slot (the word
   case checks). Reproducer: a program doing `lui $t1 0x40; ori $t1 $t1 0x4000; lb $t0 0($t1)`
   (past the end of the code) exits 139. The port exits 139 too (`console.CrashError`).

### 2026-11-10 — milestone 5: registered in port/meson.build

- `['python', 'port/python/spimulator-python']` is in `port/meson.build`'s `ports` list.
  `meson test --suite port-python`: **64 OK, 0 failed** (28 regression tests, 34 demos,
  `python-documented`, `python-compare`); the whole `meson test` (128 tests) passes.
- Found by `python-documented`: `spimulator -f yes.asm | head -3` never ended. Python ignores
  SIGPIPE and the port swallowed `BrokenPipeError`, so it kept printing into a closed pipe
  forever; C dies of SIGPIPE. `spim.main` now restores SIGPIPE's default action.

### 2026-11-10 — milestone 6: the examples show the port

- `tasks/adhoc/engine-port-python/add_port_lines.py` added, to all 74 BUILD AND RUN headers,
  the `spimulator-python` twin of each `spimulator` line ("the same assembly, in the Python
  port:", or "... versions ..." for the two demos with numbered versions), right after the
  spimulator lines and before any note. A second run changed nothing (0 files).
- `tools/check_example_commands.py` runs a `spimulator-NAME` line through the executable
  `--port NAME=PATH` names, compared with the native run as for `spimulator`; without the
  option it skips such lines and says how many (76 now) in a `note:` line, so the C-only
  `documented-commands` test is unchanged. Its docstring and
  `tasks/reference/example-c-teaching-style.md` show the new line. The example template
  (`examples/src/template/src/template.c`) has no BUILD AND RUN block, so it needed nothing.
- `port/meson.build`'s `<name>-documented` test now passes `--spimulator` (the C binary) and
  `--port <name>=<exe>`: 74/74 demos agree, the port's lines included. Full `meson test`: 128 OK.
- Decision: the port lines go after the spimulator lines and before a header's notes
  (options: after the notes, or at the end of the block). The notes describe the assembly
  version, which both lines run, so they read as covering both.

### 2026-11-10 — left for the coordinator

- `entrypoint/0N-install-ports.sh` (Plan step 4, `tasks/engine-ports.md`) is not written: it is
  outside the files this task's brief allowed. The port needs only Fedora's `python3`, which the
  image already has (the harness and `tools/` use it).
- `port/harness/corpus.txt` still lacks the corpus items `tasks/engine-ports.md` lists beyond
  `tests/` (allinsts.s from `f570a87^`, `tt.alu.bare.s`, the examples, the PGU programs); the
  harness was outside the brief too. `tasks/adhoc/engine-port-python/diff_engines.py --run` covers
  every `.s` under `tests/`, `examples/` and `pgu/` locally (172 programs identical), and the
  meson suite covers the 34 demos and the documented example commands.
- Lessons for the other ports are in `tasks/reference/engine-port-language-choice.md`, "What the
  Python port found".
