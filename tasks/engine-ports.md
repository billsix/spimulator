# Several implementations of the spimulator engine, with the C engine as the standard

**Status:** ready — the ordering and the shared rules are decided; no step started
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)

## BLUF

The maintainer wants several implementations of the engine side by side in this repo. **The C
engine is the standard** for now. Every other implementation is required to behave equivalently, meaning
the same output and the same exit status as the C engine on every case in the shared corpus. How a floating-point number is turned into text does not gate a port (Cross-step
decisions). Two ports are planned, Python and GHC Haskell, and one differential harness runs every case
through the C engine and every port. Done for the umbrella: both ports report zero functional differences on
the whole corpus in the meson gate.

## Context

- `tasks/reference/engine-port-language-choice.md` — the language study: per-language traps, the
  measured formatting results, and the Haskell `%.18g` solution.
- `tasks/review/2026/11/10/repo-review.md` §5 (behavior inventory: what every port must match), §6
  (oracle quality), §7 (which oracle to trust).

## Steps (in order)

1. [ ] `tasks/engine-port-prereq-c-fixes.md` — fix the C engine's wrong-value bugs, close the test
   gaps, drop `,` as an FP decimal separator. The standard must be right and fully pinned before
   anything is compared with it.
2. [ ] `tasks/engine-port-python.md` — the Python port. It also builds the shared harness (its Plan
   step 2), generic over any number of engines.
3. [ ] `tasks/engine-port-haskell.md` — the GHC Haskell port. It can start once the harness exists,
   in parallel with the Python port's later milestones.

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
- **Layout:** `port/python/`, `port/haskell/`, the harness under `port/harness/` (William Emerison
  Six <billsix@gmail.com>, 2026-11-10: "port works for now").
- **Order of milestones, the same for every port:** machine arithmetic → assembler (gate: AST-parity
  bytes) → executor + syscalls (gate: regression + examples) → CLI/REPL → teaching mode
  (`-explain`, after `tasks/explain-mode-correctness-bugs.md` lands in C).
