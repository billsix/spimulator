# The engine ports: the rules every port follows

The spimulator engine has implementations in C (`src/`), Python (`port/python/`), GHC Haskell
(`port/haskell/`) and Rust (`port/rust/`). This doc records the rules they share and why. The day's
history (who did what, in what order, and the C bugs the ports found) is in
`tasks/archive/2026/11/10/engine-ports.md`. Each port's own decisions are in its done-record,
`tasks/archive/2026/11/10/engine-port-{python,haskell,rust}.md`. Per-language lessons are in
`tasks/reference/engine-port-language-choice.md`. Decided by the maintainer (William Emerison Six
<billsix@gmail.com>) and the agent on 2026-11-10.

## The rules


- **The C engine is the standard** (William Emerison Six <billsix@gmail.com>, 2026-11-10). A
  difference between a port and C is a bug in the port, unless it exposes a bug in C. Then the C bug
  is fixed first, with a regression test, and the port follows; a port never carries a "C is wrong
  here" exception. Whether a port could later become the standard is left open ("for now").
- **Equivalent means functionally identical** (William Emerison Six <billsix@gmail.com>,
  2026-11-10): for the same arguments and stdin, the same stdout, stderr and exit status, on every case
  in the corpus.
- **Float-to-text differences do not gate a port.** Where two outputs differ only inside a printed
  floating-point number, and the two numbers parse to the same value, or to values within 4 units in
  the last place of a double (`ULP_TOLERANCE` in `port/harness/compare_engines.py`), the harness
  reports a formatting difference as a warning and passes the case. Two NaNs count as the same
  whatever their sign. Anything beyond that is a functional failure: `0.5` against `0.6`, or `3.14159`
  against `3.1416`, is a different value, not a formatting difference. (William Emerison Six
  <billsix@gmail.com>, 2026-11-11, from `tasks/review/2026/11/11/repo-review.md` §5.1; the earlier rule
  also forgave one unit in the last printed digit, which passed real errors.)
  A port still matches glibc exactly where that is cheap: each port's formatter is checked against
  glibc by a meson test in its suite (`python-float-format`, `haskell-float-format`,
  `rust-cargo-test`), so a warning should be rare. (William Emerison Six <billsix@gmail.com>,
  2026-11-10: "floating point to string issues are not something that should stop a port … but if
  you can fix it like you do for ghc haskell, please do.")
- **The corpus:** `tests/`, the AST-parity goldens, `tt.alu.bare.s`/`tt.fpu.bare.s`
  under `-bare`, `allinsts.s` from `f570a87^`, the examples, the PGU programs. A case that cannot be
  run deterministically (timing, environment addresses) is fixed or excluded by name in the harness,
  with the reason.
- **One harness for all engines**: each engine is a command line, so adding a port is one entry.
  It runs as a meson suite, so `make image` gates every port.
- **Layout:** `port/python/`, `port/haskell/`, `port/rust/`, the harness under `port/harness/`
  (William Emerison Six <billsix@gmail.com>, 2026-11-10: "port works for now").
- **A port's opcode table is generated, not written or parsed** (2026-11-11). `tools/gen_opcodes.py`
  writes every engine's table from `include/opcodes.tsv`, including the decode table that settles
  shared encodings. A new language adds an emitter there: `tasks/reference/opcode-table-generator.md`,
  "Adding an engine port in a new language".
- **Each port is a command with the C engine's CLI**: `spimulator-python`, `spimulator-haskell`,
  `spimulator-rust`. It takes the same flags and arguments as `spimulator`, so the harness and the
  example headers call each one the same way.
- **The image installs every port's toolchain, unconditionally** (William Emerison Six
  <billsix@gmail.com>, 2026-11-10: "no conditions for that in the Dockerfile"). One group script,
  `entrypoint/0N-install-ports.sh` (`python3`, `ghc`, `rust`, `cargo`), runs from the Dockerfile with
  no `ARG` and no `if`. A lean image keeps it too, since the ports are part of the test gate. The
  first port to start adds the script and lists every language; later ports only use it. GHC adds
  about 1 GB.
- **When a port is done, the examples show it** (William Emerison Six <billsix@gmail.com>,
  2026-11-10). Every example's BUILD AND RUN header gains a line per finished port, after the
  `spimulator` line:

      *   the same assembly, in the Python port:
      *     $ spimulator-python -f echo.asm hello world

  `tools/check_example_commands.py` runs each documented `spimulator*` line and compares it with the
  native run, as it does for `spimulator`, so the documented commands stay tested. Its docstring,
  `tasks/reference/example-c-teaching-style.md` and the example template get the new line too. This
  is the last step of each port's task.
- **The teaching mode is not ported yet** (William Emerison Six <billsix@gmail.com>, 2026-11-10). Each port stops after the CLI/REPL milestone;
  `-explain` waits for `tasks/explain-mode-correctness-bugs.md` and `tasks/explain-levels-redesign.md`
  in C, because porting it now would port output that is about to change. A port given `-explain`
  says it is not supported, and the harness excludes the explain cases by name, with that reason.
- **Order of milestones, the same for every port:** machine arithmetic → assembler (gate: AST-parity
  bytes) → executor + syscalls (gate: regression + examples) → CLI/REPL → teaching mode
  (`-explain`, after `tasks/explain-mode-correctness-bugs.md` lands in C).


## How a port is tested

`port/meson.build` lists the ports (`ports`). For each port it registers, in the suite
`port-<name>`:
- every regression test except the teaching mode;
- every examples demo;
- the documented example commands (`tools/check_example_commands.py --port`);
- `port/harness/compare_engines.py`: 28 cases from `port/harness/corpus.txt`;
- `port/harness/compare_all_programs.py`: every `.s`/`.asm` in the repo, assembled six ways and run,
  about 1,300 cases, six skipped by name (two step-limit programs and yes(1), in run and listing modes).

The image build runs them all. A behavior change in C fails every port's suite until the port
follows it. A new port is an entry in `ports`, plus `tools/add_port_lines.py NAME LABEL` for the
example headers.

## What is not ported yet

The teaching mode (`-explain`): `tasks/engine-ports-teaching-mode.md`, blocked on the C redesign.
