/* SPIM S20 MIPS simulator.
   Hand-written recursive-descent parser.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause.
*/

#include <stdio.h>
#include <string.h>

#include "spim.h"
#include "instruction.h"
#include "data.h"
#include "symbol-table.h"
#include "scanner.h"
#include "parser.h"
#include "tokens.h"
#include "opcode-types.h"
#include "spim-utils.h"
#include "parser.h"
#include "pseudo-op.h"
#include "ast.h"

extern int scanner_peek(void);
extern int scanner_peek2(void);
extern int scanner_advance(void);
extern void scanner_init(FILE* in);
extern void scanner_start_line(void);
extern void scanner_force_identifier(void);

/* ------- runtime-visible globals --------------------------- */

bool data_dir = false; /* => item in data segment */
bool text_dir = true;  /* => item in text segment */
/* parse_error_occurred / parse_errors_seen live in pseudo_op.c
   alongside the parse_error funnel that sets them; declared extern
   here for the parse_line tail. */
extern bool parse_error_occurred;
extern int parse_errors_seen;

/* Source file name, used by parse_error / parse_warn for messages.  Exposed
   via the accessor below so pseudo_op.c can read it without
   needing the static directly. */
static char* input_file_name = nullptr;
char* input_file_name_get(void) { return input_file_name; }
void set_input_file_name(char* name) { input_file_name = name; }

/* These globals control directive emission.  Local to this TU. */

static bool null_term;
static void (*store_op)(int);
static void (*store_fp_op)(double*);

/* ------- Forward declarations for items defined below ------ */

static void cons_label(label* l);
static bool auto_align; /* defined as static further down */

/* ------- Parser mode and AST state ------------------------- */

/* `parse_mode_` selects between two equivalent emit strategies:
   - PARSE_DIRECT: the parser calls action helpers (r_type_inst,
     store_word, ...) inline as each statement is parsed.  No AST
     is built.
   - PARSE_AST (default): the parser builds an AST during parse,
     then emit_ast walks it in source order calling the same action
     helpers.  Equivalent memory contents; the AST lets us inspect
     or transform the program between parse and emit.
   The `print_ast_only_` flag (set by -print-ast / -show-expansion /
   -print-ast-json) suppresses the emit pass entirely in AST mode
   so spim can dump the tree without committing anything to memory. */
static parse_mode_t parse_mode_ = PARSE_DIRECT;
static ast_node* current_file = nullptr;
static bool print_ast_after_parse = false;
static FILE* ast_print_out = nullptr;
static bool print_ast_only_ = false;

/* When false, AST mode runs in tee form: both the inline action
   helpers AND the AST-construction path fire.  Useful as a parity
   oracle when debugging — the listing output should be identical
   either way.  True is the production setting; the AST is the
   driver and the action helpers only fire from emit_ast. */
static bool defer_emit_to_emit_ast = true;

/* When AST mode is deferred, parse_factor stashes the FIRST
   unresolved label it sees in the current expression here so
   data_int_push can attach symbol info to the AST imm_expr.
   emit_ast then records the forward-reference use at the
   correct (emit-time) data PC.  In PARSE_DIRECT mode this slot
   is unused — record_data_uses_symbol fires inline from
   parse_factor with the right PC already. */
static label* expr_unresolved_label = nullptr;

/* When non-null, AST nodes built by the dispatch helpers go into this
   pseudo-op's child list instead of the file-level child list.
   parse_pseudo sets this around its expansion body so the AST keeps
   the structural "this is one pseudo-op that expanded to N
   instructions" relationship.  Nesting via saved_pseudo lets a
   pseudo-op that internally uses another expander (e.g. `li` with a
   big constant inside `la`) keep its outer wrapping. */
static ast_node* current_pseudo = nullptr;

/* Append `node` to the current scope: if a pseudo-op is being built,
   its child list; otherwise the file root. */
static void ast_append(ast_node* node) {
  if (node == nullptr) return;
  if (current_pseudo != nullptr) {
    ast_pseudo_append(current_pseudo, node);
  } else if (current_file != nullptr) {
    ast_file_append(current_file, node);
  }
}

void emit_ast(const ast_node* file);

void parser_set_mode(parse_mode_t mode) { parse_mode_ = mode; }
parse_mode_t parser_get_mode(void) { return parse_mode_; }

void parser_set_print_ast(bool on, FILE* out) {
  print_ast_after_parse = on;
  ast_print_out = (out != nullptr) ? out : stderr;
}

static bool show_expansion_after_parse = false;
static FILE* show_expansion_out = nullptr;

void parser_set_show_expansion(bool on, FILE* out) {
  show_expansion_after_parse = on;
  show_expansion_out = (out != nullptr) ? out : stderr;
}

static bool print_ast_json_after_parse = false;
static FILE* ast_json_out = nullptr;

void parser_set_print_ast_json(bool on, FILE* out) {
  print_ast_json_after_parse = on;
  ast_json_out = (out != nullptr) ? out : stderr;
}

/* Walk the AST and print just the AST_PSEUDO wrappers + their
   expanded children.  Used by -show-expansion. */
static void print_pseudo_walk(const ast_node* node, FILE* out);
static void print_pseudo_in_chain(const ast_node* head, FILE* out) {
  for (const ast_node* n = head; n != nullptr; n = n->next)
    print_pseudo_walk(n, out);
}
static void print_pseudo_walk(const ast_node* node, FILE* out) {
  if (node == nullptr) return;
  if (node->kind == AST_PSEUDO) {
    /* ast_print already knows how to render a pseudo node + its
       children.  Reuse it. */
    ast_print(node, out);
  } else if (node->kind == AST_FILE) {
    print_pseudo_in_chain(node->u.file.child, out);
  }
  /* Other node kinds are skipped — -show-expansion is a focused view. */
}

void parser_set_print_ast_only(bool on) { print_ast_only_ = on; }

bool parser_get_print_ast_only(void) { return print_ast_only_; }

/* Whether the inline action helpers should fire during parse.
   False when:
     - -print-ast / -show-expansion / -print-ast-json is set (we want
       the tree built but no side effects on the simulator); or
     - we're in AST mode with deferred emit (the default — parse
       builds the tree, then emit_ast drives the action calls). */
static inline bool should_emit(void) {
  if (print_ast_only_) return false;
  if (parse_mode_ == PARSE_AST && defer_emit_to_emit_ast) return false;
  return true;
}
static inline bool should_build_ast(void) { return parse_mode_ == PARSE_AST; }

/* Deep-copy an imm_expr so the AST can own its own copy independent
   of the one the action helper sees. */
static imm_expr* dup_imm(const imm_expr* e) {
  if (e == nullptr) return nullptr;
  imm_expr* c = (imm_expr*)xmalloc(sizeof(imm_expr));
  *c = *e;
  return c;
}

/* ----- instruction dispatch helpers ------------------------ */

void emit_r(int op, int rd, int rs, int rt) {
  if (should_emit()) r_type_inst(op, rd, rs, rt);
  if (should_build_ast()) ast_append(ast_make_inst_r(op, rd, rs, rt));
}

void emit_r_shift(int op, int rd, int rt, int shamt) {
  if (should_emit()) r_sh_type_inst(op, rd, rt, shamt);
  if (should_build_ast()) ast_append(ast_make_inst_r_shift(op, rd, rt, shamt));
}

/* emit_i: caller keeps ownership of imm (matches i_type_inst). */
void emit_i(int op, int rt, int rs, imm_expr* imm) {
  if (should_build_ast()) ast_append(ast_make_inst_i(op, rt, rs, dup_imm(imm)));
  if (should_emit()) i_type_inst(op, rt, rs, imm);
}

/* emit_i_free: caller transfers ownership (matches i_type_inst_free). */
void emit_i_free(int op, int rt, int rs, imm_expr* imm) {
  if (should_build_ast()) ast_append(ast_make_inst_i(op, rt, rs, dup_imm(imm)));
  if (should_emit())
    i_type_inst_free(op, rt, rs, imm);
  else
    free(imm); /* AST took a copy; original needs to go */
}

void emit_j(int op, imm_expr* target) {
  /* j_type_inst copies its arg; caller (parse_j) frees the
     original. AST-only mode dups for the AST and lets the caller's
     free still run. */
  if (should_build_ast()) ast_append(ast_make_inst_j(op, dup_imm(target)));
  if (should_emit()) j_type_inst(op, target);
}

void emit_fp_r(int op, int fd, int fs, int ft) {
  if (should_emit()) r_co_type_inst(op, fd, fs, ft);
  if (should_build_ast()) ast_append(ast_make_inst_fp_r(op, fd, fs, ft));
}

void emit_fp_compare(int op, int fs, int ft, int cc) {
  if (should_emit()) r_cond_type_inst(op, fs, ft, cc);
  if (should_build_ast()) ast_append(ast_make_inst_fp_compare(op, fs, ft, cc));
}

/* ----- data dispatch helpers ------------------------------- */

/* Buffered current-directive accumulator.  parse_dir_word / half / byte
   set the kind, then parse_expr_list collects values into this buffer;
   at the end of the list, finalize_data_int_node builds one AST node. */
static ast_kind data_int_kind = AST_DATA_WORD;
static imm_expr** data_int_buf = nullptr;
static int data_int_count = 0;
static int data_int_cap = 0;
/* Source line of the directive that opened the current accumulator.
   Captured at *_begin so the AST node reflects the directive's first
   line even if its value list spans onto the next line (`.word a,b,\n
   c,d`). */
static int data_directive_line = 0;

static double* data_fp_buf = nullptr;
static int data_fp_count = 0;
static int data_fp_cap = 0;

static void data_int_begin(ast_kind kind) {
  data_int_kind = kind;
  data_int_count = 0;
  data_directive_line = line_no;
  if (data_int_buf == nullptr) {
    data_int_cap = 8;
    data_int_buf = (imm_expr**)xmalloc(data_int_cap * sizeof(imm_expr*));
  }
}

static void data_int_push(int value) {
  if (data_int_count == data_int_cap) {
    data_int_cap *= 2;
    imm_expr** grow = (imm_expr**)xmalloc(data_int_cap * sizeof(imm_expr*));
    memcpy(grow, data_int_buf, data_int_count * sizeof(imm_expr*));
    free(data_int_buf);
    data_int_buf = grow;
  }
  /* If parse_factor stashed an unresolved label for this expression
     (deferred mode), wire it into the imm_expr so emit_ast can
     register the use against the correct PC. */
  const char* sym_name =
      expr_unresolved_label != nullptr ? expr_unresolved_label->name : nullptr;
  data_int_buf[data_int_count++] = make_imm_expr(value, (char*)sym_name, false);
  expr_unresolved_label = nullptr;
}

