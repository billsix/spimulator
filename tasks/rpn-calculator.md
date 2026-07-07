# Example: RPN calculator (C + spim asm)

**Status:** proposed — not started
**Created:** 2026-07-07 (Bill)

## Request

Add an example RPN (reverse-Polish notation) calculator to the examples
tree, in the usual paired form: portable C demo + hand-written MIPS `.asm`
that runs under spimulator.

## Sketch

`dc`-flavored, integers only:

```
$ echo "3 4 + 2 *" | spimulator -f rpn.asm
14
```

- Read whitespace-separated tokens from stdin: decimal integers push;
  `+ - * /` (and `%`?) pop two, push the result; at EOF (or `p`/newline —
  decide), print the top of stack.  Syscall 5's new scanf-style reading
  handles the numbers; operators arrive via read_char or by tokenizing a
  read line — decide during implementation (mixed int/operator tokenizing
  is the one wrinkle; simplest is byte-at-a-time with an accumulating
  number state, same pattern as the demos' `atoi`).
- Errors: stack underflow and divide-by-zero print a message, exit 1.

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
