# Hand-written parser — design

Phase 1 deliverable for [`handwritten-parser-migration.md`](handwritten-parser-migration.md).
Companion to [`scanner-parser-inventory.md`](scanner-parser-inventory.md),
which catalogs what the existing flex+bison front-end does;
this document specifies what the replacement looks like.

Authored 2026-05-19.

## What this document is

A concrete design — module shape, header file contents,
public APIs, per-production function map, error-recovery
model, lookahead model, build integration plan.  No
implementation yet.  Phase 2 (pilot) starts by committing
the headers below as actual `.h` files and writing the
first ~10 production functions plus the parity harness.

## What this document isn't

- Actual `.c` implementations.  Phase 2.
- The pseudo-op expansion bytecodes.  Those move from
  `parser.y`'s postlude unchanged; see Part 4 of the
  inventory.
- A redesign of the spim runtime.  The 35 external C
  functions (`r_type_inst`, `i_type_inst`, …) stay
  untouched.

## Module shape

Files added in Phase 2 (do not yet exist):

```
src/scanner.c          /* hand-written lexer, replaces scanner.l */
src/parser.c           /* recursive-descent parser, replaces parser.y */
src/pseudo_op.c        /* relocated from parser.y postlude */
include/pseudo_op.h    /* exports of pseudo-op helpers (most stay static) */
include/token.h        /* token enum + YYSTYPE (split from parser_yacc.h) */
```

Files modified in Phase 2:

```
include/scanner.h      /* expanded API: peek, advance, expect, sync */
include/parser.h       /* parse_file entry point */
meson.build            /* drop flex/bison custom_targets; add new .c files */
```

Files removed in Phase 5 (after the bison path is dropped):

```
src/scanner.l
src/parser.y
builddir/lex.yy.c       /* generated, disappears with the rule */
builddir/parser_yacc.c  /* same */
builddir/parser_yacc.h  /* contents move to include/token.h */
```

## Public APIs

### `include/token.h` — new

The 390 `Y_*` tokens currently live in bison-generated
`parser_yacc.h`.  Move them to a hand-maintained header so
the hand-written scanner and parser can include them without
running bison.

```c
#ifndef TOKEN_H
#define TOKEN_H

/* Token codes.  Values in 0..255 reserved for ASCII
   passthrough (the scanner emits `+`, `-`, `*`, `:`, etc.
   as their own char code).  Y_* tokens start at 256. */

enum {
  TOK_EOF = 0,           /* clean EOF, no \001 sentinel */

  /* ASCII punctuation tokens are returned as their char
     code in the range 1..255. */

  Y_NL = 256,
  Y_INT,
  Y_ID,
  Y_REG,
  Y_FP_REG,
  Y_STR,
  Y_FP,

  /* Opcode tokens — generated from op.h via X-macro.
     Order matches op.h's OP() entries.  381 entries. */
#define OP(NAME, OPCODE, TYPE, R_OPCODE) OPCODE,
#include "op.h"
#undef OP
};

/* YYSTYPE.  Kept as an explicit { int i; void *p; } union for
   parity with the existing bison-generated definition.  All
   semantic values fit one or the other (operand-side data via
   .p, integer values + opcode numbers via .i). */

typedef union {
  int i;
  void* p;
} yylval_t;

#endif
```

**Migration step:** the existing `include/parser.h` and
`scanner.h` already reference `YYSTYPE`/`yylval_t` via
`intptr_union` (see `spim.h:57-60`).  Keep that
typedef — `intptr_union` IS the YYSTYPE.

### `include/scanner.h` — expanded

```c
#ifndef SCANNER_H
#define SCANNER_H

#include <stdio.h>
#include "spim.h"
#include "token.h"

/* Scanner lifecycle ------------------------------------- */

void scanner_init(FILE* in);          /* replaces initialize_scanner */
void scanner_push_input(FILE* in);    /* replaces push_scanner */
void scanner_pop_input(void);         /* replaces pop_scanner */
void scanner_start_line(void);        /* unchanged */

/* Token stream interface --------------------------------- */

/* Return the next token, advancing the input position.
   Sets yylval as a side effect.  Returns TOK_EOF at end of
   file (no \001 sentinel hack). */
int scanner_next(void);

/* Look at the current and next tokens WITHOUT consuming.
   peek() returns the token that scanner_next() will return.
   peek2() returns the one after.  yylval is NOT set by peek;
   it's only set when the token is actually consumed via
   scanner_next() or advance(). */
int scanner_peek(void);
int scanner_peek2(void);

/* Consume the current token, returning it.  Same as
   scanner_next() but reads more naturally in parser code. */
int scanner_advance(void);

/* Consume the current token if it matches `expected`;
   otherwise emit an error and DO NOT consume.  Returns true
   on match. */
bool scanner_expect(int expected, const char* what);

/* Set the next-identifier-must-not-be-a-keyword flag.
   Replaces the global `only_id` toggle.  Cleared
   automatically after the next token is read. */
void scanner_force_identifier(void);

/* Source-text helpers (used by error printer and REPL) */

char* erroneous_line(void);            /* unchanged */
char* source_line(void);               /* unchanged */
int   register_name_to_number(char*);  /* unchanged */

/* Shared globals (read-only from parser side) */

extern int line_no;
extern yylval_t yylval;

#endif
```

