# Added incremental teaching versions of the worked examples

**Completed:** 2026-10-30 (branch `incremental-example-versions`)
**Method (durable):** `tasks/reference/incremental-example-decomposition.md`

Several worked examples confronted a novice with several independent ideas at once; the calculator
(`examples/src/lang/calc/`) hit the reader with whitespace handling and `term`/`factor`/`expr`
precedence before they had seen a program that reads a number and prints it. This initiative turned
such examples into ordered ladders of smaller, runnable, golden-tested versions, each adding one
concept and ending at the finished program. The idea came from William Emerison Six
<billsix@gmail.com> (2026-10-29). Done for the agreed scope: calc (6 rungs), rpn (3 rungs),
bubble-sort, head and cksum decomposed, the I/O-separation axis realized as `calc-eval`, and every
other short-ladder candidate triaged and left whole with a reason.

## Grounding and constraints

Researched 2026-10-29 and recorded in the method doc: Common Core teaches grouping/parentheses in
grade 5 (5.OA.A.1), negative numbers in grade 6 (6.NS.C) and integer arithmetic in grade 7 (7.NS.A),
so whole-number arithmetic and parentheses come before negatives. gacalc's
`algebra-teaching-order.md` puts addition/subtraction before multiplication/division and introduces
negation late. Rungs follow `tasks/reference/example-c-teaching-style.md` (C→asm-legible teaching
C), and finished demos kept their place in `examples/READING-ORDER.md`.

Every rung is a paired demo: the `.c` (with its generated `.s` and native binary) and the matching
hand-written spim `.asm` changed together and were golden-tested on both sides through
`examples/tests/run-demo.sh`. Studying all demos also produced a catalog of recurring decomposition
patterns (peel off file-or-stdin in 18 demos, argv+`atoi` in 17, flag parsing, leaf-before-non-leaf,
iterative-before-recursive, one-operator-at-a-time), recorded in the method doc under "Recurring
decomposition patterns".

## Triage

Each example was classed by how many independent new ideas a novice meets at once
(`examples/READING-ORDER.md`, Parts 0–9):

- Already minimal, no ladder: `exit`, `helloworld`, `print1through10`, `increment-ints`, `clear`,
  `yes`, and the single-pattern filters `tr`, `rot13`, `touch`.
- Short-ladder candidates (2–3 rungs): `fizzbuzz`, `bubble-sort`, `cksum`, `head`, `fibonacci`,
  `hanoi`, `queens`, and the libraries.
- Full ladders (4+ rungs): `calc` (the exemplar) and `rpn` (its gentler postfix predecessor).

## Decisions (2026-10-29, William Emerison Six <billsix@gmail.com>)

1. Ladder ordering (b), math-teaching order: negatives land late (after whole numbers, +/−, ×/÷ and
   parentheses) and whitespace is the last rung. A teaching curriculum tracks the learner's math
   background, not the parser's line count, and this kept rung 1 minimal. The calc ladder is
   recorded in `tasks/archive/2026/10/30/calc-incremental-versions.md`.
2. Scope: ladder candidates only. Already-minimal demos were left alone; they are rung 1 of the
   whole curriculum.
3. calc's rung files were implemented first as the proof of concept, before rolling the pattern out.
4. File layout: rungs sit beside the demo, rung-suffixed (`calc-1-numbers.{c,asm,input,expected}`
   …), with the final rung being the unchanged `calc-sdt` and `calc-tree` the epilogue, listed as
   sub-entries under calc's existing `READING-ORDER.md` number.

## What was done (autonomous run, 2026-10-30)

The maintainer, while away, authorized completing the whole initiative with discretion and logged
decisions. Every rung was verified under spim with `meson test --suite examples`.

- **calc**: 6 rungs, recorded in `tasks/archive/2026/10/30/calc-incremental-versions.md`; spec
  in `tasks/reference/calc-incremental-versions.md`.
- **rpn**: 3 rungs (`rpn-1-stack`, `rpn-2-ops`, `rpn-3-float`), recorded in
  `tasks/archive/2026/10/30/rpn-incremental-versions.md`.
- **I/O separation** (the orthogonal axis: the core operates on an in-memory buffer instead of doing
  its own stdin/stdout): `calc-eval` + `calc-eval-test`, recorded in
  `tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md`.
- **bubble-sort**, decomposed: `bubble-sort-1-fixed` (the nested-loop sort on a fixed array) →
  `bubble-sort` (adds reading ints from stdin, syscall 5 + `$a3` EOF). The finished demo also gained
  a golden.
- **head**, decomposed: `head-1-count` (fixed-N stdin line filter) → `head` (adds the `-n N` flag
  and a FILE; argv is its own topic, Part 5). The finished demo gained a golden on its default path.
- **cksum**, decomposed at the maintainer's request: `cksum-1-stdin` (the POSIX CRC32 table +
  bitwise loop over stdin, with a private `print_uint`) → `cksum` (adds reading a named FILE and the
  filename column). Both matched the system `cksum` (`printf 'hello\n' | cksum` → `3015617425 6`),
  and `cksum` gained a golden.

Left whole, not decomposed:

- `fizzbuzz`: one idea (modulo + multi-way branch); argv/`atoi` is already taught by `echo` and
  `factorial`, so a fixed-N → argv split added little.
- `fibonacci`: already computes iteratively and recursively in one file to compare them; splitting
  would fragment that comparison.
- `hanoi` and `queens`: one idea each (the 4-arg recursive frame; backtracking), with
  optional argv-N.
- The libraries (`libctype`, `libstr`, `libstdlib`): already a leaf → non-leaf sequence across
  files, which `READING-ORDER.md` orders.
