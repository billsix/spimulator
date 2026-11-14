# Explain where the stack and heap live, and what the break is

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Work out, from the code, where spimulator puts the stack and the heap, how each grows, and what the
"break" is (the end of the data segment that the `sbrk` syscall moves). Write it up for students
and for the docs. Done means a reference doc with the layout and the growth rules, each claim
citing the code, plus a pointer from the man page or teaching docs.

## Context

- `src/memory.c` defines the segments; its comment at the data segment says it is grown by "the
  SBRK system call". `src/syscall.c` handles `SBRK_SYSCALL`.
- Tests that exercise growth: `tests/tt.big_sbrk.s`, `tests/tt.big_stack.s`.
- Teaching mode and the examples that use the stack frame: `tasks/reference/mips-asm-teaching-style.md`
  (the stack-visible style), `examples/READING-ORDER.md`.
- Output: a new `tasks/reference/memory-layout-stack-heap-break.md` (or a section of an existing
  reference doc, if one covers the segments).