static void data_int_finalize(void) {
  if (!should_build_ast() || data_int_count == 0) {
    /* Free any unused literals built during accumulation. */
    for (int i = 0; i < data_int_count; i++) free(data_int_buf[i]);
    data_int_count = 0;
    return;
  }
  /* Hand off the array as-is; ast_node owns it now. */
  imm_expr** exprs = (imm_expr**)xmalloc(data_int_count * sizeof(imm_expr*));
  memcpy(exprs, data_int_buf, data_int_count * sizeof(imm_expr*));
  ast_node* node;
  switch (data_int_kind) {
    case AST_DATA_BYTE:
      node = ast_make_data_byte(data_int_count, exprs);
      break;
    case AST_DATA_HALF:
      node = ast_make_data_half(data_int_count, exprs);
      break;
    case AST_DATA_WORD:
      node = ast_make_data_word(data_int_count, exprs);
      break;
    default:
      node = ast_make_data_word(data_int_count, exprs);
      break;
  }
  node->source_line = data_directive_line;
  ast_append(node);
  data_int_count = 0;
}

static void data_fp_begin(ast_kind kind) {
  data_int_kind = kind; /* reuse the discriminant */
  data_fp_count = 0;
  data_directive_line = line_no;
  if (data_fp_buf == nullptr) {
    data_fp_cap = 8;
    data_fp_buf = (double*)xmalloc(data_fp_cap * sizeof(double));
  }
}

static void data_fp_push(double value) {
  if (data_fp_count == data_fp_cap) {
    data_fp_cap *= 2;
    double* grow = (double*)xmalloc(data_fp_cap * sizeof(double));
    memcpy(grow, data_fp_buf, data_fp_count * sizeof(double));
    free(data_fp_buf);
    data_fp_buf = grow;
  }
  data_fp_buf[data_fp_count++] = value;
}

static void data_fp_finalize(void) {
  if (!should_build_ast() || data_fp_count == 0) {
    data_fp_count = 0;
    return;
  }
  double* values = (double*)xmalloc(data_fp_count * sizeof(double));
  memcpy(values, data_fp_buf, data_fp_count * sizeof(double));
  ast_node* node = (data_int_kind == AST_DATA_FLOAT)
                       ? ast_make_data_float(data_fp_count, values)
                       : ast_make_data_double(data_fp_count, values);
  node->source_line = data_directive_line;
  ast_append(node);
  data_fp_count = 0;
}

static void emit_data_string(char* s, int len, bool null_term_in) {
  if (should_emit()) store_string(s, len, null_term_in);
  if (should_build_ast())
    ast_append(ast_make_data_string(s, len, null_term_in));
}

/* ----- label + directive dispatch helpers ------------------ */

static void emit_label_normal(const char* name, mem_addr addr) {
  if (should_emit()) {
    label* l = record_label((char*)name, addr, 0);
    cons_label(l);
  }
  if (should_build_ast()) ast_append(ast_make_label_normal(name));
}

static void emit_label_const(const char* name, int v) {
  if (should_emit()) {
    label* l = record_label((char*)name, (mem_addr)v, 1);
    l->const_flag = 1;
  }
  if (should_build_ast()) ast_append(ast_make_label_const(name, v));
}

static void emit_dir_globl(const char* name) {
  if (should_emit()) make_label_global((char*)name);
  if (should_build_ast()) ast_append(ast_make_dir_globl(name));
}

static void emit_dir_align(int n) {
  if (should_emit()) {
    if (text_dir)
      align_text(n);
    else
      align_data(n);
  }
  if (should_build_ast()) ast_append(ast_make_dir_align(n));
}

static void emit_dir_space(int v) {
  if (should_emit()) increment_data_pc(v);
  if (should_build_ast()) ast_append(ast_make_dir_space(v));
}

static void emit_dir_extern(const char* sym, int sz) {
  if (should_emit()) {
    make_label_global((char*)sym);
    if (lookup_label((char*)sym)->addr == 0) {
      record_label((char*)sym, current_data_pc(), 1);
    }
    increment_data_pc(sz);
  }
  if (should_build_ast()) ast_append(ast_make_dir_extern(sym, sz));
}

static void emit_dir_comm(const char* sym, int sz) {
  if (should_emit()) {
    align_data(2);
    if (lookup_label((char*)sym)->addr == 0) {
      record_label((char*)sym, current_data_pc(), 1);
    }
    increment_data_pc(sz);
  }
  if (should_build_ast()) ast_append(ast_make_dir_comm(sym, sz));
}

static void emit_dir_seg(ast_kind kind, bool kernel, bool has_addr,
                         mem_addr addr) {
  /* Update parser-state flags unconditionally so subsequent directive
     parsing (e.g. the .asciiz text/data check) sees the right segment
     even in -print-ast-only mode. */
  if (kind == AST_DIR_TEXT || kind == AST_DIR_KTEXT) {
    data_dir = false;
    text_dir = true;
  } else {
    data_dir = true;
    text_dir = false;
    auto_align = true;
  }
  /* Action-helper side effects (only when emitting). */
  if (should_emit()) {
    if (kind == AST_DIR_TEXT || kind == AST_DIR_KTEXT) {
      user_kernel_text_segment(kernel);
      if (has_addr) set_text_pc(addr);
    } else {
      user_kernel_data_segment(kernel);
      enable_data_alignment();
      if (has_addr) set_data_pc(addr);
    }
  }
  if (should_build_ast()) {
    ast_node* n;
    switch (kind) {
      case AST_DIR_TEXT:
        n = ast_make_dir_text(has_addr, addr);
        break;
      case AST_DIR_DATA:
        n = ast_make_dir_data(has_addr, addr);
        break;
      case AST_DIR_KTEXT:
        n = ast_make_dir_ktext(has_addr, addr);
        break;
      case AST_DIR_KDATA:
        n = ast_make_dir_kdata(has_addr, addr);
        break;
      default:
        n = ast_make_dir_data(has_addr, addr);
        break;
    }
    ast_append(n);
  }
}

/* Labels collected on the current line, flushed (resolved + freed)
   at line end.  The list persists across newlines until an ASM_CODE
   or ASM_DIRECTIVE clears it, so that `set_data_alignment` (called
   from inside the directive) can retroactively update label
   addresses via fix_current_label_address. */

typedef struct label_cell {
  label* label;
  struct label_cell* next;
} label_cell;

static label_cell* this_line_labels = nullptr;

/* Mirror of data.c's `enable_data_auto_alignment` static.
   Cleared by `.align 0`, set by `.data` / `.kdata`. */
static bool auto_align = true;

static void cons_label(label* l) {
  label_cell* c = (label_cell*)xmalloc(sizeof(label_cell));
  c->label = l;
  c->next = this_line_labels;
  this_line_labels = c;
}

/* Resolve and free. */
static void clear_labels(void) {
  while (this_line_labels != nullptr) {
    label_cell* next = this_line_labels->next;
    resolve_label_uses(this_line_labels->label);
    free(this_line_labels);
    this_line_labels = next;
  }
}

/* Retroactive label fix-up.  Called both internally (before
   alignment-emitting directives) and externally from data.c /
   instruction.c whenever `align_data` / `align_text` advances the PC for
   alignment, so any labels recorded on the current line follow
   the PC to its aligned position. */
void fix_current_label_address(mem_addr new_addr) {
  for (label_cell* c = this_line_labels; c != nullptr; c = c->next) {
    c->label->addr = new_addr;
  }
}
/* Internal call sites kept under the old name to minimise churn. */
#define fix_current_label_address fix_current_label_address

/* ---------------- error helpers ---------------- */

static void parse_error_at(const char* msg) {
  parse_error_occurred = true;
  parse_errors_seen += 1;
  error("spim: (parser) %s on line %d of file %s\n", msg, line_no,
        input_file_name ? input_file_name : "(stdin)");
}

/* Skip tokens up to (but NOT past) the next newline.  parse_line's
   tail consumes that newline; consuming it here would leave the
   outer loop pointing at the next line's first token and the
   newline check would mis-fire "Extra tokens after instruction"
   on every line that follows an ignored directive like `.set`. */
static void sync_to_nl(void) {
  while (scanner_peek() != TOK_NL && scanner_peek() != TOK_EOF) {
    scanner_advance();
  }
}

/* Consume a token and assert it matches `expected`; on
   mismatch, emit an error and DON'T consume. */
static bool expect(int expected, const char* what) {
  if (scanner_peek() == expected) {
    scanner_advance();
    return true;
  }
  parse_error_at(what);
  return false;
}

/* ---------------- operand parsers ---------------- */

/* TOK_REG → register number 0..31 */
static int parse_register(void) {
  if (scanner_peek() == TOK_REG) {
    scanner_advance();
    return scan_value.i;
  }
  parse_error_at("Expected register");
  return 0;
}

/* TOK_FP_REG → FP register number 0..31 */
static int parse_fp_register(void) {
  if (scanner_peek() == TOK_FP_REG) {
    scanner_advance();
    return scan_value.i;
  }
  parse_error_at("Expected FP register");
  return 0;
}

/* ABS_ADDR : TOK_INT | TOK_INT '+' TOK_INT | TOK_INT TOK_INT (negative sub) */
static int parse_abs_addr(void) {
  if (scanner_peek() != TOK_INT) {
    parse_error_at("Expected integer");
    return 0;
  }
  scanner_advance();
  int v = scan_value.i;
  if (scanner_peek() == '+') {
    scanner_advance();
    if (scanner_peek() != TOK_INT) {
      parse_error_at("Expected integer after '+'");
      return v;
    }
    scanner_advance();
    return v + scan_value.i;
  }
  /* TOK_INT TOK_INT (with the second negative) is the historical
     subtraction handling — see scanner-parser-inventory Part 8F. */
  if (scanner_peek() == TOK_INT) {
    /* peek the next value */
    scanner_advance();
    if (scan_value.i >= 0) parse_error_at("Syntax error");
    return v - (-scan_value.i);
  }
  return v;
}

/* FACTOR : TOK_INT | '(' EXPR ')' | ID.
   ID resolves to its label's address; forward references
   record a data-uses-symbol entry for later resolution. */
extern void record_data_uses_symbol(mem_addr location, label* sym);

static int parse_expr(void); /* forward */
static int parse_factor(void) {
  if (scanner_peek() == TOK_INT) {
    scanner_advance();
    return scan_value.i;
  }
  if (scanner_peek() == '(') {
    scanner_advance();
    int v = parse_expr();
    expect(')', "Expected ')'");
    return v;
  }
  if (scanner_peek() == TOK_ID) {
    scanner_advance();
    char* sym_name = (char*)scan_value.p;
    label* l = lookup_label(sym_name);
    if (l->addr == 0) {
      /* Forward reference.  In SDT mode record the use at parse time
         (current_data_pc is correct because emission is happening
         inline).  In AST-deferred mode current_data_pc isn't the
         right address yet — stash the label and let emit_ast record
         the use against the actual data PC. */
      if (should_emit()) {
        record_data_uses_symbol(current_data_pc(), l);
      } else if (expr_unresolved_label == nullptr) {
        expr_unresolved_label = l;
      }
      free(sym_name);
      return 0;
    }
    int addr = (int)l->addr;
    free(sym_name);
    return addr;
  }
  parse_error_at("Expected expression");
  return 0;
}

