# Parser ownership model & AST-driven emission

**What this is:** how `src/parser.c` / `src/scanner.c` / `src/instruction.c`
turn assembly text into memory, who owns the heap allocations along the way,
and why the parser has exactly one mode. Distilled from the
`parser-leak-cleanup` work (leak fix + PARSE_DIRECT deletion, 2026-08-03).
Read this before touching the parser, the emit helpers, or the AST node
lifetime.

## The pipeline (one mode, since 2026-08-03)

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

### History: PARSE_DIRECT is gone

spim used to carry a second mode, **PARSE_DIRECT** (syntax-directed
translation): the parser called the action helpers *inline* during the
parse, committing to memory as each statement was read, building no tree.
It was the historical default; PARSE_AST was opt-in via `-parser=ast` (and
implied by `-print-ast`/`-show-expansion`/`-print-ast-json`). The dispatch
helpers branched on `should_emit()` (fire inline action) vs
`should_build_ast()` (append node).

PARSE_DIRECT was deleted 2026-08-03 (task: `parser-leak-cleanup`, Option C)
to end the two-ownership-model hazard. Consequences worth knowing:

- The `-parser=sdt`/`-parser=ast` flags are gone (an unknown `-parser=` is
  now ignored with a usage note). `parse_mode_t`, `parser_set_mode`,
  `should_emit`, `should_build_ast`, and the `store_op`/`store_fp_op`
  function-pointer indirection were removed.
- The `this_line_labels` / `align_labels_to` / `clear_labels` machinery is
  now driven entirely from `emit_ast`/`emit_one` (labels are `cons_label`'d
  at emit time). The same calls still appear in the parse phase but are
  guarded no-ops there (`this_line_labels` is empty during parse), left in
  place because they are shared with the live emit path.

## Ownership model (the leak-prone part)

**`make_imm_expr(offs, sym, ...)` and `make_addr_expr(offs, sym, reg)` only
*read* `sym`** — they look it up with `lookup_label` and store the resulting
`label*` (the `imm_expr`/`addr_expr` struct holds a `label*`, never a
`char*`). They neither retain nor free the string. **The caller always owns
`sym` and must free it.** This is the single fact that all the parser's
`free(sym)` calls depend on; violating it (or, as the old `make_addr_expr`
did, `strdup`'ing `sym` before the lookup) leaks.

Scanner-side: a TOK_ID / TOK_STR token owns a heap string; `scanner_advance`
moves it into the global `scan_value`. The parse function that consumes it
owns it from there — free it (`parse_string_list`, `parse_factor`, etc.) or
transfer it. A token discarded without consuming (error recovery, ignored
directives via `sync_to_nl`) must have its `scan_value.p` freed at the
discard site. TOK_FP's `scan_value.p` points at a `static double` — **not**
heap, never freed.

`imm_expr` **nodes**: `emit_i` copies (`dup_imm`) for the AST and leaves the
caller owning the original; `emit_i_free` transfers (frees the original).
Wrapping a fresh `const_imm_expr(...)` in `lower_bits_of_expr(...)` (which
copies) orphans the inner node unless you capture and free it. Pass inline
`const_imm_expr(...)` allocations to `emit_i_free`, never `emit_i`.

Verification: `valgrind --leak-check=full` on `tests/tt.argv.s` /
`tt.explain.s` must report `definitely lost: 0`. The ASan address gate
(`-Db_sanitize=address`) is the corruption gate; spim.c pins
`detect_leaks=0` (spim's intentional exit-time statics), so valgrind is the
authoritative leak check.

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
