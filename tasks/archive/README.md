# Archived task / plan documents

Plan docs from completed migrations.  Kept for historical context
(decisions, rationale, alternatives considered) but no longer
actionable.

## Explain-mode consistency audit (May 2026)

Per-instruction consistency audit of the `-explain=1` through
`-explain=4` narration, with five phases of targeted fixes.
Branch `explainModeConsistency`.

Addressed seven concrete inconsistencies including the `j`-opcode
missing-prefix, zero-offset omission in load/store inputs, uneven
modifier description verbosity, the reading-order dependency
created by the show-once first-encounter logic, sparse try-it
blocks, the store-side memory-snapshot asymmetry, and the L1
pseudo-op header being 4× the size of plain instruction blocks.

- **`explain-mode-consistency-audit.md`** — final-state summary
  covering the seven items, what fixed each, the per-level
  spread numbers before and after (L1 hit the 1.5× target;
  L2-L4 modestly improved), the deferred cross-mnemonic
  template-shape normalization, and a followup round on branch
  `moreExplainModeUpdates` that trimmed the repetitive
  family-enumeration anti-pattern in category and modifier
  descriptions (~5% additional L2 reduction).

Files updated: `src/explain.c` and `tests/tt.explain.expected`.

## Variable name expansion (May 2026)

Renamed ~2500 identifier sites and 9 source files to expand
non-MIPS-spec abbreviations.  Eleven phases on the
`renameVariables` branch.

- **`variable-name-expansion.md`** — final-state summary
  covering naming policy (MIPS-spec names stayed abbreviated,
  rarely-used names got fully spelled, high-frequency names got
  moderate forms), phase-by-phase deliverables, and the design
  details (e.g. the `inst` → `instruction` + `instruction`
  type → `mips_instruction` two-part rename forced by C's
  shadow rules and a subtle `sizeof()` correctness bug).

Files renamed include `src/sym-tbl.{c,h}` → `src/symbol-table.{c,h}`,
`src/mem.{c,h}` → `src/memory.{c,h}`, `include/reg.h` →
`include/registers.h`, `include/op.h` → `include/opcodes.h`,
and several others.

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
- `examples/TEACHING-ASSEMBLER-INTERNALS.md` for a student-facing
  tour of the teaching surfaces (`-print-ast`, `-show-expansion`,
  `-listing`, `-explain`).

## Merge /examples into /spimulator (May 2026)

7-phase repo restructuring that absorbed the standalone
`/examples` curriculum into `/spimulator/examples/` as a
literal mirror, then unified the build / test / container
story.  After the merge: one Dockerfile, one `meson test`,
and goldens enforced at container-build time (any drift
between the C side and the spim-asm side of a paired demo
fails the build).

- **[`merge-examples-into-spimulator.md`](merge-examples-into-spimulator.md)**
  — full plan + phase-by-phase status writeup.  Covers the
  decisions (literal mirror, no subtree, manual commit replay
  preserving original author/email/date/message), the prune
  list (book, Dockerfile, Makefile, entrypoint, output, musl
  — none needed in the merged tree), the meson surgery
  (subdir + per-target flag scoping so -nostdlib doesn't leak
  into spim's libc-needing build), the Dockerfile changes
  (diffutils, COPY examples/, dual-suite test step), and the
  sed gotcha when bulk-replacing `/examples/` substrings.

Files updated: many.  Net effect: `/spimulator/examples/`
contains the full curriculum with 45 historical commits in
`git log examples/`, the 6 library-demo tests run under both
the C and asm sides at every container build, and the
standalone `/examples` repo can be deleted.

## Investigate tree-sitter for parser replacement (May 2026)

Investigation task — assessed whether the hand-written
recursive-descent parser (~1700 lines in `src/parser.c`)
could profitably be replaced by a tree-sitter grammar.
Conclusion: NOT for the simulator's runtime parser (the
hand-written version is fine for what it does); MAYBE as a
separate editor-integration grammar (`tree-sitter-mips-spim`
in its own repo).

- **[`investigate-tree-sitter.md`](investigate-tree-sitter.md)**
  — covers the three use-cases considered (A: editor
  integration, B: runtime replacement, C: education tool),
  the trade-offs in each direction, the "what's hard"
  section (preprocessor-like include handling, span
  computation across token kinds), and a "first concrete
  step" recommendation IF use case A is the chosen direction
  (start a separate `tree-sitter-mips-spim` repo, write
  a 50-line op.h→grammar.js converter, iterate from
  `examples/01-helloworld.asm`).

If the editor-integration grammar work actually starts,
that's a new task (not a re-open of this one).
