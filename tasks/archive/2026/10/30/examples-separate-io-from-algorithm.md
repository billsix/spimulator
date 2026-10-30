# Separate I/O from the algorithm in the worked examples

**Status:** done + archived 2026-10-30 — realized as `examples/src/lang/calc/calc-eval` (C + spim
asm), the pure `double eval(const char* expr)` core over a line buffer with `my_main` doing the I/O;
shares calc-sdt's golden, verified (C + asm). Branch `incremental-example-versions`. The pattern is
established on the calculator (the maintainer's named example); applying it to rpn and the transform
filters is the same shape and can follow on request.
**Priority:** 5
**Difficulty:** 4
**Related:** `tasks/archive/2026/10/30/incremental-example-versions.md` (orthogonal axis),
`tasks/reference/incremental-example-decomposition.md` (method)

## BLUF

A *second* axis of refactoring for the worked examples, orthogonal to the incremental-concept ladder:
split the **algorithm** from the **I/O** so the core operates on an **in-memory buffer** and
**returns a value**, with a thin wrapper doing the reading and printing. For the calculator, the core
becomes something like `double eval(const char *expr)` over a `char` array, and `my_main` just reads
a line into a buffer, calls `eval`, and prints the result — instead of the parser pulling bytes from
stdin itself. The payoff is a pure, testable, reusable core and a clean teaching point (pure
computation vs. effects); it composes with the version ladder but is tracked separately so each can
be greenlit on its own. **Proposed — do not start until the maintainer greenlights it** (the calc
ladder PoC comes first).

## Context

The idea (William Emerison Six <billsix@gmail.com>, 2026-10-29): many demos interleave their real
algorithm with doing their own I/O. The calculator is the example — `calc-sdt.c`'s `parse_factor`/
`parse_term`/`parse_expr` consume the input through a global lookahead `cur` fed by `read_char()`, so
the grammar and the I/O are entangled. If the core instead scanned a `const char *` buffer and
returned the value, the algorithm would stand alone: callable from a test with a string literal, from
another program, or from a REPL, with no syscalls in it.

This is a recognised separation-of-concerns lesson, and it is the natural partner of the
file-or-stdin pattern in the method doc: the "operate on a fixed in-program buffer" first rung of that
pattern *is* this I/O/algorithm split. Keeping it a separate axis means a demo can teach "add one
concept" (the ladder) and "separate the core from its I/O" (this) independently.

## Shape of the change (per demo, sketch — to be firmed on go-ahead)

- **Core takes a buffer, returns a value.** C: `double eval(const char *expr)` (calc); a filter's
  core becomes `int transform(const char *in, size_t n, char *out)` or similar. No `read_char`/
  `print_*` inside the core.
- **Thin I/O wrapper.** `my_main` reads a line / block into a `.data` (or stack) buffer, calls the
  core, prints the result — the only place syscalls live.
- **MIPS port mirrors it.** Per the lockstep invariant (method doc): the core subroutine takes the
  buffer pointer in `$a0` (+ length in `$a1` where needed) and returns in `$v0`/`$f0`; the wrapper
  owns the read/print syscalls. Both the C and the `.asm` move together, golden-tested on both sides
  (`examples/tests/run-demo.sh`).
- **The golden is unchanged** — same stdin in, same stdout out; only the internal boundary moved. So
  this refactor is verifiable against the *existing* golden with no new expected output.

## Learning objectives (why it's worth a demo)

- A function that **takes data and returns data** vs. one that **performs effects** — the pure-core /
  thin-shell split a student will reuse everywhere.
- In MIPS: passing a **buffer pointer** (and length) in `$a0`/`$a1`, returning in `$v0`/`$f0`, and
  confining syscalls to one wrapper — the calling-convention payoff made concrete.
- Sets up the **mini C compiler** (`tasks/mini-c-compiler.md`), whose front end is exactly "string in,
  value/AST out," and makes the algorithm **unit-testable** from a string literal without stdin.

## Scope (to decide on go-ahead)

Which demos get the treatment — the same triage spirit as the ladder initiative. Strong candidates:
`calc` (the motivating case), `rpn`, and the transform filters where the core and the stdin/file
plumbing are clearly separable (`wc`, `rev`, `cut`, `rot13`, `tr`, `expand`). A demo whose whole
lesson *is* the I/O (`cat`, `nologin`, `cp`, `touch`) is not a candidate — there is no algorithm to
separate out.

## Decisions (2026-10-30, agent discretion under the maintainer's away go-ahead)

1. **A new paired demo**, `calc-eval` — NOT an in-place refactor of `calc-sdt` (that one is the
   ladder's final rung and must stay unchanged, and a separate demo lets a student compare the
   I/O-coupled `calc-sdt` with the I/O-separated `calc-eval`).
2. **Beside the finished demos**, as the orthogonal axis — not a rung of the calc ladder. `calc-eval`
   keeps the full calculator behaviour (it shares calc-sdt's golden); only the algorithm↔I/O boundary
   moved (`eval(const char*)` is pure; `my_main` reads a line into a buffer and calls it).
