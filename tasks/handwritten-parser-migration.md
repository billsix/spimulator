# Task: replace flex+bison with a hand-written recursive-descent parser in C

## Goal

Replace `src/scanner.l` (flex) and `src/parser.y` (bison) with
a hand-written lexer and recursive-descent parser, both in
plain C, integrated into the existing meson build without
the `flex` and `bison` generators.  Same input syntax accepted,
same semantic effects on the simulator's data structures.

The pure-C runtime in `src/inst.c`, `src/data.c`,
`src/sym-tbl.c`, etc. stays unchanged.  Only the front-end
(scanner + parser) changes.

## Why hand-written, not ANTLR

This task was originally written as an ANTLR migration.  After
the scope-and-risk audit (2026-05-19) and a comprehension-cost
discussion with Bill, the decision was to switch to a
hand-written recursive-descent parser.  Key reasons:

1. **Comprehensibility for a solo maintainer.**  flex+bison
   is opaque to read.  ANTLR moves the opaqueness into a
   different DSL plus a runtime, but doesn't eliminate it.
   A hand-written parser is plain C — every line does
   exactly what it looks like it does.
2. **No new build dependency.**  The simulator stays pure C.
   No JVM, no jar at build time, no C++ runtime at link time.
3. **Incremental migration is possible.**  The bison path can
   stay alive in the binary until the hand-written path
   covers everything, switched by a `-parser=…` flag.  ANTLR
   requires the grammar to be complete before it can run.
4. **In spim's pedagogical spirit.**  spim teaches the low
   level.  A hand-written parser is the parser equivalent of
   writing assembly by hand — readable as part of the
   curriculum if a student is curious.

ANTLR's genuine advantages (grammar-visualization IDE
plugins, sync-set error recovery out of the box,
industry-standard tooling) don't help a solo maintainer who
wants to understand his own parser.

## Phase 0 — inventory (the only thing to start now)

Produce `tasks/scanner-parser-inventory.md` cataloging every
rule and semantic action in the current flex+bison front-end.
This document is the gate for everything that follows.

### Inventory data already collected

Initial numbers from a Phase-0 audit on 2026-05-19:

| Surface | Size |
|---|---|
| `src/scanner.l` | 742 lines |
| `src/parser.y` | 2987 lines |
| `include/op.h` (X-macro keyword list) | 541 lines, 381 `OP()` entries |
| Total front-end | 4270 lines |
| `tests/tt.*.s` regression corpus | ~11,600 lines |
| `parser.y` `%token` directives | 390 |
| Grammar productions in `parser.y` | 106 |
| `ASM_CODE` alternatives | 119 (lines 592-1642) |
| `ASM_DIRECTIVE` alternatives | 46 |
| Silent shift-reduce conflicts (`%expect 25`) | 25 |
| External C functions called from semantic actions | 35 |
| Helper functions in `parser.y` postlude | 22 |
| Lexer start-conditions (`%x`/`%s`) | 0 |

### From `src/scanner.l`

Single-state scanner (no `%x`/`%s`).  ~15 distinct lex rules:
whitespace, newline, comment, signed/unsigned/hex int, FP
literal, identifier+keyword, `$reg`, punctuation, comma,
quoted string, char literal, fall-through error.

Notable details:

- **Custom `yywrap`** (line 386) inserts a `\001` marker
  before hard EOF so `Y_EOF` is returned as a non-zero
  token (bison reserves 0 for EOF).  The hand-written
  scanner can use a real EOF sentinel without the hack.
- **`erroneous_line()`** (line 554) reconstructs the source
  line with a `^` caret for error messages.  Must be
  preserved — the curriculum's exit-code path
  (`parse_errors_seen > 0 → exit 2`) depends on this message
  reaching stderr.
- **`scan_escape()`** and **`copy_str()`** decode `\NNN`,
  `\n`, `\t`, `\x..` in char and string literals.  The
  `copy_str` octal parser had a recently-fixed bug
  (`\134 → \\`; fix in scanner.l:493, regression test
  `tests/tt.octal_escape.s`).  Reimplement carefully —
  match the test.
- **`check_keyword()`** consults `keyword_tbl[]` built by
  including `op.h` with `OP()` redefined.  381 keywords.
- **`register_name_to_number()`** maps `at`/`v0`/`a0`/.../`zero`
  to register numbers (lines 629-665).  Stays as-is.

### From `src/parser.y`

Grammar (lines 533-2684):

