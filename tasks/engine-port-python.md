# Port the simulator engine to Python, proven against the C engine

**Status:** proposed — needs go-ahead
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
  `tasks/reference/engine-port-language-choice.md` (read first). Rust is the documented alternative;
  the maintainer picks the language at go-ahead.
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

## Open questions

1. Python or Rust? Recommendation: Python (reference doc, "Recommendation").
2. Does the port live in this repo (e.g. `port/`) or a new repo? Recommendation: this repo, so the
   harness can build the C oracle in the same image.
