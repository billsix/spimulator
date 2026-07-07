# Example: calculator language (TI-83-style), SDT and AST versions

**Status:** proposed — not started
**Created:** 2026-07-07 (Bill)

## Request

Add a "computer math language" example to the spimulator examples — an
infix expression evaluator like a TI-83 prompt — as paired C + MIPS asm.
**Two versions of the same language:**

1. **SDT version** — syntax-directed translation: recursive-descent parse
   that *evaluates while parsing*; no tree, values returned up the call
   chain.
2. **Tree version** — same grammar, but the parser builds a concrete/
   abstract syntax tree first, then a separate walker evaluates it.

Same input language, same outputs, two architectures — the student diffs
the two sources to see exactly what a tree buys (and costs).

## Language sketch (v1 — decide exactly during implementation)

```
$ echo "(3 + 4) * 2 - 10 / 4" | spimulator -f calc-sdt.asm
11.5
```

- **Floating point** (Bill, 2026-07-07: like what students are used to —
  a TI-83 computes reals).  Values are doubles; `/` is real division.
  Asm side runs on the FPU (`$f` registers, `add.d`-family, syscall 3 to
  print) — together with [`rpn-calculator.md`](rpn-calculator.md) these
  are the curriculum's first FPU demos.
- Number literals with optional fraction (`3`, `3.5`, `.5`); `+ - * /`
  with standard precedence, parentheses, unary minus.
- One expression per line; print each result.
- Later extensions (explicitly out of v1): variables (`A`–`Z` like the
  TI-83), `^`, comparison ops.
- Same golden-formatting note as the RPN task: pin one output shape and
  don't get dragged into float-printing depth.

Grammar (classic layered form — the teaching artifact itself):

```
expr   := term   (('+'|'-') term)*
term   := factor (('*'|'/') factor)*
factor := NUMBER | '-' factor | '(' expr ')'
```

## Why it earns a slot

- **This is the bridge to the mini C compiler** (`mini-c-compiler.md`):
  the SDT version demonstrates in ~100 lines the exact technique the
  compiler will use (per Bill's design principles: SDT, no AST), and the
  tree version shows the road not taken — the student sees *both* and
  understands the compiler's design choice rather than taking it on faith.
- The AST version in **MIPS asm** forces heap allocation for nodes — the
  sbrk bump-allocator lesson (allocate, never free) that also mirrors what
  the compiler task is allowed to do.
- Recursion with real payload: `expr → term → factor → ( expr )` recursion
  in asm exercises the full `$ra`/frame discipline the recursion chapter
  taught, with mutual recursion as the new twist.

## Deliverables

- `calc-sdt.{c,asm}` and `calc-tree.{c,asm}` (four sources, one language)
  under `examples/src/` — likely a new `lang/` category housing this and
  [`rpn-calculator.md`](rpn-calculator.md).
- Shared golden inputs: one `.expected` covering precedence, parens, unary
  minus, division truncation, and a malformed-input error case; both
  versions must produce byte-identical output.
- READING-ORDER entry: after recursion, before the compiler; RPN first,
  then calc-sdt, then calc-tree.

## Ordering

[`rpn-calculator.md`](rpn-calculator.md) first (postfix, no precedence —
the gentler step), then this, then it all feeds `mini-c-compiler.md`.

## Effort

Medium: the C pair is a day; the asm pair (especially calc-tree with its
node allocation) is another two-ish.  Worth doing C-first and letting the
goldens pin the language before the asm ports start.