**Notes on the design:**

- `scanner_next` / `scanner_peek` replace flex's
  `yylex`.  Same byte-level semantics; different name to
  signal the change.
- One-token + two-token peek covers the inventory's
  identified lookahead needs.  Implemented as a small
  buffer (`tok current; tok next; tok next2;`) refilled
  on each `scanner_next`.
- `scanner_force_identifier` replaces the `only_id`
  global.  Cleaner: it's a flag attached to the next
  `scanner_next` call rather than a free-floating global
  toggled by parser actions.  The flag self-clears after
  one token consumption.
- `scanner_expect` is the common "if current matches X,
  consume; else error" pattern.  Helper to keep parser
  functions terse.

### `include/parser.h` — expanded

Existing `parser.h` keeps its declarations
(`parse_error_occurred`, `parse_errors_seen`, `data_dir`,
`text_dir`).  Add:

```c
/* Parse the entire file currently bound to the scanner's
   FILE*.  Returns the number of parse errors encountered.
   On success, returns 0.  Drives the scanner until TOK_EOF.

   In Phase 2-4, this is what the new path calls; the old
   path still goes through bison's yyparse(). */
int parse_file(void);

/* Initialize parser state.  Resets data_dir/text_dir,
   parse_errors_seen, this_line_labels.  Called by
   read_assembly_file before parse_file(). */
void initialize_parser(char* file_name);
```

### `include/pseudo_op.h` — new

Most of the 22 postlude helpers are internal to the parser
and don't need to be public.  Only those called from
multiple TUs (today: none, because they're all called from
inside parser.y) get exported:

```c
/* Pseudo-op expansion helpers.  Each emits a sequence of
   real R-type / I-type instructions that implements the
   user-visible pseudo-op.  Called from parser.c.

   The function signatures intentionally match what
   parser.y's postlude defines today.  Migration is
   relocate-and-link. */

void div_inst(int op, int rd, int rs, int rt, int const_divisor);
void mult_inst(int op, int rd, int rs, int rt);
void set_le_inst(int op, int rd, int rs, int rt);
void set_gt_inst(int op, int rd, int rs, int rt);
void set_ge_inst(int op, int rd, int rs, int rt);
void set_eq_inst(int op, int rd, int rs, int rt);

/* Common helpers used by parser body too. */

void nop_inst(void);
void trap_inst(void);
imm_expr* branch_offset(int n_inst);

/* Range checkers. */

void check_imm_range(imm_expr* expr, int32 min, int32 max);
void check_uimm_range(imm_expr* expr, uint32 min, uint32 max);

/* Opcode lookups (op ↔ immediate form). */

int op_to_imm_op(int opcode);
int imm_op_to_op(int opcode);
```

The other postlude functions (`fix_current_label_address`,
`cons_label`, `clear_labels`, `store_word_data`,
`mips32_r2_inst`, `cc_to_rt`, `yyerror`, `yywarn`) stay
static inside `parser.c`.

## Token lifecycle

```
                                       +---------------+
   FILE* in --> scanner_init --------->| scanner state |
                                       |  buf[3]       |
   parse_file() -+                     |  flags        |
                 |                     |  line_no      |
                 v                     +------+--------+
            scanner_peek() <----+             |
            scanner_advance() --+             |
                                              v
                                       yylval (extern)
```

`scanner_peek()` returns the lookahead without advancing.
`scanner_advance()` advances and sets `yylval`.  Internal
state is a 3-slot buffer of recently-read tokens — current,
next, next2 — refilled lazily.

**No re-entrancy.**  Same as today.

**No buffer state stack.**  `scanner_push_input` /
`scanner_pop_input` from the old API are kept (REPL uses
them for nested file reads) but they save/restore the FILE*,
not the lookahead buffer.

## Recursive-descent parser shape

### Naming convention

| Production family | Function name |
|---|---|
| `LINE` | `parse_line()` |
| `LBL_CMD`, `CMD`, `OPT_LBL` | inlined into `parse_line` (small) |
| `ASM_CODE` (dispatch) | `parse_asm_code()` |
| `LOAD_OPS … ADDRESS` | `parse_load(int opcode)` |
| `STORE_OPS … ADDRESS` | `parse_store(int opcode)` |
| `BINARYI_OPS … …` | `parse_binary_i(int opcode)` |
| `BINARY_ARITHI_OPS … …` | `parse_binary_arithi(int opcode)` |
| `SHIFT_OPS … …` | `parse_shift(int opcode)` |
| `BINARY_BR_OPS … …` | `parse_branch_binary(int opcode)` |
| `UNARY_BR_OPS … …` | `parse_branch_unary(int opcode)` |
| `J_OPS LABEL` | `parse_jump(int opcode)` |
| `NULLARY_OPS` | `parse_nullary(int opcode)` |
| `MOVE_FROM_HILO_OPS` | `parse_move_from_hilo(int opcode)` |
| (more families …) | (corresponding `parse_*`) |
| `ASM_DIRECTIVE` (dispatch) | `parse_directive(int dir_token)` |
| `EXPR`/`TRM`/`FACTOR` | `parse_expr()`, `parse_term()`, `parse_factor()` |
| `ADDR`/`ADDRESS` | `parse_address(void)` returns `addr_expr*` |
| `IMM16`/`UIMM16`/`IMM32` | `parse_imm16(void)`/etc. returns `imm_expr*` |
| `ABS_ADDR` | `parse_abs_addr(void)` returns int |
| `STR_LST` (under `.ascii`) | `parse_string_list(void)` |
| `EXPR_LST` | `parse_expr_list(void)` |
| `FP_EXPR_LST` | `parse_fp_expr_list(void)` |

