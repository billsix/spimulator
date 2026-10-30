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

## Recurring decomposition patterns across the curriculum

Studying all the demos (2026-10-29, grounded in `rg` over `examples/src` + `READING-ORDER.md`), the
same peelable layers recur — a ladder should reuse these shared shapes, not invent a bespoke split
per example. When you decompose any demo, check it against this catalog first:

- **Peel off "where the input comes from."** 18 demos accept a FILE argument *or* bare stdin
  (`cat`, `wc`, `head`, `rev`, `cut`, `od`, `tac`, `tail`, `uniq`, `nl`, `expand`, `base64`, …). The
  ladder rung order is: operate on a **fixed in-program buffer** → read **stdin** → accept a **FILE
  arg**. The core transform is the lesson; file-vs-stdin plumbing is a later, separate rung.
- **Peel off argv + `atoi`.** 17 demos take a number from the command line via `atoi`
  (`factorial`, `gcd`, `seq`, `factor`, `binary-search`, `fizzbuzz`, `hanoi`, `queens`, …). Rung
  order: run the core on a **hardcoded** value → add **argv + `atoi`**. (`echo` is the pure "just
  walk argv, no atoi" first rung this pattern builds on.)
- **Peel off flag parsing.** `head -n N`, `cut -c N-M`, `od -c` mix a dash-flag parser with the
  core filter. Rung order: the filter with the parameter **fixed** → add the **`-flag` parse**.
- **Leaf before non-leaf (libraries).** `libctype` is all one-line leaf functions; `libstdlib`'s
  `atoi` is the first function that *calls* another (saving `$ra`, keeping state in `$s*`). Rung
  order: a library of **leaves** → the first **non-leaf** member.
- **Iterative/base case before the recursive case.** `fibonacci` already ships iterative and
  recursive in one demo; the general rung order is **iterative** (state in `$s*`) → **recursive**
  (per-call stack frame), which is the whole point of the recursion chapter.
- **One operator / one case at a time.** `calc` (numbers → `+`/`-` → `*`/`/` → parens → unary minus)
  and `fizzbuzz` (the multi-way branch, one arm at a time) add a single grammar rule / branch per
  rung.
- **Same algorithm, second architecture (an epilogue, not a rung).** `calc-sdt` (evaluate while
  parsing) vs `calc-tree` (build an AST, then walk it) is a *comparison* read after the ladder, not
  a step within it.

These patterns also compose: a demo like `factor N` is "argv+atoi" **over** a core trial-division
loop, so its ladder is `core-on-hardcoded-N` → `+argv/atoi`.

### Lessons from the first two ladders (calc, rpn — 2026-10-30)

- **A concept the example makes *fundamental* cannot be deferred to a late rung.** In calc,
  whitespace was an add-on (space-free grammar first, whitespace last). In rpn, whitespace is the
  token *separator* — a sequence of numbers is unreadable without it — so it is present from rung 1.
  Decide per example whether a given concern is incidental (defer it) or constitutive (it is in the
  foundation), rather than copying another ladder's rung order.
- **Combine into one rung the steps that teach the *same shape*; split only where a step adds a new
  idea.** calc split `*`/`/` from `+`/`-` because `*`/`/` introduced *precedence*. rpn did **not**
  split its operators: RPN has no precedence, so all four are the identical pop-two-apply-push step —
  one concept, one rung. "One concept per rung" is about concepts, not operator count.
- **Integer-first with a dedicated float/FPU rung** worked for both (whole-number arithmetic before
  decimals, mirroring the school sequence), and that float rung is the sanctioned
  representation-change rung where the golden legitimately changes (`/` gains decimals).
- **I/O-separating a monolith into a *called* core may force state out of a register or off `$sp`.**
  The I/O-separation variant (`calc-eval`/`rpn-eval`) factors the algorithm into a function the driver
  calls, so in asm the core now needs a normal `$sp` frame for its saved `$ra`. Any state the
  monolithic demo kept *in* `$sp` must relocate: `rpn.asm`'s operand stack IS the hardware `$sp`
  stack, but `rpn-eval.asm`'s `eval` holds it in a `.data` array indexed by `depth` instead (a
  non-recursive routine's local array behaves like static storage). Say so in-file — the
  `$sp`-is-the-operand-stack lesson stays in the monolithic rpn ladder; the I/O-separated variant's
  lesson is the algorithm/I/O split. Also: an error is I/O the pure core cannot do, so it records
  *which* error in a `status`/flag and returns, and the driver maps that to the message + exit code.

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
- **Goldens are stable within a representation, and may change only at a deliberate
  representation-change rung.** Prefer that a rung's `.expected` be a prefix/subset of the next
  rung's for the same inputs; the one sanctioned exception is a rung whose whole lesson is a change
  of data representation — e.g. calc's integer→double `calc-6-float`, where `10 / 4` legitimately goes
  from `2` to `2.500000`. Call out such a rung explicitly in the example's task doc; everywhere else,
  a changed golden across rungs is a bug.
- **Every rung is a real, runnable, golden-tested program** (the examples bar: `.c` + `.asm` +
  `.expected`/`.input`, both the native C binary and the asm-under-spim diffed against the golden,
  per `examples/tests/run-demo.sh`). A rung that can't run isn't a teaching version.
- **The C and the spim MIPS asm move together — this is non-negotiable.** Each rung is a *paired*
  demo: the `.c` (and its generated `.s` listing + native binary) **and** the hand-written `.asm`
  that runs under spim, kept in lockstep. When a rung adds a concept to the C, its `.asm` port is
  written/updated **in the same change** so the two read as one lesson and both sides pass the
  golden. You never add a C rung and leave the spim version behind, and never update one side
  without the other. (The curriculum already uses the inverse of this freedom deliberately — one
  `.c` with *two* `.asm` variants: `get-char-from-user` ships a `-1.asm` bug and a `-2.asm` fix,
  `subrountines` a longhand/idiomatic pair — so an asm-only variant rung is legitimate where the
  *lesson itself* is an asm distinction, but the default is one matched `.c`+`.asm` per rung.)
- **Each rung keeps the paired-demo teaching-C style** (`example-c-teaching-style.md`): declare
  locals at the top, flat control flow, no modern-C idioms that hide the codegen.
- **Naming/layout** (proposed, pending the maintainer's call — see the umbrella task): versions live
  beside the finished demo, suffixed by rung, e.g. `calc/calc-1-numbers.asm` … with the final rung
  being today's `calc-sdt.asm`; the reading-order doc gains the ladder as sub-entries under the
  demo's existing number.
