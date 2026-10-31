# Step 8 — asm simplification rollout: lang/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 9 — `algorithms/`

**Status:** done + archived 2026-10-31 — all 10 calc demos converted, byte-identical to
pre-change output, all 10 checker-green (8 share pinned goldens too).
**Priority:** 5
**Difficulty:** 8

## BLUF

Convert all 10 `lang/calc/` demos (the recursive-descent calculator ladder) to the
stack-visible style. None exempt. This is the corpus's hardest topic: mutually-recursive
parsers, an FPU/double rung, an AST builder, an I/O-separated core, and a test battery.
"Done" = each byte-identical to its pre-change output; the step archives.

## Context

Cite `tasks/reference/mips-asm-teaching-style.md`. The key enabling fact: the parser state —
the lookahead `cur`, the error flag `had_error`, the buffer `cursor`, the AST node heap, the
case/number tables — is all C `static` (GLOBAL) or sbrk-allocated, so it stays in `.data`/heap,
NOT on any frame. Each parse routine's own running local is what moves onto its `$fp` frame.

## The 10 files and the pattern

- **calc-1..calc-5** (integer rungs): each parse routine (`parse_expr`/`parse_term`/
  `parse_factor`/`parse_number`) homes its accumulator / value at `-12($fp)`; `main` has no C
  locals (`cur` is global). The recursion already kept the accumulator on the stack across
  calls — regularized to the `$fp` convention.
- **calc-6-float** (FPU): the value held across a recursive call is a DOUBLE at the 8-byte,
  8-aligned slot `-16($fp)` (ldc1/sdc1); the actual numbers are visibly on the stack. The
  parse-internal `fractional_digits`/`seen_digit` counters stay transient in `$t2`/`$t3` (the
  leaf `advance` preserves them — the gcd-`t` boundary).
- **calc-sdt** (full SDT): calc-6 + `skip_blanks` + `had_error` (a global) + error recovery.
- **calc-tree** (AST): parse routines return `Node*` (words) held on the frame; `eval` holds
  the left operand (a double) at `-24($fp)` across `eval(right)`; `main` homes the tree pointer.
  Nodes live in the sbrk heap (`new_node`).
- **calc-eval** (I/O-separated): buffer-based parser; `main` homes the lookahead `c`; `eval` is
  a thin wrapper (globals only); `advance`/`blank_line` are leaf buffer walkers.
- **calc-eval-test**: `main` loops the case table (pointer + current string homed on its frame);
  the eval core is calc-eval's verbatim.

`advance`/`skip_blanks`/`new_node`/`blank_line` are helpers; `print_double` is a shared
register-based display helper (left as-is).

## Verification (done)

- Before/after output equivalence, all 10 byte-identical (feeding each demo's pinned `.input`;
  calc-tree/calc-eval share calc-sdt.input; calc-eval-test is input-free).
- `tools/check_asm_canonical.py` on all 10: PASS (each calc demo is single-file, so the checker
  runs it directly with its stdin). 8 also have pinned `run-demo.sh` goldens, which stay green.
- `make image` is the batch "done" bar.

## Exempt

None in `lang/`.
