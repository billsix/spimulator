# calc — incremental teaching versions (the exemplar)

**Status:** in progress — ladder (b) chosen 2026-10-29; per-version reference doc + rung files next
**Priority:** 4
**Difficulty:** 5
**Part of:** `tasks/incremental-example-versions.md`
**Method:** `tasks/reference/incremental-example-decomposition.md`

## BLUF

Decompose the TI-83-style calculator (`examples/src/lang/calc/calc-sdt.asm` +
`calc-sdt.c`, grammar `expr → term → factor`) into an ordered ladder of runnable, golden-tested
versions, each adding one concept, ending at today's finished `calc-sdt`. This doc fixes the ladder
and its learning objectives; the per-version file spec goes in a reference doc once the ordering
question (Q1) is settled. The finished `calc-tree` (same language, AST architecture) stays the
architectural epilogue the student diffs against `calc-sdt`, not a ladder rung.

## Context

The motivating case for the whole initiative: `calc-sdt` is a recursive-descent, evaluate-while-
parsing (syntax-directed translation) calculator. Its finished form stacks six independent ideas —
tokenizing a number, the evaluate-while-parsing strategy, +/− , precedence (*/÷ binding tighter),
parentheses (mutual recursion), unary minus, and whitespace/error-recovery — on a reader who may not
yet have seen a program that reads one number and prints it. The feature set (from `calc-sdt.c` and
the golden `calc-sdt.input`/`.expected`): float literals with optional `.`, unary minus,
`+ - * /` with precedence, parentheses, intra-line whitespace, one expression per line, per-line
`error` recovery, deterministic float printing.

The ladder is built on the **SDT architecture** (evaluate while parsing — the simpler of the two, and
the technique the planned mini C compiler reuses, `tasks/mini-c-compiler.md`). `calc-tree` is reached
*after* the top rung as the "same grammar, now build a tree and walk it" comparison it already is.

## The ladder

Two candidate orderings; they differ only in **where unary minus lands** (umbrella Q1). Each rung is
a real program with its own `.c`/`.asm`/`.input`/`.expected`, passing `examples/tests/run-demo.sh`;
each adds exactly one concept; the last rung is today's `calc-sdt`. **The C and the hand-written spim
`.asm` move together at every rung** — the MIPS port is written to match the C in the same change and
both sides are golden-tested (method-doc lockstep invariant); a rung is not done until its spim
version matches.

### Candidate (b) — math-teaching order (CHOSEN 2026-10-29)

| Rung | Adds | Grammar at this rung | Learning objectives (asm + math) | Math anchor |
|---|---|---|---|---|
| 1 `numbers` | read & print one non-negative number per line | `factor := NUMBER` | tokenize a number from digit bytes (+ optional `.`); the deterministic float printer; the per-line read/print loop; priming lookahead | reading a numeral / place value |
| 2 `addsub` | binary `+` and `-`, left-to-right | `expr := NUMBER (('+'|'-') NUMBER)*` | a left-fold loop over an operator-then-operand tail; holding a running value | add/subtract; additive pair (gacalc order: + before ×) |
| 3 `muldiv` | `*` and `/`, binding tighter | `expr := term (('+'|'-') term)*`, `term := NUMBER (('*'|'/') NUMBER)*` | precedence by **grammar layering** (the two-function nesting IS the precedence) | order of operations / MDAS |
| 4 `parens` | parentheses (grouping) | `factor := NUMBER | '(' expr ')'` | **mutual recursion** `expr↔factor`; saving an operand on the stack across a recursive call (the recursion-chapter frame discipline with a real payload) | grouping — CCSS **5.OA.A.1** |
| 5 `neg` | unary minus / negative numbers | `factor := … | '-' factor` | a prefix rule that recurses on itself; negation as the additive inverse | negatives — **6.NS.C / 7.NS.A** (taught late) |
| 6 `calc-sdt` | intra-line whitespace, blank lines, per-line `error` recovery (= today's demo) | adds `skip_blanks` + recovery, no new arithmetic | tokenizer robustness; error recovery that resyncs at newline | none (parsing robustness, not math) |

Rationale for the order: it mirrors the sequence a student actually met these ideas in school — whole
numbers, then +/− , then ×/÷ with order-of-operations, then parentheses (all grade 5 and earlier),
then negative numbers (grade 6/7), with whitespace last because it is not an arithmetic idea at all.
It keeps rung 1 genuinely minimal (the thing the current demo skips over), which was the whole
complaint. Six rungs (vs. the five in the original sketch) because it separates unary minus from the
first number rung and separates whitespace from error recovery.

### Candidate (a) — parser-minimal (NOT chosen — kept for the record)

Same spine, but unary minus joins rung 1 (`factor := NUMBER | '-' factor`), and whitespace +
newlines are one final rung — five rungs total: `1` numbers±sign → `2` addsub → `3` muldiv → `4`
parens → `5` whitespace(=calc-sdt). Argument for it: `-5` is one trivial grammar rule, so from a pure
"least parser machinery" view it costs nothing to include early.

**Recommendation: (b).** This is a teaching curriculum; the stronger "least knowledge first" axis is
the learner's math background, not the parser's line count, and (b) keeps rung 1 truly minimal.

## Decisions

1. **Ladder (b) chosen** (2026-10-29) — the math-teaching order above; negatives are rung 5 (late),
   whitespace rung 6. Candidate (a) is kept above only for the record.
2. **`calc-tree` stays the post-ladder architectural epilogue** (diff against `calc-sdt`), not its
   own ladder — its lesson, "same language, tree architecture," only lands once the SDT version is
   understood.

## Next (after Q1/Q2)

Write `tasks/reference/calc-incremental-versions.md` (the per-rung file spec: exact grammar, the
`.input`/`.expected` golden for each rung as a prefix of the final golden, the one-line diff from the
previous rung), then implement the rung files and wire them into `examples/` + `READING-ORDER.md`.
