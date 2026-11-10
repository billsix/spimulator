# Ported the simulator engine to Python, equivalent to the C engine

**Created:** 2026-11-10 · **Completed:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/engine-ports.md` · **Source:** tasks/review/2026/11/10/repo-review.md §5, §6, §7 F2

## Summary

`port/python/spimulator-python` (package `port/python/spimulator_py/`, standard library only, about
8,400 lines) is a Python implementation of the engine without the teaching mode. It behaves the same
as the C engine, the standard, everywhere tested:
- the `port-python` meson suite: 69 tests (every regression test but `explain`, the 34 demos, the
  documented example commands);
- `port/harness/compare_engines.py`: 23 cases;
- `port/harness/compare_all_programs.py`: 1,218 cases, every assembly program in the repo assembled
  six ways and run.

None of the comparisons had a float-formatting warning. Every example's BUILD AND RUN header shows
the `spimulator-python` run. The port was written by an agent from a written brief while the
maintainer was away, and reviewed and registered by the coordinating session. The port found three
C engine bugs, fixed in C in `a0eefb8` (below). The teaching mode waits on the C redesign
(`tasks/engine-ports.md`).

## Decisions

- The port is in **Python** (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The C engine's wrong-value bugs (`tasks/review/2026/11/10/repo-review.md` §3, F5) are **fixed before
  the port**, so the port is compared against correct output (William Emerison Six <billsix@gmail.com>, 2026-11-10). Plan step 1.
- `,` as a floating-point decimal separator is **dropped** from the C scanner before the port (it
  makes `.word 1,2` a syntax error); the maintainer left the call to the agent, who recommended
  dropping it (William Emerison Six <billsix@gmail.com>, 2026-11-10). Tracked with the other
  step 1 prerequisites in `tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md`.
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

All three were **fixed in C in `a0eefb8`** (with three more the Haskell port found), each with a
regression test; the port then followed the fixed C engine (Log, "after the C fixes").

1. **Code before the first `.data`, with no `.text`, was assembled into the data segment**: the
   emit pass started in the segment the parse ended in. Test `text_before_data`. Reproducer:
   `printf 'main: li $v0 1\n li $a0 7\n syscall\n li $v0 10\n syscall\n .data\nx: .word 1\n'`
   assembled with `-dump`.
2. **A run-time error while the `-f` files load crashed with SIGSEGV**: `run_error` longjmp-ed
   through a `jmp_buf` no `setjmp` had filled (`-stext 64 -f tests/tt.le.s`). Now it prints the
   message and exits 1. Test `load_error`.
3. **A byte or halfword load from an empty user-text word crashed** (a null pointer in
   `bad_mem_read`); it now reads 0, as a word load did. Test `engine_fixes` case 9.

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

### 2026-11-10 — after the C fixes (`a0eefb8`)

The C engine fixed six bugs (the three above and three from the Haskell port); the port now
matches the fixed engine:

1. `parser.parse_file` emits starting in the segment that was current when the parse began
   (code before a first `.data` goes in the text segment);
2. it emits only the nodes added after the tree's previous last child, so the REPL `.` command
   emits just its line;
3. REPL `print_all_regs`, `breakpoint` and `delete` with no operand do not read on into the next
   line; `breakpoint`/`delete` with no address print `Must supply an address for breakpoint`
   and return;
4. a run-time error while the `-f` files load prints its message and exits 1 (the port's
   emulated SIGSEGV exit 139 is gone);
5. `lb`/`lh` of an empty text word reads 0 (`console.CrashError` is gone);
6. `expand_k_data`'s zeroing: no change, `bytearray.extend` already zeroes.

Checked against a C reference rebuilt from the current tree: `meson test --suite port-python`
69 OK, 0 failed (including the new `text_before_data`, `load_error`, `repl_no_operand`,
`repl_dot` and `engine_fixes` case 9); `compare_engines.py` 23 passed, 0 warned, 0 failed;
`compare_all_programs.py` (`python-all-programs`) 1218 passed, 0 warned, 0 failed.
