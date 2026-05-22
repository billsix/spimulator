# Archived task / plan documents

Plan docs from completed migrations.  Kept for historical context
(decisions, rationale, alternatives considered) but no longer
actionable.

## AST migration (May–June 2026)

Migration of spim's parser from syntax-directed translation to an
abstract syntax tree representation, with `emit_ast` walking the
tree to drive the action helpers.

- **`PLAN-parse-tree-investigation.md`** — decision rationale for
  the AST representation (SDT vs parse tree vs event log).
- **`PLAN-parse-tree-migration.md`** — phased migration plan
  (event log → AST types → parser refactor → emit_ast → pseudo-op
  first-class → teaching surfaces → comment cleanup).
- **`parse-actions-catalog.md`** — exhaustive inventory of every
  state-mutating action call in `parser.c` and which AST node
  absorbs it.

## Hand-written parser migration (May 2026)

Earlier migration that replaced flex+bison with a hand-written
recursive-descent parser, on the way to enabling the AST work.

- **`handwritten-parser-design.md`** — design for the hand-written
  parser.
- **`handwritten-parser-migration.md`** — incremental migration
  steps.
- **`scanner-parser-inventory.md`** — initial inventory of the
  flex/bison front-end.

## Current architecture

See:

- `include/parser.h`, `include/ast.h`, `include/asm_event.h` for
  the interfaces.
- `src/parser.c`, `src/ast.c`, `src/asm_event.c` for the
  implementations.
- `/examples/TEACHING-ASSEMBLER-INTERNALS.md` for a student-facing
  tour of the teaching surfaces (`-print-ast`, `-show-expansion`,
  `-listing`, `-explain`).
