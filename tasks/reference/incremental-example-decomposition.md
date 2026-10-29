# Decomposing a worked example into incremental teaching versions

**Reference document** — the systematic method for turning one finished example program into an
ordered ladder of smaller programs, each adding exactly one concept, so a novice reaches the
finished version by the shortest conceptual path. Read before designing the version ladder for any
example. Written 2026-10-29 (William Emerison Six <billsix@gmail.com>), from the idea that the
calculator demo (`examples/src/lang/calc/`) "does too much for the novice" — it confronts the
reader with spaces, and `term`/`factor`/`expr` precedence, before they have seen a program that
merely reads a number and prints it.

Companion research this doc rests on: the gacalc reference doc
`geometricalgebra:tasks/reference/algebra-teaching-order.md` ("The order math is taught") and the
Common Core math standards (5.OA.A.1 grouping; 6.NS.C negative-number concept; 7.NS.A integer
arithmetic). Consistency constraints: `tasks/reference/example-c-teaching-style.md` (every version
is still C-→-asm-legible teaching C, declare-at-top, flat control flow — the standard C style does
**not** apply) and `examples/READING-ORDER.md` (the finished versions keep their place in the
curriculum order).

## When an example warrants decomposition (the triage)

Not every demo needs this. An example earns a version ladder only when a novice meets **two or more
independent new ideas at once**. Classify each example:

- **Already minimal — leave it.** One idea, no smaller honest version exists: `exit`, `helloworld`,
  `yes`, `clear`, `print1through10`. These *are* the first rungs of the curriculum; decomposing them
  invents nothing.
- **Short ladder (2–3 versions).** One core idea plus one or two incidental complications that can be
  peeled off: a demo that mixes its real lesson with argv parsing, error handling, or a second data
  structure.
- **Full ladder (4+ versions).** Several independent ideas stacked: the calculator (`calc`, the
  motivating case) and the RPN calculator (`rpn`) are the clear members — a tokenizer, an evaluation
  strategy, precedence, grouping, unary minus, and whitespace are each a separate idea.

The triage itself is part of "studying the example": decide the class, and for a ladder, list the
independent ideas before ordering them.

## Ordering principle — least knowledge first, one concept per rung

The ladder is ordered so each version needs **strictly less prior knowledge** than the next, and
**adds exactly one concept**. Two ordering forces apply, and they can conflict:

1. **Parser/program complexity** — how much machinery the code needs. Reading and printing one
   number needs the least; binary operators with precedence need a layered grammar; grouping needs
   recursion; whitespace-skipping is orthogonal plumbing.
2. **The school math-teaching order** (the research above). Whole-number arithmetic and *grouping*
   (parentheses, CCSS 5.OA.A.1, grade 5) are taught **before** negative numbers (6.NS, grade 6) and
   integer arithmetic (7.NS, grade 7); and within operations, **addition/subtraction before
   multiplication/division**, with **negation (the additive inverse) introduced late**, not first
   (gacalc `algebra-teaching-order.md`).

Where the two agree, follow them. Where they conflict — the sharp case is **unary minus /
negative numbers**, which is parser-trivial (one rule, `factor := '-' factor`) but math-pedagogically
*late* — state the conflict in the example's task doc and let the maintainer choose; do not silently
pick one. (This is the open question in `tasks/calc-incremental-versions.md`.)

A rung adds **one** concept. "Add subtraction alongside addition" is one concept (the additive
inverse pairs with addition); "add multiplication *and* division" is one (the multiplicative pair);
"add parentheses" is one (grouping/recursion); "allow whitespace" is one (tokenizer robustness).
Never bundle two.

## Each version's spec (what a rung's doc states)

For every version, the example's reference doc records:

- **A one-line "does" summary** and its input/output contract (what parses, what is rejected).
- **Learning objectives** — a short bulleted list, each a single capability the reader gains at this
  rung and *only* this rung (MIPS/asm concept AND the math concept, since these are paired C+asm
  demos).
- **Prerequisites** — the exact prior rung(s) it builds on, so the "least knowledge first" claim is
  checkable.
- **The CCSS / math-teaching anchor** it mirrors, when one applies (e.g. precedence ↔ order of
  operations; parentheses ↔ 5.OA.A.1).
- **The diff from the previous rung** — what one thing changed, ideally expressible as "add this
  grammar rule / this branch."

## Invariants

- **The last rung IS the existing example**, unchanged (or a documented refactor of it). The ladder
  is a path *to* the shipped program, not a replacement — the finished `calc-sdt.asm` /
  `calc-tree.asm` stay the destination, and the final version's golden equals the current golden on
  the overlapping input.
- **Every rung is a real, runnable, golden-tested program** (the examples bar: `.c` + `.asm` +
  `.expected`/`.input`, both the native C binary and the asm-under-spim diffed against the golden,
  per `examples/tests/run-demo.sh`). A rung that can't run isn't a teaching version.
- **Each rung keeps the paired-demo teaching-C style** (`example-c-teaching-style.md`): declare
  locals at the top, flat control flow, no modern-C idioms that hide the codegen.
- **Naming/layout** (proposed, pending the maintainer's call — see the umbrella task): versions live
  beside the finished demo, suffixed by rung, e.g. `calc/calc-1-numbers.asm` … with the final rung
  being today's `calc-sdt.asm`; the reading-order doc gains the ladder as sub-entries under the
  demo's existing number.
