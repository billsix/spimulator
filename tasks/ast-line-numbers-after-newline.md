# AST nodes get the next line's number when their last operand ends the line

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** conversation, 2026-11-11 (found during the session-end sweep, while refreshing
`examples/TEACHING-ASSEMBLER-INTERNALS.md`)

## BLUF

An instruction with no operands, or one whose last operand is an expression, is recorded in the AST
with the line number of the line after it, because the parser reads `line_no` after the scanner has
consumed the newline. Done means every AST node carries its own line in all four engines, with a
regression test, and the student-facing sample output in
`examples/TEACHING-ASSEMBLER-INTERNALS.md` shows the right numbers.

## Context

- Repro (`-print-ast`, any engine):

  ```
  	.text
  main:
  	syscall            # line 3, reported [line 4]
  	addi $a0, $0, 0    # line 4, reported [line 5]
  	jr $ra             # line 5, reported [line 5] (right)
  ```

- The same slip was fixed for error messages in the 2026-11-11 batch: the parser captures
  `line_no` before parsing the operands (`check_imm_range`'s `line` parameter, and
  `parse_error_on_line` in `src/parser.c`; `tasks/archive/2026/11/11/c-undefined-behavior-fixes-then-ports.md`).
  The AST node constructors (`new_node` in `src/ast.c`, called from the parser's `emit_*` helpers)
  still read the live `line_no`.
- A pseudo-op's children already take the pseudo-op's line
  (`tasks/archive/2026/11/11/explain-mode-correctness-bugs.md`), which is why `li`/`la` print the right
  number while a following `syscall` doesn't.
- What the line feeds: the `[line N]` of `-print-ast`/`-show-expansion`, `source_line` in
  `-print-ast-json`, `-listing` events, the `; N:` source annotation in listings and the teaching mode,
  and emit-time error messages. The goldens (`tests/golden.ast_parity*.txt`, `tests/tt.explain.expected`)
  will change.
- Every port copies the C behavior today, so each needs the same change (`CLAUDE.md`).

## Plan

1. Add a regression case: the three-line repro above, checked with `-print-ast` and
   `-print-ast-json`; it fails today.
2. Capture the statement's line when the parser starts it (before any operand is scanned) and pass it
   to the node constructor, instead of reading `line_no` at construction.
3. Regenerate the goldens and read the whole diff: only line numbers may change.
4. Follow in Python, Haskell and Rust.
5. Refresh every sample in `examples/TEACHING-ASSEMBLER-INTERNALS.md` from a real run and drop the
   note about this bug. Its `-listing` sample was already stale before this task (found the same
   day): the real format interleaves the instruction listing with `fref`/`fres` lines, and the
   string is now emitted after the text, so the prose saying `.data` is emitted first is false too.
6. Gate: `make image`.

## Open questions

None.
