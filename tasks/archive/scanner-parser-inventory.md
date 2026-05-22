# Scanner + parser inventory — Phase 0 deliverable

A complete walkthrough of `src/scanner.l` and `src/parser.y` for
whoever implements the hand-written recursive-descent
replacement in Phase 1+.  Companion to
[`handwritten-parser-migration.md`](handwritten-parser-migration.md).

Authored 2026-05-19.  Sources studied:

- `src/scanner.l` — 742 lines
- `src/parser.y` — 2987 lines
- `include/op.h` — 541 lines, 381 `OP()` entries (X-macro keyword list)
- `meson.build` — flex/bison invocations
- Bison's full state report regenerated to enumerate conflicts

## Overview

### File roles

| File | Role |
|---|---|
| `src/scanner.l` | flex grammar.  Tokenizes one source line at a time.  Custom `yywrap` inserts a sentinel before EOF.  Reuses spim's keyword table from `op.h` via the X-macro pattern. |
| `src/parser.y` | bison grammar.  104 production rules.  Semantic actions call into spim's runtime to emit instructions, store data, record labels.  Postlude contains 22 C helpers — chiefly the pseudo-op expansion engine. |
| `include/op.h` | X-macro list of 381 keywords.  Re-included six times across the build to populate different per-TU tables (`keyword_tbl`, `name_tbl`, `i_opcode_tbl`, `a_opcode_tbl`). |
| `include/parser.h` | Globals shared with the scanner (`data_dir`, `text_dir`, `parse_error_occurred`, `parse_errors_seen`). |
| `include/scanner.h` | Scanner API: `initialize_scanner`, `push/pop_scanner`, `scanner_start_line`, `register_name_to_number`. |

### Generated outputs

| Generator | Input | Output |
|---|---|---|
| flex | `src/scanner.l` | `builddir/lex.yy.c` |
| bison | `src/parser.y` | `builddir/parser_yacc.c` + `builddir/parser_yacc.h` |

`parser_yacc.h` defines the `YYSTYPE` union and the 390 `Y_*`
token enum.  Every other compilation unit gets the `Y_*` enum
by including `parser_yacc.h`.

### Build flow (meson)

`meson.build` lines 39-52:

```meson
flex = find_program('flex', required: true)
bison = find_program('bison', required: true)

lex_gen = custom_target('lex',
  input: 'src/scanner.l',
  output: 'lex.yy.c',
  command: [flex, '-o', '@OUTPUT@', '@INPUT@'])

parser_gen = custom_target('parser',
  input: 'src/parser.y',
  output: ['parser_yacc.c', 'parser_yacc.h'],
  command: [bison, '-d', '@INPUT@', '-o', '@OUTPUT0@'])
```

Both feed `spim_source_files`.

## Part 1 — scanner.l

### Lexer state

Single state (no `%x` / `%s` declarations).  Order-of-rule
disambiguation only.  Hand-written equivalent: one big
switch on `peek()` per token request.

### Lex rules (in source order)

| Pattern | Action |
|---|---|
| `[ \t]` | whitespace; if `current_line == NULL`, record `current_line_no` and `current_line` from `yytext` |
| `[\n]` | `line_no += 1`; return `Y_NL` |
| `[\r]` | discard (silent CR handling) |
| `[;]` | return `Y_NL` (semicolon also ends a line) |
| `[\001]` | return `Y_EOF`.  This is the EOF-sentinel inserted by `yywrap` — see below |
| `"#".*` | discard (line comment) |
| `(-[0-9]+)|([0-9]+)` | `yylval.i = atoi(yytext)`; return `Y_INT`.  **Note: negative integers are tokenized directly — there is no unary minus operator.** |
| `((0x)|(-0x))[0-9A-Fa-f]+` | parse as hex, into `yylval.i`; return `Y_INT` |
| `(\+|\-)?[0-9]+[.,'][0-9]+(e)?(\+|\-)?[0-9]*` | parse as FP; `yylval.p = &scan_float`; return `Y_FP` |
| `[a-zA-Z_.][a-zA-Z0-9_.]*` | identifier — see "keyword/label disambiguation" below |
| `\$[a-zA-Z0-9_.$]+` | register — see "register handling" below |
| `[*/:()+-]\|">"\|"="` | return the punctuation character as its own ASCII token |
| `,` | discard (commas are optional separators) |
| `?` | `yylval.p = str_copy(yytext)`; return `Y_ID` (used for REPL help) |
| `"…"` (quoted string with `\"` escapes) | `yylval.p = copy_str(yytext+1, 1)`; return `Y_STR` |
| `'…'` (quoted char with backslash escape) | `yylval.i = char value`; return `Y_INT`.  Uses `scan_escape` for `\NNN`/`\X`/named-escape forms |
| `.` (fall-through) | `yyerror("Unknown character")` |

### Keyword / label disambiguation

The identifier rule (`[a-zA-Z_.][a-zA-Z0-9_.]*`) handles three
cases:

1. **Keyword.**  `check_keyword(yytext, …)` looks up the
   identifier in `keyword_tbl[]` (built from `op.h`).  If
   matched: `yylval.i = token_number`; return that token.
2. **Constant label (`=`-defined).**  `label_is_defined` +
   `l->const_flag` set → `yylval.i = l->addr`; return `Y_INT`.
3. **Other identifier.**  `yylval.p = str_copy(yytext)`; return
   `Y_ID`.

The `only_id` global (set by parser actions) forces case 3
even when case 1 would match — used in contexts where the
identifier MUST be a label, not an opcode.  See "Shared
state" below.

