# calc — incremental teaching versions (the exemplar)

**Status:** done + archived 2026-10-30 — all 6 rungs (`calc-1-numbers` … `calc-6-float`) implemented
+ verified (C binary and asm-under-spim both green via `meson test --suite examples`);
`calc-sdt`/`calc-tree` unchanged; no regression. On branch `incremental-example-versions` (unmerged).
Durable spec: `tasks/reference/calc-incremental-versions.md`.
**Priority:** 4
**Difficulty:** 5
**Part of:** `tasks/archive/2026/10/30/incremental-example-versions.md`
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

**Number type: integer-first** (maintainer, 2026-10-29). Rungs 1–5 compute in **integers** — so
integer division (`10 / 4` → `2`); a dedicated **float rung** (6) then introduces the FPU and decimal
literals, upgrading the evaluator to doubles (`10 / 4` → `2.500000`); the final rung adds whitespace +
error recovery and **is** today's float `calc-sdt`. This is the one place the "golden is a prefix of
the final" rule is relaxed: goldens are consistent **within** the integer tier (1–5) and **within**
the float tier (6–final), but division results gain decimals at rung 6 (see the method doc's relaxed
invariant). **I/O:** each rung reads stdin itself, like today's demo (I/O-separation is the parked
`examples-separate-io-from-algorithm.md` axis, not this ladder). **Error handling** enters only at the
final rung; rungs 1–6 assume well-formed input (their goldens use only valid lines).

| Rung (file) | Adds | Grammar at this rung | Learning objectives (asm + math) | Math anchor |
|---|---|---|---|---|
| 1 `calc-1-numbers` | read & print one whole non-negative **integer** per line | `factor := DIGITS` | tokenize an integer from digit bytes; print an integer; the per-line read/print loop; priming lookahead | reading a numeral / place value |
| 2 `calc-2-addsub` | binary `+` and `-`, left-to-right (integer) | `expr := DIGITS (('+'|'-') DIGITS)*` | a left-fold loop over an operator-then-operand tail; holding a running value | add/subtract; additive pair (gacalc order: + before ×) |
| 3 `calc-3-muldiv` | `*` and `/`, binding tighter (**integer** division) | `expr := term (('+'|'-') term)*`, `term := DIGITS (('*'|'/') DIGITS)*` | precedence by **grammar layering** (the two-function nesting IS the precedence); `div`/`mflo` | order of operations / MDAS |
| 4 `calc-4-parens` | parentheses (grouping) | `factor := DIGITS | '(' expr ')'` | **mutual recursion** `expr↔factor`; saving an operand on the stack across a recursive call (the recursion-chapter frame discipline with a real payload) | grouping — CCSS **5.OA.A.1** |
| 5 `calc-5-neg` | unary minus / negative integers | `factor := … | '-' factor` | a prefix rule that recurses on itself; negation as the additive inverse; signed `print_int` | negatives — **6.NS.C / 7.NS.A** (taught late) |
| 6 `calc-6-float` | the **FPU**: decimal literals (`3.5`, `.5`) and **double** arithmetic (so `10/4` → `2.5`) | same grammar; `NUMBER` gains a `.`-fraction, all ops become `.d` | `$f0..$f31`, `cvt.d.w`, `add.d`/`sub.d`/`mul.d`/`div.d`, the deterministic truncating float printer | integers → fractions/decimals (OpenStax §1.5–1.7, after integers) |
| 7 `calc-sdt` | intra-line whitespace, blank lines, per-line `error` recovery (= **today's demo**, unchanged) | adds `skip_blanks` + recovery, no new arithmetic | tokenizer robustness; error recovery that resyncs at newline | none (parsing robustness, not math) |

Rationale: it mirrors the sequence a student met these ideas in school — whole numbers, then +/−,
then ×/÷ with order-of-operations, then parentheses (grade 5 and earlier), then negatives (grade 6/7),
**then** fractions/decimals via the FPU (OpenStax puts decimals after integers), with whitespace +
error last because they are not arithmetic ideas. It keeps rung 1 genuinely minimal — the thing the
current demo skips over — and isolates the FPU as its own rung (it is Part 8 of the curriculum; `rpn`
introduces the FPU just before `calc`). Seven rungs: six numbered leaders (`calc-1-numbers` …
`calc-6-float`) ending at the unchanged `calc-sdt`.

### Candidate (a) — parser-minimal (NOT chosen — kept for the record)

Same spine, but unary minus joins rung 1 (`factor := NUMBER | '-' factor`), and whitespace +
newlines are one final rung — five rungs total: `1` numbers±sign → `2` addsub → `3` muldiv → `4`
parens → `5` whitespace(=calc-sdt). Argument for it: `-5` is one trivial grammar rule, so from a pure
"least parser machinery" view it costs nothing to include early.

**Recommendation: (b).** This is a teaching curriculum; the stronger "least knowledge first" axis is
the learner's math background, not the parser's line count, and (b) keeps rung 1 truly minimal.

## Decisions (2026-10-29, maintainer — William Emerison Six <billsix@gmail.com>)

1. **Ladder (b) chosen** — math-teaching order; negatives late (rung 5), whitespace+error last (rung
   7). Candidate (a) kept above only for the record.
2. **Integer-first numbers.** Rungs 1–5 integer; rung 6 (`calc-6-float`) introduces the FPU/decimals;
   the final `calc-sdt` is float. The strict "golden is a prefix" rule is relaxed only at this
   int→float transition.
3. **Each rung does its own stdin I/O** (final rung = today's `calc-sdt` verbatim); I/O-separation is
   the separate parked task `tasks/examples-separate-io-from-algorithm.md`.
4. **Error handling only at the final rung**; rungs 1–6 assume well-formed input.
5. **Naming:** `calc-1-numbers` … `calc-6-float`, final = `calc-sdt` (unchanged), `calc-tree` the
   epilogue — beside the demo, listed as sub-entries under calc's `READING-ORDER.md` number.
6. **`calc-tree` stays the post-ladder architectural epilogue** (diff against `calc-sdt`), not its
   own ladder.

## Next (decisions settled — implementing)

Per-rung file spec: `tasks/reference/calc-incremental-versions.md` (exact grammar, the
`.input`/`.expected` golden for each rung, the one-line diff from the previous rung). Then implement
`calc-1-numbers` … `calc-6-float` (each `.c` + matching spim `.asm` + goldens), wire into
`examples/src/lang/calc/meson.build` + `examples/tests/` + `READING-ORDER.md`, and golden-test both
the native C binary and the asm-under-spim via `examples/tests/run-demo.sh`. `calc-sdt`/`calc-tree`
are unchanged. Build spim (meson or `make image`) to run the harness.
