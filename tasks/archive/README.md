# Archived task / plan documents

Plan docs from completed migrations.  Kept for historical context
(decisions, rationale, alternatives considered) but no longer
actionable.

## C23 modernization (May 2026)

Consumption of the C23 features the project had already opted into
via `c_std=gnu23`.  Twelve numbered commits on the
`c23modernization` branch deliver `[[noreturn]]`, `[[nodiscard]]`,
`[[fallthrough]]` with `-Wimplicit-fallthrough=5`, `enum E : T`
underlying types, two `#define` clusters promoted to enums
(`op_type`, `mips_exc_code`), `constexpr` for ~85 typed
constants, `static inline` / `typeof` macros for `MIN`/`MAX`/
`streq`/`sign_ex`, `<stdbit.h>` for CLO/CLZ, `<stdckdint.h>`
for arithmetic overflow detection, `#embed` for the default
exception handler, and `= {}` empty initializers.

- **`c23-modernization.md`** — final-state summary covering
  what's in use, design decisions worth knowing (especially the
  op.h X-macro split and the `RAISE_EXCEPTION` carve-out), and
  followup items.

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