/* TRM : FACTOR ( ('*'|'/') FACTOR )* */
static int parse_term(void) {
  int v = parse_factor();
  while (scanner_peek() == '*' || scanner_peek() == '/') {
    int op = scanner_advance();
    int r = parse_factor();
    if (op == '*')
      v *= r;
    else
      v = (r == 0) ? 0 : v / r;
  }
  return v;
}

/* EXPR : TRM ( ('+'|'-') TRM )* */
static int parse_expr(void) {
  int v = parse_term();
  while (scanner_peek() == '+' || scanner_peek() == '-') {
    int op = scanner_advance();
    int r = parse_term();
    if (op == '+')
      v += r;
    else
      v -= r;
  }
  return v;
}

/* EXPRESSION: like EXPR but with the next-token force-identifier flag.
   Resets expr_unresolved_label so the side-channel only carries the
   symbol of THIS expression, not a leftover from a previous one. */
static int parse_expression(void) {
  scanner_force_identifier();
  expr_unresolved_label = nullptr;
  return parse_expr();
}

/* IMM32 : ABS_ADDR | '(' ABS_ADDR ')' '>' '>' TOK_INT
        | TOK_ID | TOK_ID '+' ABS_ADDR | TOK_ID '-' ABS_ADDR */
static imm_expr* parse_imm32(void) {
  scanner_force_identifier();
  /* TOK_ID lookahead: maybe followed by + or - */
  if (scanner_peek() == TOK_ID) {
    scanner_advance();
    char* sym = (char*)scan_value.p;
    if (scanner_peek() == '+') {
      scanner_advance();
      int off = parse_abs_addr();
      imm_expr* r = make_imm_expr(off, sym, false);
      /* make_imm_expr copies sym internally, so free our local. */
      free(sym);
      return r;
    }
    if (scanner_peek() == '-') {
      scanner_advance();
      int off = parse_abs_addr();
      imm_expr* r = make_imm_expr(-off, sym, false);
      free(sym);
      return r;
    }
    return make_imm_expr(0, sym, false);
    /* sym ownership passes to make_imm_expr; don't free */
  }
  if (scanner_peek() == '(') {
    /* '(' ABS_ADDR ')' '>' '>' TOK_INT */
    scanner_advance();
    int v = parse_abs_addr();
    expect(')', "Expected ')'");
    expect('>', "Expected '>'");
    expect('>', "Expected '>'");
    if (scanner_peek() != TOK_INT) {
      parse_error_at("Expected integer after '>>'");
      return make_imm_expr(0, nullptr, false);
    }
    scanner_advance();
    int sh = scan_value.i;
    return make_imm_expr(v >> sh, nullptr, false);
  }
  /* ABS_ADDR */
  int v = parse_abs_addr();
  return make_imm_expr(v, nullptr, false);
}

/* IMM16, UIMM16 — IMM32 plus range check */
static imm_expr* parse_imm16(void) {
  imm_expr* e = parse_imm32();
  check_imm_range(e, IMM_MIN, IMM_MAX);
  return e;
}

static imm_expr* parse_uimm16(void) {
  imm_expr* e = parse_imm32();
  check_uimm_range(e, UIMM_MIN, UIMM_MAX);
  return e;
}

/* LABEL — like ID but produces a PC-relative imm_expr suitable
   for a branch target.  Matches exactly:
   the initial offset is `-current_text_pc()`, which gets
   overwritten with `-pc` in resolve_a_label_sub at fix-up
   time, encoding the (target - branch_pc) displacement that
   MIPS branches require. */
static imm_expr* parse_label(void) {
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected label");
    return make_imm_expr(0, nullptr, true);
  }
  scanner_advance();
  char* sym = (char*)scan_value.p;
  imm_expr* r = make_imm_expr(-(int)current_text_pc(), sym, true);
  /* sym ownership: the original LABEL action does NOT free name
     (because make_imm_expr stores it).  Mirror that. */
  return r;
}

/* ADDR — the 9-alternative address production.  This is where
   the 25 shift-reduce conflicts collapse into one explicit
   lookahead. */
static addr_expr* parse_address(void) {
  scanner_force_identifier();

  /* '(' REGISTER ')' */
  if (scanner_peek() == '(') {
    scanner_advance();
    int reg = parse_register();
    expect(')', "Expected ')' after register");
    return make_addr_expr(0, nullptr, reg);
  }

  /* ABS_ADDR [ '(' REGISTER ')' ] */
  if (scanner_peek() == TOK_INT) {
    int imm = parse_abs_addr();
    if (scanner_peek() == '+' && scanner_peek2() == TOK_ID) {
      /* ABS_ADDR '+' ID */
      scanner_advance(); /* + */
      scanner_force_identifier();
      scanner_advance();
      char* sym = (char*)scan_value.p;
      addr_expr* r = make_addr_expr(imm, sym, 0);
      return r;
    }
    if (scanner_peek() == '(') {
      scanner_advance();
      int reg = parse_register();
      expect(')', "Expected ')'");
      return make_addr_expr(imm, nullptr, reg);
    }
    return make_addr_expr(imm, nullptr, 0);
  }

  /* TOK_ID [ '+' ABS_ADDR | '-' ABS_ADDR ] [ '(' REGISTER ')' ] */
  if (scanner_peek() == TOK_ID) {
    scanner_advance();
    char* sym = (char*)scan_value.p;
    int off = 0;
    if (scanner_peek() == '+') {
      scanner_advance();
      off = parse_abs_addr();
    } else if (scanner_peek() == '-') {
      scanner_advance();
      off = -parse_abs_addr();
    }
    int reg = 0;
    if (scanner_peek() == '(') {
      scanner_advance();
      reg = parse_register();
      expect(')', "Expected ')'");
    }
    addr_expr* r = make_addr_expr(off, sym, reg);
    free(sym);
    return r;
  }

  parse_error_at("Expected address");
  return make_addr_expr(0, nullptr, 0);
}

/* ---------------- instruction parsers ---------------- */

/* Return true if `op` is a BINARYI_OPS opcode (has both reg-reg
   and reg-imm forms — add, addu, and, or, xor, slt, sltu).
   */
static bool op_has_imm_form(int op) {
  switch (op) {
    case TOK_ADD_OPCODE:
    case TOK_ADDU_OPCODE:
    case TOK_AND_OPCODE:
    case TOK_OR_OPCODE:
    case TOK_XOR_OPCODE:
    case TOK_SLT_OPCODE:
    case TOK_SLTU_OPCODE:
      return true;
    default:
      return false;
  }
}

/* R3_TYPE_INST: <op> DEST, SRC1, SRC2.  For BINARYI_OPS opcodes,
   also accepts <op> DEST, SRC1, IMM and <op> DEST, IMM (see
SUB_OPS (sub, subu) similarly accept an
   immediate, converted to addi/addiu with a negated value
*/
extern int32_t eval_imm_expr(imm_expr* expr);

