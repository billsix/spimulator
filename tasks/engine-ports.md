# Several implementations of the spimulator engine, with the C engine as the standard

**Status:** ready — the ordering and the shared rules are decided; no step started
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)

## BLUF

The maintainer wants several implementations of the engine side by side in this repo. **The C
engine is the standard** for now. Every other implementation is required to behave equivalently, meaning
the same output and the same exit status as the C engine on every case in the shared corpus. How a floating-point number is turned into text does not gate a port (Cross-step
decisions). Three ports are planned, Python, GHC Haskell and Rust. One differential harness runs every case
through the C engine and every port. Done for the umbrella:
- every port reports zero functional differences on the whole corpus in the meson gate;
- every example's header shows how to run its `.asm` through each port.

## Context

- `tasks/reference/engine-port-language-choice.md` — the language study: per-language traps, the
  measured formatting results, and the Haskell `%.18g` solution.
- `tasks/review/2026/11/10/repo-review.md` §5 (behavior inventory: what every port must match), §6
  (oracle quality), §7 (which oracle to trust).

## Steps (in order)

1. [x] `tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md` (done) — fix the C engine's wrong-value bugs, close the test
   gaps, drop `,` as an FP decimal separator. The standard must be right and fully pinned before
   anything is compared with it.
2. [ ] `tasks/engine-port-python.md` — the Python port. It also builds the shared harness (its Plan
   step 2), generic over any number of engines.
3. [ ] `tasks/engine-port-haskell.md` — the GHC Haskell port.
4. [ ] `tasks/engine-port-rust.md` — the Rust port.

Steps 3 and 4 can start once the harness exists, in either order, alongside the Python port's later
milestones.

## Cross-step decisions

- **The C engine is the standard** (William Emerison Six <billsix@gmail.com>, 2026-11-10). A
  difference between a port and C is a bug in the port, unless it exposes a bug in C. Then the C bug
  is fixed first, with a regression test, and the port follows; a port never carries a "C is wrong
  here" exception. Whether a port could later become the standard is left open ("for now").
- **Equivalent means functionally identical** (William Emerison Six <billsix@gmail.com>,
  2026-11-10): for the same arguments and stdin, the same stdout, stderr and exit status, on every case
  in the corpus.
- **Float-to-text differences do not gate a port.** Where two outputs differ only inside a printed
  floating-point number, and the two numbers parse to the same value or to very close values, the
  harness reports a formatting difference as a warning and passes the case. Two NaNs count as the same
  whatever their sign. A different value beyond that tolerance is a functional failure, because it
  means the arithmetic differs. The tolerance, defined in one place in the harness: the two values
  differ by at most one unit in the last digit printed (covers `%.8f` rounding a tie the other way),
  or by a relative 1e-15, about ten units in the last place of a double (covers digit-printing and
  last-bit rounding differences, not a wrong computation).
  A port still fixes its formatting where that is cheap: tested formatters that match glibc exactly
  exist for Python, Haskell and Rust (`tasks/adhoc/engine-ports-float-format/`), so a warning should
  be rare. (William Emerison Six <billsix@gmail.com>, 2026-11-10: "floating point to string issues
  are not something that should stop a port … but if you can fix it like you do for ghc haskell,
  please do.")
- **The corpus:** `tests/`, the AST-parity goldens, `tt.alu.bare.s`/`tt.fpu.bare.s`
  under `-bare`, `allinsts.s` from `f570a87^`, the examples, the PGU programs. A case that cannot be
  run deterministically (timing, environment addresses) is fixed or excluded by name in the harness,
  with the reason.
- **One harness for all engines**: each engine is a command line, so adding a port is one entry.
  It runs as a meson suite, so `make image` gates every port.
- **Layout:** `port/python/`, `port/haskell/`, `port/rust/`, the harness under `port/harness/`
  (William Emerison Six <billsix@gmail.com>, 2026-11-10: "port works for now").
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
