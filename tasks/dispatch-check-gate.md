# Make `parse_j` pass the dispatch check, and run the checker in meson

**Status:** proposed — needs go-ahead
**Priority:** 3
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.1, §2.3 F5

## BLUF

`CLAUDE.md` says `python3 tools/c_dispatch_check.py BUILDDIR src` must exit 0. It exits 1 today:
the cop2/jalr.hb change to `parse_j` (commit `625b30b`) added a guard run two minutes after the
checker was vendored (`2452136`). No gate runs the checker, so nothing noticed. Done means `parse_j`
is one `if`/`else if`/`else` chain, and a meson test runs the checker so the tree stays clean.

## Context

- Reproduce: `CC=clang meson setup /tmp/b . && python3 tools/c_dispatch_check.py /tmp/b src` prints
  `src/parser.c:943 guard-run parse_j 3 ifs on op | scanner_peek()`, and exits 1.
- The run is `if (op == TOK_COP2_OPCODE) { …; return; }`, then
  `if (scanner_peek() == TOK_REG) { … return; }`, then `if (op == TOK_JALR_HB_OPCODE) { …; return; }`,
  then the label form.
- The C standard's Dispatch section (the c-coding-standard.md reference doc from
  github.com/billsix/runClaudeInContainer) says such a run is one chain, with the tail in the final
  `else`.
- The image builds with `CC=clang`, so its builddir's `compile_commands.json` is the one the
  checker needs. `CLAUDE.md` names the tool; `tasks/archive/2026/11/11/vendor-shared-dispatch-checker.md`
  says it wasn't wired into a gate.
- The C behavior must not change, so the port suites stay green unchanged.

## Plan

1. Rewrite `parse_j` as one chain: `if (cop2) … else if (scanner_peek() == TOK_REG) … else if
   (jalr.hb) … else { label form }`.
2. Register a meson test, `dispatch_check`, that runs `tools/c_dispatch_check.py
   meson.current_build_dir() src`. It only works under clang, so skip it under gcc with a message,
   or make the image the gate.
3. Gate: `meson test` and `make image`. The test must fail if the chain is reverted (check that
   once).

## Open questions

None.
