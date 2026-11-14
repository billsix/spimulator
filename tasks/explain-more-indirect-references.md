# More indirect references in the teaching tests and examples; name the syscall constants in explain mode

**Status:** proposed — questions answered; ready to start on go-ahead
**Priority:** 6
**Difficulty:** 4
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Two requests from the maintainer, recorded as given:
1. Add more **indirect references** (memory reached through a register holding an address), above
   all in `tests/tt.explain.s`, drawing on the programs under `tests/` and the examples, and check
   how the parser handles each form.
2. In agent-written code that switches on bare constants, find out why, and change the constants
   to named ones. The site is `explain_syscall` in `src/explain.c` (decision below).

Done means the new cases are in the regression goldens (and the explain goldens where they exercise
teaching mode), and `explain_syscall` switches on the `include/spim-syscall.h` names.

## Context

- `tests/tt.explain.s` is the teaching-mode smoke test; its header lists what it covers, including
  "loads and stores by symbol AND by N($reg) base+offset" and `jalr`/`jr`. Goldens:
  `tests/tt.explain.expected`; suite `regression` (`tests/run-test.sh`).
- The parser is `src/parser.c` (AST design: `tasks/reference/parser-ast-emit.md`); addressing-mode
  parsing is where an indirect form is accepted or rejected.
- Examples: `examples/` (`examples/READING-ORDER.md`). Any C added there stays inside the subset
  `tasks/mini-c-compiler.md` will compile (no structs, declarations first), so it stays simple to
  parse.
- The generated sources in this repo are the opcode tables (`tools/gen_opcodes.py`, `CLAUDE.md` ›
  Conventions) and the tree-sitter parser (`tree-sitter/`).

## Decisions

- **The bare-constant switch is `explain_syscall`** (William Emerison Six <billsix@gmail.com>,
  2026-11-14). The request's "quad generated code" was dictation of "Claude-generated code": much of
  explain mode was written by the agent. `explain_syscall`'s `switch (v0)` uses `case 1:`, `4`, `5`,
  `8`, `10`, `11`, while `include/spim-syscall.h` already names every call (`PRINT_INT_SYSCALL`,
  `READ_INT_SYSCALL`, ...). The other bare-number switches in the core need no change: `switch
  (step)` in `src/explain.c` counts teaching steps, and `src/run.c`'s LWL/LWR switches are byte
  offsets.
- **Cover every indirect form** (William Emerison Six <billsix@gmail.com>, 2026-11-14):
  pointer-to-pointer loads, a jump table (`jr` through a loaded address), function pointers (`jalr`
  through a table), and stores through a computed address.
- **"Spimulator examples" means the `examples/` tree in this repo** (William Emerison Six
  <billsix@gmail.com>, 2026-11-14), not the separate `spimulator-examples` repository.

## Open questions

None.
