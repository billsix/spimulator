# rpn — incremental teaching versions

**Status:** in progress — started 2026-10-30 (branch `incremental-example-versions`, after the calc PoC)
**Priority:** 4
**Difficulty:** 4
**Part of:** `tasks/incremental-example-versions.md` (step 2)
**Method:** `tasks/reference/incremental-example-decomposition.md`

## BLUF

Decompose the RPN calculator (`examples/src/algorithms/rpn/rpn.{c,asm}`) — the postfix,
stack-machine predecessor to calc — into an ordered ladder of runnable, golden-tested rungs, each
adding one concept, ending at today's finished `rpn`. The calc decisions carry over
(integer-first; a dedicated float rung; error handling only at the final rung; rung-suffixed naming
beside the demo with the final rung = today's `rpn` unchanged; the C side uses an array stack and the
asm side uses the `$sp` stack, exactly as the finished demo already does). One rpn-specific call
(logged below): the four operators are **one** rung, not split, because RPN has no precedence.

## Context

`rpn` reads whitespace-separated tokens from one line of stdin: a number is pushed on an evaluation
stack; an operator (`+ - * /`) pops two, applies, pushes; at EOF the top of stack is printed
(`3 4 + 2 * 1 3 / -` → `13.666666`). The finished demo is floating point, uses the `$sp` stack on the
asm side (`addi $sp,-8` + `sdc1` / `ldc1` + `addi $sp,8`), and handles errors (under/overflow, bad
input, empty). It is the gentle postfix step before the infix calc.

**What is genuinely NEW in rpn (vs calc, which the student has seen):** the **evaluation stack**
(push/pop) and the **postfix / stack-machine** model — operators act immediately on the two values
below them, so there is no grammar, no recursion, and **no precedence**. The arithmetic itself (+ − ×
÷, integer then float) the student already met in calc.

**Whitespace is core here, not an add-on.** In calc, whitespace was the last rung (an add-on to a
space-free grammar). In rpn, whitespace *is* the token separator — a sequence of numbers is
unreadable without it — so every rung skips inter-token whitespace from rung 1. (A legitimate
divergence from calc, per the method doc: the ladder reuses shared shapes but the example's nature
rules.)

## The ladder

Integer-first (rungs 1–2 integer; rung 3 the FPU); each rung reads its own stdin and assumes
well-formed input (error handling is the final rung); C side = array stack, asm side = `$sp` stack
(as the finished demo). Prints the top of stack at EOF (one value per run).

| Rung (file) | Adds | Learning objectives (asm + math) | `.input` → `.expected` |
|---|---|---|---|
| 1 `rpn-1-stack` | whitespace-separated integers, each PUSHED; print the top at EOF | the evaluation stack (push only) + the whitespace tokenizer + print-top-at-EOF; asm: a word pushed on `$sp` (`addi $sp,-4` + `sw`), depth in `$s2` | `3 4 5` → `5` |
| 2 `rpn-2-ops` | the four operators `+ - * /` (pop two, apply, push), **integer** | the pop-two/push-one stack-machine step; the uniform operator model (no precedence); integer `/` truncates | `3 4 + 2 * 1 3 / -` → `14` |
| 3 `rpn-3-float` | the **FPU**: doubles + decimal literals (so `/` yields decimals) | `$f` registers, `cvt.d.w`, `.d` ops, `trunc.w.d`; the `$sp` stack widens to 8-byte doubles (`sdc1`/`ldc1`); the deterministic float printer | `3 4 + 2 * 1 3 / -` → `13.666666` |
| 4 `rpn` | error handling — under/overflow, bad input, empty (= **today's demo**, unchanged) | guarding the stack machine; exit-with-status on error | existing `rpn.input`/`.expected` |

Rung 2 and rung 3 share the input `3 4 + 2 * 1 3 / -` on purpose: `14` (integer, `1 3 /` = 0) vs
`13.666666` (float) is the representation-change made vivid — the same relaxed-golden point as calc's
`calc-6-float`.

## Decisions / judgment calls

1. **One operators rung, not split** (calc split `+`/`-` from `*`/`/` because `*`/`/` introduced
   precedence; RPN has none, so the four operators are the same pop-two-apply-push shape — one
   concept). Logged as my call under the "just start on the next thing" go-ahead; say so if you'd
   rather split them.
2. Carried from calc (not re-asked): integer-first + a float rung; error handling only at the final
   rung; naming `rpn-1-stack` … `rpn-3-float` + unchanged `rpn`; C array stack / asm `$sp` stack as
   the finished demo already does.

## Wiring (per rung — same checklist as calc)

Files in `examples/src/algorithms/rpn/`; `examples/src/meson.build` demo table + `lib_demo_tests`;
`examples/tests/run-demo.sh` case; `READING-ORDER.md` sub-entries under rpn (#41); verify with
`meson test --suite examples <rung>` (C binary and asm-under-spim both match the golden).
