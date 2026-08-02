# Example: RPN calculator (C + spim asm)

**Status:** proposed — not started
**Priority:** 6
**Difficulty:** 5
**Created:** 2026-07-07 (Bill)

## Request

Add an example RPN (reverse-Polish notation) calculator to the examples
tree, in the usual paired form: portable C demo + hand-written MIPS `.asm`
that runs under spimulator.

## Sketch

`dc`-flavored, **floating point** (Bill, 2026-07-07: "like what students
are used to" — a calculator does real arithmetic, not integer division):

```
$ echo "3 4 + 2 *" | spimulator -f rpn.asm
14
$ echo "1 3 /" | spimulator -f rpn.asm
0.33333333
```

- Values are doubles (`.double` / `$f`-register arithmetic on the asm
  side — making this the **first demo to exercise spim's FPU**: lwc1/
  ldc1, add.d/sub.d/mul.d/div.d, and print via syscall 3).
- Read whitespace-separated tokens from stdin: numbers (with optional
  `.` fraction) push; `+ - * /` pop two, push the result; at EOF print
  the top of stack.  Tokenize byte-at-a-time with an accumulating number
  state (an `atof` sibling of the demos' `atoi` — itself a worthwhile
  teaching artifact).
- Errors: stack underflow prints a message, exit 1.  Divide-by-zero just
  produces inf/nan the IEEE way — worth *showing* rather than trapping
  (calculators show it too).
- Output formatting: pick one shape (e.g. syscall 3's default double
  printing) and pin it in the golden; note in the demo header that
  formatting floats is its own deep topic the demo sidesteps.

## Why it earns a slot

- **The evaluation stack is the lesson.**  RPN evaluation IS a stack
  machine; in the asm version the operand stack can literally be the MIPS
  `$sp` stack — push/pop become real `addi $sp/-4; sw` sequences.  Nothing
  else in the curriculum makes the stack-as-data-structure this direct.
- Natural predecessor to [`calc-language.md`](calc-language.md) (infix →
  needs precedence/recursion) and, further up, the mini C compiler: RPN is
  what an expression *compiles to*.
- Unix-tool authenticity: it's `dc`, continuing the sbase/ubase tradition
  (`unix-tools.md`).

## Placement / plumbing

- `examples/src/algorithms/rpn/rpn.{c,asm}` (or a new `lang/` category
  shared with calc-language — decide when calc-language lands).
- Meson demo registration + `.expected` golden, per `run-demo.sh`
  conventions; add to `examples/READING-ORDER.md` after the
  recursion set.
- Symbol-table header block per the demo conventions (frame diagram if the
  asm keeps its own operand stack in a frame).

## Effort

Small-to-medium: ~a day for C + asm + goldens; the tokenizer state machine
is the only non-boilerplate part.
