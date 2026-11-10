# Port the simulator engine to Python, proven against the C engine

**Status:** ready — decisions made (Decisions); not started
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (from `tasks/archive/2026/11/10/engine-port-language-investigation.md`)
**Source:** tasks/review/2026/11/10/repo-review.md §5, §6, §7 F2

## BLUF

Write a Python implementation of the spimulator engine (assembler, executor, syscalls, REPL, and
last the teaching mode) that matches today's C engine byte-for-byte on every regression test, the
AST-parity goldens, the example programs and the PGU programs, checked by a differential harness in
the meson gate. Done means: the harness reports zero differences on the whole corpus, and the C
engine stays the oracle until the maintainer decides otherwise.

## Context

- Language choice, the fixed-width-integer answer, subsystem map and the test plan:
  `tasks/reference/engine-port-language-choice.md` (read first). The language is Python (Decisions).
- What to match: audit record `tasks/review/2026/11/10/repo-review.md` §5 (behavior inventory) and §7
  (which oracle to trust).
- Old grammar, as a second syntax spec: `git show 6cb6a08:CPU/parser.y`, `git show 6bafa31^:src/parser.y`.

## Plan

1. **Prerequisites in C** (separate tasks, before porting): fix the wrong-value bugs the record lists
   in §3 (F5) and pin the self-checking tests' full output as goldens (§6.4, F7), so the oracle is
   right and complete.
2. Harness first: a driver that runs a case through two engines and diffs stdout/status, wired as a
   meson suite; corpus = `tests/`, the AST goldens, `tt.alu.bare.s`/`tt.fpu.bare.s` under `-bare`,
   `allinsts.s` from `f570a87^`, the examples, the PGU programs.
3. Port in this order, each milestone gated by the harness: machine-arithmetic module → assembler
   (gate: AST-parity bytes) → executor + syscalls (gate: regression + examples) → CLI/REPL → explain.
4. Package layout, typing and test gate per the python-coding-standard.md reference doc from
   github.com/billsix/runClaudeInContainer (`ty`, ruff, doctests).

## Decisions

- The port is in **Python** (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The C engine's wrong-value bugs (`tasks/review/2026/11/10/repo-review.md` §3, F5) are **fixed before
  the port**, so the port is compared against correct output (William Emerison Six <billsix@gmail.com>, 2026-11-10). Plan step 1.
- `,` as a floating-point decimal separator is **dropped** from the C scanner before the port (it
  makes `.word 1,2` a syntax error); the maintainer left the call to the agent, who recommended
  dropping it (William Emerison Six <billsix@gmail.com>, 2026-11-10). No task doc exists for it yet; scaffold one with the step 1 prerequisites.
- The port lives **in this repo**, under `port/`, so the harness builds the C oracle in the same
  image ("for now"; William Emerison Six <billsix@gmail.com>, 2026-11-10).