static void parse_r3(int op) {
  int rd = parse_register();
  if (op == TOK_CLO_OPCODE || op == TOK_CLZ_OPCODE) {
    /* COUNT_LEADING_OPS DEST SRC1 — RT must equal RD.
     */
    int rs = parse_register();
    emit_r(op, rd, rs, rd);
    return;
  }
  if (op == TOK_MUL_OPCODE) {
    /* MULT_OPS3 DEST SRC1 (SRC2|IMM).  3-reg or 3-with-imm. Imm form uses $at +
     * ori. */
    int rs = parse_register();
    if (scanner_peek() == TOK_REG) {
      int rt = parse_register();
      emit_r(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      emit_i_free(TOK_ORI_OPCODE, 1, 0, imm);
      emit_r(op, rd, rs, 1);
    }
    return;
  }
  if (op == TOK_SUB_OPCODE || op == TOK_SUBU_OPCODE) {
    /* SUB_OPS: 3-reg, 3-with-imm, or 2-with-imm.  Convert imm
       form to addi/addiu with negated value. */
    if (scanner_peek() != TOK_REG) {
      imm_expr* imm = parse_imm32();
      int val = eval_imm_expr(imm);
      emit_i(op == TOK_SUB_OPCODE ? TOK_ADDI_OPCODE : TOK_ADDIU_OPCODE, rd, rd,
             make_imm_expr(-val, nullptr, false));
      free(imm);
      return;
    }
    int rs = parse_register();
    if (scanner_peek() == TOK_REG) {
      int rt = parse_register();
      emit_r(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      int val = eval_imm_expr(imm);
      emit_i(op == TOK_SUB_OPCODE ? TOK_ADDI_OPCODE : TOK_ADDIU_OPCODE, rd, rs,
             make_imm_expr(-val, nullptr, false));
      free(imm);
    }
    return;
  }
  if (op_has_imm_form(op)) {
    /* Two-operand form: <op> DEST, IMM */
    if (scanner_peek() != TOK_REG) {
      imm_expr* imm = parse_imm32();
      emit_i_free(op_to_imm_op(op), rd, rd, imm);
      return;
    }
    int rs = parse_register();
    /* Three-operand form: <op> DEST, SRC1, SRC2 (reg) or
       <op> DEST, SRC1, IMM (immediate) */
    if (scanner_peek() == TOK_REG) {
      int rt = parse_register();
      emit_r(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      emit_i_free(op_to_imm_op(op), rd, rs, imm);
    }
    return;
  }
  /* Plain R3 (no immediate form): three registers */
  int rs = parse_register();
  int rt = parse_register();
  emit_r(op, rd, rs, rt);
}

/* R2sh_TYPE_INST: <op> DEST, SRC1, SHAMT (immediate 0..31).
   ssnop is a special no-operand case emitting `sll $0, $0, 1`. */
static void parse_r2sh(int op) {
  if (op == TOK_SSNOP_OPCODE) {
    emit_r_shift(TOK_SLL_OPCODE, 0, 0, 1);
    return;
  }
  int rd = parse_register();
  int rs = parse_register(); /* this is rt in MIPS shift encoding */
  if (scanner_peek() != TOK_INT) {
    parse_error_at("Expected integer shift amount");
    return;
  }
  scanner_advance();
  int sh = scan_value.i;
  emit_r_shift(op, rd, rs, sh);
}

/* R1s_TYPE_INST: <op> SRC1 (jr) */
static void parse_r1s(int op) {
  int rs = parse_register();
  emit_r(op, 0, rs, 0);
}

/* I2_TYPE_INST: <op> DEST, SRC1, IMM16
   Also accepts the 2-operand shorthand `<op> DEST, IMM16` for
   andi/ori/xori where the missing SRC1 defaults to DEST — exception handlers
   use `ori $k0, 0x1`. */
static void parse_i2(int op) {
  int rt = parse_register();
  int rs;
  if (scanner_peek() == TOK_REG) {
    rs = parse_register();
  } else {
    rs = rt;
  }
  /* andi/ori/xori take unsigned; addi/addiu/slti signed.  The
     range check happens inside parse_imm16/parse_uimm16. */
  imm_expr* imm;
  if (op == TOK_ANDI_OPCODE || op == TOK_ORI_OPCODE || op == TOK_XORI_OPCODE) {
    imm = parse_uimm16();
  } else {
    imm = parse_imm16();
  }
  emit_i_free(op, rt, rs, imm);
}

/* I1t_TYPE_INST: <op> DEST, UIMM16 (lui) */
static void parse_i1t(int op) {
  int rt = parse_register();
  imm_expr* imm = parse_uimm16();
  emit_i_free(op, rt, 0, imm);
}

/* I2a_TYPE_INST: <op> DEST, ADDRESS — loads and stores */
static void parse_i2a(int op) {
  int rt = parse_register();
  addr_expr* addr = parse_address();
  emit_i(op, rt, addr_expr_reg(addr), addr_expr_imm(addr));
  /* Bison frees the addr_expr's inner imm + the addr_expr; do the
     same.  spim runtime exposes free_imm_expr but not
     free_addr_expr — see instruction.h.  Use free() since that's what */
  free(addr_expr_imm(addr));
  free(addr);
}

/* B2_TYPE_INST: <op> SRC1, SRC2, LABEL (beq, bne) — reg/reg form.
   Also <op> SRC1, IMM, LABEL — reg/imm form
   which uses $at + ori to materialize the immediate, then beq/bne. */
static void parse_b2(int op) {
  int src1 = parse_register();
  if (scanner_peek() == TOK_REG) {
    int src2 = parse_register();
    imm_expr* target = parse_label();
    emit_i_free(op, src2, src1, target);
  } else {
    /* Immediate form */
    imm_expr* imm = parse_imm32();
    imm_expr* target = parse_label();
    extern bool is_zero_imm(imm_expr * expr);
    if (is_zero_imm(imm)) {
      /* Special case: comparing against literal 0 → use $0 directly */
      emit_i(op, src1, 0, target);
    } else {
      /* Use $at: ori $at, $0, imm; <op> src1, $at, target */
      emit_i(TOK_ORI_OPCODE, 1, 0, imm);
      emit_i(op, src1, 1, target);
    }
    free(imm);
    free(target);
  }
}

/* B1_TYPE_INST: <op> SRC1, LABEL (bgez, bltz, etc.) */
static void parse_b1(int op) {
  int rs = parse_register();
  imm_expr* target = parse_label();
  emit_i_free(op, 0, rs, target);
}

/* J_TYPE_INST: <op> LABEL  OR  <op> SRC1 (j/jal/jr/jalr).
   Matches.  J_OPS covers j, jal, jr, jalr;
   with a label argument we emit j_type_inst with the right
   absolute-jump opcode; with a register argument we emit
   r_type_inst with the indirect-jump opcode. */
static void parse_j(int op) {
  if (scanner_peek() == TOK_REG) {
    /* J_OPS SRC1  or  J_OPS DEST SRC1 — register-indirect jump.
     */
    int r1 = parse_register();
    if (scanner_peek() == TOK_REG) {
      /* DEST SRC1 form */
      int r2 = parse_register();
      if (op == TOK_J_OPCODE || op == TOK_JR_OPCODE)
        emit_r(TOK_JR_OPCODE, 0, r2, 0);
      else if (op == TOK_JAL_OPCODE || op == TOK_JALR_OPCODE)
        emit_r(TOK_JALR_OPCODE, r1, r2, 0);
      return;
    }
    /* SRC1-only form */
    if (op == TOK_J_OPCODE || op == TOK_JR_OPCODE)
      emit_r(TOK_JR_OPCODE, 0, r1, 0);
    else if (op == TOK_JAL_OPCODE || op == TOK_JALR_OPCODE)
      emit_r(TOK_JALR_OPCODE, 31, r1, 0);
    return;
  }
  /* J_OPS LABEL — absolute-target jump */
  imm_expr* target = parse_label();
  if (op == TOK_J_OPCODE || op == TOK_JR_OPCODE)
    emit_j(TOK_J_OPCODE, target);
  else if (op == TOK_JAL_OPCODE || op == TOK_JALR_OPCODE)
    emit_j(TOK_JAL_OPCODE, target);
  free(target);
}

/* NOARG_TYPE_INST: <op>  (syscall) or <op> IMM (break N, sync N).
   See. */
static void parse_noarg(int op) {
  if (scanner_peek() == TOK_INT) {
    /* break N or sync N — encode the int in the rd slot.
       Bison: emit_r(op, $2.i, 0, 0). */
    scanner_advance();
    int n = scan_value.i;
    if (op == TOK_BREAK_OPCODE && n == 1) {
      parse_error_at("Breakpoint 1 is reserved for debugger");
    }
    emit_r(op, n, 0, 0);
  } else {
    emit_r(op, 0, 0, 0);
  }
}

/* ---------------- directive parsers ---------------- */

static void parse_dir_data(bool kernel) {
  bool has_addr = false;
  mem_addr addr = 0;
  if (scanner_peek() == TOK_INT) {
    scanner_advance();
    has_addr = true;
    addr = (mem_addr)scan_value.i;
  }
  emit_dir_seg(kernel ? AST_DIR_KDATA : AST_DIR_DATA, kernel, has_addr, addr);
}

static void parse_dir_text(bool kernel) {
  bool has_addr = false;
  mem_addr addr = 0;
  if (scanner_peek() == TOK_INT) {
    scanner_advance();
    has_addr = true;
    addr = (mem_addr)scan_value.i;
  }
  emit_dir_seg(kernel ? AST_DIR_KTEXT : AST_DIR_TEXT, kernel, has_addr, addr);
}

static void parse_dir_globl(void) {
  scanner_force_identifier();
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected identifier after .globl");
    return;
  }
  scanner_advance();
  char* sym = (char*)scan_value.p;
  emit_dir_globl(sym);
  free(sym);
}

static void parse_dir_align(void) {
  int v = parse_expression();
  if (v == 0) auto_align = false; /* mirror data.c's flag */
  emit_dir_align(v);
}

static void parse_dir_extern(void) {
  scanner_force_identifier();
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected ID");
    return;
  }
  scanner_advance();
  char* sym = (char*)scan_value.p;
  int sz = parse_expression();
  emit_dir_extern(sym, sz);
  free(sym);
}

static void parse_dir_comm(void) {
  scanner_force_identifier();
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected ID");
    return;
  }
  scanner_advance();
  char* sym = (char*)scan_value.p;
  int sz = parse_expression();
  emit_dir_comm(sym, sz);
  free(sym);
}

static void parse_dir_space(void) {
  int v = parse_expression();
  if (text_dir) {
    parse_error_at("Can't put data in text segment");
    return;
  }
  emit_dir_space(v);
}

/* EXPR_LST: emit each expression value via store_op (SDT) and/or
   collect into the AST data accumulator. */
static void parse_expr_list(void) {
  for (;;) {
    int v = parse_expression();
    if (should_emit()) store_op(v);
    if (should_build_ast()) data_int_push(v);
    /* commas are skipped by the scanner; same with whitespace */
    if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) break;
    /* Otherwise continue to next expression */
  }
  if (should_build_ast()) data_int_finalize();
}

/* Pre-fix any labels currently on this_line_labels to the
   address they'll occupy AFTER `set_data_alignment(N)` aligns
   the data PC.  `align_data` calls `fix_current_label_address` to align
   `this_line_labels`; we maintain our own and pre-fix here. Respects the same
   auto-align gate that data.c uses. */
static void align_labels_to(int alignment) {
  if (!data_dir || !auto_align || this_line_labels == nullptr) return;
  mem_addr cur = current_data_pc();
  mem_addr aligned = (cur + (1u << alignment) - 1) & (~0u << alignment);
  fix_current_label_address(aligned);
}

/* FP_EXPR_LST: consume TOK_FP tokens, emit via store_fp_op (SDT)
   and/or collect into the AST data accumulator. */
static void parse_fp_expr_list(void) {
  for (;;) {
    if (scanner_peek() != TOK_FP) {
      parse_error_at("Expected floating-point literal");
      return;
    }
    scanner_advance();
    double* val = (double*)scan_value.p;
    if (should_emit()) store_fp_op(val);
    if (should_build_ast()) data_fp_push(*val);
    if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) break;
  }
  if (should_build_ast()) data_fp_finalize();
}

static void parse_dir_float(void) {
  align_labels_to(2);
  store_fp_op = store_float;
  if (data_dir) set_data_alignment(2);
  data_fp_begin(AST_DATA_FLOAT);
  parse_fp_expr_list();
}

static void parse_dir_double(void) {
  align_labels_to(3);
  store_fp_op = store_double;
  if (data_dir) set_data_alignment(3);
  data_fp_begin(AST_DATA_DOUBLE);
  parse_fp_expr_list();
}

static void parse_dir_word(void) {
  align_labels_to(2);
  store_op = store_word;
  if (data_dir) set_data_alignment(2);
  data_int_begin(AST_DATA_WORD);
  parse_expr_list();
}
static void parse_dir_half(void) {
  align_labels_to(1);
  store_op = store_half;
  if (data_dir) set_data_alignment(1);
  data_int_begin(AST_DATA_HALF);
  parse_expr_list();
}
static void parse_dir_byte(void) {
  store_op = store_byte;
  data_int_begin(AST_DATA_BYTE);
  parse_expr_list();
}

/* STR_LST: emit each string via store_string */
static void parse_string_list(void) {
  for (;;) {
    if (scanner_peek() != TOK_STR) {
      parse_error_at("Expected string literal");
      return;
    }
    scanner_advance();
    char* s = (char*)scan_value.p;
    int len = (int)strlen(s);
    if (text_dir) {
      parse_error_at("Can't put data in text segment");
    } else {
      emit_data_string(s, len, null_term);
    }
    free(s);
    if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) break;
  }
}

static void parse_dir_ascii(void) {
  null_term = false;
  parse_string_list();
}
static void parse_dir_asciiz(void) {
  null_term = true;
  parse_string_list();
}

/* ---------------- top-level dispatch ---------------- */

/* Look up an opcode token's operand-shape type tag.

   Built from the X-macro list in op.h: each OP(name, sym, type, enc)
   row expands here to {sym, type}, dropping the name and encoding
   columns.  See op.h's top-of-file comment for the X-macro pattern. */

