# Parser / scanner allocation lifetime cleanup — DONE (2026-08-03)

**Priority:** 4 · **Difficulty:** 6
Filed May 2026 during the post-C23 ASan/valgrind audit; pre-existing.

Durable design knowledge (ownership model, AST-only emit architecture, the
deferred-emit source-annotation fix) lives in
[`tasks/reference/parser-ast-emit.md`](../../../reference/parser-ast-emit.md).
This is just the work record.

## What was done

Two phases, both landed and verified.

### Phase 1 — Option A: fix the leak drop sites

valgrind reported `definitely lost: 224 bytes / 26 blocks` on `tt.argv.s`
(and proportionally more on larger programs) from parser/scanner transients
never freed. Root model: `make_imm_expr` / `make_addr_expr` only *read*
`sym` (via `lookup_label`) and never retain it, so the caller always owns it.
The defects fixed:

1. `make_addr_expr` (`instruction.c`) `strdup`'d `sym` before the lookup that
   dropped the pointer — orphaned on every call with a symbol (largest
   source). Removed the copy.
2. `parse_label` (`parser.c`) — never freed the TOK_ID string (comment
   wrongly claimed `make_imm_expr` stores it). Added `free(sym)`.
3. `parse_imm32` (`parser.c`) bare-`TOK_ID` path — same wrong comment,
   leaked `sym`. Added `free(sym)`.
4. `parse_address` (`parser.c`) `ABS_ADDR '+' ID` path — didn't free `sym`.
   Added `free(sym)`.
5. `sync_to_nl` (`parser.c`) — discarded TOK_ID/TOK_STR (ignored directives,
   error recovery) without freeing `scan_value.p`. Freed on discard.
6. `i_type_inst_full_word` (`instruction.c`) — `lower_bits_of_expr(
   const_imm_expr(low))` orphaned the inner node (`lower_bits_of_expr`
   copies). Captured and freed the intermediate.
7. `li.d`/`li.s` (`parser.c`) — inline `const_imm_expr(...)` passed to
   `emit_i` (caller-owns) leaked the node. Switched to `emit_i_free`.

### Phase 2 — Option C: delete the PARSE_DIRECT codepath

Bill authorized deleting the inline syntax-directed-translation path so the
AST is the sole parse mode (ends the two-ownership-model hazard). Removed:
`parse_mode_t` / `PARSE_DIRECT` / `PARSE_AST` enum + `parser_set_mode` /
`parser_get_mode`; `should_emit` / `should_build_ast` and every inline-emit
branch they gated in `parser.c`; the `-parser=sdt`/`-parser=ast` flags
(`spim.c`, man page, teaching doc); the dead `store_op`/`store_fp_op`
function-pointer indirection. `emit_ast`/`emit_one` is now the only emitter.

Deleting PARSE_DIRECT exposed a **latent PARSE_AST bug**: instruction source
annotations (`; NNN: text`) were captured live from the scanner in
`store_instruction`, which is stale once emit is deferred (every instruction
showed the file's last line). Fixed by giving `ast_node` a `char* src_text`
snapshot (captured in `new_node`, freed in `ast_free`) and an
`emit_source_set`/`emit_source_clear` override that `emit_one` drives per
node. See the reference doc.

Test change: `ast_parity` / `ast_parity_all` compared SDT-vs-AST, which no
longer exist. Converted both to **golden-dump regression tests** — the golden
is the pre-removal SDT `-dump` output, so they now prove the AST-only emitter
is byte-identical to what SDT produced. Added `tests/gen-parity-golden.sh`
and `tests/golden.ast_parity{,_all}.txt`.

## Files changed

`src/parser.c`, `src/instruction.c`, `src/ast.c`, `src/spim.c`,
`include/parser.h`, `include/instruction.h`, `include/ast.h`,
`Documentation/spim.1`, `examples/TEACHING-ASSEMBLER-INTERNALS.md`,
`tests/run-test.sh`, `tests/gen-parity-golden.sh`,
`tests/golden.ast_parity.txt`, `tests/golden.ast_parity_all.txt`.
(github.com/billsix/spimulator)

## Verification

- `meson test`: **32/32** pass (default debug build).
- valgrind `--leak-check=full` on `tt.argv.s`, `tt.explain.s`, and a
  broad-coverage input, in the new AST-default mode: **0 definitely/
  indirectly lost**.
- ASan address build (`-Db_sanitize=address`) regression suite: **25/25**,
  no double-free/corruption; transient `detect_leaks=1` run showed no leaks.
- `explain` golden (full-diff, catches the annotation bug the parity tests
  strip) passes byte-for-byte.
- clang-format clean on all changed sources.
- Not run: full `make image` container gate (shared-RAM constraint);
  change is C-only and touches no tree-sitter/docs-flag inputs.

## Follow-on

`ast-column-tracking.md` is now **unblocked** (single parse mode; the new
`src_text` field and per-node `emit_source_set` are the pattern column
tracking extends).