Roughly **50-60 distinct functions**, not 119 — because most
family productions just dispatch on opcode.

### Function signatures

Three return-type conventions:

1. **`void`** — for productions that perform side effects
   (emit an instruction, record a label) and don't need to
   return a value to the caller.  Example: `parse_load`.
2. **Pointer return** — for productions that build an
   AST-like value the caller consumes.  Example:
   `parse_imm32(void)` returns `imm_expr*`.  Caller owns the
   returned pointer and is responsible for freeing it via
   `free_imm_expr` (existing) when done.
3. **`int` return** — for productions that produce a small
   integer (register number, condition code, ABS_ADDR
   numeric value).  Caller doesn't free.

### Dispatch shape

`parse_asm_code` reads one token, looks at the keyword's
TYPE (from `op.h`), dispatches to the family parser:

```c
void parse_asm_code(void) {
    int op_tok = scanner_advance();   /* the opcode keyword */
    int op_type = op_type_of(op_tok); /* from op.h's TYPE field */

    switch (op_type) {
    case LOAD_TYPE:        parse_load(op_tok); break;
    case STORE_TYPE:       parse_store(op_tok); break;
    case BINARYI_TYPE:     parse_binary_i(op_tok); break;
    case SHIFT_TYPE:       parse_shift(op_tok); break;
    case BRANCH_BIN_TYPE:  parse_branch_binary(op_tok); break;
    /* … */
    default:
        yyerror("Unknown opcode");
        sync_to_newline();
    }
}
```

`op_type_of(int tok)` is a new helper backed by the same
keyword table the scanner consults.  Returns a small
classifier enum.  The existing `op.h` TYPE field
(`R3_TYPE_INST`, `I2_TYPE_INST`, etc.) is the source of
truth and stays unchanged — `op_type_of` just maps from
token-number to a family enum.

### Sample function bodies

These are sketches, not implementations.  They show the
shape Phase 2 produces.

```c
/* LOAD_OPS DEST ADDRESS */
static void parse_load(int opcode) {
    int dest = parse_register();
    addr_expr* addr = parse_address();
    i_type_inst(opcode_to_real(opcode), dest,
                addr_expr_reg(addr),
                addr_expr_imm(addr));
    free_addr_expr(addr);
}
```

```c
/* BINARY_BR_OPS SRC1 SRC2 LABEL */
static void parse_branch_binary(int opcode) {
    int rs = parse_register();
    int rt = parse_register();
    imm_expr* target = parse_br_imm32();
    i_type_inst_free(opcode, rs, rt, target);
}
```

```c
/* ADDR : '(' REGISTER ')'
        | ABS_ADDR
        | ABS_ADDR '(' REGISTER ')'
        | Y_ID
        | Y_ID '(' REGISTER ')'
        | Y_ID '+' ABS_ADDR
        | Y_ID '-' ABS_ADDR
        | Y_ID '+' ABS_ADDR '(' REGISTER ')'
        | Y_ID '-' ABS_ADDR '(' REGISTER ')' */
static addr_expr* parse_address(void) {
    scanner_force_identifier();           /* matches `only_id = 1` */

    if (scanner_peek() == '(') {
        /* ( REGISTER ) */
        scanner_advance();
        int reg = parse_register();
        scanner_expect(')', "address");
        return make_addr_expr(0, NULL, reg);
    }

    if (scanner_peek() == Y_INT) {
        /* ABS_ADDR [ '(' REGISTER ')' ] */
        int imm = parse_abs_addr();
        if (scanner_peek() == '(') {
            scanner_advance();
            int reg = parse_register();
            scanner_expect(')', "address");
            return make_addr_expr(imm, NULL, reg);
        }
        return make_addr_expr(imm, NULL, 0);
    }

    if (scanner_peek() == Y_ID) {
        /* Y_ID [ '+'|'-' ABS_ADDR ] [ '(' REGISTER ')' ] */
        scanner_advance();
        char* id = (char*)yylval.p;
        int offset = 0;
        if (scanner_peek() == '+' || scanner_peek() == '-') {
            int sign = (scanner_advance() == '+') ? 1 : -1;
            offset = sign * parse_abs_addr();
        }
        int reg = 0;
        if (scanner_peek() == '(') {
            scanner_advance();
            reg = parse_register();
            scanner_expect(')', "address");
        }
        addr_expr* r = make_addr_expr(offset, id, reg);
        free(id);
        return r;
    }

    yyerror("Expected address");
    return make_addr_expr(0, NULL, 0);    /* recovery: empty */
}
```

