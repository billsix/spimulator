# `.word a - b` assembles to an address, not the difference of two labels

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-21 (William Emerison Six <billsix@gmail.com>), found while writing the Forth
ladder (`tasks/archive/2026/11/21/forth-incremental-versions.md`).

## BLUF

In a data directive, `.word a - b` with two labels silently assembles to a value that looks like one
label's address (seen: `0x10010010`), not `a − b`. A demo author who writes a label difference (a
length, a branch offset in threaded code) gets a wrong number and no error. Done = the assembler either
computes the difference of two labels in the same segment or rejects the expression with an error,
the C engine and every port agree, and a regression test pins it.

## Context

- Read first: `CLAUDE.md` (the parser/AST/emit pipeline; "a C behavior change must be followed in every
  port"), `tasks/reference/parser-ast-emit.md`.
- The Forth rungs (`examples/src/lang/forth/`) hand-count their offsets because of this; once fixed,
  they could use label differences (optional).
- Check first what upstream SPIM does with the same input, and what GNU `as` (MIPS) does, to decide
  between "compute it" and "reject it".

## Open questions

None yet; the upstream comparison decides the behaviour.