`check_keyword` second argument: when `bare_machine` is true
or `accept_pseudo_insts` is false, pseudo-op tokens are
ignored.  Reproduce this in hand-written by skipping
keyword-table entries whose `value2 == PSEUDO_OP` under those
flag combinations.

### Register handling

The `\$[…]+` rule:

1. Strip leading `$`.
2. `register_name_to_number(name)` returns one of:
   - Numeric (`$0`-`$31`): direct atoi.
   - `f` followed by digits (`$f0`-`$f31`): atoi of suffix.
   - ABI name (`v0`, `a0`, `s8`, `zero`, …): look up
     `register_tbl[]` (35 entries, lines 629-665 of
     scanner.l).
3. If the result is `'f'` followed by digits *and* isn't `$fp`,
   return `Y_FP_REG`.  Otherwise return `Y_REG`.
4. If `register_name_to_number` returned -1, fall back to
   identifier handling (constant-label or `Y_ID`).

### Special functions in scanner.l

| Function | Lines | Role |
|---|---|---|
| `initialize_scanner` | 326-350 | Reset `yyin`, `yyrestart`, `line_no`, `current_line`, `line_returned`, `eof_returned` |
| `push_scanner` | 352-357 | `yypush_buffer_state` for nested input |
| `pop_scanner` | 359-363 | `yypop_buffer_state` |
| `scanner_start_line` | 365-370 | Reset `current_line = NULL`, `line_returned = 0` |
| `yywrap` | 386-399 | Insert `\001` sentinel via `unput`, return 0 once.  This is what produces `Y_EOF` cleanly |
| `scan_escape` | 404-450 | Decode a `\X` escape *for char literals only*.  Named escapes `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\\`, `\"`, `\'`; hex form `\X..`.  **Does NOT support octal** — that's `copy_str`'s job |
| `copy_str` | 456-546 | Decode `\` escapes *for string literals*.  Named escapes (`\n`, `\t`, `\"`); octal `\NNN` (high digit 0-3); hex `\X..`; default branch copies `\` then next char (so `\\` doesn't decode to `\`).  **Note**: bug in octal-digit shift was fixed 2026-05-19 (line 493: `<< 6`).  Guarded by `tests/tt.octal_escape.s` |
| `erroneous_line` | 553-603 | Reconstruct the source line with a `^` caret for error messages.  Uses `yyinput`/`unput` to advance to next newline.  Load-bearing for `parse_errors_seen > 0 → exit 2` |
| `check_keyword` | 613-626 | Look up identifier in `keyword_tbl[]` |
| `register_name_to_number` | 667-688 | Map `$reg` to register number (0-31) |
| `source_line` | 695-742 | Return the current source line as a freshly-allocated string with `N: ` line-number prefix.  Called by the REPL when echoing source.  One-shot per line (gated by `line_returned`) |

### Shared globals (scanner-owned)

| Symbol | Type | Reset by | Read by |
|---|---|---|---|
| `only_id` | int (exported) | parser actions | scanner's identifier rule |
| `line_no` | int (exported) | `initialize_scanner` and `\n` rule | error printers; parser uses for label addresses |
| `current_line_no` | static int | first non-space token | `source_line` |
| `current_line` | static char* | first non-space token, `scanner_start_line`, end of rule | error printing |
| `scan_float` | static double | FP literal rule | parser via `yylval.p` |
| `line_returned` | static int | `initialize_scanner`, `scanner_start_line`, `source_line` | `source_line` |
| `eof_returned` | static int | `initialize_scanner`, `yywrap` | `yywrap` |

### keyword_tbl[]

Built by re-including `op.h` with `OP()` redefined to emit
`{NAME, OPCODE, TYPE},`.  381 entries.  Looked up by
`map_string_to_name_val_val` (linear or binary search; the
table is alphabetically sorted, but the lookup function in
`string-stream.c` does linear scan — verify in Phase 1
whether perf matters).

The hand-written scanner can reuse the same table verbatim;
the construction is already independent of bison.

### register_tbl[]

35 entries, alphabetically sorted, hand-coded in
scanner.l:629-665.  Maps ABI names (`at`, `v0`-`v1`, `a0`-`a3`,
`t0`-`t9`, `s0`-`s8`, `k0`-`k1`, `kt0`-`kt1`, `gp`, `sp`, `fp`,
`ra`, `zero`) to register numbers.  Numeric (`$0`-`$31`) and
FP (`$f0`-`$f31`) are special-cased to avoid table entries.

Hand-written: relocate this table verbatim to `src/scanner.c`.

## Part 2 — token inventory

### Token categories

The 390 `Y_*` tokens break down:

| Category | Count | Notes |
|---|---|---|
| Generic | 9 | `Y_EOF`, `Y_NL`, `Y_INT`, `Y_ID`, `Y_REG`, `Y_FP_REG`, `Y_STR`, `Y_FP`, plus ASCII punctuation passed through as their own char codes (`+`, `-`, `*`, `/`, `(`, `)`, `:`, `=`, `>`) |
| Integer instruction opcodes (`Y_*_OP`) | ~230 | Every R/I/J-type opcode, including MIPS32 R2 extensions |
| Pseudo-op opcodes (`Y_*_POP`) | ~50 | `Y_LA_POP`, `Y_LI_POP`, `Y_DIV_POP`, `Y_MULO_POP`, `Y_BEQZ_POP`, etc.  These tokens come from the same keyword table but `TYPE == PSEUDO_OP` |
| FP opcodes (`Y_*_S_OP`, `Y_*_D_OP`, `Y_*_PS_OP`) | ~100 | Single, double, paired-single forms.  Often grouped in productions like `FP_BINARY_OPS` covering all three for a given operation |
| Directive tokens (`Y_*_DIR`) | ~30 | `Y_DATA_DIR`, `Y_TEXT_DIR`, `Y_WORD_DIR`, `Y_ASCIIZ_DIR`, etc. |

