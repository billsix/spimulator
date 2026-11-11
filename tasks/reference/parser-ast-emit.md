# Parser ownership model & AST-driven emission

**What this is:** how `src/parser.c` / `src/scanner.c` / `src/instruction.c`
turn assembly text into memory, who owns the heap allocations along the way,
and why the parser has exactly one mode. Distilled from
`tasks/archive/2026/08/03/parser-leak-cleanup.md`.
Read this before touching the parser, the emit helpers, or the AST node
lifetime.

## The pipeline

The parser is **parse-to-tree, then emit**:

1. **Parse.** The recursive-descent parser (`parse_file` → `parse_line` → …)
   turns each statement into an `ast_node`, appended to the current file
   tree (`current_file`). Pseudo-ops (`la`, `li`, `bge`, …) expand into
   their real instructions as **child nodes** of an `AST_PSEUDO` wrapper.
   Nothing is written to simulator memory during parse.
2. **Emit.** `emit_ast(current_file)` walks the tree in source order;
   `emit_one` calls the action helpers (`r_type_inst`, `store_word`,
   `record_label`, `user_kernel_text_segment`, …) that commit each node's
   effect to memory and the symbol table.

The parse phase routes every instruction/directive through the `emit_*`
dispatch helpers (`emit_r`, `emit_i`, `emit_i_free`, `emit_j`, `emit_fp_r`,
`emit_fp_compare`, `emit_dir_*`, `emit_label_*`, `emit_data_string`), which
now do exactly one thing: append the corresponding AST node. Actual
emission happens only in `emit_one`.

### Why one mode

The scanner and parser are hand-written recursive descent (`scanner.c` / `parser.c`). Every
statement is parsed into the tree before anything is emitted, so each heap allocation has one owner
at a time: the AST node, until `emit_one` consumes it. A second, emit-while-parsing mode would need
a second ownership model, which is how the parser once leaked; the git history of
`tasks/archive/2026/08/03/parser-leak-cleanup.md` has that mode and its removal.

The label machinery (`this_line_labels`, `align_labels_to`, `clear_labels`) is driven from
`emit_ast`/`emit_one`, where labels are `cons_label`'d. The same calls also appear in the parse
phase, where they are no-ops (`this_line_labels` is empty during parse); they stay because they are
shared with the emit path.

## Source-line annotations under deferred emit

An assembled instruction's listing/explain annotation (`; NNN: <source>`)
is `SOURCE(instruction)`, set in `store_instruction` (`instruction.c`). It
used to come from the live scanner via `source_line()`. Under deferred
(AST) emit the scanner has moved to the file's last line by emit time, so a
live read yields the wrong source for **every** instruction (symptom seen
2026-08-03: every instruction annotated with the exception handler's last
line, `__eoth:`). This was a latent PARSE_AST bug, exposed when AST became
the only mode.

Fix (the pattern column-tracking should extend):

- `ast_node` carries `char* src_text` — the `"NNN: text"` snapshot from
  `source_line()`, captured in `new_node` (`ast.c`) while the scanner is
  still on the node's line; freed in `ast_free`.
- `emit_one` sets `line_no = node->source_line` **and**
  `emit_source_set(node->src_text)` before emitting each node; `emit_ast`
  calls `emit_source_clear()` when done.
- While active, `store_instruction` records `strdup(emit_source_text)`
  instead of `source_line()`; while cleared (e.g. interactive REPL, where
  the scanner is live) it falls back to `source_line()`.

The `ast_parity`/`ast_parity_all` tests **strip** these annotations before
diffing (they check bytes), so they did not catch the annotation bug — the
`explain` golden (full diff, no strip) did. Keep that in mind when changing
listing output.

## Test goldens

`ast_parity` (tt.core.s) and `ast_parity_all` (20 programs) are now
golden-dump regression tests: the golden is the pre-removal SDT `-dump`
output, so they prove the AST-only emitter reproduces byte-identical memory.
Regenerate with `tests/gen-parity-golden.sh <spim-binary>` only on an
intentional codegen change, and review the diff.
