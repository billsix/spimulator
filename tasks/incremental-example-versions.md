# Incremental teaching versions of the worked examples

**Status:** in progress — decisions made 2026-10-29; calc rung implementation is next
**Priority:** 4
**Difficulty:** 5

## BLUF

Several worked examples confront a novice with several independent ideas at once — the calculator
(`examples/src/lang/calc/`) is the clearest: before the reader has seen a program that merely reads
a number and prints it, it hits them with whitespace handling and `term`/`factor`/`expr` precedence.
This initiative turns each such example into an **ordered ladder of smaller, runnable, golden-tested
versions**, each adding exactly one concept, ending at today's finished program. Deliverable per
decomposed example: a task doc (the ladder plan) and a reference doc (the per-version spec with
learning objectives, ordering, and reasoning). The *method* is settled and written up
(`tasks/reference/incremental-example-decomposition.md`); the calculator is the exemplar
(`tasks/calc-incremental-versions.md`); the rollout to the rest waits on the open questions here.

## Context

The idea (William Emerison Six <billsix@gmail.com>, 2026-10-29), inspired by the TI-83-style calc
demo's `factor`/`term`/`expr` structure: the finished demos are good programs but steep first
contact. Make the on-ramp explicit — one concept per version, least prior knowledge first — so the
finished version is the *top* of a ladder, not the only rung.

Grounding researched 2026-10-29 (recorded in the method doc):
- **Common Core** — grouping/parentheses is grade 5 (5.OA.A.1); negative numbers are a grade-6
  concept (6.NS.C); integer arithmetic is grade 7 (7.NS.A). Schools teach whole-number arithmetic
  and parentheses **before** negatives.
- **gacalc `algebra-teaching-order.md`** — addition/subtraction before multiplication/division;
  negation (additive inverse) introduced late, not first; concrete before abstract.

Consistency constraints: `tasks/reference/example-c-teaching-style.md` (versions stay C→asm-legible
teaching C) and `examples/READING-ORDER.md` (finished versions keep their curriculum place).

## Study of all examples — the triage

Per the method doc, each example is classed by how many independent new ideas a novice meets at once.
Studying the current curriculum (`examples/READING-ORDER.md`, Parts 0–9):

- **Already minimal — no ladder** (they *are* the first rungs): `exit`, `helloworld`,
  `print1through10`, `increment-ints`, `clear`, `yes` (Parts 0–1); and the single-idea Unix-toolchest
  filters whose whole point is one pattern (`yes`, `tr`, `rot13`, `touch`).
- **Candidate short ladders (2–3 rungs)** — one core idea plus an incidental complication to peel
  off first: `fizzbuzz` (modulo vs. multi-way branch), `bubble-sort` (stdin read vs. nested-loop
  swap), `cksum` (the `$s*` save discipline vs. the table+bitwise checksum), `head` (flag parse vs.
  the line-count filter), the recursion demos `fibonacci`/`hanoi`/`queens` (a per-call frame vs. the
  algorithm), and the libraries (`libctype`/`libstr`/`libstdlib` — leaf functions before the first
  non-leaf `atoi`).
- **Full ladders (4+ rungs)** — several stacked ideas: **`calc`** (the exemplar — tokenizing,
  evaluate-while-parsing vs. tree, precedence, grouping, unary minus, whitespace), and **`rpn`** (the
  FPU, the operand stack, tokenizing a float) as the gentle postfix predecessor.

This triage is the proposed scope, not a commitment to decompose all of them — see Q3.

Studying all the demos also surfaced the **recurring decomposition patterns** they share — peel off
file-or-stdin (18 demos), peel off argv+`atoi` (17), peel off flag parsing, leaf-before-non-leaf
(libraries), iterative-before-recursive, one-operator-at-a-time — so a ladder reuses a shared shape
rather than a bespoke per-example split. The catalog (grounded in `rg` over `examples/src`) is in the
method doc, `tasks/reference/incremental-example-decomposition.md` › "Recurring decomposition
patterns."

**Every rung is a PAIRED demo — the C and the spim MIPS `.asm` move together.** A rung ships the
`.c` (+ its generated `.s` + native binary) *and* the matching hand-written `.asm` that runs under
spim, updated in the same change and golden-tested on both sides (`examples/tests/run-demo.sh`).
Adding a C rung without updating its spim version — or vice versa — is not allowed; see the invariant
in the method doc.

## Rollout (step-tasks, once the questions are answered)

Express ordering with Priority + "Depends on", not `blocked` (each step is within our control):
1. **`calc`** — the exemplar and motivating case. Task drafted: `tasks/calc-incremental-versions.md`.
2. **`rpn`** — the postfix predecessor to calc (shares the FPU/stack lessons; see
   `tasks/reference/mips-fpu-and-float-demos.md`).
3. The short-ladder candidates, in curriculum order, as the maintainer greenlights each.

Each step gets its own task doc + reference doc (the per-version spec) and archives on its own
completion; this umbrella archives when the last greenlit step lands.

## Decisions (2026-10-29, maintainer — William Emerison Six <billsix@gmail.com>)

1. **Ladder ordering: (b) math-teaching order** — negatives land *late* (after whole numbers, +/−,
   ×/÷, and parentheses), not in v1. A teaching curriculum tracks the learner's math background, not
   the parser's line count, and (b) keeps rung 1 genuinely minimal. Whitespace is the **last** rung
   in the ladder. The chosen calc ladder is in `tasks/calc-incremental-versions.md` (candidate (b)).
2. **Scope: ladder candidates only.** Decompose demos that hit a novice with 2+ ideas at once
   (`calc`, `rpn`, and the short-ladder candidates in the triage); leave the already-minimal demos
   (`exit`, `helloworld`, `yes`, `clear`, …) alone — they are already rung 1 of the whole curriculum.
3. **Next: implement calc's rung files as the proof-of-concept.** Finalize calc's per-version
   reference doc, then write the actual C + matching spim `.asm` + goldens for each calc rung and wire
   them into `examples/` + `READING-ORDER.md`, proving the pattern end-to-end before rolling the docs
   out to `rpn` and the short-ladder candidates.
4. **File layout: beside the demo, rung-suffixed.** `calc/calc-1-numbers.{c,asm,input,expected}` …
   with the final rung being today's `calc-sdt.{c,asm}` (unchanged) and `calc-tree` the epilogue;
   listed as sub-entries under calc's existing `READING-ORDER.md` number.

## Related follow-up (orthogonal axis)

`tasks/examples-separate-io-from-algorithm.md` (proposed) — a *second* axis of refactoring: separate
the I/O from the algorithm so the core operates on an in-memory buffer (e.g. the calculator evaluates
a `char` array and returns the value) rather than doing its own stdin/stdout. It composes with this
ladder (the "operate on a fixed in-program buffer" first rung of the file-or-stdin pattern is exactly
this separation) but is tracked separately so each can be greenlit on its own.