### Token-to-string mapping

The table that produces tokens from identifiers (`keyword_tbl`)
is the same source as `name_tbl` and `i_opcode_tbl` and
`a_opcode_tbl` — all built from `op.h` with different `OP()`
redefinitions.  The hand-written port preserves this — one
source of truth in `op.h`, multiple table builds via the
X-macro pattern.

### YYSTYPE union

bison auto-generates this from the `%union` declaration —
which `parser.y` does NOT have, so YYSTYPE defaults to int.
But `yylval.i` and `yylval.p` are used throughout, implying a
de facto union of `int` and `void*`.

In Phase 1, an explicit `YYSTYPE` struct with `int i` and
`void* p` will be needed in `include/scanner.h`.  Two fields
only; bison's de facto convention.

## Part 3 — parser.y productions

### Top of grammar

```
LINE      = action(reset error/start-line) LBL_CMD
LBL_CMD   = OPT_LBL CMD | CMD
OPT_LBL   = ID ':' action(record label)
          | ID '=' EXPR action(record constant label)
CMD       = ASM_CODE action(clear_labels) TERM
          | ASM_DIRECTIVE action(clear_labels) TERM
          | TERM
TERM      = Y_NL { LINE_PARSE_DONE; }
          | Y_EOF { clear_labels(); FILE_PARSE_DONE; }
```

`LINE_PARSE_DONE` and `FILE_PARSE_DONE` are macros defined in
`include/parser.h` that `YYACCEPT` the current parse.  Bison
calls `yyparse` repeatedly (once per source line); after each
`YYACCEPT` it returns control to `read_assembly_file` which
loops until EOF.

Hand-written equivalent: a single-pass loop over the file
(no per-line `yyparse` call), with `parse_line()` consuming
one line per iteration.

### ASM_CODE — the heart (119 alternatives, lines 592-1642)

Grouped by operand-shape family.  Each alternative dispatches
into a small action calling helpers in spim's C runtime.

The "family" productions (`LOAD_OPS`, `STORE_OPS`, etc.)
collapse multiple opcodes into one shape.  For example:

```
ASM_CODE : LOAD_OPS DEST ADDRESS  { i_type_inst($1.i, $2.i, …); …}
         | LOAD_OPS DEST UIMM16   { …}
         | …
         ;
LOAD_OPS : Y_LB_OP | Y_LBU_OP | Y_LH_OP | Y_LHU_OP | Y_LL_OP
         | Y_LW_OP | Y_LWL_OP | Y_LWR_OP | Y_PFW_OP | Y_LD_POP
         ;
```

So one alternative-line in `ASM_CODE` covers 10 opcodes.

The full breakdown by operand-shape family (parent in
`ASM_CODE`, children listed underneath):

| Family | Alternatives | Children — what gets matched |
|---|---|---|
| LOAD | 1 alt | `LOAD_OPS DEST ADDRESS` (10 opcodes: lb, lbu, lh, lhu, ll, lw, lwl, lwr, pref, ld) |
| LOADC (coproc) | 1 | `LOADC_OPS COP_REG ADDRESS` (ldc2, lwc2) |
| LOADFP | 1 | `LOADFP_OPS F_SRC1 ADDRESS` (ldc1, lwc1, l.s, l.d) |
| LOADI (lui) | 1 | `LOADI_OPS DEST UIMM16` |
| la pseudo | 1 | `Y_LA_POP DEST ADDRESS` — expands to `addi`/`ori`+`lui` |
| li pseudo (32-bit) | 1 | `Y_LI_POP DEST IMM32` — expands to one or two instructions depending on imm magnitude |
| la.d/li.d pseudo (FP) | 2 | FP-loaded constants via emit-`.word` pattern |
| ulh/ulhu/ulw pseudo | 3 | Unaligned-load expansions |
| STORE | 1 | `STORE_OPS SRC1 ADDRESS` (7 opcodes: sb, sc, sh, sw, swl, swr, sd) |
| STOREC | 1 | `STOREC_OPS COP_REG ADDRESS` |
| STOREFP | 1 | `STOREFP_OPS F_SRC1 ADDRESS` |
| ush/usw pseudo | 2 | Unaligned-store expansions |
| BINARY_OPS (R-type 3-reg) | several | `BINARY_OPS DEST SRC1 SRC2` family; also 2-reg forms (rs/rt only) |
| BINARYI (R-type with immediate variant) | several | `BINARYI_OPS DEST SRC1 IMM` — picks R-type or I-type based on whether imm fits |
| SHIFT | several | `SHIFT_OPS DEST SRC1 Y_INT` — immediate shifts; SHIFT_OPS_REV2 covers MIPS32-R2 rotr; SHIFTV covers variable shifts |
| SUB | 1 | `SUB_OPS DEST SRC1 SRC2` (sub, subu); allows immediate via negation |
| DIV_POPS / MUL_POPS | several | `div`/`mult` pseudo-ops with 3-operand forms expand to instruction sequences (postlude helpers `div_inst`/`mult_inst`) |
| SET_LE_POPS / SET_GT_POPS / SET_GE_POPS / SET_EQ_POPS | 4 | Comparison pseudo-ops — postlude helpers `set_le_inst` etc. |
| MULT_OPS | 1 | True 2-operand multiply (no destination, results in HI/LO) |
| MULT_OPS3 | 1 | MIPS32 3-operand mul |
| BR_OPS (branch families) | many | `BINARY_BR_OPS SRC1 SRC2 LABEL` (beq, bne, ...), `UNARY_BR_OPS SRC1 LABEL` (bgez, bgtz, ...), pseudo-op forms `Y_BEQZ_POP`, `Y_BGT_POP`, etc. |
| J_OPS | 1 | `J_OPS LABEL` (j, jal); pseudo `b` for unconditional branch |
| BINARY_TRAP_OPS / BINARYI_TRAP_OPS | 2 | teq/tne/tlt/tge family, immediate and register forms |
| MOVE_FROM_HILO / MOVE_TO_HILO | 2 | mfhi/mflo/mthi/mtlo |
| MOVEC | 1 | movn/movz conditional move |
| MOVE_COP_OPS / CTL_COP_OPS | 2 | mfc0/mfc1/cfc0/cfc1 family |
| NULLARY | 2 | syscall, break, eret, deret (no operands) |
| UNARY_REV2 | 1 | di, ei — MIPS32-R2 interrupt enable/disable |
| BF_OPS_REV2 | 1 | ext, ins — bitfield extract/insert |
| COUNT_LEADING | 1 | clz, clo |
| SYS_OPS | 1 | rfe |
| PREFETCH | 1 | prefx |
| CACHE | 1 | cache |
| TLB | 1 | tlbp, tlbr, tlbwi, tlbwr |
| BR_COP_OPS | 1 | Coprocessor branches (bc1f/bc1t/bc1fl/bc1tl + bc2 forms) |
| FP_MOVE_OPS | 1 | mov.s, mov.d |
| FP_MOVEC / FP_MOVECC | 2 | Conditional FP moves |
| FP_UNARY_OPS | 1 | abs.s, neg.s, sqrt.s, etc. (~20 covered by FP_UNARY_OPS + REV2) |
| FP_BINARY_OPS | 1 | add.s, sub.s, mul.s, div.s (7 base + REV2 set) |
| FP_TERNARY_OPS_REV2 | 1 | madd.s, msub.s, nmadd.s, nmsub.s |
| FP_CMP_OPS | 1 | c.eq.s, c.lt.s, c.le.s, etc. (~31 conditions covered) |
| MOVECC | 1 | movf, movt (FP-condition conditional moves into integer regs) |
| SPIM-only directives | 1 | `.alias` etc. (some are no-ops or live in ASM_DIRECTIVE) |
| nop pseudo | 1 | `nop` keyword → `sll $0, $0, 0` |

