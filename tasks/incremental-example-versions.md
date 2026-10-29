# Incremental teaching versions of the worked examples

**Status:** in progress — open questions block the rollout (see below)
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

## Rollout (step-tasks, once the questions are answered)

Express ordering with Priority + "Depends on", not `blocked` (each step is within our control):
1. **`calc`** — the exemplar and motivating case. Task drafted: `tasks/calc-incremental-versions.md`.
2. **`rpn`** — the postfix predecessor to calc (shares the FPU/stack lessons; see
   `tasks/reference/mips-fpu-and-float-demos.md`).
3. The short-ladder candidates, in curriculum order, as the maintainer greenlights each.

Each step gets its own task doc + reference doc (the per-version spec) and archives on its own
completion; this umbrella archives when the last greenlit step lands.

## Open questions (block the rollout — answer by number)

1. **Unary-minus / negative-number placement.** Parser complexity makes `-5` trivial (one rule,
   `factor := '-' factor`) and argues for an early rung; the school math order makes negatives *late*
   (grade 6/7, after whole-number arithmetic and parentheses). Your sketch put negatives in v1; the
   research pulls the other way. Which wins for the ladders — (a) parser-minimal (negatives early,
   your sketch), or (b) math-teaching order (negatives late)? See the two candidate calc ladders in
   `tasks/calc-incremental-versions.md`. **Recommendation: (b)** — this is a *teaching* curriculum,
   so mirroring how a student met these ideas in school is the stronger ordering, and it keeps v1
   genuinely minimal (read/print a whole number).
2. **Is whitespace its own final rung?** It's orthogonal to every math concept (a tokenizer-robustness
   idea, not an arithmetic one). Keep it as the last rung (my plan, and your sketch's v5), or fold it
   in earlier? **Recommendation: keep it last** — it teaches nothing arithmetical and is the one
   change that touches every rule.
3. **Scope — how far does "each example" go?** All ~44 demos, or only the ladder candidates in the
   triage above (leaving the already-minimal ones alone)? **Recommendation: only the candidates** —
   decomposing `exit`/`helloworld` invents nothing; they are already rung 1 of the whole curriculum.
4. **This turn's deliverable — docs only, or also the version files?** I've written the method +
   exemplar *docs*; do you want me to also implement the actual per-rung `.c`/`.asm`/golden files for
   `calc` once the ordering is settled, or produce all the design docs first and implement later?
   **Recommendation: finish the calc reference doc on your Q1/Q2 answers, then implement calc's rungs
   as the proof-of-concept before rolling the docs out to the rest.**
5. **File/naming + reading-order placement.** Proposed: rungs beside the finished demo
   (`calc/calc-1-numbers.asm` … final = today's `calc-sdt.asm`), listed as sub-entries under the
   demo's existing `READING-ORDER.md` number. OK, or do you want a different layout?