This one example demonstrates how the 25 shift-reduce
conflicts disappear.  The choice between `Y_ID '+' ABS_ADDR`
and `Y_ID` (followed by something else) is made by a single
`scanner_peek() == '+' || == '-'` check.  Bison sweats over
this; recursive descent doesn't.

## Top-level loop

```c
int parse_file(void) {
    while (scanner_peek() != TOK_EOF) {
        parse_error_occurred = false;
        scanner_start_line();
        parse_line();
    }
    clear_labels();
    return parse_errors_seen;
}

static void parse_line(void) {
    /* OPT_LBL CMD | CMD | TERM */

    /* Empty line? */
    if (scanner_peek() == Y_NL) { scanner_advance(); return; }

    /* Look ahead for the optional label.  Two cases:
       - `id ':'`  (a label definition)
       - `id '='`  (a constant-label definition)
       Both require Y_ID lookahead and a peek2 check. */

    if (scanner_peek() == Y_ID
        && (scanner_peek2() == ':' || scanner_peek2() == '=')) {
        parse_opt_label();
    }

    /* CMD : ASM_CODE | ASM_DIRECTIVE | (just NL/EOF) */
    int t = scanner_peek();
    if (t == Y_NL || t == TOK_EOF) {
        if (t == Y_NL) scanner_advance();
        return;
    }

    if (is_directive_token(t)) {
        parse_directive(scanner_advance());
    } else {
        parse_asm_code();
    }

    /* TERM */
    if (parse_error_occurred) {
        sync_to_newline();
    } else {
        if (scanner_peek() == Y_NL) scanner_advance();
        else if (scanner_peek() != TOK_EOF) {
            yyerror("Extra tokens after instruction");
            sync_to_newline();
        }
    }
    clear_labels();
}
```

The `if (is_directive_token(t))` test consults the keyword
table's TYPE field — directive tokens have TYPE == ASM_DIR
in `op.h`.

## Lookahead model

**One-token lookahead is enough almost everywhere.**
`scanner_peek()` returns the upcoming token; productions
branch on it.

**Two-token lookahead is needed in three places:**

1. **Label vs. instruction** (`parse_line` above): `Y_ID :`
   or `Y_ID =` means label-definition; otherwise the `Y_ID`
   belongs to the instruction body.  `peek + peek2`.
2. **`Y_ID '+' ABS_ADDR` vs. `Y_ID` alone in operand
   expressions** (the 25 shift-reduce conflicts): `peek2`
   checks for `+`/`-`.
3. **`Y_INT Y_INT` quirk** in `ABS_ADDR` (negative-integer
   tokenization): `peek + peek2` to detect.

**No general k-lookahead or backtracking is needed.**  The
scanner buffer holds three tokens (current, next, next2)
and that's it.

## Error recovery

### Strategy: skip-to-newline

On `yyerror`, set `parse_error_occurred = true`, increment
`parse_errors_seen`, and call `sync_to_newline()`:

```c
static void sync_to_newline(void) {
    while (scanner_peek() != Y_NL
           && scanner_peek() != TOK_EOF
           && scanner_peek() != ';') {
        scanner_advance();
    }
    if (scanner_peek() == Y_NL || scanner_peek() == ';') {
        scanner_advance();
    }
    clear_labels();
}
```

Matches bison's effective behavior: one syntax error per
line, skip the rest of the line, continue with the next.

### Error message format

Use the existing `yywarn`/`yyerror` functions (move to
`parser.c` from postlude).  Same format:

```
spim: (parser) <message> on line <N> of file <F>
<source line>
   <caret>
```

Maintained by `erroneous_line()` (stays in scanner.c).
The newly-load-bearing `parse_errors_seen → exit 2` path is
preserved.

### Recovery quality vs bison

Bison's behavior:

- Default: emit one error per syntax error, attempt to
  recover via `error` productions (spim has none), fall
  through to next valid token.
- spim's effective behavior: one error per line.

Hand-written matches the effective behavior more cleanly —
because the "one error per line" pattern is implemented
explicitly in `sync_to_newline`, no implicit recovery
weirdness.

## Pseudo-op expansion

The 22 helpers move to `src/pseudo_op.c` **verbatim** from
`parser.y`'s postlude.  Phase 2's pilot proves that the
emitted instruction sequences are byte-identical by running
the parity harness.

Helpers called from `parser.c`:

- `div_inst`, `mult_inst` — `DIV_POPS`/`MUL_POPS` families
- `set_le_inst`, `set_gt_inst`, `set_ge_inst`, `set_eq_inst`
  — `SET_LE_POPS`/`SET_GT_POPS`/`SET_GE_POPS`/`SET_EQ_POPS`
  families