typedef struct {
  int op;
  op_type type;
} op_type_entry;

static op_type_entry op_type_table[] = {
#define OP(NAME, OPCODE, TYPE, R_OPCODE) {OPCODE, TYPE},
#include "opcodes.h"
};

/* Returns -1 when `op` isn't in the table.  Caller switches treat the
   missing case via `default:` (parse_error_at).  Return type is `int`
   so the -1 sentinel is representable; case labels in the caller's
   switch compare against the typed `op_type` enumerators via the
   usual int/enum conversions. */
static int find_op_type(int op) {
  /* Linear scan is fine — table is small relative to anything
     else this parser does per source line. */
  for (size_t i = 0; i < sizeof(op_type_table) / sizeof(op_type_entry); i++) {
    if (op_type_table[i].op == op) return op_type_table[i].type;
  }
  return -1;
}

/* Pseudo-op dispatcher.  Handled separately from the TYPE
   switch because pseudo-ops share TYPE=PSEUDO_OP but have
   different operand shapes. */
/* Pseudo-op expander.  Each pseudo-op expands into one or more real
   instructions via the emit_* dispatch helpers.  In AST mode this
   function wraps the expansion in an AST_PSEUDO node so the listing
   and -show-expansion output keep the structural pseudo→expansion
   relationship.  do_parse_pseudo holds the per-op switch — the
   outer parse_pseudo handles the wrapping. */
static void do_parse_pseudo(int op);

static void parse_pseudo(int op) {
  ast_node* pseudo_node = nullptr;
  ast_node* saved_pseudo = nullptr;
  int saved_line = line_no;
  if (should_build_ast()) {
    pseudo_node = ast_make_pseudo(op_token_name(op));
    pseudo_node->source_line = saved_line;
    saved_pseudo = current_pseudo;
    current_pseudo = pseudo_node;
  }

  do_parse_pseudo(op);

  if (should_build_ast()) {
    current_pseudo = saved_pseudo;
    ast_append(pseudo_node);
  }
}

static void do_parse_pseudo(int op) {
  switch (op) {
    case TOK_LI_PSEUDO_OP: {
      /* li DEST, IMM32  →  ori DEST, $0, imm */
      int rt = parse_register();
      imm_expr* imm = parse_imm32();
      emit_i_free(TOK_ORI_OPCODE, rt, 0, imm);
      break;
    }
    case TOK_MOVE_PSEUDO_OP: {
      /* move DEST, SRC1  →  addu DEST, $0, SRC1 */
      int rd = parse_register();
      int rs = parse_register();
      emit_r(TOK_ADDU_OPCODE, rd, 0, rs);
      break;
    }
    case TOK_NEG_PSEUDO_OP: {
      /* neg DEST, SRC1  →  sub DEST, $0, SRC1 */
      int rd = parse_register();
      int rs = parse_register();
      emit_r(TOK_SUB_OPCODE, rd, 0, rs);
      break;
    }
    case TOK_NEGU_PSEUDO_OP: {
      /* negu DEST, SRC1  →  subu DEST, $0, SRC1 */
      int rd = parse_register();
      int rs = parse_register();
      emit_r(TOK_SUBU_OPCODE, rd, 0, rs);
      break;
    }
    case TOK_NOT_PSEUDO_OP: {
      /* not DEST, SRC1  →  nor DEST, SRC1, $0 */
      int rd = parse_register();
      int rs = parse_register();
      emit_r(TOK_NOR_OPCODE, rd, rs, 0);
      break;
    }
    case TOK_ROR_PSEUDO_OP:
    case TOK_ROL_PSEUDO_OP: {
      /* Rotate right / left.  Two forms:
           DEST SRC1 SRC2   — register rotate
           DEST SRC1 IMM    — constant rotate */
      int rd = parse_register();
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        /* ROR: subu $at,$0,rt; sllv $at,$at,rs; srlv rd,rt,rs; or rd,rd,$at
           ROL: subu $at,$0,rt; srlv $at,$at,rs; sllv rd,rt,rs; or rd,rd,$at */
        emit_r(TOK_SUBU_OPCODE, 1, 0, rt);
        if (op == TOK_ROR_PSEUDO_OP) {
          emit_r(TOK_SLLV_OPCODE, 1, 1, rs);
          emit_r(TOK_SRLV_OPCODE, rd, rt, rs);
        } else {
          emit_r(TOK_SRLV_OPCODE, 1, 1, rs);
          emit_r(TOK_SLLV_OPCODE, rd, rt, rs);
        }
        emit_r(TOK_OR_OPCODE, rd, rd, 1);
      } else {
        imm_expr* imm = parse_imm32();
        long dist = eval_imm_expr(imm);
        check_imm_range(imm, 0, 31);
        if (op == TOK_ROR_PSEUDO_OP) {
          emit_r_shift(TOK_SLL_OPCODE, 1, rs, -dist);
          emit_r_shift(TOK_SRL_OPCODE, rd, rs, dist);
        } else {
          emit_r_shift(TOK_SRL_OPCODE, 1, rs, -dist);
          emit_r_shift(TOK_SLL_OPCODE, rd, rs, dist);
        }
        emit_r(TOK_OR_OPCODE, rd, rd, 1);
        free(imm);
      }
      break;
    }
    case TOK_SLE_PSEUDO_OP:
    case TOK_SLEU_PSEUDO_OP:
    case TOK_SGT_PSEUDO_OP:
    case TOK_SGTU_PSEUDO_OP:
    case TOK_SGE_PSEUDO_OP:
    case TOK_SGEU_PSEUDO_OP:
    case TOK_SEQ_PSEUDO_OP:
    case TOK_SNE_PSEUDO_OP: {
      /* SET_*_POPS: DEST SRC1 (SRC2|IMM32).. IMM form uses $at + ori when imm
       * != 0. */
      int rd = parse_register();
      int rs = parse_register();
      int rt;
      if (scanner_peek() == TOK_REG) {
        rt = parse_register();
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr*);
        if (!is_zero_imm(imm)) {
          emit_i(TOK_ORI_OPCODE, 1, 0, imm);
          rt = 1;
        } else {
          rt = 0;
        }
        free(imm);
      }
      switch (op) {
        case TOK_SLE_PSEUDO_OP:
        case TOK_SLEU_PSEUDO_OP:
          set_le_inst(op, rd, rs, rt);
          break;
        case TOK_SGT_PSEUDO_OP:
        case TOK_SGTU_PSEUDO_OP:
          set_gt_inst(op, rd, rs, rt);
          break;
        case TOK_SGE_PSEUDO_OP:
        case TOK_SGEU_PSEUDO_OP:
          set_ge_inst(op, rd, rs, rt);
          break;
        case TOK_SEQ_PSEUDO_OP:
        case TOK_SNE_PSEUDO_OP:
          set_eq_inst(op, rd, rs, rt);
          break;
      }
      break;
    }
    case TOK_ABS_PSEUDO_OP: {
      /* abs DEST, SRC1: if DEST != SRC1,
         first move; then bgez SRC1 +3; nop; sub DEST, $0, SRC1. */
      int rd = parse_register();
      int rs = parse_register();
      if (rd != rs) {
        emit_r(TOK_ADDU_OPCODE, rd, 0, rs);
      }
      emit_i_free(TOK_BGEZ_OPCODE, 0, rs, branch_offset(3));
      nop_inst();
      emit_r(TOK_SUB_OPCODE, rd, 0, rs);
      break;
    }
    case TOK_NOP_PSEUDO_OP: {
      nop_inst();
      break;
    }
    case TOK_REM_PSEUDO_OP:
    case TOK_REMU_PSEUDO_OP: {
      /* rem/remu — DIV_POPS pseudo, 3-operand form only.
       */
      int rd = parse_register();
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        div_inst(op, rd, rs, rt, 0);
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr * expr);
        if (is_zero_imm(imm)) {
          parse_error_at("Divide by zero");
        } else {
          emit_i_free(TOK_ORI_OPCODE, 1, 0, imm);
          div_inst(op, rd, rs, 1, 1);
        }
      }
      break;
    }
    case TOK_MULO_PSEUDO_OP:
    case TOK_MULOU_PSEUDO_OP: {
      /* mulo/mulou — MUL_POPS, 3-operand form.. */
      int rd = parse_register();
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        mult_inst(op, rd, rs, rt);
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr * expr);
        if (is_zero_imm(imm)) {
          /* Optimize: n * 0 == 0 */
          emit_i_free(TOK_ORI_OPCODE, rd, 0, imm);
        } else {
          emit_i_free(TOK_ORI_OPCODE, 1, 0, imm);
          mult_inst(op, rd, rs, 1);
        }
      }
      break;
    }
    case TOK_MFC1_D_PSEUDO_OP: {
      /* mfc1.d REG COP_REG  →  two mfc1 instructions
       */
      int reg = parse_register();
      int copreg;
      if (scanner_peek() == TOK_FP_REG)
        copreg = parse_fp_register();
      else
        copreg = parse_register();
      emit_fp_r(TOK_MFC1_OPCODE, 0, copreg, reg);
      emit_fp_r(TOK_MFC1_OPCODE, 0, copreg + 1, reg + 1);
      break;
    }
    case TOK_MTC1_D_PSEUDO_OP: {
      /* mtc1.d REG COP_REG  →  two mtc1 instructions
       */
      int reg = parse_register();
      int copreg;
      if (scanner_peek() == TOK_FP_REG)
        copreg = parse_fp_register();
      else
        copreg = parse_register();
      emit_fp_r(TOK_MTC1_OPCODE, 0, copreg, reg);
      emit_fp_r(TOK_MTC1_OPCODE, 0, copreg + 1, reg + 1);
      break;
    }
    case TOK_LI_D_PSEUDO_OP: {
      /* li.d F_DEST <TOK_FP>  →  load double-precision constant
         via two mtc1. */
      int fd = parse_fp_register();
      if (scanner_peek() != TOK_FP) {
        parse_error_at("Expected FP literal");
        break;
      }
      scanner_advance();
      int* x = (int*)scan_value.p;
      emit_i(TOK_ORI_OPCODE, 1, 0, const_imm_expr(*x));
      emit_fp_r(TOK_MTC1_OPCODE, 0, fd, 1);
      emit_i(TOK_ORI_OPCODE, 1, 0, const_imm_expr(*(x + 1)));
      emit_fp_r(TOK_MTC1_OPCODE, 0, fd + 1, 1);
      break;
    }
    case TOK_LI_S_PSEUDO_OP: {
      /* li.s F_DEST <TOK_FP>  →  single-precision const via one mtc1
       */
      int fd = parse_fp_register();
      if (scanner_peek() != TOK_FP) {
        parse_error_at("Expected FP literal");
        break;
      }
      scanner_advance();
      float fval = (float)*((double*)scan_value.p);
      int* y = (int*)&fval;
      emit_i(TOK_ORI_OPCODE, 1, 0, const_imm_expr(*y));
      emit_fp_r(TOK_MTC1_OPCODE, 0, fd, 1);
      break;
    }
    case TOK_L_D_PSEUDO_OP: {
      /* l.d F_DEST ADDRESS  →  ldc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_LDC1_OPCODE, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_L_S_PSEUDO_OP: {
      /* l.s F_DEST ADDRESS  →  lwc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_LWC1_OPCODE, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_S_D_PSEUDO_OP: {
      /* s.d F_SRC1 ADDRESS  →  sdc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_SDC1_OPCODE, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_S_S_PSEUDO_OP: {
      /* s.s F_SRC1 ADDRESS  →  swc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_SWC1_OPCODE, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_LD_PSEUDO_OP: {
      /* ld DEST ADDRESS  — load doubleword pseudo, expands to
         two lw instructions */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_LW_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      emit_i_free(TOK_LW_OPCODE, rt + 1, addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 4));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_SD_PSEUDO_OP: {
      /* sd SRC1 ADDRESS — store doubleword pseudo. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_SW_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      emit_i_free(TOK_SW_OPCODE, rt + 1, addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 4));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_ULW_PSEUDO_OP: {
      /* Unaligned load word. , little-endian
         path (the macOS/Linux x86 default).  Emits LWL+LWR with
         offset bumped by 3 on the LWL. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i_free(TOK_LWL_OPCODE, rt, addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 3));
      emit_i(TOK_LWR_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_ULH_PSEUDO_OP:
    case TOK_ULHU_PSEUDO_OP: {
      /* Unaligned load half (signed/unsigned). LE path. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i_free(op == TOK_ULH_PSEUDO_OP ? TOK_LB_OPCODE : TOK_LBU_OPCODE, rt,
                  addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 1));
      emit_i(TOK_LBU_OPCODE, 1, addr_expr_reg(addr), addr_expr_imm(addr));
      emit_r_shift(TOK_SLL_OPCODE, rt, rt, 8);
      emit_r(TOK_OR_OPCODE, rt, rt, 1);
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_USW_PSEUDO_OP: {
      /* Unaligned store word.  LE path. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i_free(TOK_SWL_OPCODE, rt, addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 3));
      emit_i(TOK_SWR_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_USH_PSEUDO_OP: {
      /* Unaligned store half.. ROL, store
         high byte, ROR. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      emit_i(TOK_SB_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      /* ROL SRC, SRC, 8 (via SLL+SRL+OR) */
      emit_r_shift(TOK_SLL_OPCODE, 1, rt, 24);
      emit_r_shift(TOK_SRL_OPCODE, rt, rt, 8);
      emit_r(TOK_OR_OPCODE, rt, rt, 1);
      emit_i_free(TOK_SB_OPCODE, rt, addr_expr_reg(addr),
                  incr_expr_offset(addr_expr_imm(addr), 1));
      /* ROR SRC, SRC, 8 (via SRL+SLL+OR) */
      emit_r_shift(TOK_SRL_OPCODE, 1, rt, 24);
      emit_r_shift(TOK_SLL_OPCODE, rt, rt, 8);
      emit_r(TOK_OR_OPCODE, rt, rt, 1);
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case TOK_LA_PSEUDO_OP: {
      /* la DEST, ADDRESS.  If ADDRESS has a
         base register, emit `addi DEST, base, offset`; else emit
         `ori DEST, $0, offset`. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      if (addr_expr_reg(addr)) {
        emit_i(TOK_ADDI_OPCODE, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      } else {
        emit_i(TOK_ORI_OPCODE, rt, 0, addr_expr_imm(addr));
      }
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }

    /* UNARY_BR_POPS: beqz, bnez SRC1 LABEL */
    case TOK_BEQZ_PSEUDO_OP:
    case TOK_BNEZ_PSEUDO_OP: {
      int rs = parse_register();
      imm_expr* target = parse_label();
      int real_op = (op == TOK_BEQZ_PSEUDO_OP) ? TOK_BEQ_OPCODE : TOK_BNE_OPCODE;
      emit_i_free(real_op, 0, rs, target);
      break;
    }

    /* B_OPS: b, bal LABEL */
    case TOK_B_PSEUDO_OP:
    case TOK_BAL_PSEUDO_OP: {
      imm_expr* target = parse_label();
      int real_op = (op == TOK_BAL_PSEUDO_OP) ? TOK_BGEZAL_OPCODE : TOK_BGEZ_OPCODE;
      emit_i_free(real_op, 0, 0, target);
      break;
    }

    /* BR_GT_POPS: bgt/bgtu SRC1 (SRC2|IMM) LABEL */
    case TOK_BGT_PSEUDO_OP:
    case TOK_BGTU_PSEUDO_OP: {
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        emit_r(op == TOK_BGT_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1, rt, rs);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
      } else {
        /* Immediate form — see */
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        if (op == TOK_BGT_PSEUDO_OP) {
          imm_expr* imm_inc = incr_expr_offset(imm, 1);
          emit_i_free(TOK_SLTI_OPCODE, 1, rs, imm_inc);
          emit_i(TOK_BEQ_OPCODE, 0, 1, target);
        } else {
          emit_i(TOK_ORI_OPCODE, 1, 0, imm);
          emit_i_free(TOK_BEQ_OPCODE, rs, 1, branch_offset(3));
          emit_r(TOK_SLTU_OPCODE, 1, rs, 1);
          emit_i(TOK_BEQ_OPCODE, 0, 1, target);
        }
        free(imm);
        free(target);
      }
      break;
    }

    /* BR_GE_POPS: bge/bgeu SRC1 (SRC2|IMM) LABEL */
    case TOK_BGE_PSEUDO_OP:
    case TOK_BGEU_PSEUDO_OP: {
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        emit_r(op == TOK_BGE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1, rs, rt);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        emit_i(op == TOK_BGE_PSEUDO_OP ? TOK_SLTI_OPCODE : TOK_SLTIU_OPCODE, 1, rs, imm);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
        free(imm);
      }
      break;
    }

    /* BR_LT_POPS: blt/bltu SRC1 (SRC2|IMM) LABEL */
    case TOK_BLT_PSEUDO_OP:
    case TOK_BLTU_PSEUDO_OP: {
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        emit_r(op == TOK_BLT_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1, rs, rt);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        emit_i(op == TOK_BLT_PSEUDO_OP ? TOK_SLTI_OPCODE : TOK_SLTIU_OPCODE, 1, rs, imm);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
        free(imm);
      }
      break;
    }

    /* BR_LE_POPS: ble/bleu SRC1 (SRC2|IMM) LABEL */
    case TOK_BLE_PSEUDO_OP:
    case TOK_BLEU_PSEUDO_OP: {
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        emit_r(op == TOK_BLE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1, rt, rs);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        if (op == TOK_BLE_PSEUDO_OP) {
          imm_expr* imm_inc = incr_expr_offset(imm, 1);
          emit_i_free(TOK_SLTI_OPCODE, 1, rs, imm_inc);
          emit_i(TOK_BNE_OPCODE, 0, 1, target);
        } else {
          emit_i(TOK_ORI_OPCODE, 1, 0, imm);
          emit_i(TOK_BEQ_OPCODE, rs, 1, target);
          emit_r(TOK_SLTU_OPCODE, 1, rs, 1);
          emit_i(TOK_BNE_OPCODE, 0, 1, target);
        }
        free(imm);
        free(target);
      }
      break;
    }

    default:
      parse_error_at("Pseudo-op not yet supported");
      sync_to_nl();
      break;
  }
}