- `LINE` is the top production; one line per parse cycle.
- `LBL_CMD` allows an optional `LABEL:` prefix.
- `ASM_CODE` covers every instruction form (119 alternatives).
  Grouped into `LOAD_OPS`, `STORE_OPS`, `BINARY_OPS`, etc.
  meta-productions per operand-shape family.
- `ASM_DIRECTIVE` covers every `.foo` directive (46
  alternatives).
- `EXPR`/`TRM`/`FACTOR`/`ADDRESS`/etc. cover operand
  expressions, addressing modes, immediate ranges.

C postlude (lines 2685-2987, 22 helper functions):

- **Pseudo-op expansion engine**: `div_inst`, `mult_inst`,
  `set_le_inst`, `set_ge_inst`, `set_eq_inst`, etc.  Each
  expands one source-level pseudo-op into a multi-instruction
  R-type/I-type sequence.  ~600 lines of plain C; **moves
  unchanged to `src/pseudo_op.c`** during migration.
- `cons_label`, `clear_labels`, `record_label`, `flush_local_labels`
  — label-table mutation, ordering matters.
- `yyerror`, `yywarn` — emit error messages via spim's
  `error()` function (which now goes to stderr).
- `check_imm_range`, `check_uimm_range`, `branch_offset` —
  utility predicates.

### External call surface

The grammar's `{ }` action blocks call into **35 distinct
spim C functions**.  Top callers by frequency:

| Function | Calls |
|---|---|
| `imm_expr` constructors (`const_imm_expr`, `make_imm_expr`, ...) | 101 |
| `addr_expr` constructors (`make_addr_expr`, ...) | 70 |
| `r_type_inst` | 67 |
| `i_type_inst` | 38 |
| `r_co_type_inst` | 15 |
| `r_sh_type_inst` | 14 |
| `nop_inst` | 10 |
| `user_kernel_data_segment` | 8 |
| (~25 others) | misc |

These functions stay unchanged.  The hand-written parser
calls exactly the same functions with the same arguments.

### Shared global state

| Symbol | Owner | Used by |
|---|---|---|
| `yylval`, `yytext` | scanner | parser |
| `line_no`, `current_line`, `line_returned`, `eof_returned` | scanner | error messages |
| `parse_error_occurred` | parser | label-flush in scanner |
| `parse_errors_seen` | parser | main()'s exit code |
| `data_dir`, `text_dir` | parser | scanner's `OPT_LBL` PC selection |
| `only_id` | parser | scanner's identifier-vs-label disambiguation |
| `null_term`, `store_op`, `store_fp_op` | parser | data-emission helpers |

Each becomes a normal C-file static or extern — straightforward.

## Phase 1 — design

Sketch the hand-written architecture before writing real
code.  Deliverables:

### 1. Module shape

```
src/scanner.c          /* hand-written lexer, replaces scanner.l */
src/parser.c           /* recursive-descent parser, replaces parser.y */
src/pseudo_op.c        /* relocated from parser.y postlude */
include/scanner.h      /* token enum, scanner API */
include/parser.h       /* parser entry points (mostly unchanged) */
```

### 2. Token definitions

The 390 `Y_*` tokens currently live in `parser.y` as `%token`
directives, generating an enum in `parser_yacc.h`.  Move that
enum to `include/scanner.h` (or a new `include/token.h`).
The X-macro in `op.h` continues to populate the keyword
table; no change to op.h.

### 3. Recursive-descent shape

One function per grammar production.  Naming convention:
`parse_<production>()`.  Examples:

```c
static int parse_expr(void);
static int parse_term(void);
static int parse_factor(void);
static addr_expr* parse_address(void);
static void parse_asm_code(void);
static void parse_asm_directive(void);
static void parse_load_op(int opcode);
static void parse_store_op(int opcode);
static void parse_binary_arith_op(int opcode);
/* etc. */
```

### 4. Top-level loop

```c
int parse_file(FILE* in) {
    scanner_init(in);
    while (peek() != TOK_EOF) {
        parse_error_occurred = false;
        scanner_start_line();
        parse_line();
    }
    return parse_errors_seen;
}
```

### 5. Lookahead / error recovery strategy

- **Lookahead**: a single 1-token peek window.  `peek()` returns
  current token without consuming; `advance()` consumes it.
  Bison's 25 shift-reduce conflicts disappear because we'll
  hand-resolve each with explicit lookahead checks.
- **Backtracking**: avoid.  Document any place where it's
  needed.  In practice, the grammar is LL(1)-clean after
  refactoring the conflicting productions.