- `nop_inst`, `trap_inst` — used by other expansions
- `branch_offset` — utility
- `op_to_imm_op`, `imm_op_to_op` — utility opcode lookups
- `check_imm_range`, `check_uimm_range` — `IMM16`/`UIMM16`
  range checks

Other postlude functions (`fix_current_label_address`,
`cons_label`, `clear_labels`, `store_word_data`,
`mips32_r2_inst`, `cc_to_rt`) stay as file-static helpers in
`parser.c`.

## Build integration

### Phase 2 (pilot)

`meson.build` adds the new sources alongside the existing
ones:

```meson
src/scanner.c
src/parser.c
src/pseudo_op.c
```

Keep the existing flex/bison rules.  Both paths compile
into the binary.  At call sites in `read_assembly_file`:

```c
if (use_handwritten_parser) {
    parse_file();
} else {
    while (!yyparse());
}
```

`use_handwritten_parser` is set by a new CLI flag:

```
spim -parser=hand    # use the new path
spim -parser=bison   # use the old path (default during Phase 2-3)
```

### Phase 4 (flip default)

Change the default from `bison` to `hand` in `init_options`
(`spim.c:357` area).  Keep the flag.

### Phase 5 (drop bison)

Remove the `flex`/`bison` `find_program` calls; remove the
two `custom_target` rules; remove the `-parser=` flag;
remove the bison branch in `read_assembly_file`.

## Per-production function map

The full 1-to-1 mapping of grammar productions to
hand-written functions.  Read alongside Part 3 of the
inventory.

### Top-level

| Production | Function |
|---|---|
| `LINE` | inlined into `parse_file` |
| `LBL_CMD` | inlined into `parse_line` |
| `OPT_LBL` | `parse_opt_label` |
| `CMD` | inlined into `parse_line` |
| `TERM` | inlined into `parse_line` (NL/EOF check) |
| `ASM_CODE` | `parse_asm_code` (dispatcher) |
| `ASM_DIRECTIVE` | `parse_directive` (dispatcher) |

### Operand families (each one function)

| Production | Function | Returns |
|---|---|---|
| `ADDRESS` / `ADDR` | `parse_address` | `addr_expr*` |
| `BR_IMM32` | `parse_br_imm32` | `imm_expr*` |
| `IMM16` | `parse_imm16` | `imm_expr*` |
| `UIMM16` | `parse_uimm16` | `imm_expr*` |
| `IMM32` | `parse_imm32` | `imm_expr*` |
| `ABS_ADDR` | `parse_abs_addr` | `int` |
| `SRC1`/`SRC2`/`DEST`/`REG`/`REGISTER` | `parse_register` | `int` |
| `F_DEST`/`F_SRC1`/`F_SRC2`/`FP_REGISTER` | `parse_fp_register` | `int` |
| `CC_REG` | `parse_cc_reg` | `int` |
| `COP_REG` | `parse_cop_reg` | `int` |
| `LABEL` | `parse_label` | `char*` (caller frees) |
| `STR_LST` | `parse_string_list` | `void` (emits) |
| `STR` | `parse_string` | `void` (emits bytes) |
| `EXPRESSION` / `EXPR` | `parse_expr` | `int` |
| `TRM` | `parse_term` | `int` |
| `FACTOR` | `parse_factor` | `int` |
| `EXPR_LST` | `parse_expr_list` | `void` |
| `FP_EXPR_LST` | `parse_fp_expr_list` | `void` |
| `OPTIONAL_ID` / `OPT_ID` / `ID` | `parse_id` | `char*` |

### Instruction families (each one function, opcode passed in)

