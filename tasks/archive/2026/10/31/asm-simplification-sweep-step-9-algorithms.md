# Step 9 — asm simplification rollout: algorithms/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 10 — `transforms/` (last topic)

**Status:** done + archived 2026-10-31 — 9 demos converted, all byte-identical to pre-change
output; the 4 `$sp`-operand-stack rpn rungs left exempt.
**Priority:** 5
**Difficulty:** 6

## BLUF

Convert the non-exempt `algorithms/` demos to the stack-visible style. 13 `.asm`; **4 exempt**
(the `$sp`-as-operand-stack rpn ladder — see the expanded set below), **9 converted**. "Done" =
each converted demo byte-identical to its pre-change output; the step archives.

## Exempt set — CONFIRMED and EXPANDED (flag for the maintainer)

The style doc named `rpn/rpn.asm` + `rpn/rpn-1-stack.asm` as the "$sp is the operand stack"
exempt pair. Confirming the full set per the umbrella's instruction, **`rpn/rpn-2-ops.asm` and
`rpn/rpn-3-float.asm` use `$sp` as the operand stack too** (they are the intermediate rungs of
the same ladder: rpn-2-ops "pops the top two integers off the `$sp` stack", rpn-3-float "the
`$sp` stack widens to 8-byte doubles"). They teach the exact lesson the maintainer exempted, and
Shape B is fundamentally incompatible with "the operand stack IS `$sp`", so I **extended the
exempt set to all 4 `$sp`-operand-stack rungs** (rpn, rpn-1-stack, rpn-2-ops, rpn-3-float).
`rpn-eval.asm`/`rpn-eval-test.asm` instead use a `.data` `opstack` array (deliberately NOT `$sp`
— the I/O-split lesson), so they ARE converted. **If you'd rather rpn-2-ops/rpn-3-float were
converted anyway, say so and I'll do them** (it is a one-step follow-up).

## The 9 converted files

- **fizzbuzz** — n, i on the stack; atoi leaf.
- **sieve** — limit, sbrk base pointer, i, j on the stack (flag array stays in the sbrk heap).
- **pascals-triangle** — rows, n, j on the stack (row array stays `.data`).
- **binary-search** — target, n on main's stack; `n` passed to the search helpers in `$a1`
  (was a de-facto shared `$s2`); `linear_search` homes `i`, `binary_search` homes `lo`/`hi`
  (`mid` transient). (The `.asm` reads stdin + tracks a runtime `n`, unlike the C's fixed
  `static const` array — no shared golden.)
- **bubble-sort-1-fixed, bubble-sort** — n, i, j on the stack (arrays stay `.data`; the sort is
  inline, no jal).
- **bubble-sort-core-test** — main homes i/arr/n; the `bubble_sort` core homes i/j (leaf);
  `print_ints` is a register-based display helper.
- **rpn-eval, rpn-eval-test** — `.data` `opstack`/`depth`/`status` globals, so no persistent
  scalar C locals; main (rpn-eval-test) homes the case pointer + current string; `eval` just
  carries the standard `$ra`/`$fp` frame.

### `#CANONICAL` coverage

Added for fizzbuzz (args 15), binary-search (args+new `binary-search.input`), bubble-sort-1-fixed,
bubble-sort (its input), rpn-eval (rpn.input). SKIPPED (noted in-file) for sieve/pascals-triangle
(trailing-space lines), bubble-sort-core-test (empty-case " => " trailing space), rpn-eval-test
(empty-case " => empty" leading space) — all four are golden-tested instead.

## Verification (done)

- Before/after output equivalence, all 9 byte-identical (incl. binary-search found/not-found,
  fizzbuzz 15/default, pascals 5/default, sieve default/30, rpn-eval value/underflow). The 4
  exempt rpn files confirmed untouched (`git status`).
- `tools/check_asm_canonical.py` PASS on the 5 annotated; the golden-tested demos stay green.

## Exempt (unchanged)

`rpn/rpn.asm`, `rpn/rpn-1-stack.asm`, `rpn/rpn-2-ops.asm`, `rpn/rpn-3-float.asm` — the
`$sp`-operand-stack ladder.