The full per-alternative list lives at `parser.y` lines
592-1642.  When implementing Phase 2's pilot, focus on:

- `BINARYI_OPS` (add, addu)
- `BINARY_ARITHI_OPS` (addi, addiu)
- `BINARY_LOGICALI_OPS` (andi, ori, xori)
- `SHIFT_OPS`
- `LOAD_OPS` + simplest `ADDR` forms
- `STORE_OPS` + simplest `ADDR` forms
- `BINARY_BR_OPS` (beq, bne)
- `UNARY_BR_OPS` (bgez)
- `J_OPS`
- `NULLARY_OPS` (syscall)

That subset matches what `/examples/src/01-helloworld.asm`
needs.  Get that file's parity test green before expanding.

### ASM_DIRECTIVE (47 alternatives, lines 2123-2413)

| Directive | What it does in the semantic action |
|---|---|
| `.alias` | reserved/no-op |
| `.align EXPR` | `align_text` or `align_data` |
| `.ascii STR_LST` / `.asciiz STR_LST` | toggles `null_term`; emits via `STR_LST` |
| `.byte EXPR_LST` | sets `store_op = store_byte`; emits via `EXPR_LST` |
| `.comm ID EXPR` | aligns data; defines symbol; advances data PC |
| `.data` / `.data IMM` | switches to user data segment, sets data PC if explicit |
| `.kdata` / `.kdata IMM` | same for kernel data |
| `.text` / `.text IMM` | switches to user text |
| `.ktext` / `.ktext IMM` | same for kernel text |
| `.double FP_EXPR_LST` | sets `store_fp_op = store_double`, alignment 3 |
| `.float FP_EXPR_LST` | sets `store_fp_op = store_single`, alignment 2 |
| `.half EXPR_LST` | sets `store_op = store_half`, alignment 1 |
| `.word EXPR_LST` | sets `store_op = store_word`, alignment 2 |
| `.space EXPR` | advances data PC; aligns; emits zeros |
| `.globl ID` | `make_label_global` |
| `.extern ID EXPR` | `make_label_global` + `record_label` + advance |
| `.set REG REG` | (alias directive — reserved) |
| `.end ID` / `.ent ID` | function-boundary markers (no-op in spim) |
| `.frame REG INT REG` | stack-frame metadata (no-op) |
| `.mask INT INT` / `.fmask INT INT` | register-save mask metadata (no-op) |
| `.file INT STR` | source-line metadata (no-op) |
| `.lab` / `.livereg` / `.loc` / `.option` | metadata directives (no-op) |
| `.bgnb INT` / `.endb` | block-begin/end metadata (no-op) |
| `.endr` | end-of-record metadata (no-op) |
| `.err` | yyerror unconditionally |
| `.asm0` | spim-specific directive (no-op) |
| `.repeat INT` (?) | rare; check |

Most of these are MIPS-spec assembly directives that spim
recognizes but doesn't act on (debug info, stack-frame
metadata).  About a third have real semantics.  The
hand-written port can treat the no-op directives as `consume
arguments; do nothing`.

### Operand productions (lines 2414-2683)

