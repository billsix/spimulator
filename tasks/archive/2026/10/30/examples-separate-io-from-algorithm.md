# Separated I/O from the algorithm in the calculator example (`calc-eval` + unit tests)

**Completed:** 2026-10-30
**Related:** `tasks/archive/2026/10/30/incremental-example-versions.md` (the orthogonal ladder
axis), `tasks/reference/incremental-example-decomposition.md` (method)

A second refactoring axis for the worked examples, orthogonal to the incremental-concept ladder, was
applied to the calculator: the algorithm was split from the I/O so the core scans an in-memory
buffer and returns a value, with a thin wrapper doing the reading and printing. The result is
`examples/src/lang/calc/calc-eval` (C + spim asm) and its unit-test demo `calc-eval-test`. The work
was done on the `incremental-example-versions` branch.

## Motivation

The idea came from William Emerison Six <billsix@gmail.com> (2026-10-29): many demos interleave
their algorithm with their own I/O. In `calc-sdt.c`, `parse_factor`/`parse_term`/`parse_expr`
consume input through a global lookahead `cur` fed by `read_char()`, so grammar and I/O are
entangled. A core that scans a `const char*` and returns the value can be called from a test with a
string literal, another program or a REPL, with no syscalls in it. The lesson is the pure-core /
thin-shell split; in MIPS it shows a buffer pointer passed in `$a0` (length in `$a1` where needed),
a result in `$v0`/`$f0`, and syscalls confined to one wrapper. It also prepares for the mini C
compiler (`tasks/mini-c-compiler.md`), whose front end is "string in, value/AST out". It matches the
"operate on a fixed in-program buffer" first rung of the file-or-stdin pattern in the method doc.

Good candidates were identified as `calc`, `rpn` and the transform filters with a separable core
(`wc`, `rev`, `cut`, `rot13`, `tr`, `expand`); demos whose whole lesson is I/O (`cat`, `nologin`,
`cp`, `touch`) were ruled out. Only `calc` was done here; `rpn` and the remaining demos were handled
by `tasks/archive/2026/10/30/rpn-separate-io-and-unit-tests.md` and
`tasks/archive/2026/10/30/separate-io-from-algorithm-remaining-demos.md`.

## Decisions (2026-10-30, agent discretion under the maintainer's away go-ahead)

1. `calc-eval` was made a new paired demo, not an in-place refactor of `calc-sdt`, which is the
   ladder's final rung and had to stay unchanged. The two side by side let a student compare the
   I/O-coupled and I/O-separated forms.
2. It sits beside the finished demos as the orthogonal axis, not as a ladder rung. It keeps the full
   calculator behavior and shares `calc-sdt`'s golden: `double eval(const char* expr)` is pure and
   `my_main` reads a line into a buffer and calls it. Because the stdin-to-stdout behavior did not
   change, the existing golden verified the refactor with no new expected output.

## Unit tests (added 2026-10-30 at the maintainer's request)

`calc-eval-test` (C + spim asm) runs `eval()` over a hardcoded battery of expressions with no stdin
and prints `<expr> => <result>` per case (`2+3 => 5`, `10/4 => 2.500000`,
`(3+4)*2-10/4 => 11.500000`, `-5+3 => -2`, `3*-2 => -6`, …); `run-demo.sh` pins that output, so a
regression in the parser turns the golden red. Both the C binary and the asm under spim matched
(`meson test --suite examples calc-eval-test`).

The planned shape changed in two ways during implementation:

- The core was not factored into a shared `calc_eval.c` + `calc_eval.h` translation unit. Each calc
  version stays self-contained like the rest of the ladder, so `calc-eval-test` carries a verbatim
  copy of `calc-eval`'s core and differs only in its driver.
- Instead of a `{expr, expected, expect_error}` table printing PASS/FAIL with a float tolerance, the
  test prints each result and the pinned golden is the oracle.