static void parse_asm_code(void) {
  int op = scanner_advance();
  int type = find_op_type(op);

  switch (type) {
    case NOARG_TYPE_INST:
      parse_noarg(op);
      break;
    case R3_TYPE_INST:
      parse_r3(op);
      break;
    case R2sh_TYPE_INST:
      parse_r2sh(op);
      break;
    case R1s_TYPE_INST:
      parse_r1s(op);
      break;
    case I2_TYPE_INST:
      parse_i2(op);
      break;
    case I1t_TYPE_INST:
      parse_i1t(op);
      break;
    case I2a_TYPE_INST:
      parse_i2a(op);
      break;
    case B2_TYPE_INST:
      parse_b2(op);
      break;
    case B1_TYPE_INST:
      parse_b1(op);
      break;
    case J_TYPE_INST:
      parse_j(op);
      break;
    case PSEUDO_OP:
      parse_pseudo(op);
      break;
    case R2td_TYPE_INST: {
      /* mfc0/mtc0/etc.: <op> REG COP_REG.  COP_REG accepts
         TOK_REG or TOK_FP_REG. */
      int reg = parse_register();
      int copreg;
      if (scanner_peek() == TOK_FP_REG) {
        copreg = parse_fp_register();
      } else {
        copreg = parse_register();
      }
      emit_fp_r(op, 0, copreg, reg);
      break;
    }
    case R2ds_TYPE_INST: {
      /* jalr — register-indirect jump-and-link.  Defer to parse_j
         since J_OPS handles the dispatch. */
      parse_j(op);
      break;
    }
    case R2st_TYPE_INST: {
      /* Two-source R-type with no destination — covers
         mult/multu (always 2-operand) and div/divu (which ALSO
         accept 3-operand DIV_POPS pseudo forms).

         Dispatch by operand count: count registers after the
         first one. */
      int r1 = parse_register();
      if (scanner_peek() != TOK_REG && scanner_peek() != TOK_INT &&
          scanner_peek() != TOK_ID) {
        /* Not enough operands?  Fall back to plain 2-form
           anyway — produces a syntax error if anything's wrong. */
        parse_error_at("Expected operand for div/mult");
        break;
      }
      if (op == TOK_TEQ_OPCODE || op == TOK_TGE_OPCODE || op == TOK_TGEU_OPCODE ||
          op == TOK_TLT_OPCODE || op == TOK_TLTU_OPCODE || op == TOK_TNE_OPCODE) {
        /* BINARY_TRAP_OPS: <op> SRC1 SRC2 → emit_r(op, 0, r1, r2)
         */
        int r2 = parse_register();
        emit_r(op, 0, r1, r2);
      } else if (op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE) {
        /* DIV_POPS: can be 2-op (real hardware) or 3-op (pseudo) */
        int r2 = parse_register();
        if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) {
          /* 2-operand form: emit_r(op, 0, r1, r2)
             — note r1 is rs , r2 is rt (SRC1). */
          emit_r(op, 0, r1, r2);
        } else if (scanner_peek() == TOK_REG) {
          int r3 = parse_register();
          div_inst(op, r1, r2, r3, 0);
        } else {
          imm_expr* imm = parse_imm32();
          extern bool is_zero_imm(imm_expr * expr);
          if (is_zero_imm(imm)) {
            parse_error_at("Divide by zero");
          } else {
            emit_i_free(TOK_ORI_OPCODE, 1, 0, imm);
            div_inst(op, r1, r2, 1, 1);
            /* don't free imm again — i_type_inst_free already freed */
            break;
          }
          free(imm);
        }
      } else {
        /* mult/multu: always 2-operand */
        int r2 = parse_register();
        emit_r(op, 0, r1, r2);
      }
      break;
    }
    case R1d_TYPE_INST: {
      /* One-destination-register R-type: mfhi/mflo.  See
       */
      int rd = parse_register();
      emit_r(op, rd, 0, 0);
      break;
    }
    case R3sh_TYPE_INST: {
      /* BINARYIR_OPS: sllv, srav, srlv (variable-register shift).
         DEST SRC1 SRC2 — note the r_type_inst call below has rs/rt
         swapped (rt=SRC2, rs=SRC1).  Also accepts immediate form
         which uses op_to_imm_op for the shamt encoding. */
      int rd = parse_register();
      int rs = parse_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        emit_r(op, rd, rt, rs);
      } else {
        /* DEST SRC1 TOK_INT → emit_r_shift(op_to_imm_op(op), DEST, SRC1,
         * shamt) */
        if (scanner_peek() != TOK_INT) {
          parse_error_at("Expected register or integer shift");
          break;
        }
        scanner_advance();
        int shamt = scan_value.i;
        emit_r_shift(op_to_imm_op(op), rd, rs, shamt);
      }
      break;
    }
    case I1s_TYPE_INST: {
      /* BINARYI_TRAP_OPS: teqi/tgei/tgeiu/tlti/tltiu/tnei.
         <op> SRC1 IMM16 → emit_i_free(op, 0, rs, imm).
 */
      int rs = parse_register();
      imm_expr* imm = parse_imm16();
      emit_i_free(op, 0, rs, imm);
      break;
    }
    case BC_TYPE_INST: {
      /* BR_COP_OPS: bc1f/bc1t/bc1fl/bc1tl.  Two forms:
           <op> LABEL                  → cc=0
           <op> CC_REG LABEL           → cc=TOK_INT
         The RT field is cc_to_rt(cc, nd, tf); RS is 0 (
         uses BIN_RS($1.i) which is the token-number's bits 21-25,
         always 0 for spim's token-number range).  See
        */
      extern bool opcode_is_nullified_branch(int);
      extern bool opcode_is_true_branch(int);
      int nd = opcode_is_nullified_branch(op) ? 1 : 0;
      int tf = opcode_is_true_branch(op) ? 1 : 0;
      int cc = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        cc = scan_value.i;
      }
      imm_expr* target = parse_label();
      int rt = (cc << 2) | (nd << 1) | tf;
      emit_i_free(op, rt, 0, target);
      break;
    }
    case MOVC_TYPE_INST: {
      /* MOVECC_OPS: movf/movt DEST SRC1 TOK_INT (cc number).
        : emit_r($1, DEST, SRC1, (TOK_INT&7)<<2). */
      int rd = parse_register();
      int rs = parse_register();
      int cc = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        cc = scan_value.i;
      }
      emit_r(op, rd, rs, (cc & 0x7) << 2);
      break;
    }

    /* --- FP family --- */
    case FP_R2ds_TYPE_INST: {
      /* FP_UNARY_OPS / FP_MOVE_OPS: <op> F_DEST F_SRC2.
         etc.: emit_fp_r($1, $2, $3, 0). */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      emit_fp_r(op, fd, fs, 0);
      break;
    }
    case FP_R3_TYPE_INST: {
      /* FP_BINARY_OPS: <op> F_DEST F_SRC1 F_SRC2.
       */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      int ft = parse_fp_register();
      emit_fp_r(op, fd, fs, ft);
      break;
    }
    case FP_CMP_TYPE_INST: {
      /* FP_CMP_OPS: two forms:
           <op> F_SRC1 F_SRC2                   → cc=0
           <op> CC_REG F_SRC1 F_SRC2            → cc=TOK_INT */
      int cc = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        cc = scan_value.i;
      }
      int fs = parse_fp_register();
      int ft = parse_fp_register();
      emit_fp_compare(op, fs, ft, cc);
      break;
    }
    case FP_MOVC_TYPE_INST: {
      /* FP_MOVC_TYPE covers two families with different operand
         shapes:
           FP_MOVEC_OPS  (movn/movz.{s,d}) : F_DEST F_SRC1 REG
              → emit_fp_r(op, fd, fs, rt)
           FP_MOVECC_OPS (movf/movt.{s,d}) : F_DEST F_SRC1 [CC_REG]
              → emit_fp_r(op, fd, fs, cc_to_rt(cc, 0, 0))
         Disambiguate by inspecting the third operand. */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      if (scanner_peek() == TOK_REG) {
        int rt = parse_register();
        emit_fp_r(op, fd, fs, rt);
      } else if (scanner_peek() == TOK_INT) {
        scanner_advance();
        int cc = scan_value.i;
        emit_fp_r(op, fd, fs, (cc & 0x7) << 2);
      } else {
        /* No third operand (movf/movt with implicit cc=0). */
        emit_fp_r(op, fd, fs, 0);
      }
      break;
    }
    case FP_I2a_TYPE_INST: {
      /* lwc1 / ldc1 / lwc2 etc.: <op> F_REG ADDRESS.
       */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      emit_i(op, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case FP_R2ts_TYPE_INST: {
      /* mfc1/mtc1/cfc0/ctc0/cfc1/ctc1: <op> REG COP_REG.
         COP_REG accepts either TOK_REG OR TOK_FP_REG (
         2574) — both name the coprocessor register number. */
      int reg = parse_register();
      int copreg;
      if (scanner_peek() == TOK_FP_REG) {
        copreg = parse_fp_register();
      } else {
        copreg = parse_register();
      }
      emit_fp_r(op, 0, copreg, reg);
      break;
    }
    default:
      parse_error_at("Instruction not yet supported");
      sync_to_nl();
      break;
  }
}