| Family | Function | Operand pattern |
|---|---|---|
| `LOAD_OPS` | `parse_load` | DEST ADDRESS |
| `LOADI_OPS` (lui) | `parse_lui` | DEST UIMM16 |
| `LOADC_OPS` | `parse_loadc` | COP_REG ADDRESS |
| `LOADFP_OPS` | `parse_loadfp` | F_SRC1 ADDRESS |
| `LOADFP_INDEX_OPS` | `parse_loadfp_index` | F_DEST SRC1 SRC2 |
| `Y_LA_POP` | `parse_la_pseudo` | DEST ADDRESS |
| `Y_LI_POP` | `parse_li_pseudo` | DEST IMM32 |
| `Y_L_D_POP` / `Y_L_S_POP` | `parse_load_fp_pseudo` | F_DEST ADDRESS |
| `ULOADH_POPS` / `Y_ULW_POP` | `parse_uload_pseudo` | DEST ADDRESS |
| `STORE_OPS` | `parse_store` | SRC1 ADDRESS |
| `STOREC_OPS` | `parse_storec` | COP_REG ADDRESS |
| `STOREFP_OPS` | `parse_storefp` | F_SRC1 ADDRESS |
| `STOREFP_INDEX_OPS` | `parse_storefp_index` | F_SRC1 SRC1 SRC2 |
| `BINARY_OPS` | `parse_binary` | DEST SRC1 SRC2 |
| `BINARY_OPS_REV2` | `parse_binary_rev2` | DEST SRC1 SRC2 |
| `BINARYI_OPS` | `parse_binary_i` | DEST SRC1 (SRC2\|IMM) |
| `BINARYIR_OPS` | `parse_binaryir` | DEST SRC2 SRC1 |
| `BINARY_ARITHI_OPS` | `parse_binary_arithi` | DEST SRC1 IMM16 |
| `BINARY_LOGICALI_OPS` | `parse_binary_logicali` | DEST SRC1 UIMM16 |
| `SHIFT_OPS` | `parse_shift` | DEST SRC1 INT |
| `SHIFT_OPS_REV2` | `parse_shift_rev2` | DEST SRC1 INT |
| `SHIFTV_OPS_REV2` | `parse_shiftv_rev2` | DEST SRC1 SRC2 |
| `SUB_OPS` | `parse_sub` | DEST SRC1 SRC2 (incl. immediate via negation) |
| `DIV_POPS` | `parse_div_pseudo` | DEST SRC1 (SRC2\|IMM) → `div_inst` |
| `MUL_POPS` | `parse_mul_pseudo` | DEST SRC1 (SRC2\|IMM) → `mult_inst` |
| `MULT_OPS` | `parse_mult` | SRC1 SRC2 |
| `MULT_OPS3` | `parse_mult3` | DEST SRC1 SRC2 |
| `SET_LE_POPS` etc. | `parse_set_cmp_pseudo` | DEST SRC1 (SRC2\|IMM) |
| `BR_COP_OPS` | `parse_branch_coproc` | LABEL or CC_REG LABEL |
| `UNARY_BR_OPS` | `parse_branch_unary` | SRC1 LABEL |
| `UNARY_BR_POPS` (beqz) | `parse_branch_unary_pseudo` | SRC1 LABEL |
| `BINARY_BR_OPS` | `parse_branch_binary` | SRC1 SRC2 LABEL |
| `BR_GT_POPS` / `BR_GE_POPS` / `BR_LT_POPS` / `BR_LE_POPS` | `parse_branch_cmp_pseudo` | SRC1 (SRC2\|IMM) LABEL |
| `J_OPS` | `parse_jump` | LABEL or SRC1 |
| `B_OPS` (unconditional b) | `parse_b_pseudo` | LABEL |
| `BINARYI_TRAP_OPS` / `BINARY_TRAP_OPS` | `parse_trap` | SRC1 (SRC2\|IMM) |
| `MOVE_FROM_HILO_OPS` | `parse_move_from_hilo` | DEST |
| `MOVE_TO_HILO_OPS` | `parse_move_to_hilo` | SRC1 |
| `MOVEC_OPS` | `parse_movec` | DEST SRC1 SRC2 |
| `MOVE_COP_OPS` / `MOVE_COP_OPS_REV2` | `parse_move_cop` | DEST COP_REG |
| `CTL_COP_OPS` | `parse_ctl_cop` | DEST COP_REG |
| `FP_MOVE_OPS` / `FP_MOVE_OPS_REV2` | `parse_fp_move` | F_DEST F_SRC1 |
| `MOVECC_OPS` / `FP_MOVEC_OPS` / `FP_MOVECC_OPS` (+REV2 variants) | `parse_movecc` | varies |
| `FP_UNARY_OPS` / `_REV2` | `parse_fp_unary` | F_DEST F_SRC1 |
| `FP_BINARY_OPS` / `_REV2` | `parse_fp_binary` | F_DEST F_SRC1 F_SRC2 |
| `FP_TERNARY_OPS_REV2` | `parse_fp_ternary` | F_DEST F_SRC1 F_SRC2 F_SRC2 |
| `FP_CMP_OPS` / `_REV2` | `parse_fp_cmp` | F_SRC1 F_SRC2 |
| `BF_OPS_REV2` (ext/ins) | `parse_bitfield` | DEST SRC1 INT INT |
| `COUNT_LEADING_OPS` | `parse_count_leading` | DEST SRC1 |
| `NULLARY_OPS` / `_REV2` | `parse_nullary` | (no operands) |
| `UNARY_OPS_REV2` (di/ei) | `parse_unary_rev2` | DEST |
| `SYS_OPS` | `parse_sys` | (no operands) |
| `PREFETCH_OPS` | `parse_prefetch` | SRC1 SRC2 SRC2 |
| `CACHE_OPS` | `parse_cache` | UIMM16 ADDRESS |
| `TLB_OPS` | `parse_tlb` | (no operands) |

That's **~55 function names** for the instruction parsers,
plus the ~20 operand parsers above, plus `parse_directive`
which itself handles 47 alternatives via a switch on the
directive token.

### Directive dispatch shape

