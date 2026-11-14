# Write the core's struct tables with designated initializers

**Status:** in progress
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Branch:** `c23-designated-initializers`

## BLUF

The simulator core initializes its `name_val_val` and `struct pseudo_info` tables positionally
(`{"a0", 4, 0}`) instead of with designated initializers (`{.name = "a0", .value1 = 4}`), which the
shared C coding standard asks for. Rewrite every positional struct initializer in `src/` and
`include/`, including the generated opcode tables (through their generator). Done means a re-scan
finds none, and `make image` passes.

## Context

- The rule: "Use designated initializers" in the shared C standard (the `c-coding-standard.md`
  reference doc of github.com/billsix/runClaudeInContainer, section "Structs, constants, and the
  preprocessor"). Scope is `src/` + `include/` (`CLAUDE.md` › "Modernization scope"); `examples/` and
  `pgu/` opt out of designated initializers on purpose (`tasks/reference/example-c-teaching-style.md`).
- The maintainer asked for this after spotting `name_val_val` (2026-11-14) and expected the earlier
  C23 passes to have caught it. They did not: `tasks/archive/2026/11/10/c23-modernization-pass3.md`
  considered designated initializers only for the `mips_instruction` decode constructors (opted out,
  with a reason) and never scanned the static tables.
- Discovery: `tasks/adhoc/c23-designated-initializers/discover.py BUILDDIR` walks clang's JSON AST
  of every translation unit in a clang-configured meson build and lists each struct or union
  `InitListExpr` written in this repo whose first token after `{` is not a designator. clang-tidy's
  `modernize-use-designated-initializers` would be the obvious tool, but clang-tidy 23 reports nothing
  for C. Snapshot: `tasks/adhoc/c23-designated-initializers/data/before.tsv`.

## Findings (scan of 2026-11-14, all 103 translation units)

1082 positional initializers, all in three tables; none in `examples/`, `pgu/` or `include/`:

| Table | File | Rows | Fix |
|---|---|---:|---|
| `opcode_tbl`, `opcode_encoding_tbl`, `opcode_decode_tbl` (`name_val_val`) | `src/opcode-tables.c` (generated) | 1005 | the C emitter in `tools/gen_opcodes.py`, then `make generate` |
| `register_tbl` (`name_val_val`) | `src/scanner.c` | 35 | by hand |
| `pseudo_ops` (`struct pseudo_info`) | `src/explain.c` | 42 | by hand |

## Plan

1. Change the C emitter in `tools/gen_opcodes.py` to write `{.name = …, .value1 = …, .value2 = …}`
   and regenerate; only `src/opcode-tables.c` should change.
2. Rewrite `register_tbl` (dropping the always-zero `.value2`) and `pseudo_ops`.
3. Re-scan (zero rows), build, `make image`.
4. Promote the scanner to `tools/` so a later audit can run it, and record the method where audits
   and the shared standard will find it.