static void parse_directive(int dir_tok) {
  switch (dir_tok) {
    case TOK_DATA_DIRECTIVE:
      parse_dir_data(false);
      break;
    case TOK_K_DATA_DIRECTIVE:
      parse_dir_data(true);
      break;
    case TOK_TEXT_DIRECTIVE:
      parse_dir_text(false);
      break;
    case TOK_K_TEXT_DIRECTIVE:
      parse_dir_text(true);
      break;
    case TOK_GLOBAL_DIRECTIVE:
      parse_dir_globl();
      break;
    case TOK_WORD_DIRECTIVE:
      parse_dir_word();
      break;
    case TOK_HALF_DIRECTIVE:
      parse_dir_half();
      break;
    case TOK_BYTE_DIRECTIVE:
      parse_dir_byte();
      break;
    case TOK_ASCII_DIRECTIVE:
      parse_dir_ascii();
      break;
    case TOK_ASCIIZ_DIRECTIVE:
      parse_dir_asciiz();
      break;
    case TOK_FLOAT_DIRECTIVE:
      parse_dir_float();
      break;
    case TOK_DOUBLE_DIRECTIVE:
      parse_dir_double();
      break;
    case TOK_SPACE_DIRECTIVE:
      parse_dir_space();
      break;
    case TOK_ALIGN_DIRECTIVE:
      parse_dir_align();
      break;
    case TOK_COMM_DIRECTIVE:
      parse_dir_comm();
      break;
    case TOK_EXTERN_DIRECTIVE:
      parse_dir_extern();
      break;
    case TOK_ERR_DIRECTIVE:
      parse_error_at(".err directive");
      break;
    /* Metadata directives — swallow the rest of the line. */
    case TOK_FILE_DIRECTIVE:
    case TOK_LOC_DIRECTIVE:
    case TOK_FRAME_DIRECTIVE:
    case TOK_MASK_DIRECTIVE:
    case TOK_FMASK_DIRECTIVE:
    case TOK_ENT_DIRECTIVE:
    case TOK_END_DIRECTIVE:
    case TOK_LABEL_DIRECTIVE:
    case TOK_LIVEREG_DIRECTIVE:
    case TOK_OPTIONS_DIRECTIVE:
    case TOK_BGNB_DIRECTIVE:
    case TOK_ENDB_DIRECTIVE:
    case TOK_ENDR_DIRECTIVE:
    case TOK_ASM0_DIRECTIVE:
    case TOK_ALIAS_DIRECTIVE:
    case TOK_SET_DIRECTIVE:
      sync_to_nl();
      break;
    default:
      parse_error_at("Directive not yet supported");
      sync_to_nl();
      break;
  }
}

/* Helper: is this token an assembler directive? */
static bool is_directive(int tok) {
  int t = find_op_type(tok);
  return t == ASM_DIR;
}

/* OPT_LBL: ID ':'  |  ID '=' EXPR */
static void parse_opt_label(void) {
  /* The peek2 disambiguation already happened in parse_line.
     We know peek == TOK_ID and peek2 == ':' or '='.  Don't call
     scanner_force_identifier here — the TOK_ID was already
     classified at the peek site, so the flag would
     incorrectly affect a LATER token (e.g., misclassifying
     `buf: .word 42`'s `.word` as TOK_ID). */
  scanner_advance(); /* consume TOK_ID */
  char* sym = (char*)scan_value.p;
  int sep = scanner_advance(); /* ':' or '=' */

  if (sep == ':') {
    mem_addr addr = text_dir ? current_text_pc() : current_data_pc();
    /* DO NOT resolve uses immediately.  If a subsequent .word (etc.)
       on the next line bumps the data PC for alignment,
       fix_current_label_address must be able to update this label's
       addr first.  emit_label_normal also conses onto
       this_line_labels in SDT mode. */
    emit_label_normal(sym, addr);
    free(sym);
  } else {
    /* ID '=' EXPR : constant label */
    int v = parse_expression();
    emit_label_const(sym, v);
    free(sym);
  }
}

/* LINE / LBL_CMD / CMD */
static void parse_line(void) {
  /* Empty line? */
  if (scanner_peek() == TOK_NL) {
    scanner_advance();
    return;
  }
  if (scanner_peek() == TOK_EOF) {
    return;
  }

  /* Look ahead for optional label */
  if (scanner_peek() == TOK_ID &&
      (scanner_peek2() == ':' || scanner_peek2() == '=')) {
    parse_opt_label();
  }

  /* Then maybe directive or instruction */
  int t = scanner_peek();
  if (t == TOK_NL) {
    scanner_advance();
    return;
  }
  if (t == TOK_EOF) {
    return;
  }

  if (is_directive(t)) {
    int dt = scanner_advance();
    parse_directive(dt);
    clear_labels(); /* clear labels after the directive */
  } else {
    parse_asm_code();
    clear_labels(); /* clear labels after the instruction */
  }

  /* Expect newline or EOF */
  if (parse_error_occurred) {
    sync_to_nl();
  } else if (scanner_peek() == TOK_NL) {
    scanner_advance();
  } else if (scanner_peek() != TOK_EOF) {
    parse_error_at("Extra tokens after instruction");
    sync_to_nl();
  }
}