```c
static void parse_directive(int dir_tok) {
    switch (dir_tok) {
    case Y_DATA_DIR:     parse_dir_data(false); break;
    case Y_K_DATA_DIR:   parse_dir_data(true); break;
    case Y_TEXT_DIR:     parse_dir_text(false); break;
    case Y_K_TEXT_DIR:   parse_dir_text(true); break;
    case Y_WORD_DIR:     store_op = store_word;   parse_expr_list(); break;
    case Y_BYTE_DIR:     store_op = store_byte;   parse_expr_list(); break;
    case Y_HALF_DIR:     store_op = store_half;   parse_expr_list(); break;
    case Y_ASCII_DIR:    null_term = false;       parse_string_list(); break;
    case Y_ASCIIZ_DIR:   null_term = true;        parse_string_list(); break;
    case Y_FLOAT_DIR:    store_fp_op = store_single; set_data_alignment(2); parse_fp_expr_list(); break;
    case Y_DOUBLE_DIR:   store_fp_op = store_double; set_data_alignment(3); parse_fp_expr_list(); break;
    case Y_SPACE_DIR:    parse_dir_space(); break;
    case Y_ALIGN_DIR:    parse_dir_align(); break;
    case Y_COMM_DIR:     parse_dir_comm(); break;
    case Y_GLOBAL_DIR:   parse_dir_globl(); break;
    case Y_EXTERN_DIR:   parse_dir_extern(); break;
    case Y_ERR_DIR:      yyerror(".err directive"); break;
    /* ... no-op directives (debug metadata) ... */
    case Y_FILE_DIR:
    case Y_LOC_DIR:
    case Y_FRAME_DIR:
    case Y_MASK_DIR:
    case Y_FMASK_DIR:
    case Y_ENT_DIR:
    case Y_END_DIR:
    case Y_LAB_DIR:
    case Y_LIVEREG_DIR:
    case Y_OPTION_DIR:
    case Y_BGNB_DIR:
    case Y_ENDB_DIR:
    case Y_ENDR_DIR:
    case Y_ASM0_DIR:
    case Y_ALIAS_DIR:
        consume_directive_args();   /* swallow rest of line */
        break;
    default:
        yyerror("Unknown directive");
        sync_to_newline();
    }
}
```

The `consume_directive_args` helper reads tokens until the
next NL/EOF without acting on them — handles all the
metadata-only directives uniformly.

## Phase 2 scope (the pilot)

The pilot proves the model.  Cover:

- `LINE`, `OPT_LBL`, label definitions
- `BINARYI_OPS` (`add`, `addu`)
- `BINARY_ARITHI_OPS` (`addi`, `addiu`)
- `BINARY_LOGICALI_OPS` (`andi`, `ori`, `xori`)
- `SHIFT_OPS` (`sll`, `sra`, `srl`)
- `LOAD_OPS` with simple `ADDR` (`lb`, `lbu`, `lh`, `lhu`,
  `lw`)
- `STORE_OPS` with simple `ADDR` (`sb`, `sh`, `sw`)
- `BINARY_BR_OPS` (`beq`, `bne`)
- `UNARY_BR_OPS` (`bgez`, `bltz`)
- `J_OPS` (`j`, `jal`, `jr`)
- `NULLARY_OPS` (`syscall`)
- `EXPR`/`TRM`/`FACTOR`
- `ADDR` (all 9 alternatives — needed for parity)
- `IMM16`/`UIMM16`/`IMM32`/`ABS_ADDR`
- `.data`, `.text`, `.globl`, `.word`, `.byte`, `.asciiz`,
  `.space`, `.align`

That subset assembles all of `examples/src/01-helloworld.asm`
and most of `02-print1through10.asm`.