- **Error recovery**: on syntax error, set
  `parse_error_occurred = true`, increment `parse_errors_seen`,
  call the existing `error()` helper (which now correctly
  goes to stderr), and skip tokens to the next newline.  This
  matches bison's effective behavior with the existing
  `%error` directive convention.

### 6. Pseudo-op expansion

The 22 postlude functions move to `src/pseudo_op.c` with no
behavior change.  The parser calls them at exactly the same
points bison did.

### 7. Build integration

Drop `flex` and `bison` from `meson.build`.  Drop the two
`custom_target` rules.  Add `src/scanner.c`, `src/parser.c`,
`src/pseudo_op.c` to the source-files list.

Phase 1 produces a written-down design that says, for each
of the 106 grammar productions, what hand-written function
will implement it.

## Phase 2 — pilot

Implement the new front-end for a **subset** of the grammar:

- `.data` / `.text` / `.globl` / `.word` / `.asciiz` / `.byte`
  directives.
- Integer R-type and I-type instructions (`add`, `addi`,
  `lw`, `sw`, `beq`, `bne`, `j`, `jal`, `syscall`).
- Labels and forward references.

Build both parsers into the binary.  Add a CLI flag
`-parser=bison` (default) / `-parser=hand`.  Run **the same
input file** through both parsers; dump the resulting text/
data segments; diff byte-for-byte.

Phase 2 gate: **parity-comparison harness must show zero
diff** on `tests/tt.core.s` for the covered instruction
subset before continuing.  Without the harness this whole
project is a foot-gun.

## Phase 3 — full coverage

Expand the hand-written parser to cover every production
currently in `parser.y`:

- All pseudo-ops (`la`, `li` with large constants, `div`,
  `mult`, `sle`, `sgt`, etc.) — by calling the relocated
  `pseudo_op.c` helpers from the new parser.
- FP family (`add.s`, `c.lt.d`, `mfc1`, etc.) — ~70 opcodes.
- Coprocessor and TLB instructions.
- Every remaining `.foo` directive.
- Constant-expression evaluation in `EXPR`/`TRM`/`FACTOR`.

Run the **full** regression corpus through the parity harness
on every commit.  No production lands until its parity check
is green.

## Phase 4 — switch the default

Once the parity harness has been green across the full corpus
for some agreed-upon settling period (~2 weeks of CI runs?),
flip the default from `bison` to `hand`.  Bison stays in the
build behind the `-parser=bison` flag.  CI keeps running both
on every commit for one more cycle to catch any latent
regression.

## Phase 5 — drop bison

Remove `src/scanner.l`, `src/parser.y`, the `flex` and
`bison` `find_program` lines in `meson.build`, the two
`custom_target` rules, the `parser_yacc.h` and `lex.yy.c`
generated outputs from the build, the `-parser=` flag.  Add
a ChangeLog entry.

## Risks

The risk vector is the same as the ANTLR option — divergent
parser output.  The hand-written approach has the same
exposures, but two of them shrink:

1. **Pseudo-op expansion semantics.**  Same risk.  Mitigated
   by moving the 22 helper functions to `pseudo_op.c`
   unchanged: same C source, same behavior, just called from
   a different parser.
2. **25 silently-resolved shift-reduce conflicts.**  Smaller
   risk than ANTLR.  In recursive descent each conflict
   becomes an explicit lookahead branch the author writes by
   hand — visible, reviewable, testable.  ANTLR's LL(*) is a
   black box; hand-written is not.
3. **Error message format.**  Now load-bearing
   (`parse_errors_seen > 0 → exit 2` since 2026-05-19).
   Mitigated by reusing the existing `error()` /
   `erroneous_line()` helpers — same call sites, same output
   shape.
4. **Octal escape parsing.**  Same risk.  Reimplement
   `scan_escape` and `copy_str` in `scanner.c` against
   `tests/tt.octal_escape.s` as a regression check.
5. **Label resolution timing.**  Same risk.  Forward
   references resolved in a second pass via
   `flush_local_labels`.  The hand-written parser calls
   `flush_local_labels` at the same points (line-end on
   success, on error).
6. **The `\001` EOF marker.**  Disappears.  Hand-written
   scanner returns `TOK_EOF` directly.  No equivalent hack
   needed.

### Smaller risk than ANTLR

- **No grammar-generator-output divergence.**  All output is
  hand-written; there's no "generator might change between
  versions" worry.
- **Incremental migration is possible.**  The bison path stays
  alive through Phase 4.  Any regression caught after the
  default switch flips can be worked around by setting
  `-parser=bison` while the bug is found.