| Production | Alternatives | Semantic value |
|---|---|---|
| `ADDRESS` | 1 (wraps `ADDR` with `only_id` toggling) | passes through `ADDR`'s `$$` |
| `ADDR` | 9 | builds `addr_expr*` via `make_addr_expr`.  Covers `(reg)`, `imm`, `imm(reg)`, `id`, `id(reg)`, `id+imm`, `imm+id`, `id-imm`, `id+imm(reg)`, `id-imm(reg)`.  This is the main source of shift-reduce conflicts (see Part 6) |
| `BR_IMM32` | 1 | `IMM32` for branch targets (toggles `only_id`) |
| `IMM16` | 1 | `IMM32` with range check `IMM_MIN..IMM_MAX` |
| `UIMM16` | 1 | `IMM32` with range check `UIMM_MIN..UIMM_MAX` |
| `IMM32` | 5 | builds `imm_expr*` via `make_imm_expr` |
| `ABS_ADDR` | 3 | numeric literal; `Y_INT + Y_INT` for sums; `Y_INT Y_INT` (negative) for differences — see note below |
| `SRC1`, `SRC2`, `DEST` | 1 each | aliases for `REGISTER` |
| `REG` | 1 | alias for `REGISTER` |
| `REGISTER` | 2 | `Y_REG` directly, or `'$' Y_INT` form (numeric `$N`) |
| `F_DEST`, `F_SRC1`, `F_SRC2` | 1 each | aliases for `FP_REGISTER` |
| `FP_REGISTER` | 2 | `Y_FP_REG` or `'$' Y_INT` numeric form |
| `CC_REG` | 1 | `Y_INT` — FP condition-code register number |
| `COP_REG` | 1 | `Y_REG` — coprocessor register |
| `LABEL` | 1 | `ID` |
| `STR_LST` | 2 | recursive list of `STR` |
| `STR` | 1 | `Y_STR`; emits bytes via store_byte loop; if `null_term`, appends NUL |
| `EXPRESSION` | 1 | `EXPR` with `only_id` toggle |
| `EXPR` | 3 | `TRM`, `EXPR + TRM`, `EXPR - TRM` |
| `TRM` | 3 | `FACTOR`, `TRM * FACTOR`, `TRM / FACTOR` |
| `FACTOR` | 2 | `Y_INT`, `'(' EXPR ')'` |
| `EXPR_LST` | 2 | recursive list of `EXPRESSION`; calls `store_op` per element |
| `FP_EXPR_LST` | 2 | recursive list of `Y_FP`; calls `store_fp_op` per element |
| `OPTIONAL_ID` | 1 | `OPT_ID` with `only_id` toggle |
| `OPT_ID` | 1 | `Y_ID` |
| `ID` | 1 | `Y_ID` with `only_id` toggle |

**ABS_ADDR's `Y_INT Y_INT` quirk** (line 2523): because the
scanner tokenizes `-123` as a single negative `Y_INT`,
`Y_INT - Y_INT` doesn't parse — the second `-` is part of the
literal.  So bison sees `Y_INT Y_INT_negative` and treats that
as subtraction.  The hand-written scanner can choose to
tokenize unary minus separately and avoid this hack.

### Constant-folding in EXPR/TRM/FACTOR

`EXPR`/`TRM`/`FACTOR` do constant arithmetic at parse time
(`$$.i = $1.i + $3.i`).  All operands are integers; no
identifier resolution at this stage.  Used in `.align N`,
`.byte 5 + 3`, etc.

Hand-written: same.  Recursive descent on `parse_expr` →
`parse_term` → `parse_factor`, returning `int`.

### only_id flag mechanism

This is the trickiest part of the grammar.  Several
productions toggle `only_id = 1` before consuming a token,
then `only_id = 0` after.  The flag is read by the scanner's
identifier rule to **suppress keyword matching**.  Without
it, a label named `add` would be tokenized as `Y_ADD_OP` in
a context like `add:` (label definition).

Productions that toggle `only_id`:

- `ID` — anywhere `ID` is expected (label definitions,
  `.globl ID`, etc.)
- `OPTIONAL_ID` / `OPT_ID`
- `EXPRESSION` — to allow labels in operand expressions
- `ADDRESS`, `BR_IMM32` — to allow labels in addresses

In hand-written, the equivalent is **never call
`check_keyword` when parsing an identifier in an
identifier-only context**.  Cleaner without the global toggle:
pass a "treat as identifier" flag to a parameterized scanner
function.

## Part 4 — parser.y postlude (lines 2685-2987)

22 C functions.  Pseudo-op expansion engine + utilities.
All can move to `src/pseudo_op.c` unchanged.