Plus: the **parity-comparison harness** (Phase 2's gate).

### Parity harness

A small program that:

1. Takes one `.asm` file as input.
2. Calls `read_assembly_file` twice — once with the bison
   parser, once with the hand-written parser.
3. Dumps the resulting text and data segments after each
   call.
4. Byte-diffs the dumps.
5. Returns 0 on identical, 1 on diff.

Wrapped in a meson test target that runs the harness across
every `tests/tt.*.s` AND every `examples/src/*/*.asm` once
those files are within the pilot's covered subset.

## What Phase 2 does NOT cover

- FP family (`add.s`, `c.lt.d`, etc.) — Phase 3.
- Pseudo-op expansion (`la`, `li`, `div`, `mulo`, `sle`,
  etc.) — Phase 3.  This is the load-bearing risk area.
- Coprocessor instructions (`mfc1`, `mtc1`) — Phase 3.
- TLB instructions — Phase 3.
- The rest of the directives — Phase 3.
- The 25 shift-reduce conflict resolutions — Phase 2 covers
  enough of them via `parse_address` to validate the
  approach; full coverage is Phase 3.

## Estimated Phase 2 effort

| Sub-task | Days |
|---|---|
| Commit `include/token.h`, expand `scanner.h`/`parser.h` | 1 |
| Write scanner stub (returns TOK_EOF immediately) + harness | 1 |
| Implement scanner.c fully (port from scanner.l, drop \001 hack) | 1 |
| Implement parser.c subset above | 2 |
| Implement parity harness + meson test target | 0.5 |
| Debug parity differences for the subset | 0.5 |
| **Phase 2 total** | **~6 days** |

After Phase 2: gate check.  If parity is green for the
covered subset, Phase 3 starts.  If parity is red and the
diffs are not explicable, project pauses for diagnosis.

## Estimated Phase 3 effort

The remaining ~75% of the grammar.  Risks concentrate here.

| Sub-task | Days |
|---|---|
| FP family productions (~30 functions) | 5 |
| Pseudo-op productions calling the postlude helpers | 4 |
| Coproc/TLB/cache productions | 2 |
| Remaining 35 directives (most are no-ops) | 1 |
| Parity coverage of all `tt.*.s` | 5 |
| Parity coverage of all `examples/src/*/*.asm` | 3 |
| **Phase 3 total** | **~20 days = ~4 weeks** |

## Open design questions

These are decisions Phase 2 will need to make concrete:

1. **Source location tracking.**  Bison's `@N` mechanism
   gives each token a `YYLTYPE` struct with line/column.
   spim uses only `line_no` today.  Preserve line-only? Or
   add column tracking now since hand-written makes it
   cheap?  Recommendation: keep line-only for parity, add
   column later if students complain about error message
   precision.
2. **Lookahead buffer size.**  Three slots (`current`,
   `next`, `next2`) for two-token peek.  Any production
   that needs three-token lookahead would force a larger
   buffer.  Inventory didn't find any.  Verify in Phase 2.
3. **Scanner's `only_id` flag self-clear semantics.**  Does
   it auto-clear after one token, or stay set until
   explicitly cleared?  Bison's version stays set until the
   parser clears it.  Recommendation: auto-clear after one
   token consumption — simpler invariant, matches all
   current call sites.
4. **`Y_INT Y_INT` negative-tokenization quirk.**  Preserve
   as-is in Phase 2/3 (call it `parse_abs_addr_pair`),
   defer cleanup to Phase 5 or later.  Touched on in the
   inventory's risk section.
5. **`\X` vs `\x` hex escape consistency.**  Char literals
   support both; string literals only `\X`.  Recommendation:
   make the hand-written `copy_str` equivalent accept both
   (matching `scan_escape`).  This is a strict superset of
   the old behavior so no parity diff.

## Validation plan

Before Phase 2 starts, confirm:

- [ ] `include/token.h` with the X-macro pattern compiles
      and produces the same enum values as
      `parser_yacc.h` (run a small test).
- [ ] `yylval_t` matches the existing `intptr_union` from
      `spim.h:57-60`.  No change needed there.
- [ ] The pseudo-op helpers in `parser.y`'s postlude
      compile cleanly when copy-pasted into a new
      `pseudo_op.c` (test by symlinking the postlude lines
      and adding `#include "..."` for the spim runtime).
- [ ] The 35 external functions called from parser actions
      all have public declarations in spim's headers
      (`inst.h`, `data.h`, `sym-tbl.h`).  Most do; verify
      no implicit-declaration is hiding.

## Migration safety net

Through Phase 4 (settling), the bison path stays live
behind `-parser=bison`.  If the hand-written path proves to
have a regression that wasn't caught by the parity harness:

1. Default flips back to `-parser=bison` (one-line change in
   `init_options`).
2. Bug filed against the hand-written path.
3. Fix + new regression test added to `tests/tt.*.s` or the
   parity harness's coverage.
4. After settling period, flip default back.

This safety net is the reason the recommendation is "start
Phase 2" rather than "decide now whether to migrate at
all" — the hand-written path can be developed, tested, and
deployed without committing to its correctness.  Bison is
the fallback.

## What this design intentionally doesn't include

- **A formal grammar specification.**  The grammar IS the
  hand-written code in Phase 2+.  Bison's productions
  served as a formal grammar; hand-written replaces them
  with executable code.  Both expressions of the grammar
  exist during Phase 2-4 (bison still building); the
  parity harness keeps them in sync.
- **Performance tuning.**  Hand-written and bison are both
  fast enough for ~1000-line asm files.  Don't optimize.
- **Restructuring the runtime.**  35 external functions
  stay external.  Their signatures don't change.

## What Phase 2 starts with

The first commit:

1. `include/token.h` (new file, content above).
2. Stub `src/scanner.c` that just returns `TOK_EOF` —
   compiles, linked, doesn't run yet.
3. Stub `src/parser.c` with `parse_file()` that just
   returns 0.
4. `src/pseudo_op.c` containing the 22 postlude functions
   relocated verbatim.
5. `include/pseudo_op.h` with the public declarations.
6. `meson.build` adds the new sources to spim_source_files.
7. `spim.c` reads new `-parser=` flag, dispatches to the
   right `parse_file` impl.
8. The parity harness binary.
9. CI integration: meson test target running the harness.

That's the Phase 2 ground-zero — about a day of work.
Real grammar implementation starts after.

---

**End of Phase 1 deliverable.**  Read alongside
[`scanner-parser-inventory.md`](scanner-parser-inventory.md).
Phase 2 (pilot + parity harness) is the next gate.