- **No build-environment dependency change.**  Spim builds on
  any host that has a C compiler.

### Larger risk than ANTLR

- **Operator-precedence handling.**  Bison's `%left`/`%right`/
  `%nonassoc` directives don't appear in the current
  `parser.y` (spim's grammar happens to not need explicit
  precedence — the precedence is encoded structurally in
  `EXPR`→`TRM`→`FACTOR`).  Hand-written follows the same
  structural pattern; same precedence.  Worth verifying in
  Phase 0 inventory but not expected to bite.
- **More lines of source.**  Hand-written is longer than the
  equivalent `.g4` would be.  Estimate: 3000-4000 lines of
  hand-written C vs ~2000 lines of `.g4` grammar.  Same
  order; trade-off is that every line is plain C.

## Mitigations (non-optional)

- **Parity-comparison harness.**  CI step that builds both
  parsers, runs every `tests/tt.*.s` and every
  `/examples/src/*/*.asm` through both, byte-diffs the
  assembled text/data segments.  Phase 2 cannot proceed
  without this.
- **`-parser=` flag.**  Both parsers in the binary, switchable
  at runtime.  Lets the user (Bill, future sessions, CI)
  pick if they want to verify a fix on the old parser before
  trusting the new one.
- **Targeted pseudo-op tests.**  New `tests/tt.pseudo_ops.s`
  that exercises every pseudo-op expansion individually,
  with expected text-segment contents documented as a golden
  file.  Catch expansion-sequence regressions immediately.
- **Settling period before dropping bison.**  Phase 4 ships
  the hand-written parser as default but keeps bison in the
  build for ~2 weeks of CI runs.  No data dropped that can't
  be recovered.

## Effort estimate

| Phase | Description | Effort |
|---|---|---|
| 0 | Inventory document (start here, pre-authorized) | ~1 week |
| 1 | Design sketch + module shape + token enum migration | ~3-5 days |
| 2 | Pilot covering R/I-type + simple directives + parity harness | ~1 week |
| 3 | Full coverage including pseudo-op expansion and FP family | ~4-6 weeks |
| 4 | Flip default; bison stays as fallback for ~2-week settling | ~3 days work + ~2 weeks wall clock |
| 5 | Drop bison + flex; cleanup; ChangeLog | ~2-3 days |
| **Total active work** | | **~6-9 weeks** |

Compares favorably to the ANTLR estimate (~8-12 weeks), and
critically: **at every phase boundary the binary still works**,
because the bison path stays alive through Phase 4.

The project can be killed at any boundary once cost/benefit
becomes clear:

- After Phase 0: kill if the inventory reveals more grammar
  weirdness than expected (lots of conflicts that resolve
  surprisingly, semantic actions with hidden state).
- After Phase 1: kill if the design sketch comes out
  significantly longer than the bison grammar — at that point
  the readability win is suspect.
- After Phase 2: kill if the parity harness shows divergence
  the design can't resolve cleanly.
- After Phase 3: kill if the test corpus reveals
  pseudo-op-expansion edge cases that the relocated
  `pseudo_op.c` can't handle without significant rework.

## Recommendation

Start Phase 0 (inventory, ~1 week, pre-authorized).  The
inventory turns the rest of the plan from estimates into
concrete numbers.

Then decide at the Phase 1 gate.  If Phase 1's design sketch
holds the 6-9 week total, proceed to Phase 2 with the parity
harness as the load-bearing tool.  If not, kill cleanly —
bison's still there.

## First concrete deliverable

Start `tasks/scanner-parser-inventory.md`.  Read every line
of `src/scanner.l` and `src/parser.y` and document what's
there.  Don't write any new parser code yet.  The inventory
document is the gate that lets the rest of the project be
evaluated honestly.

The data already gathered (line counts, conflict count,
external-function frequencies, postlude function list) seeds
that document.  The remaining work for Phase 0 is the
per-production walk-through: what each of the 106 productions
matches, what its semantic action does, which C functions it
calls.

## Out of scope

- Changes to the C runtime in `src/inst.c`, `src/data.c`,
  `src/sym-tbl.c`, etc.  Front-end only.
- The 22 pseudo-op expansion helpers' internal logic.  They
  relocate from `parser.y` postlude to `src/pseudo_op.c`
  unchanged.
- Performance work.  Bison and hand-written are both fast
  enough for ~1000-line asm files; no measurable difference.
- Grammar additions.  Migration preserves accepted syntax
  exactly.  Any new syntax happens after the migration lands.
