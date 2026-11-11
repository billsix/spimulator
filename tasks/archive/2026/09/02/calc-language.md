# Added the TI-83-style calculator language example, in SDT and AST versions

**Created:** 2026-07-07 (Bill)
**Completed:** 2026-09-02 (William Emerison Six <billsix@gmail.com>)

A "computer math language" example, an infix expression evaluator like a TI-83 prompt, was added to
the examples as paired C + MIPS asm in two architectures over one language, so a student can diff
the sources and see what a tree buys and costs:

- `examples/src/lang/calc/calc-sdt.{c,asm}`: syntax-directed translation, a recursive-descent parse
  that evaluates while parsing and returns values up the call chain.
- `examples/src/lang/calc/calc-tree.{c,asm}`: the same grammar, but the parser builds an AST and a
  separate recursive walker evaluates it.

## Why it earned a slot

It is the bridge to the mini C compiler (`tasks/mini-c-compiler.md`): the SDT version shows in about
100 lines the technique the compiler uses (per the maintainer's design principles, SDT with no AST),
and the tree version shows the road not taken. The asm tree version needs heap nodes, teaching the
sbrk bump allocator (allocate, never free) the compiler is also allowed to use.
`expr → term → factor → ( expr )` exercises the recursion chapter's `$ra`/frame discipline with
mutual recursion as the new twist. Ordering: `tasks/archive/2026/09/02/rpn-calculator.md` (postfix,
no precedence) came first, then calc-sdt, then calc-tree, all leading to the compiler. With rpn
these are the curriculum's first FPU demos.

## The language (decided for v1)

```
expr   := term   (('+'|'-') term)*
term   := factor (('*'|'/') factor)*
factor := NUMBER | '-' factor | '(' expr ')'
```

```
$ echo "(3 + 4) * 2 - 10 / 4" | spimulator -f calc-sdt.asm
11.5
```

- Floating point, because a TI-83 computes reals (the maintainer, 2026-07-07): values are doubles,
  `/` is real division, and the asm runs on the FPU.
- Numbers are `[0-9]*('.'[0-9]*)?` with at least one digit (`3`, `3.5`, `.5`). `-` is both subtract
  and unary minus, so no negative literals are needed.
- One expression per line. Malformed input prints `error` and resumes on the next line, chosen over
  exit-on-error so a multi-line golden can show good lines around a bad one.
- Output format matches rpn's (`tasks/reference/mips-fpu-and-float-demos.md`).
- Variables (`A`–`Z`), `^` and comparison operators were left out of v1.

## SDT version

`calc-sdt.{c,asm}` with `calc-sdt.input`/`calc-sdt.expected`, registered in
`examples/src/meson.build` (`lib_demo_tests`), a case in `examples/tests/run-demo.sh`, and Part 8
entry 42 (after rpn) in `examples/READING-ORDER.md`. The asm saves operands on the stack across the
mutually recursive calls, and reuses rpn's FP-constant synthesis and deterministic `print_double` so
C and asm match byte for byte. It handles precedence, parentheses, unary minus, leading-dot numbers,
per-line error recovery, and inf/nan. Verified on 18 expressions C vs asm plus a 6-line golden;
`meson test` passed 35/35.

## Tree version

Design decisions (the maintainer approved the agent's recommendations, 2026-09-02):

1. Nodes come from an `sbrk` bump allocator on both sides, never freed: the heap lesson this version
   exists to teach. Syscall 9 was confirmed available (the sieve demo uses it).
2. The C side mirrors the asm's manual allocator via `os_brk`, not a static pool, so both
   comparisons (sdt vs tree, tree C vs tree asm) read as the same lesson. `tac` already used
   incremental sbrk as a C model.
3. Unary minus is a dedicated `NEG` node mirroring `factor := '-' factor`, not desugared to `0 - x`.
4. calc-tree shares calc-sdt's golden and input, proving both architectures give identical output.

`calc-tree.{c,asm}` was registered in `examples/src/meson.build` (`demos` + `lib_demo_tests`), with
a `run-demo.sh` case pointing at `calc-sdt.expected`/`calc-sdt.input` and Part 8 entry 43 in
`examples/READING-ORDER.md` (libstr moved from 43 to 44; the Part 8 header went from 2 to 3 demos).
It uses calc-sdt's scanner, number parsing, `print_double` and error recovery, with NUM/BINOP/NEG
nodes of 24 bytes from syscall 9. The node layout, the verified 8-aligned sbrk stride and the eval
walker's frame discipline were harvested into `tasks/reference/mips-fpu-and-float-demos.md`.
Verified: clean `warning_level=3` compile, clang-format clean, a 35-expression battery
byte-identical across calc-tree C, calc-tree asm, calc-sdt C and calc-sdt asm, and `meson test`
36/36. Commit: `examples: add calc-tree — the AST-building companion to calc-sdt`.
