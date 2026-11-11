# Built the calc incremental teaching ladder (the exemplar)

**Completed:** 2026-10-30
**Part of:** `tasks/archive/2026/10/30/incremental-example-versions.md`
**Method:** `tasks/reference/incremental-example-decomposition.md`
**Durable spec:** `tasks/reference/calc-incremental-versions.md` (per-rung grammar, goldens, and the
one-line diff from the previous rung)

The TI-83-style calculator (`examples/src/lang/calc/calc-sdt.c` + `calc-sdt.asm`, grammar
`expr → term → factor`) was decomposed into an ordered ladder of runnable, golden-tested versions,
each adding one concept and ending at the unchanged `calc-sdt`. All six new rungs (`calc-1-numbers`
… `calc-6-float`) were implemented as a `.c`, a matching hand-written spim `.asm`, and
`.input`/`.expected` goldens, wired into `examples/src/meson.build` (the original plan named a
per-directory `meson.build` that does not exist), `examples/tests/` and `examples/READING-ORDER.md`.
The C binary and the asm under spim both passed `meson test --suite examples` via
`examples/tests/run-demo.sh`; `calc-sdt` and `calc-tree` were unchanged and nothing regressed. The
work was done on the `incremental-example-versions` branch.

## Why calc

`calc-sdt` is a recursive-descent, evaluate-while-parsing (syntax-directed translation)
calculator. Its finished form stacks several independent ideas (tokenizing a number,
evaluate-while-parsing, +/−, precedence, parentheses through mutual recursion, unary minus,
whitespace and error recovery) on a reader who may not yet have seen a program that reads one
number and prints it. The ladder uses the SDT architecture, the simpler of the two and the one the
planned mini C compiler reuses (`tasks/mini-c-compiler.md`).

## The ladder (candidate (b), math-teaching order)

The C and the spim `.asm` moved together at every rung, and a rung counted as done only when its
spim version matched.

| Rung (file) | Adds | Grammar at this rung | Learning objectives (asm + math) | Math anchor |
|---|---|---|---|---|
| 1 `calc-1-numbers` | read & print one whole non-negative **integer** per line | `factor := DIGITS` | tokenize an integer from digit bytes; print an integer; the per-line read/print loop; priming lookahead | reading a numeral / place value |
| 2 `calc-2-addsub` | binary `+` and `-`, left-to-right (integer) | `expr := DIGITS (('+'|'-') DIGITS)*` | a left-fold loop over an operator-then-operand tail; holding a running value | add/subtract; additive pair (gacalc order: + before ×) |
| 3 `calc-3-muldiv` | `*` and `/`, binding tighter (**integer** division) | `expr := term (('+'|'-') term)*`, `term := DIGITS (('*'|'/') DIGITS)*` | precedence by **grammar layering** (the two-function nesting IS the precedence); `div`/`mflo` | order of operations / MDAS |
| 4 `calc-4-parens` | parentheses (grouping) | `factor := DIGITS | '(' expr ')'` | **mutual recursion** `expr↔factor`; saving an operand on the stack across a recursive call (the recursion-chapter frame discipline with a real payload) | grouping — CCSS **5.OA.A.1** |
| 5 `calc-5-neg` | unary minus / negative integers | `factor := … | '-' factor` | a prefix rule that recurses on itself; negation as the additive inverse; signed `print_int` | negatives — **6.NS.C / 7.NS.A** (taught late) |
| 6 `calc-6-float` | the **FPU**: decimal literals (`3.5`, `.5`) and **double** arithmetic (so `10/4` → `2.5`) | same grammar; `NUMBER` gains a `.`-fraction, all ops become `.d` | `$f0..$f31`, `cvt.d.w`, `add.d`/`sub.d`/`mul.d`/`div.d`, the deterministic truncating float printer | integers → fractions/decimals (OpenStax §1.5–1.7, after integers) |
| 7 `calc-sdt` | intra-line whitespace, blank lines, per-line `error` recovery (= the existing demo, unchanged) | adds `skip_blanks` + recovery, no new arithmetic | tokenizer robustness; error recovery that resyncs at newline | none (parsing robustness, not math) |

The order mirrors when a student meets these ideas in school: whole numbers, +/−, ×/÷ with order of
operations, parentheses (grade 5 and earlier), negatives (grade 6/7), then decimals (OpenStax puts
them after integers). Whitespace and error recovery came last because they are not arithmetic ideas.
Rung 1 stayed minimal, and the FPU got its own rung (it is Part 8 of the curriculum, and `rpn`
introduces it just before `calc`).

Candidate (a), parser-minimal, was rejected: it put unary minus into rung 1 and made whitespace the
only final rung (five rungs). `-5` is one trivial grammar rule, but for a teaching curriculum the
learner's math background was judged the stronger "least knowledge first" axis.

## Decisions (2026-10-29, William Emerison Six <billsix@gmail.com>)

1. Ladder (b) was chosen: negatives late (rung 5), whitespace and error last (rung 7).
2. Numbers were integer-first: rungs 1–5 compute in integers (`10 / 4` → `2`), rung 6 introduced the
   FPU and doubles (`10 / 4` → `2.500000`), and the final `calc-sdt` is float. The "golden is a
   prefix of the final" rule was relaxed only at this int→float transition: goldens are consistent
   within each tier (see the method doc's relaxed invariant).
3. Each rung does its own stdin I/O, as `calc-sdt` does. I/O separation was the separate
   task `tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md`.
4. Error handling enters only at the final rung; rungs 1–6 assume well-formed input and their
   goldens use only valid lines.
5. Naming: `calc-1-numbers` … `calc-6-float` beside the demo, final = `calc-sdt` (unchanged), listed
   as sub-entries under calc's `READING-ORDER.md` number.
6. `calc-tree` stayed the post-ladder architectural epilogue (same grammar, build a tree and walk
   it), diffed against `calc-sdt`, not a rung or its own ladder.