| Function | Lines | Role |
|---|---|---|
| `fix_current_label_address` | 2690-2702 | Move local labels' addresses when relocated |
| `cons_label` | 2703-2713 | Cons a label onto a list |
| `clear_labels` | 2714-2729 | Free `this_line_labels` |
| `op_to_imm_op` | 2731-2750 | Convert a non-immediate op to its immediate form (add → addi, sub → addi-neg, etc.) |
| `imm_op_to_op` | 2751-2769 | Inverse of above |
| `nop_inst` | 2773-2776 | `r_type_inst(Y_SLL_OP, 0, 0, 0)` — emit a nop |
| `trap_inst` | 2780-2783 | `r_type_inst(Y_BREAK_OP, 0, 0, 0)` |
| `branch_offset` | 2787-2790 | Build a constant `imm_expr` for branch displacement |
| `div_inst` | 2794-2817 | Expand `div $rd, $rs, $rt` pseudo-op into 6 instructions (BNE-NOP-BREAK-DIV-MFLO + optional sign check) |
| `mult_inst` | 2821-2845 | Expand `mulo`/`mulou` pseudo-op |
| `set_le_inst` | 2849-2856 | Expand `sle` pseudo-op |
| `set_gt_inst` | 2859-2863 | Expand `sgt` pseudo-op (single SLT with operands swapped) |
| `set_ge_inst` | 2867-2873 | Expand `sge` pseudo-op |
| `set_eq_inst` | 2877-2898 | Expand `seq` pseudo-op |
| `store_word_data` | 2900-2910 | Helper for `.word` expression list emission |
| `initialize_parser` | 2911-2920 | Reset `data_dir`, `text_dir`, parser state; set `input_file_name` |
| `check_imm_range` | 2921-2940 | Range-check signed immediate (-32768..32767) |
| `check_uimm_range` | 2941-2949 | Range-check unsigned immediate (0..65535) |
| `yyerror` | 2952-2957 | Set `parse_error_occurred`, increment `parse_errors_seen` (added 2026-05-19); call `clear_labels`, `yywarn` |
| `yywarn` | 2961-2964 | Format error string with line number, file name, call `error()` |
| `mips32_r2_inst` | 2977-2983 | Helper for MIPS32-R2-specific opcode emission |
| `cc_to_rt` | 2984-end | Convert FP condition-code + nd/tf flags to an rt field encoding |

The pseudo-op expansion functions (`div_inst`, `mult_inst`,
`set_*_inst`) are the most important — students observe their
output instruction-by-instruction in `-explain` mode.  Each
expansion must produce **byte-for-byte identical** instruction
sequences in the migration.

## Part 5 — shared global state catalog

| Symbol | Defined in | Read by | Written by | Reset semantics |
|---|---|---|---|---|
| `yylval` | bison-generated | scanner ← parser → actions | scanner | per token |
| `yytext` | flex-generated | scanner, parser actions | scanner | per token |
| `line_no` | scanner.l | scanner, error printers, parser actions | scanner (`\n` rule) | `initialize_scanner` |
| `current_line`, `current_line_no` | scanner.l (static) | `source_line`, `erroneous_line` | scanner (first non-space token) | `scanner_start_line` |
| `line_returned` | scanner.l (static) | `source_line` | `source_line`, `scanner_start_line` | `initialize_scanner` |
| `eof_returned` | scanner.l (static) | `yywrap` | `yywrap` | `initialize_scanner` |
| `parse_error_occurred` | parser.y | scanner (via `flush_local_labels` in `read_assembly_file`) | `yyerror` (set true), `LINE` action (reset false) | per LINE |
| `parse_errors_seen` | parser.y | `main()` in spim.c | `yyerror` | `read_assembly_file` (init to 0) |
| `data_dir`, `text_dir` | parser.y | scanner's `OPT_LBL` action (PC selection) + parser everywhere | `.data`/`.text`/`.kdata`/`.ktext` directives | per file |
| `only_id` | scanner.l (exported) | scanner's identifier rule | parser actions before/after specific productions | per production |
| `null_term` | parser.y (static) | `.ascii`/`.asciiz` STR_LST emit | `.ascii` action (false), `.asciiz` action (true) | per directive |
| `store_op` | parser.y (static) | `EXPR_LST` emit | `.byte`/`.half`/`.word`/`.space` actions | per directive |
| `store_fp_op` | parser.y (static) | `FP_EXPR_LST` emit | `.float`/`.double` actions | per directive |
| `this_line_labels` | parser.y (static) | `flush_local_labels` | `OPT_LBL` action; `clear_labels` | per line |
| `input_file_name` | parser.y | `yywarn` (error messages) | `initialize_parser` | per file |

Hand-written: most become normal file-statics in `parser.c`
or `scanner.c`.  The `only_id` toggle becomes a parameter to
the scanner's "next token" function.

## Part 6 — shift-reduce conflicts catalog

Bison reports **25 shift/reduce conflicts**, all suppressed by
`%expect 25`.  All 25 are at the same grammatical phenomenon:
**when `Y_ID` is the next token in an `IMM32`/`ADDRESS`
context, bison can either shift (begin matching `Y_ID '+'
ABS_ADDR`) or reduce (treat the empty marker production
`$@15` as complete and re-enter the `ID` action that toggles
`only_id`)**.

