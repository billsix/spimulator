# Redesign evaluation — the `opcodes.h` OP X-macro

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 5 (the investigation; a build-out to external codegen would be ~7)
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)

## BLUF

Decide whether to keep the `include/opcodes.h` OP X-macro as-is or replace it with external code
generation, and record the decision with its rationale. The one weird part is that `opcodes.h` is
`#include`d **inside an enum body** in `include/tokens.h`, which forces the file to be nothing but
comma-terminated `OP()` rows (no header guard, no typedefs). "Done" for this task = a written
adopt/keep decision in this doc; if the decision is to replace, this task spawns the build-out as a
follow-on. **`_Generic` is not a candidate** — it dispatches on a single expression's type and cannot
iterate a table to generate code (full reasoning in the reference doc). This is scoped as
investigate-and-decide, defaulting to **keep the X-macro** unless retiring the enum-body trick is
judged worth a generator step.

## Context (read first, cold-start)

How the mechanism works today — the 381-row table, the four columns, the seven consumers, the three
constraints, the `BUILD_TREE_SITTER=1` gate, and the full design analysis — is written up once in
**`tasks/reference/opcodes-x-macro.md`**. Read that first; this task does not repeat it.

Short version: `opcodes.h` is the single source of truth for every MIPS instruction / pseudo-op /
directive / REPL command. Each consumer `#define`s `OP(name, sym, type, enc)` to project out the
columns it needs, then `#include`s the file, so one list feeds the `TOK_*` enum (`tokens.h`), the
keyword table (`scanner.c`), the op-type table (`parser.c`), three instruction tables
(`instruction.c`), a dump utility (`dump-opcodes.c`), and — already out-of-tree, read as text, not via
cpp — the tree-sitter grammar's keyword lists (`extract-keywords.py`).

This was split out of the simulator-core modernization (`tasks/c23-modernization-pass2.md`) because it
is a design decision, not part of the mechanical C23 sweep.

## The decision to make

Choose one, and write the rationale here:

1. **Keep the X-macro (default).** Self-contained in the C build, no generator step, no generated
   files, correct-by-construction (every table is the same 381 rows and cannot drift). Accept the
   `tokens.h` enum-body `#include` and the "no structure in `opcodes.h`" rule as a documented cost.
   If chosen, the only possible follow-up is small hardening (e.g. a build-time check that the table
   stays alphabetized / has no duplicate mnemonics), optional.
2. **Replace with external code generation** (the direction the tree-sitter pipeline already takes). A
   generator reads the table — `opcodes.h` as-is, or a cleaner data source (TSV/JSON) — and emits
   plain generated C (the `TOK_*` enum and the lookup tables), retiring the enum-body trick. If
   chosen, this task spawns a build-out follow-on bound by `~/.claude/reference/codegen-conventions.md`.

## Investigate (to inform the choice)

- **Enumerate the consumers precisely** and confirm each still matches the reference doc's table (grep
  `#define OP(` across `include/` and `src/`). Note any consumer whose expansion a generator would have
  to reproduce exactly.
- **Decide the source of truth under option 2** — keep parsing `opcodes.h`, or move the rows to a data
  file and regenerate `opcodes.h` too. The tree-sitter script already parses `opcodes.h`; reusing that
  parse path argues for keeping `opcodes.h` as the source.
- **Scope the enum-body dependency.** `tokens.h` is the only consumer that forces the "pure
  comma-terminated rows" constraint. Confirm nothing else relies on including `opcodes.h` inside
  another declaration, so that option 2 can fully retire the constraint.
- **Cost of the generated-file discipline.** Under option 2: a parity harness (generated tables
  byte-identical to today's), a determinism gate (same bytes run-to-run), and the never-hand-edit rule
  — weigh this against the enum-body trick it removes.

## Constraints / notes

- **`_Generic` is out** — recorded here and in the reference doc so it is not re-proposed; it solves a
  different problem (type dispatch, not list iteration).
- **`BUILD_TREE_SITTER=1` is mandatory for any verification** that touches `opcodes.h` /
  `opcode-types.h` — the keyword pipeline is the consumer most likely to break, and a
  `BUILD_TREE_SITTER=0` build never exercises it (`CLAUDE.md` §Conventions).
- **Any change must be table-for-table identical** — the existing `ast_parity` goldens and the full
  `meson test` suite are the oracle; a redesign that changes any emitted table is a regression.
- In-container verification only: `make image BUILD_TREE_SITTER=1` after any change.

## Acceptance

- A decision (keep / replace) recorded in this doc with its rationale.
- If **keep**: optionally the small alphabetization/duplicate guard, or an explicit note that none is
  wanted; task archives.
- If **replace**: a build-out follow-on task scaffolded (`proposed — needs go-ahead`), cross-linked,
  carrying the parity-harness + determinism-gate plan; this evaluation task archives once that exists.

## Open questions

None — the framing is settled.

**Resolved 2026-10-16 (William Emerison Six <billsix@gmail.com>): keep this as an investigation.** Do
not commit to a codegen build-out now; the keep-vs-replace decision is the investigation's deliverable
(option 1 vs option 2 above), and the default remains keeping the X-macro. `_Generic` stays ruled out.
The task is still `proposed` — it needs a go-ahead to *run* the investigation, not to decide its shape.

## Cross-links

- `tasks/reference/opcodes-x-macro.md` — how it works + the full design analysis (read first).
- `tasks/c23-modernization-pass2.md` — the simulator-core modernization this was split out of.
- `~/.claude/reference/codegen-conventions.md` — rules any codegen build-out must follow.
