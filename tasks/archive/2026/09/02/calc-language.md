# Example: calculator language (TI-83-style), SDT and AST versions

**Status:** DONE 2026-09-02 (William Emerison Six <billsix@gmail.com>) — BOTH the
SDT and the tree versions shipped and verified. Archived.
**Priority:** 6
**Difficulty:** 6
**Created:** 2026-07-07 (Bill)

## Done — the TREE version (2026-09-02)

`examples/src/lang/calc/calc-tree.{c,asm}`, registered in `meson.build`
(`demos` + `lib_demo_tests`), a `run-demo.sh` case that **shares calc-sdt's
golden + input** (`calc-sdt.expected`/`calc-sdt.input`), and Part 8 entry #43 in
`READING-ORDER.md` (libstr bumped 43->44, Part 8 header 2->3 demos). Same
grammar/scanner/number-parse/`print_double`/error-recovery as calc-sdt, but the
parser **builds an AST** (NUM/BINOP/NEG nodes, unary minus a dedicated NEG node)
and a separate recursive `eval` walks it. Nodes are **bump-allocated off the
program break and never freed** — asm via syscall 9 (sbrk, 24-byte node), C via
`os_brk` mirroring it (not a static pool). Durable design notes (node layout, the
verified 8-aligned sbrk stride, the eval-walker frame discipline) are harvested
into `tasks/reference/mips-fpu-and-float-demos.md`. Verified: clean
warning_level=3 compile, clang-format clean, a 35-expression battery byte-identical
across calc-tree C / calc-tree.asm / calc-sdt C / calc-sdt.asm, `meson test`
**36/36**. Commit: `examples: add calc-tree — the AST-building companion to
calc-sdt`.

## Done — the SDT version (2026-09-02)

`examples/src/lang/calc/calc-sdt.{c,asm}` + `calc-sdt.input`/`calc-sdt.expected`,
registered in `meson.build` (`lib_demo_tests`), a `run-demo.sh` case, and a Part 8
entry (#42, after rpn) in `READING-ORDER.md`. Recursive-descent, **evaluates while
parsing** (no tree); the asm exercises **mutual recursion** (`expr`↔`term`↔`factor`
via `( expr )`) with operands saved on the stack across recursive calls. Floating
point (reuses rpn's FP-constant synthesis + deterministic `print_double`, so C and
asm match byte-for-byte). Handles precedence, parentheses, unary minus, leading-dot
numbers (`.5`), per-line **error recovery** (bad line → "error", resume next line),
and inf/nan. Verified across 18 expressions C-vs-asm + a 6-line multi-line golden;
`meson test` full suite **35/35**.

**Language decided (v1):** grammar exactly as sketched below; `-` is subtract and
unary minus (no negative literals needed — unary minus in `factor` covers it);
numbers are `[0-9]*('.'[0-9]*)?` with at least one digit; malformed input recovers
per line rather than aborting (chosen over exit-on-error so a multi-line golden can
show good lines around a bad one). Output format matches rpn's (see
`tasks/reference/mips-fpu-and-float-demos.md`).

## Remaining — the TREE version (calc-tree.c + calc-tree.asm)

The AST-building companion: same grammar, but parse into a tree, then a separate
walker evaluates it — so a student diffs the two architectures. Should produce
byte-identical output to calc-sdt.

**Design decisions (Bill approved my recommendations, 2026-09-02):**
1. **Node allocation = an `sbrk` bump-allocator on BOTH sides** (grab a region,
   hand out nodes by bumping a pointer, never free) — the "allocate, never free"
   heap lesson this version exists to teach, and what the mini C compiler will do.
   `sbrk` (syscall 9) is confirmed available (the sieve demo uses it).
2. **The C side mirrors the asm's manual bump allocator** (`os_brk`-based), NOT a
   static pool — so calc-sdt-vs-calc-tree and calc-tree.c-vs-calc-tree.asm both
   read as the same lesson. (`tac` already uses incremental sbrk as a C model.)
3. **Unary minus is a dedicated AST node** (`NEG`), mirroring the
   `factor := '-' factor` grammar rule — not desugared to `0 - x`.
4. **Share calc-sdt's golden** — calc-tree's run-demo.sh case points at
   `lang/calc/calc-sdt.expected`, proving the two architectures produce identical
   output.

Same language/grammar/error-recovery as calc-sdt (see above); the parser reuses
calc-sdt.asm's structure but *builds nodes* instead of evaluating, and a separate
recursive tree-walk evaluator produces the value. The harder, ~2-day piece the
original estimate flagged.

---

_Original request below._

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