Bison's default-shift resolution means: try to parse `Y_ID +
ABS_ADDR` first.  This is correct in 99% of real source —
operand expressions like `array + 4` parse as label+offset.
The "reduce" branch only fires when the next token after
`Y_ID` doesn't match `+`/`-`, at which point bison
backtracks (one-token lookahead, not full backtracking) and
takes the reduction.

The 25 conflict states (from `bison --report=all`):

| State | Conflict | Production context |
|---|---|---|
| 102, 465, 479, 538, 555, 557, 558, 562, 564, 590, 632, 633, 689, 694, 696, 701, 704, 705, 706, 707, 708, 709, 710, 713, 720 | All shift/reduce on `Y_ID` | Various contexts where `IMM32`/`ADDRESS` / `BR_IMM32` / `EXPRESSION` could start with `Y_ID` and the marker production `$@15` (the `only_id = 1` injection) could fire instead |

For the hand-written parser this collapses to **one explicit
lookahead branch**:

```c
static imm_expr* parse_imm32(void) {
    /* If we see Y_ID followed by + or -, parse it as a
       label-plus-offset form.  Otherwise let the standard
       IMM32 productions handle it. */
    if (peek() == TOK_ID && (peek2() == '+' || peek2() == '-')) {
        return parse_label_plus_offset();
    }
    /* … */
}
```

Two-token lookahead resolves it cleanly.  No `%expect`, no
warnings, no surprises.  The 25 conflicts disappear because
they're all the same conflict.

### Specific places to test in Phase 2

When the pilot parses each of these forms, byte-diff against
bison:

1. `lw $t0, label`  — label-only ADDRESS
2. `lw $t0, label($s0)` — label-base
3. `lw $t0, label + 4` — label+constant (the conflict case)
4. `lw $t0, label - 4` — label-constant
5. `lw $t0, label + 4($s0)` — full form
6. `.word label + 4` — IMM32 in EXPR_LST
7. `.word (0x1000) >> 2` — IMM32 shifted form
8. `.word label - 4` — IMM32 with subtraction

Each tickles the conflict from a different production.

## Part 7 — meson build flow

Already inventoried in the Overview section.  Phase 5
deletions:

- Lines 9-10: `flex` and `bison` `find_program` calls
- Lines 39-44: `lex_gen` custom_target
- Lines 46-52: `parser_gen` custom_target
- Line 69: `+ lex_gen + parser_gen` in `spim_source_files`

Phase 5 additions:

- `src/scanner.c`, `src/parser.c`, `src/pseudo_op.c` in
  `spim_source_files`
- (Optional) keep `flex`/`bison` for Phase 4 settling so
  the bison path is still buildable.

## Part 8 — risk-relevant findings

### A. The `\001` EOF sentinel

`yywrap` inserts `\001` via `unput` so the scanner re-enters
the lex loop one more time and matches the `[\001]` rule,
returning `Y_EOF` as a real token.  Without this hack, bison
would see EOF (token 0) which it reserves as the end-of-input
marker — but spim wants a real `Y_EOF` token so its `TERM`
production can call `FILE_PARSE_DONE` semantically.

Hand-written: the scanner returns a real `TOK_EOF` enum value
directly when it hits EOF.  No sentinel needed.  The
`TERM`-equivalent code checks for `TOK_EOF` explicitly and
exits the loop.

### B. Per-line yyparse loop

`read_assembly_file` calls `while (!yyparse());` — bison's
`yyparse` returns 1 on success (file done) or 0 to keep
going.  spim's `LINE_PARSE_DONE` macro is `YYACCEPT` which
returns from yyparse with success after each line.

Hand-written: drop the per-line dance.  `parse_file` is a
single function with a single loop over tokens.

### C. Recently-fixed octal-escape bug

`scanner.l:493` — fixed 2026-05-19.  Hand-written port must
preserve the fix.  `tests/tt.octal_escape.s` is the regression.

### D. Newly-load-bearing error message format

After 2026-05-19's Unix-process conformance fixes,
`parse_errors_seen > 0 → main returns 2`.  The user-visible
error format is produced by `yywarn` calling `error()` with
the string `"spim: (parser) %s on line %d of file %s\n%s"`.
Hand-written must produce the same shape so any external
script grepping spim output for parse errors keeps working.

### E. Forward references and the two-pass model

Labels referenced before they're defined go into the
`undefined_symbols` list.  At end-of-file (`FILE_PARSE_DONE`),
`end_of_assembly_file` resolves them.  This happens
**outside** the parser — the parser just collects undefined
references via `make_imm_expr`/`make_addr_expr` with a label
name; resolution is `sym-tbl.c`'s job.

Hand-written: same.  No two-pass parsing; the symbol table
already handles forward references.

### F. Negative-integer tokenization

The scanner matches `-[0-9]+` as a single `Y_INT` with a
negative value.  This means:

- `-5` lexes as one token with value -5.
- `7 - 5` lexes as `Y_INT(7), Y_INT(-5)` — no `-` operator
  between them.  `ABS_ADDR` has a special production
  `Y_INT Y_INT` (line 2523) that interprets the pair as
  subtraction.

Hand-written has a choice:

- Preserve the quirk (match the existing scanner exactly).
- Change to "always tokenize `-` separately, treat as unary
  minus during parse".  Cleaner but diverges from bison's
  behavior — needs parity-harness check.

Recommendation: preserve in Phase 2/3, fix as a clearly-scoped
cleanup in Phase 4 or after.

### G. The `?` token

The identifier rule treats bare `?` as `Y_ID`.  Used by the
REPL's `help`/`print` commands.  Not relevant in assembly
source.

### H. Hex `\X..` vs `\x..` mismatch

`scan_escape` and `copy_str` both accept `\X` (uppercase) but
NOT `\x` (lowercase) — see scanner.l:421 (`case 'x': case 'X'`
in scan_escape) vs scanner.l:510 (`case 'X'` only in
copy_str).  This is a real inconsistency: char literals
accept `\x41`, string literals don't.

Document this in the inventory; decide in Phase 1 whether to
unify (preferring `\x` lowercase to match C) or preserve.

## Part 9 — open questions for Phase 1

1. **YYSTYPE struct shape.**  Bison default is `int`.  spim
   uses `yylval.i` and `yylval.p`.  The hand-written port
   needs an explicit `{int i; void *p;}` struct OR a tagged
   union with the type carried alongside.  Tagged union is
   cleaner; struct matches bison's de facto interface.
2. **One-token vs two-token lookahead.**  The 25 conflicts
   resolve with two-token lookahead.  Other places might
   need it too.  Decide whether to ship a `peek()` /
   `peek2()` pair or a more general "token queue" that
   supports k-lookahead.
3. **Token enum location.**  Currently `parser_yacc.h`.
   Move to `include/scanner.h` or a new `include/token.h`?
   Multiple TUs read it; either header is fine.
4. **`only_id` mechanism.**  Replace with parameterized
   scanner call (`next_token(allow_keywords)`) or keep as
   global toggle?  Parameterized is cleaner; toggle is
   simpler to migrate.
5. **`-parser=` flag default.**  Phase 4 flips it from
   `bison` to `hand`.  Document the switch point clearly in
   the ChangeLog and `tests/`.
6. **Constant-folded EXPR semantics.**  bison's
   `EXPR + TRM` does integer arithmetic at parse time.
   Hand-written same.  Edge case: overflow?  bison's
   behavior is undefined-on-int-overflow (UB); the C
   standard doesn't promise anything.  Preserve as-is unless
   the inventory turns up a real bug.

## Part 10 — what the hand-written parser DOESN'T need to do

Things bison/flex do that the hand-written parser can omit:

- Generate a state machine: hand-written IS the state machine,
  expressed directly as function calls.
- Compute lookahead tables: hand-written calls `peek()`
  explicitly where it needs to.
- Handle `%expect`/`%left`/`%right`: irrelevant for
  hand-written.
- Generate `yyparse`/`yylex` symbols: hand-written exposes
  `parse_file` (or similar) and `next_token` directly.
- Build the YYSTYPE union: hand-written defines it as a
  plain struct.

Things bison/flex provide that the hand-written parser HAS
to reproduce:

- The 35 external C function calls into spim's runtime.
- The exact pseudo-op expansion sequences.
- The error message format.
- Label-resolution timing (`flush_local_labels` after each
  line, `end_of_assembly_file` once).
- The 381-entry keyword table (reused from `op.h` X-macro).
- The 35-entry register name table.

## What to do next (Phase 1 starting point)

1. Read this document end-to-end.  Verify it matches the
   current `scanner.l` and `parser.y`.
2. Draft `src/scanner.h` with the explicit YYSTYPE struct,
   the token enum (relocated from `parser_yacc.h`), and the
   API (`scanner_init`, `next_token`, `peek`, `advance`,
   `current_line_text`).
3. Draft `src/parser.h` API (`parse_file`).
4. Draft `src/pseudo_op.h` exposing the 22 postlude
   helpers (or those of them that need to be public — most
   are internal).
5. Write a one-page design sketch of the recursive-descent
   shape: one function per `ASM_CODE` family, one function
   per `ASM_DIRECTIVE`, the EXPR/TRM/FACTOR recursion.
6. Commit the design sketch to the repo as
   `tasks/handwritten-parser-design.md`.  Phase 1 is done.

After Phase 1 lands, Phase 2 (pilot) can start with the
parity harness and a subset of opcodes.

## Appendix A — alternative counts per family production

For sizing future implementation work:

| Family | Alternatives |
|---|---|
| ASM_CODE | 119 |
| ASM_DIRECTIVE | 47 |
| FP_CMP_OPS | 31 |
| FP_UNARY_OPS_REV2 | 20 |
| FP_UNARY_OPS | 19 |
| FP_CMP_OPS_REV2 | 15 |
| FP_TERNARY_OPS_REV2 | 12 |
| UNARY_BR_OPS | 11 |
| LOAD_OPS | 9 |
| ADDR | 9 |
| MOVE_COP_OPS | 7 |
| FP_BINARY_OPS | 7 |
| BR_COP_OPS | 7 |
| STORE_OPS | 6 |
| BINARYI_OPS | 6 |
| (others) | 1-5 each |

`ASM_CODE` is the giant — but most of its 119 alternatives
delegate to small family productions like `LOAD_OPS` for the
opcode discrimination.  The actual hand-written work is
~50-60 distinct operand-shape functions plus the
30-or-so-line family-dispatch dispatcher.

## Appendix B — external C function call frequencies

From semantic-action bodies in `parser.y`:

| Function | Calls | Notes |
|---|---|---|
| `imm_expr` constructors | 101 | `const_imm_expr`, `make_imm_expr`, `incr_expr_offset` |
| `addr_expr` constructors | 70 | `make_addr_expr`, `addr_expr_imm`, `addr_expr_reg` |
| `r_type_inst` | 67 | Emit one R-type instruction |
| `i_type_inst` | 38 | Emit one I-type instruction (and `i_type_inst_free` which also frees the expr) |
| `r_co_type_inst` | 15 | Coprocessor R-type variant |
| `r_sh_type_inst` | 14 | Shift R-type variant |
| `nop_inst` | 10 | Postlude helper |
| `make_addr_expr` | 10 | Build addr_expr |
| `const_imm_expr` | 10 | Build constant imm_expr |
| `user_kernel_data_segment` | 8 | `.data` vs `.kdata` switch |
| (~25 others) | ≤5 each | `j_type_inst`, `expand_data`, `expand_text`, `record_label`, etc. |

All 35 functions stay external; hand-written parser calls
them at the same points with the same arguments.

## Appendix C — what's NOT in this inventory

- Detailed bytecode of pseudo-op expansions (the actual
  instruction sequences `div_inst` emits).  These live in
  `parser.y` lines 2794+; reading them in source is the
  right reference.
- The semantic of every `Y_*` token name.  The X-macro in
  `op.h` is the source of truth (NAME, OPCODE, TYPE,
  R_OPCODE).
- The exact register encoding (which `Y_REG` value maps to
  $0..$31).  See `register_name_to_number` and the
  `register_tbl[]` static.
- spim's runtime API (`inst.c`/`data.c`/`sym-tbl.c`).
  Unchanged by this migration; out of scope here.

---

**End of Phase 0 deliverable.**  This document is the input to
the Phase 1 design decision: proceed with the hand-written
rewrite (and at what scope), or kill the project cleanly.

Estimated reading time: ~1 hour for someone unfamiliar with
the codebase, ~20 minutes for Bill.
