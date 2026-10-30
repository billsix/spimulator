# Separate I/O from the algorithm for rpn, and add unit tests

**Status:** done + archived 2026-10-30 — verified in a standalone meson build (examples
suite 30/30, both C and asm byte-diffed against goldens) and the containerized `make image`
gate (examples suite 55/55 + both sanitizer regression runs; native listings + binaries for
both new demos). Committed and merged by the maintainer (William Emerison Six
<billsix@gmail.com>). Durable lessons harvested into
`tasks/reference/incremental-example-decomposition.md`.
**Priority:** 4
**Difficulty:** 4

## BLUF

Extends the I/O-separation + unit-test pattern — already done for the infix calculator as
`calc-eval` / `calc-eval-test` — to the RPN calculator. Adds `rpn-eval` (the pure
`double eval(const char* expr)` core that walks a buffer, with `my_main` doing all the
reading/printing) and `rpn-eval-test` (drives `eval()` over a hardcoded battery with no
stdin, printing `<expr> => <result>`, golden-pinned). "Done" = both demos build and pass
on the C side and the spim-asm side against their goldens, and the container gate is green.

## Context

Direct follow-on to `tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md`
(the calc half) at the maintainer's request ("extend the io separation and unit tests to
rpn", William Emerison Six <billsix@gmail.com>, 2026-10-30). Method + rationale are durable
in `tasks/reference/incremental-example-decomposition.md`; this task is a straight
application of that pattern to a second example, so it carries only the work record.

The separation the maintainer means: the finished `rpn` reads tokens from `read_char()`
inside `my_main`, so the evaluator depends on I/O. `rpn-eval` moves the evaluator onto a
NUL-terminated character buffer (`advance()` walks `cursor`, not stdin), so the core is a
reusable, testable pure function — the same point as calc's `parse_factor(void)` becoming
buffer-driven.

## What was done

Four new paired files in `examples/src/algorithms/rpn/` + wiring:

- `rpn-eval.{c,asm}` — pure `double eval(const char* expr)` returns the top of the
  evaluation stack; it cannot do I/O, so instead of rpn's inline "stack underflow"/exit it
  records which error stopped it in a `status` (enum `eval_status`: OK / overflow /
  underflow / bad input / empty) and returns. `my_main` slurps stdin into a buffer, calls
  `eval`, then prints the value or maps the status to rpn's exact message + exit code.
  Behavior is identical to `rpn`, so it **shares rpn's golden** (`rpn.input` / `rpn.expected`
  → `13.666666`) — only the algorithm/I/O boundary moved.
  - Asm note (in-file): the operand stack is the `.data` array `opstack` indexed by `depth`
    (eval's `double stack[STACK_MAX]` local; a non-recursive routine's array behaves like
    static storage). This is a deliberate departure from `rpn.asm`'s operand-stack-is-`$sp`
    lesson — here the lesson is the algorithm/I/O split, and `eval` must be an ordinary
    function with a normal `$sp` frame for its saved `$ra`. The `$sp`-operand-stack lesson
    still lives in `rpn` / `rpn-1-stack` / `-2-ops` / `-3-float`.
- `rpn-eval-test.{c,asm}` + `rpn-eval-test.expected` — eval core verbatim from `rpn-eval`
  (each demo is self-contained, per the ladder convention); the driver loops a `cases[]`
  table and prints `<expr> => <result>` — a value, or the error label. Battery exercises
  each operator, a float division (`1 3 / => 0.333333`), a negative result, the whole-golden
  expression, a single value, leftover operands (top printed), and each error kind
  (underflow / empty / bad input). No stdin is read — the payoff of the separation.

Wiring: `examples/src/meson.build` (both the build list and the `lib_demo_tests` golden
list), `examples/tests/run-demo.sh` (two case blocks — `rpn-eval` shares rpn's golden,
`rpn-eval-test` pins its own), and `examples/READING-ORDER.md` (an "I/O-separated variant"
note under rpn #41, mirroring calc's under #42). The native-listings `examples/src/Makefile`
needs no edit — it globs `*.c`.

## Verification

- Standalone meson build clean (176 targets). `run-demo.sh` PASS for both demos on the C
  side AND the spim-asm side; the `rpn-eval-test` golden was hand-computed and confirmed
  byte-identical to both built binaries. `meson test --suite examples` → 30/30 OK, both new
  tests registered.
- Containerized `make image` gate (nested) — the project's "done" bar.

## Follow-ups

- Optional (filed): the same split for the stream filters / other I/O-coupled demos —
  `tasks/archive/2026/10/30/separate-io-from-algorithm-remaining-demos.md` (done 2026-10-30:
  cksum + bubble-sort).
