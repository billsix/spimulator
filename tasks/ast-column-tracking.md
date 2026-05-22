# AST column tracking

Track source column begin/end on every AST node, in addition to
the existing per-node source line.  Print the column information
as part of `-print-ast` and emit it in the JSON output.

## 1. Current state (already verified)

- **`ast_node`** in `include/ast.h` carries `source_line` (int)
  and no column fields.
- **`scanner.c`** tracks `line_no` (file-static `int`) and a
  per-line buffer `current_line_buf`, but **does not** track
  column position within the line.  No `col_no` or equivalent.
- **`scan_one_token`** in `src/scanner.c` reads characters via
  `next_char` / `peek_char`, but neither maintains a column
  counter.
- **Tokens** in `tok_buf[]` carry only `type` and `val`; no
  source position.

So column information is **not** captured anywhere today.  Adding
it requires a small change at the scanner level plus plumbing to
the AST.

## 2. Why we'd want it

Three concrete reasons, each independently sufficient:

1. **Better error messages.**  Today's parse errors show
   `"on line N"` but not which token on that line caused the
   problem.  A column range lets us underline the offending span
   like compilers do.

2. **IDE / GUI integration.**  Any tool that consumes the
   `-print-ast-json` output (planned GUI scrubber, listing
   converter, syntax highlighter) needs columns to map AST nodes
   back to source-text spans.  Without columns, the tool can't
   visually highlight which characters of the source correspond
   to which AST node.

3. **Pseudo-op visualization.**  `-show-expansion` shows that
   `la $a0, msg` rewrites to `ori`.  With column info, a future
   teaching surface could highlight the specific tokens that
   triggered each part of the expansion (the mnemonic vs the
   destination register vs the operand).

## 3. Design

### 3.1 Scanner change

Add column tracking to `src/scanner.c`:

- New file-static `int col_no = 1;` — 1-based column within the
  current line.
- `next_char` increments `col_no` on each character consumed.
- The newline path (`if (c == '\n')`) resets `col_no = 1` along
  with incrementing `line_no`.
- Save `col_no` to `current_col_no_saved` in `scanner_start_line`
  so error messages and AST construction can read it.

### 3.2 Token-level capture

Extend `tok_buf[i]` entries in scanner.c with `col_begin` and
`col_end` fields:

```c
typedef struct {
  int type;
  scan_value_t val;
  int line;           /* line at start of token */
  int col_begin;      /* 1-based column at first char */
  int col_end;        /* 1-based column just past last char */
  bool present;
} buffered_token;
```

`scan_one_token` records `col_begin` at function entry (before
consuming the first char of the token) and `col_end` after the
last char.  Existing callers (`scanner_peek` / `scanner_advance`)
are unchanged; new accessors `scanner_token_col_begin()` /
`scanner_token_col_end()` expose the position of the most-recently-
advanced token.

### 3.3 AST node payload

Extend `ast_node` in `include/ast.h`:

```c
struct ast_node {
  ast_kind kind;
  int source_line;
  int col_begin;          /* 1-based, 0 if unknown */
  int col_end;            /* 1-based exclusive end */
  ast_node* next;
  union { ... } u;
};
```

The constructors in `src/ast.c` snapshot `line_no` for
`source_line`; they should also snapshot
`scanner_token_col_begin()` for `col_begin`.

For nodes that span MULTIPLE tokens (e.g. an instruction's
register + register + immediate operands), the parser should
update `col_end` after parsing the last operand to capture the
full source span.  This means parse_* functions need to update
the node's `col_end` field at the end of their work.

### 3.4 Constructor signature

Two options:

**Option A — implicit snapshot:** Constructors continue to take
the existing args; they snapshot `col_begin` from
`scanner_token_col_begin()`.  Parsers update `col_end` manually
after the last operand consumed.

**Option B — explicit position:** Add `int col_begin, int col_end`
to every constructor.  Parsers track positions explicitly.

Recommendation: **Option A.**  Keeps the existing constructor
signatures, only adds an explicit `col_end` update at the end of
each `parse_*` function that produces an AST node.

### 3.5 Printing

`ast_print` in `src/ast.c` already prefixes each node with
`[line N] `.  Extend to `[line N, col C1-C2] `:

```
[line 7, col 9-21] PSEUDO mnemonic=la
  [line 7, col 9-21] INST_I op=544 ...
```

`ast_print_json` adds two more fields to every node object:

```json
{"kind":"INST_R","source_line":12,"col_begin":9,"col_end":24,...}
```

## 4. Risk

Low.  The scanner change is small (~20 lines), the AST struct
change is two ints per node, and the parser update is a one-line
`col_end` assignment per parse_* function.  No behavioral change
in the action helpers or emit pass.

## 5. Estimated effort

- Scanner column tracking + token-level capture: 2-3 hours
- AST struct + constructor snapshot: 1 hour
- Parser `col_end` updates across ~30 parse_* functions: 3-4 hours
- ast_print + ast_print_json output update: 1 hour
- Regression test: 1 hour

**Total: ~1 day** of focused work.

## 6. Verification

- Build clean.
- Existing 21 regression tests still pass (the change is purely
  additive — no semantic effect on memory or runtime).
- New test: `-print-ast` on a known program; grep for `col_begin`
  values matching the expected source-text offsets.
- New test: `-print-ast-json` output parses as valid JSON and
  contains numeric `col_begin` / `col_end` fields on each node.

## 7. Open question

When a pseudo-op rewrites into multiple instructions (e.g.
`la` → `lui + ori`), the children should inherit the pseudo's
source range — or should they get their own (narrower) range
within the parent's?  For Option A (implicit snapshot), the
children will snapshot at the moment of construction, which
points at the original source position — so they naturally
inherit the pseudo's column range.  That's probably correct: the
expansion children have no source text of their own.
