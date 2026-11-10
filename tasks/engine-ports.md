# Several implementations of the spimulator engine, with the C engine as the standard

**Status:** ready — the ordering and the shared rules are decided; no step started
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)

## BLUF

The maintainer wants several implementations of the engine side by side in this repo. **The C
engine is the standard** for now. Every other implementation is required to behave equivalently: on
every case in the shared corpus, the same stdout, the same stderr and the same exit status as the C
engine. Two ports are planned, Python and GHC Haskell, and one differential harness runs every case
through the C engine and every port. Done for the umbrella: both ports report zero differences on the
whole corpus in the meson gate.

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
- **Equivalent means byte-identical** stdout, stderr and exit status for the same arguments and
  stdin, on every case in the corpus: `tests/`, the AST-parity goldens, `tt.alu.bare.s`/`tt.fpu.bare.s`
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