/* ---------------- public API ---------------- */

void parser_init(FILE* in, char* file_name) {
  scanner_init(in);
  input_file_name = file_name;
  parse_errors_seen = 0;
  parse_error_occurred = false;
  data_dir = false;
  text_dir = true;
  /* If a previous parse left a tree around, drop it. */
  if (current_file != nullptr) {
    ast_free(current_file);
    current_file = nullptr;
  }
  if (should_build_ast()) {
    current_file = ast_make_file(file_name);
  }
}

int parse_file(void) {
  while (scanner_peek() != TOK_EOF) {
    parse_error_occurred = false;
    scanner_start_line();
    parse_line();
  }
  /* if the last lines were bare-label definitions, their uses
     need resolving here. */
  clear_labels();

  /* If -print-ast was requested, dump the tree now (after parse,
     possibly before/instead of emit). */
  if (should_build_ast() && print_ast_after_parse && current_file != nullptr) {
    ast_print(current_file, ast_print_out ? ast_print_out : stderr);
  }

  /* If -show-expansion was requested, dump just the pseudo-op
     wrappers from this file. */
  if (should_build_ast() && show_expansion_after_parse &&
      current_file != nullptr) {
    print_pseudo_walk(current_file,
                      show_expansion_out ? show_expansion_out : stderr);
  }

  /* If -print-ast-json was requested, dump the tree as JSON. */
  if (should_build_ast() && print_ast_json_after_parse &&
      current_file != nullptr) {
    ast_print_json(current_file, ast_json_out ? ast_json_out : stderr);
  }

  /* AST mode: walk the tree and emit code via the action helpers.
     Skipped when the user asked to just inspect the tree
     (-print-ast / -show-expansion / -print-ast-json). */
  if (should_build_ast() && !print_ast_only_ && defer_emit_to_emit_ast &&
      current_file != nullptr) {
    emit_ast(current_file);
  }

  return parse_errors_seen;
}

/* ------- emit_ast — AST walker ------------------------------

   Walks the file's children in source order, calling the action
   helpers (r_type_inst, store_word, record_label, ...) that commit
   each node's effect to the simulator's memory and symbol table.

   The action helpers themselves are unchanged from when the parser
   called them inline; the AST walk is just a different driver for
   the same effect chain.  In PARSE_DIRECT mode this function isn't
   called — the parser fires the same helpers itself.

   Forward references in `.word LABEL`-style data work via a small
   side channel: the parser attaches the unresolved label to the
   imm_expr in the AST_DATA_* node, and emit_data_int_node here
   records the use at the correct data PC at emit time.
*/

static void emit_one(const ast_node* node);

void emit_ast(const ast_node* file) {
  if (file == nullptr || file->kind != AST_FILE) return;
  for (const ast_node* n = file->u.file.child; n != nullptr; n = n->next) {
    emit_one(n);
    /* Match parse_line's "clear labels after a non-label
       directive or instruction" rule. */
    if (n->kind != AST_LABEL_DEF && n->kind != AST_DIR_GLOBL) {
      clear_labels();
    }
  }
  /* Trailing labels (file ends with `foo:` and no following
     directive) get their uses resolved here, just like
     parse_file does. */
  clear_labels();
}

/* Emit one .byte/.half/.word value list.  For literal-only exprs
   (no symbol), evaluate and store.  For symbol-bearing exprs whose
   symbol is still unresolved at emit time, store the partial value
   and record the use at the (now-correct) data PC; the existing
   resolve_label_uses mechanism patches it when the label is later
   defined. */
static void emit_data_int_node(const ast_node* node, void (*store)(int)) {
  for (int i = 0; i < node->u.data_int.count; i++) {
    imm_expr* e = node->u.data_int.exprs[i];
    if (e == nullptr) {
      store(0);
      continue;
    }
    if (e->symbol != nullptr && !SYMBOL_IS_DEFINED(e->symbol)) {
      record_data_uses_symbol(current_data_pc(), e->symbol);
      store(e->offset);
    } else {
      store(eval_imm_expr(e));
    }
  }
}

static void emit_data_fp_node(const ast_node* node, void (*store)(double*)) {
  for (int i = 0; i < node->u.data_fp.count; i++) {
    double tmp = node->u.data_fp.values[i];
    store(&tmp);
  }
}

static void emit_one(const ast_node* node) {
  if (node == nullptr) return;

  /* Restore the per-node source line so listing observers (and any
     parse_error / parse_warn fired from inside an action helper)
     report the right line.  Without this, the scanner's line_no
     stays at the file's last line throughout emit_ast. */
  line_no = node->source_line;

  switch (node->kind) {
    case AST_FILE:
    case AST_ERROR:
      return; /* shouldn't appear as a direct child of the file root */

    /* Segment changes — mirror parse_dir_data / parse_dir_text. */
    case AST_DIR_TEXT:
      user_kernel_text_segment(false);
      data_dir = false;
      text_dir = true;
      if (node->u.dir_seg.has_start_addr)
        set_text_pc(node->u.dir_seg.start_addr);
      return;
    case AST_DIR_KTEXT:
      user_kernel_text_segment(true);
      data_dir = false;
      text_dir = true;
      if (node->u.dir_seg.has_start_addr)
        set_text_pc(node->u.dir_seg.start_addr);
      return;
    case AST_DIR_DATA:
      user_kernel_data_segment(false);
      data_dir = true;
      text_dir = false;
      enable_data_alignment();
      auto_align = true;
      if (node->u.dir_seg.has_start_addr)
        set_data_pc(node->u.dir_seg.start_addr);
      return;
    case AST_DIR_KDATA:
      user_kernel_data_segment(true);
      data_dir = true;
      text_dir = false;
      enable_data_alignment();
      auto_align = true;
      if (node->u.dir_seg.has_start_addr)
        set_data_pc(node->u.dir_seg.start_addr);
      return;

    case AST_DIR_ALIGN: {
      int v = node->u.dir_align.n;
      if (v == 0) auto_align = false;
      if (text_dir)
        align_text(v);
      else
        align_data(v);
      return;
    }

    case AST_DIR_SPACE:
      increment_data_pc(node->u.dir_space.size);
      return;

    case AST_DIR_GLOBL:
      make_label_global(node->u.dir_globl.name);
      return;

    case AST_DIR_EXTERN: {
      const char* sym = node->u.dir_named_size.name;
      make_label_global((char*)sym);
      if (lookup_label((char*)sym)->addr == 0)
        record_label((char*)sym, current_data_pc(), 1);
      increment_data_pc(node->u.dir_named_size.size);
      return;
    }

    case AST_DIR_COMM: {
      const char* sym = node->u.dir_named_size.name;
      align_data(2);
      if (lookup_label((char*)sym)->addr == 0)
        record_label((char*)sym, current_data_pc(), 1);
      increment_data_pc(node->u.dir_named_size.size);
      return;
    }

    /* Data. */
    case AST_DATA_BYTE:
      emit_data_int_node(node, store_byte);
      return;
    case AST_DATA_HALF:
      align_labels_to(1);
      if (data_dir) set_data_alignment(1);
      emit_data_int_node(node, store_half);
      return;
    case AST_DATA_WORD:
      align_labels_to(2);
      if (data_dir) set_data_alignment(2);
      emit_data_int_node(node, store_word);
      return;
    case AST_DATA_FLOAT:
      align_labels_to(2);
      if (data_dir) set_data_alignment(2);
      emit_data_fp_node(node, store_float);
      return;
    case AST_DATA_DOUBLE:
      align_labels_to(3);
      if (data_dir) set_data_alignment(3);
      emit_data_fp_node(node, store_double);
      return;

    case AST_DATA_STRING:
      if (!text_dir) {
        store_string(node->u.data_string.bytes, node->u.data_string.length,
                     node->u.data_string.null_terminate);
      }
      return;

    /* Labels. */
    case AST_LABEL_DEF:
      if (node->u.label_def.kind == AST_LABEL_NORMAL) {
        mem_addr addr = text_dir ? current_text_pc() : current_data_pc();
        label* l = record_label((char*)node->u.label_def.name, addr, 0);
        cons_label(l);
      } else {
        label* l = record_label((char*)node->u.label_def.name,
                                (mem_addr)node->u.label_def.value, 1);
        l->const_flag = 1;
      }
      return;

    /* Instructions — pass copies of imm_expr so the AST keeps its
       own.  The action helpers (i_type_inst, j_type_inst) already
       copy what they store, so a single dup is enough for either
       to drop on the floor / store internally. */
    case AST_INST_R:
      r_type_inst(node->u.inst_r.op, node->u.inst_r.rd, node->u.inst_r.rs,
                  node->u.inst_r.rt);
      return;
    case AST_INST_R_SHIFT:
      r_sh_type_inst(node->u.inst_r_shift.op, node->u.inst_r_shift.rd,
                     node->u.inst_r_shift.rt, node->u.inst_r_shift.shamt);
      return;
    case AST_INST_I: {
      imm_expr* copy = dup_imm(node->u.inst_i.imm);
      i_type_inst_free(node->u.inst_i.op, node->u.inst_i.rt, node->u.inst_i.rs,
                       copy);
      return;
    }
    case AST_INST_J: {
      imm_expr* copy = dup_imm(node->u.inst_j.target);
      j_type_inst(node->u.inst_j.op, copy);
      free(copy);
      return;
    }
    case AST_INST_FP_R:
      r_co_type_inst(node->u.inst_fp_r.op, node->u.inst_fp_r.fd,
                     node->u.inst_fp_r.fs, node->u.inst_fp_r.ft);
      return;
    case AST_INST_FP_COMPARE:
      r_cond_type_inst(node->u.inst_fp_compare.op, node->u.inst_fp_compare.fs,
                       node->u.inst_fp_compare.ft, node->u.inst_fp_compare.cc);
      return;

    case AST_PSEUDO:
      /* Walk the expansion children — the real instructions the
         parser emitted for this pseudo-op (la, li, bge, ...).  Same
         line-boundary clear_labels rule as the file-level walk. */
      for (const ast_node* c = node->u.pseudo.child; c != nullptr;
           c = c->next) {
        emit_one(c);
        if (c->kind != AST_LABEL_DEF && c->kind != AST_DIR_GLOBL) {
          clear_labels();
        }
      }
      return;
  }
}
