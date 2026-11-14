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
#include "opcode-tables.h"
#include "spim-utils.h"
#include "pseudo-op.h"
#include "ast.h"

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

/* ------- Forward declarations for items defined below ------ */

static void cons_label(label* l);
static bool auto_align; /* defined as static further down */

/* ------- Parser mode and AST state ------------------------- */

/* The parser builds an abstract syntax tree during the parse, then
   emit_ast walks it in source order calling the action helpers
   (r_type, store_word, ...) that commit each statement's effect to
   the simulator.  This is the sole parse strategy: the older inline
   "syntax-directed translation" path (PARSE_DIRECT), which fired the
   action helpers directly during the parse instead of building a tree,
   was removed once the AST became the only driver (2026-08-03).
   The `print_ast_only_` flag (set by -print-ast / -show-expansion /
   -print-ast-json) suppresses the emit pass entirely so spim can dump
   the tree without committing anything to memory. */
static ast_node* current_file = nullptr;
static bool print_ast_after_parse = false;
static FILE* ast_print_out = nullptr;
static bool print_ast_only_ = false;

/* parse_factor stashes the FIRST unresolved label it sees in the
   current expression here so data_int_push can attach symbol info to
   the AST imm_expr; emit_ast then records the forward-reference use at
   the correct (emit-time) data PC. */
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
  } else {
    /* No tree is being built (parser_init has not run): the node is
       dropped. */
  }
}

void emit_ast(const ast_node* file, const ast_node* after);

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
    print_pseudo_in_chain(node->payload.file.child, out);
  } else {
    /* Other node kinds are skipped — -show-expansion is a focused view. */
  }
}

void parser_set_print_ast_only(bool on) { print_ast_only_ = on; }

bool parser_get_print_ast_only(void) { return print_ast_only_; }

/* Deep-copy an imm_expr so the AST can own its own copy independent
   of the one the action helper sees. */
static imm_expr* dup_imm(const imm_expr* e) {
  if (e == nullptr) return nullptr;
  imm_expr* c = (imm_expr*)xmalloc(sizeof(imm_expr));
  *c = *e;
  return c;
}

/* ----- instruction dispatch helpers ------------------------ */

void emit_r(int opcode, int destination_register, int source_register, int target_register) {
  ast_append(ast_make_inst_r(opcode, destination_register, source_register, target_register));
}

void emit_r_shift(int opcode, int destination_register, int target_register, int shift_amount) {
  ast_append(ast_make_inst_r_shift(opcode, destination_register, target_register, shift_amount));
}

/* emit_i: caller keeps ownership of imm (the AST takes its own copy). */
void emit_i(int opcode, int target_register, int source_register, const imm_expr* immediate) {
  ast_append(ast_make_inst_i(opcode, target_register, source_register, dup_imm(immediate)));
}

/* emit_i_free: caller transfers ownership of imm.  The AST takes a copy,
   so the original is freed here. */
void emit_i_free(int opcode, int target_register, int source_register, imm_expr* immediate) {
  ast_append(ast_make_inst_i(opcode, target_register, source_register, dup_imm(immediate)));
  free(immediate);
}

void emit_j(int opcode, const imm_expr* target) {
  /* The AST takes its own copy; caller (parse_j) frees the original. */
  ast_append(ast_make_inst_j(opcode, dup_imm(target)));
}

void emit_fp_r(int opcode, int floating_point_destination, int floating_point_source, int floating_point_target) {
  ast_append(ast_make_inst_fp_r(opcode, floating_point_destination, floating_point_source, floating_point_target));
}

void emit_fp_compare(int opcode, int floating_point_source, int floating_point_target, int condition_code) {
  ast_append(ast_make_inst_fp_compare(opcode, floating_point_source, floating_point_target, condition_code));
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
    data_int_buf =
        (imm_expr**)xmalloc((size_t)data_int_cap * sizeof(imm_expr*));
  }
}

static void data_int_push(int value) {
  if (data_int_count == data_int_cap) {
    data_int_cap *= 2;
    imm_expr** const grow =
        (imm_expr**)xmalloc((size_t)data_int_cap * sizeof(imm_expr*));
    memcpy(grow, data_int_buf, (size_t)data_int_count * sizeof(imm_expr*));
    free(data_int_buf);
    data_int_buf = grow;
  }
  /* If parse_factor stashed an unresolved label for this expression
     (deferred mode), wire it into the imm_expr so emit_ast can
     register the use against the correct PC. */
  const char* const symbol_name =
      expr_unresolved_label != nullptr ? expr_unresolved_label->name : nullptr;
  data_int_buf[data_int_count++] = make_imm_expr(value, (char*)symbol_name, false);
  expr_unresolved_label = nullptr;
}

static void data_int_finalize(void) {
  if (data_int_count == 0) {
    return;
  }
  /* Hand off the array as-is; ast_node owns it now. */
  imm_expr** const expressions =
      (imm_expr**)xmalloc((size_t)data_int_count * sizeof(imm_expr*));
  memcpy(expressions, data_int_buf, (size_t)data_int_count * sizeof(imm_expr*));
  ast_node* node;
  /* Integer values are only collected after data_int_begin, whose callers pass
     AST_DATA_BYTE, _HALF or _WORD; no other kind builds a data-int node, so
     -Wswitch-enum's demand for a case per kind does not apply here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
  switch (data_int_kind) {
    case AST_DATA_BYTE:
      node = ast_make_data_byte(data_int_count, expressions);
      break;
    case AST_DATA_HALF:
      node = ast_make_data_half(data_int_count, expressions);
      break;
    case AST_DATA_WORD:
      node = ast_make_data_word(data_int_count, expressions);
      break;
    default:
      node = ast_make_data_word(data_int_count, expressions);
      break;
  }
#pragma GCC diagnostic pop
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
    data_fp_buf = (double*)xmalloc((size_t)data_fp_cap * sizeof(double));
  }
}

static void data_fp_push(double value) {
  if (data_fp_count == data_fp_cap) {
    data_fp_cap *= 2;
    double* const grow = (double*)xmalloc((size_t)data_fp_cap * sizeof(double));
    memcpy(grow, data_fp_buf, (size_t)data_fp_count * sizeof(double));
    free(data_fp_buf);
    data_fp_buf = grow;
  }
  data_fp_buf[data_fp_count++] = value;
}

static void data_fp_finalize(void) {
  if (data_fp_count == 0) {
    return;
  }
  double* const values =
      (double*)xmalloc((size_t)data_fp_count * sizeof(double));
  memcpy(values, data_fp_buf, (size_t)data_fp_count * sizeof(double));
  ast_node* node = (data_int_kind == AST_DATA_FLOAT)
                       ? ast_make_data_float(data_fp_count, values)
                       : ast_make_data_double(data_fp_count, values);
  node->source_line = data_directive_line;
  ast_append(node);
  data_fp_count = 0;
}

static void emit_data_string(const char* s, int length, bool add_null_terminator) {
  ast_append(ast_make_data_string(s, length, add_null_terminator));
}

/* ----- label + directive dispatch helpers ------------------ */

static void emit_label_normal(const char* name) {
  ast_append(ast_make_label_normal(name));
}

static void emit_label_const(const char* name, int v) {
  ast_append(ast_make_label_const(name, v));
}

static void emit_dir_globl(const char* name) {
  ast_append(ast_make_dir_globl(name));
}

static void emit_dir_align(int n) { ast_append(ast_make_dir_align(n)); }

static void emit_dir_space(int v) { ast_append(ast_make_dir_space(v)); }

static void emit_dir_extern(const char* symbol, int size) {
  ast_append(ast_make_dir_extern(symbol, size));
}

static void emit_dir_comm(const char* symbol, int size) {
  ast_append(ast_make_dir_comm(symbol, size));
}

static void emit_dir_seg(ast_kind kind, bool has_address, mem_addr address) {
  /* The kernel/user distinction is implied by `kind` (KTEXT/KDATA vs
     TEXT/DATA) in the AST node; the segment switch happens at emit time. */
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
  /* The segment change's action-helper side effects (switching segment,
     setting the PC) happen at emit time from emit_one; here we only
     record the directive in the AST. */
  ast_node* n;
  /* kind is only ever one of the four segment directives; the other kinds are
     not segments, so -Wswitch-enum's demand for a case per kind does not apply
     here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
  switch (kind) {
    case AST_DIR_TEXT:
      n = ast_make_dir_text(has_address, address);
      break;
    case AST_DIR_DATA:
      n = ast_make_dir_data(has_address, address);
      break;
    case AST_DIR_KTEXT:
      n = ast_make_dir_ktext(has_address, address);
      break;
    case AST_DIR_KDATA:
      n = ast_make_dir_kdata(has_address, address);
      break;
    default:
      n = ast_make_dir_data(has_address, address);
      break;
  }
#pragma GCC diagnostic pop
  ast_append(n);
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
    label_cell* const next = this_line_labels->next;
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
void fix_current_label_address(mem_addr new_address) {
  for (label_cell* c = this_line_labels; c != nullptr; c = c->next) {
    c->label->address = new_address;
  }
}
/* Internal call sites kept under the old name to minimise churn. */
#define fix_current_label_address fix_current_label_address

/* ---------------- error helpers ---------------- */

static void parse_error_on_line(const char* message, int line) {
  parse_error_occurred = true;
  parse_errors_seen += 1;
  error("spim: (parser) %s on line %d of file %s\n", message, line,
        input_file_name ? input_file_name : "(stdin)");
}

static void parse_error_at(const char* message) {
  parse_error_on_line(message, line_no);
}

/* Skip tokens up to (but NOT past) the next newline.  parse_line's
   tail consumes that newline; consuming it here would leave the
   outer loop pointing at the next line's first token and the
   newline check would mis-fire "Extra tokens after instruction"
   on every line that follows an ignored directive like `.set`. */
static void sync_to_nl(void) {
  while (scanner_peek() != TOK_NL && scanner_peek() != TOK_EOF) {
    const int t = scanner_advance();
    /* TOK_ID / TOK_STR tokens own a heap string that advance moves into
       scan_value; discarding the token here (ignored directives, error
       recovery) means nobody else will free it. */
    if (t == TOK_ID || t == TOK_STR) free(scan_value.pointer);
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
    return scan_value.integer;
  }
  parse_error_at("Expected register");
  return 0;
}

/* TOK_FP_REG → FP register number 0..31 */
static int parse_fp_register(void) {
  if (scanner_peek() == TOK_FP_REG) {
    scanner_advance();
    return scan_value.integer;
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
  const int v = scan_value.integer;
  if (scanner_peek() == '+') {
    scanner_advance();
    if (scanner_peek() != TOK_INT) {
      parse_error_at("Expected integer after '+'");
      return v;
    }
    scanner_advance();
    return v + scan_value.integer;
  } else if (scanner_peek() == TOK_INT) {
    /* TOK_INT TOK_INT (with the second negative) is the historical
       subtraction handling — see scanner-parser-inventory Part 8F.
       Peek the next value. */
    scanner_advance();
    if (scan_value.integer >= 0) parse_error_at("Syntax error");
    return v - (-scan_value.integer);
  } else {
    return v;
  }
}

/* FACTOR : TOK_INT | '(' EXPR ')' | ID.
   ID resolves to its label's address; forward references
   record a data-uses-symbol entry for later resolution. */

static int parse_expr(void); /* forward */
static int parse_factor(void) {
  switch (scanner_peek()) {
    case TOK_INT:
      scanner_advance();
      return scan_value.integer;
    case '(': {
      scanner_advance();
      const int v = parse_expr();
      expect(')', "Expected ')'");
      return v;
    }
    case TOK_ID: {
      scanner_advance();
      char* const symbol_name = (char*)scan_value.pointer;
      label* const l = lookup_label(symbol_name);
      if (l->address == 0) {
        /* Forward reference.  current_data_pc isn't the right address at
           parse time (emission is deferred to emit_ast), so stash the
           label and let emit_ast record the use against the actual data
           PC. */
        if (expr_unresolved_label == nullptr) {
          expr_unresolved_label = l;
        }
        free(symbol_name);
        return 0;
      }
      const int address = (int)l->address;
      free(symbol_name);
      return address;
    }
    default:
      parse_error_at("Expected expression");
      return 0;
  }
}

/* TRM : FACTOR ( ('*'|'/') FACTOR )* */
static int parse_term(void) {
  int v = parse_factor();
  while (scanner_peek() == '*' || scanner_peek() == '/') {
    const int operator = scanner_advance();
    const int r = parse_factor();
    /* The assembler's constant arithmetic wraps at 32 bits, as the machine's
       does; C's signed overflow (and INT_MIN / -1) would be undefined
       behavior.  Division by zero gives 0. */
    if (operator == '*')
      v = (int)((unsigned)v * (unsigned)r);
    else if (r == 0)
      v = 0;
    else if (r == -1)
      v = (int)(0u - (unsigned)v);
    else
      v = v / r;
  }
  return v;
}

/* EXPR : TRM ( ('+'|'-') TRM )* */
static int parse_expr(void) {
  int v = parse_term();
  while (scanner_peek() == '+' || scanner_peek() == '-') {
    const int operator = scanner_advance();
    const int r = parse_term();
    if (operator == '+')
      v = (int)((unsigned)v + (unsigned)r);
    else
      v = (int)((unsigned)v - (unsigned)r);
  }
  return v;
}

/* EXPRESSION: like EXPR but with the next-token force-identifier flag.
   Resets expression_unresolved_label so the side-channel only carries the
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
    char* const symbol = (char*)scan_value.pointer;
    if (scanner_peek() == '+') {
      scanner_advance();
      const int offset = parse_abs_addr();
      imm_expr* const r = make_imm_expr(offset, symbol, false);
      /* make_imm_expr copies sym internally, so free our local. */
      free(symbol);
      return r;
    } else if (scanner_peek() == '-') {
      scanner_advance();
      const int offset = parse_abs_addr();
      imm_expr* const r = make_imm_expr(-offset, symbol, false);
      free(symbol);
      return r;
    } else {
      imm_expr* const r = make_imm_expr(0, symbol, false);
      /* make_imm_expr does not retain `sym`; this frame owns and frees it. */
      free(symbol);
      return r;
    }
  } else if (scanner_peek() == '(') {
    /* '(' ABS_ADDR ')' '>' '>' TOK_INT */
    scanner_advance();
    const int v = parse_abs_addr();
    expect(')', "Expected ')'");
    expect('>', "Expected '>'");
    expect('>', "Expected '>'");
    if (scanner_peek() != TOK_INT) {
      parse_error_at("Expected integer after '>>'");
      return make_imm_expr(0, nullptr, false);
    }
    scanner_advance();
    const int shift_amount = scan_value.integer;
    return make_imm_expr(v >> shift_amount, nullptr, false);
  } else {
    /* ABS_ADDR */
    const int v = parse_abs_addr();
    return make_imm_expr(v, nullptr, false);
  }
}

/* IMM16, UIMM16 — IMM32 plus range check */
static imm_expr* parse_imm16(void) {
  /* The instruction's line: parsing the immediate can scan the newline after
     it, which advances line_no. */
  const int line = line_no;
  imm_expr* const e = parse_imm32();
  check_imm_range(e, IMM_MIN, IMM_MAX, line);
  return e;
}

static imm_expr* parse_uimm16(void) {
  const int line = line_no; /* as in parse_imm16 */
  imm_expr* const e = parse_imm32();
  check_uimm_range(e, UIMM_MIN, UIMM_MAX, line);
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
  char* const symbol = (char*)scan_value.pointer;
  imm_expr* const r = make_imm_expr(-(int)current_text_pc(), symbol, true);
  /* make_imm_expr looks `sym` up into a label* and does not retain the
     string, so this frame owns it and must free it. */
  free(symbol);
  return r;
}

/* ADDR — the 9-alternative address production.  This is where
   the 25 shift-reduce conflicts collapse into one explicit
   lookahead. */
static addr_expr* parse_address(void) {
  scanner_force_identifier();

  switch (scanner_peek()) {
    case '(': {
      /* '(' REGISTER ')' */
      scanner_advance();
      const int register_number = parse_register();
      expect(')', "Expected ')' after register");
      return make_addr_expr(0, nullptr, register_number);
    }

    case TOK_INT: {
      /* ABS_ADDR [ '(' REGISTER ')' ] */
      const int immediate = parse_abs_addr();
      if (scanner_peek() == '+' && scanner_peek2() == TOK_ID) {
        /* ABS_ADDR '+' ID */
        scanner_advance(); /* + */
        scanner_force_identifier();
        scanner_advance();
        char* const symbol = (char*)scan_value.pointer;
        addr_expr* const r = make_addr_expr(immediate, symbol, 0);
        /* make_addr_expr does not retain `sym` (see instruction.c); free it,
           matching the TOK_ID path below. */
        free(symbol);
        return r;
      } else if (scanner_peek() == '(') {
        scanner_advance();
        const int register_number = parse_register();
        expect(')', "Expected ')'");
        return make_addr_expr(immediate, nullptr, register_number);
      } else {
        return make_addr_expr(immediate, nullptr, 0);
      }
    }

    case TOK_ID: {
      /* TOK_ID [ '+' ABS_ADDR | '-' ABS_ADDR ] [ '(' REGISTER ')' ] */
      scanner_advance();
      char* const symbol = (char*)scan_value.pointer;
      int offset = 0;
      if (scanner_peek() == '+') {
        scanner_advance();
        offset = parse_abs_addr();
      } else if (scanner_peek() == '-') {
        scanner_advance();
        offset = -parse_abs_addr();
      } else {
        /* No offset: off stays 0. */
      }
      int register_number = 0;
      if (scanner_peek() == '(') {
        scanner_advance();
        register_number = parse_register();
        expect(')', "Expected ')'");
      }
      addr_expr* const r = make_addr_expr(offset, symbol, register_number);
      free(symbol);
      return r;
    }

    default:
      parse_error_at("Expected address");
      return make_addr_expr(0, nullptr, 0);
  }
}

/* ---------------- instruction parsers ---------------- */

/* Return true if `op` is a BINARYI_OPS opcode (has both reg-reg
   and reg-imm forms — add, addu, and, or, xor, slt, sltu).
   */
static bool op_has_imm_form(int opcode) {
  switch (opcode) {
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

/* RD_RS_RT_INST: <op> DEST, SRC1, SRC2.  For BINARYI_OPS opcodes,
   also accepts <op> DEST, SRC1, IMM and <op> DEST, IMM (see
SUB_OPS (sub, subu) similarly accept an
   immediate, converted to addi/addiu with a negated value
*/

static void parse_r3(int opcode) {
  const int destination_register = parse_register();
  if (opcode == TOK_CLO_OPCODE || opcode == TOK_CLZ_OPCODE) {
    /* COUNT_LEADING_OPS DEST SRC1 — RT must equal RD.
     */
    const int source_register = parse_register();
    emit_r(opcode, destination_register, source_register, destination_register);
    return;
  } else if (opcode == TOK_MUL_OPCODE) {
    /* MULT_OPS3 DEST SRC1 (SRC2|IMM).  3-reg or 3-with-imm. Imm form uses $at +
     * ori. */
    const int source_register = parse_register();
    if (scanner_peek() == TOK_REG) {
      const int target_register = parse_register();
      emit_r(opcode, destination_register, source_register, target_register);
    } else {
      imm_expr* const immediate = parse_imm32();
      emit_i_free(TOK_ORI_OPCODE, 1, 0, immediate);
      emit_r(opcode, destination_register, source_register, 1);
    }
    return;
  } else if (opcode == TOK_SUB_OPCODE || opcode == TOK_SUBU_OPCODE) {
    /* SUB_OPS: 3-reg, 3-with-imm, or 2-with-imm.  Convert imm
       form to addi/addiu with negated value. */
    if (scanner_peek() != TOK_REG) {
      imm_expr* const immediate = parse_imm32();
      const int value = eval_imm_expr(immediate);
      emit_i(opcode == TOK_SUB_OPCODE ? TOK_ADDI_OPCODE : TOK_ADDIU_OPCODE, destination_register, destination_register,
             make_imm_expr(-value, nullptr, false));
      free(immediate);
      return;
    }
    const int source_register = parse_register();
    if (scanner_peek() == TOK_REG) {
      const int target_register = parse_register();
      emit_r(opcode, destination_register, source_register, target_register);
    } else {
      imm_expr* const immediate = parse_imm32();
      const int value = eval_imm_expr(immediate);
      emit_i(opcode == TOK_SUB_OPCODE ? TOK_ADDI_OPCODE : TOK_ADDIU_OPCODE, destination_register, source_register,
             make_imm_expr(-value, nullptr, false));
      free(immediate);
    }
    return;
  } else if (op_has_imm_form(opcode)) {
    /* Two-operand form: <op> DEST, IMM */
    if (scanner_peek() != TOK_REG) {
      imm_expr* const immediate = parse_imm32();
      emit_i_free(op_to_imm_op(opcode), destination_register, destination_register, immediate);
      return;
    }
    const int source_register = parse_register();
    /* Three-operand form: <op> DEST, SRC1, SRC2 (reg) or
       <op> DEST, SRC1, IMM (immediate) */
    if (scanner_peek() == TOK_REG) {
      const int target_register = parse_register();
      emit_r(opcode, destination_register, source_register, target_register);
    } else {
      imm_expr* const immediate = parse_imm32();
      emit_i_free(op_to_imm_op(opcode), destination_register, source_register, immediate);
    }
    return;
  } else {
    /* Plain R3 (no immediate form): three registers */
    const int source_register = parse_register();
    const int target_register = parse_register();
    emit_r(opcode, destination_register, source_register, target_register);
  }
}

/* RD_RT_SHAMT_INST: <op> DEST, SRC1, SHAMT (immediate 0..31).
   ssnop is a special no-operand case emitting `sll $0, $0, 1`. */
static void parse_r2sh(int opcode) {
  if (opcode == TOK_SSNOP_OPCODE) {
    emit_r_shift(TOK_SLL_OPCODE, 0, 0, 1);
    return;
  }
  const int destination_register = parse_register();
  const int source_register = parse_register(); /* this is rt in MIPS shift encoding */
  if (scanner_peek() != TOK_INT) {
    parse_error_at("Expected integer shift amount");
    return;
  }
  scanner_advance();
  const int shift_amount = scan_value.integer;
  emit_r_shift(opcode, destination_register, source_register, shift_amount);
}

/* RS_INST: <op> SRC1 (jr) */
static void parse_r1s(int opcode) {
  const int source_register = parse_register();
  emit_r(opcode, 0, source_register, 0);
}

/* RT_RS_IMM_INST: <op> DEST, SRC1, IMM16
   Also accepts the 2-operand shorthand `<op> DEST, IMM16` for
   andi/ori/xori where the missing SRC1 defaults to DEST — exception handlers
   use `ori $k0, 0x1`. */
static void parse_i2(int opcode) {
  const int target_register = parse_register();
  const int source_register = scanner_peek() == TOK_REG ? parse_register() : target_register;
  /* andi/ori/xori take unsigned; addi/addiu/slti signed.  The
     range check happens inside parse_imm16/parse_uimm16. */
  imm_expr* const immediate =
      (opcode == TOK_ANDI_OPCODE || opcode == TOK_ORI_OPCODE || opcode == TOK_XORI_OPCODE)
          ? parse_uimm16()
          : parse_imm16();
  emit_i_free(opcode, target_register, source_register, immediate);
}

/* RT_IMM_INST: <op> DEST, UIMM16 (lui) */
static void parse_i1t(int opcode) {
  const int target_register = parse_register();
  imm_expr* const immediate = parse_uimm16();
  emit_i_free(opcode, target_register, 0, immediate);
}

/* RT_ADDR_INST: <op> DEST, ADDRESS — loads and stores */
static void parse_i2a(int opcode) {
  const int target_register = parse_register();
  addr_expr* const address = parse_address();
  emit_i(opcode, target_register, addr_expr_reg(address), addr_expr_imm(address));
  /* Bison frees the addr_expr's inner imm + the addr_expr; do the
     same.  spim runtime exposes free_imm_expr but not
     free_addr_expr — see instruction.h.  Use free() since that's what */
  free(addr_expr_imm(address));
  free(address);
}

/* BRANCH_RS_RT_LABEL_INST: <op> SRC1, SRC2, LABEL (beq, bne) — reg/reg form.
   Also <op> SRC1, IMM, LABEL — reg/imm form
   which uses $at + ori to materialize the immediate, then beq/bne. */
static void parse_b2(int opcode) {
  const int source1 = parse_register();
  if (scanner_peek() == TOK_REG) {
    const int source2 = parse_register();
    imm_expr* const target = parse_label();
    emit_i_free(opcode, source2, source1, target);
  } else {
    /* Immediate form */
    imm_expr* const immediate = parse_imm32();
    imm_expr* const target = parse_label();
    if (is_zero_imm(immediate)) {
      /* Special case: comparing against literal 0 → use $0 directly */
      emit_i(opcode, source1, 0, target);
    } else {
      /* Use $at: ori $at, $0, imm; <op> src1, $at, target */
      emit_i(TOK_ORI_OPCODE, 1, 0, immediate);
      emit_i(opcode, source1, 1, target);
    }
    free(immediate);
    free(target);
  }
}

/* BRANCH_RS_LABEL_INST: <op> SRC1, LABEL (bgez, bltz, etc.) */
static void parse_b1(int opcode) {
  const int source_register = parse_register();
  imm_expr* const target = parse_label();
  emit_i_free(opcode, 0, source_register, target);
}

/* J_TYPE_INST: <op> LABEL  OR  <op> SRC1 (j/jal/jr/jalr), and RD_RS_INST's
   jalr and jalr.hb.  With a label argument we emit j_type with the
   right absolute-jump opcode; with a register argument we emit r_type
   with the indirect-jump opcode.  jalr.hb keeps its own opcode (jalr with the
   hazard-barrier bit) and takes registers only.  cop2 shares J_TYPE_INST
   but spim has no coprocessor 2, so it is an error. */
static void parse_j(int opcode) {
  if (opcode == TOK_COP2_OPCODE) {
    parse_error_at("cop2 is not supported: spim has no coprocessor 2");
  } else if (scanner_peek() == TOK_REG) {
    /* J_OPS SRC1  or  J_OPS DEST SRC1 — register-indirect jump. */
    const int r1 = parse_register();
    if (scanner_peek() == TOK_REG) {
      /* DEST SRC1 form */
      const int r2 = parse_register();
      switch (opcode) {
        case TOK_J_OPCODE:
        case TOK_JR_OPCODE:
          emit_r(TOK_JR_OPCODE, 0, r2, 0);
          break;
        case TOK_JAL_OPCODE:
        case TOK_JALR_OPCODE:
          emit_r(TOK_JALR_OPCODE, r1, r2, 0);
          break;
        case TOK_JALR_HB_OPCODE:
          emit_r(TOK_JALR_HB_OPCODE, r1, r2, 0);
          break;
        default:
          /* Never reached: parse_asm_code sends only J_TYPE_INST and
             RD_RS_INST opcodes here, and cop2 is handled above. */
          fatal_error("internal error: parse_j: unexpected opcode %d\n", opcode);
      }
    } else {
      /* SRC1-only form */
      switch (opcode) {
        case TOK_J_OPCODE:
        case TOK_JR_OPCODE:
          emit_r(TOK_JR_OPCODE, 0, r1, 0);
          break;
        case TOK_JAL_OPCODE:
        case TOK_JALR_OPCODE:
          emit_r(TOK_JALR_OPCODE, 31, r1, 0);
          break;
        case TOK_JALR_HB_OPCODE:
          emit_r(TOK_JALR_HB_OPCODE, 31, r1, 0);
          break;
        default:
          /* Never reached: as in the DEST SRC1 form above. */
          fatal_error("internal error: parse_j: unexpected opcode %d\n", opcode);
      }
    }
  } else if (opcode == TOK_JALR_HB_OPCODE) {
    parse_error_at("jalr.hb needs a register operand");
  } else {
    /* J_OPS LABEL — absolute-target jump */
    imm_expr* const target = parse_label();
    switch (opcode) {
      case TOK_J_OPCODE:
      case TOK_JR_OPCODE:
        emit_j(TOK_J_OPCODE, target);
        break;
      case TOK_JAL_OPCODE:
      case TOK_JALR_OPCODE:
        emit_j(TOK_JAL_OPCODE, target);
        break;
      default:
        /* Never reached: jalr.hb and cop2 are handled above. */
        fatal_error("internal error: parse_j: unexpected opcode %d\n", opcode);
    }
    free(target);
  }
}

/* NOARG_TYPE_INST: <op>  (syscall) or <op> IMM (break N, sync N).
   See. */
static void parse_noarg(int opcode) {
  if (scanner_peek() == TOK_INT) {
    /* break N or sync N — encode the int in the rd slot.
       Bison: emit_r(op, $2.i, 0, 0). */
    scanner_advance();
    const int n = scan_value.integer;
    if (opcode == TOK_BREAK_OPCODE && n == 1) {
      parse_error_at("Breakpoint 1 is reserved for debugger");
    }
    emit_r(opcode, n, 0, 0);
  } else {
    emit_r(opcode, 0, 0, 0);
  }
}

/* ---------------- directive parsers ---------------- */

static void parse_dir_data(bool kernel) {
  bool has_address = false;
  mem_addr address = 0;
  if (scanner_peek() == TOK_INT) {
    scanner_advance();
    has_address = true;
    address = (mem_addr)scan_value.integer;
  }
  emit_dir_seg(kernel ? AST_DIR_KDATA : AST_DIR_DATA, has_address, address);
}

static void parse_dir_text(bool kernel) {
  bool has_address = false;
  mem_addr address = 0;
  if (scanner_peek() == TOK_INT) {
    scanner_advance();
    has_address = true;
    address = (mem_addr)scan_value.integer;
  }
  emit_dir_seg(kernel ? AST_DIR_KTEXT : AST_DIR_TEXT, has_address, address);
}

static void parse_dir_globl(void) {
  scanner_force_identifier();
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected identifier after .globl");
    return;
  }
  scanner_advance();
  char* const symbol = (char*)scan_value.pointer;
  emit_dir_globl(symbol);
  free(symbol);
}

static void parse_dir_align(void) {
  const int line = line_no; /* the expression can consume the newline */
  const int v = parse_expression();
  if (v < 0 || v > 31) {
    /* align_data/align_text shift a 32-bit mask by v. */
    parse_error_on_line("Alignment out of range (0 .. 31)", line);
    return;
  }
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
  char* const symbol = (char*)scan_value.pointer;
  const int size = parse_expression();
  emit_dir_extern(symbol, size);
  free(symbol);
}

static void parse_dir_comm(void) {
  scanner_force_identifier();
  if (scanner_peek() != TOK_ID) {
    parse_error_at("Expected ID");
    return;
  }
  scanner_advance();
  char* const symbol = (char*)scan_value.pointer;
  const int size = parse_expression();
  emit_dir_comm(symbol, size);
  free(symbol);
}

static void parse_dir_space(void) {
  const int v = parse_expression();
  if (text_dir) {
    parse_error_at("Can't put data in text segment");
    return;
  }
  emit_dir_space(v);
}

/* EXPR_LST: collect each expression value into the AST data
   accumulator; data_int_finalize builds the AST node. */
static void parse_expr_list(void) {
  while (true) {
    const int errors_before = parse_errors_seen;
    const int v = parse_expression();
    if (parse_errors_seen != errors_before) {
      /* parse_expression reports a token it cannot use without consuming
         it; skip the rest of the line instead of asking again forever. */
      sync_to_nl();
      break;
    }
    data_int_push(v);
    /* commas are skipped by the scanner; same with whitespace */
    if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) break;
    /* Otherwise continue to next expression */
  }
  data_int_finalize();
}

/* Pre-fix any labels currently on this_line_labels to the
   address they'll occupy AFTER `set_data_alignment(N)` aligns
   the data PC.  `align_data` calls `fix_current_label_address` to align
   `this_line_labels`; we maintain our own and pre-fix here. Respects the same
   auto-align gate that data.c uses. */
static void align_labels_to(int alignment) {
  if (!data_dir || !auto_align || this_line_labels == nullptr) return;
  const mem_addr current = current_data_pc();
  const mem_addr aligned = (current + (1u << alignment) - 1) & (~0u << alignment);
  fix_current_label_address(aligned);
}

/* FP_EXPR_LST: consume TOK_FP tokens, collecting into the AST data
   accumulator; data_fp_finalize builds the AST node. */
static void parse_fp_expr_list(void) {
  while (true) {
    if (scanner_peek() != TOK_FP) {
      parse_error_at("Expected floating-point literal");
      return;
    }
    scanner_advance();
    double* const value = (double*)scan_value.pointer;
    data_fp_push(*value);
    if (scanner_peek() == TOK_NL || scanner_peek() == TOK_EOF) break;
  }
  data_fp_finalize();
}

static void parse_dir_float(void) {
  align_labels_to(2);
  if (data_dir) set_data_alignment(2);
  data_fp_begin(AST_DATA_FLOAT);
  parse_fp_expr_list();
}

static void parse_dir_double(void) {
  align_labels_to(3);
  if (data_dir) set_data_alignment(3);
  data_fp_begin(AST_DATA_DOUBLE);
  parse_fp_expr_list();
}

static void parse_dir_word(void) {
  align_labels_to(2);
  if (data_dir) set_data_alignment(2);
  data_int_begin(AST_DATA_WORD);
  parse_expr_list();
}
static void parse_dir_half(void) {
  align_labels_to(1);
  if (data_dir) set_data_alignment(1);
  data_int_begin(AST_DATA_HALF);
  parse_expr_list();
}
static void parse_dir_byte(void) {
  data_int_begin(AST_DATA_BYTE);
  parse_expr_list();
}

/* STR_LST: emit each string via store_string */
static void parse_string_list(void) {
  while (true) {
    if (scanner_peek() != TOK_STR) {
      parse_error_at("Expected string literal");
      return;
    }
    scanner_advance();
    char* const s = (char*)scan_value.pointer;
    const int length = (int)strlen(s);
    if (text_dir) {
      parse_error_at("Can't put data in text segment");
    } else {
      emit_data_string(s, length, null_term);
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

/* Look up an opcode token's operand-shape type tag, in opcode_table
   (opcode-tables.h), which holds {name, token, op_type} sorted by token.

   Returns -1 when `op` isn't in the table.  Caller switches treat the
   missing case via `default:` (parse_error_at).  Return type is `int`
   so the -1 sentinel is representable; case labels in the caller's
   switch compare against the typed `op_type` enumerators via the
   usual int/enum conversions. */
static int find_op_type(int opcode) {
  const name_val_val* const entry =
      map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, opcode);
  return entry ? entry->value2 : -1;
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
static void do_parse_pseudo(int opcode);

static void parse_pseudo(int opcode) {
  const int saved_line = line_no;
  ast_node* pseudo_node = ast_make_pseudo(op_token_name(opcode));
  pseudo_node->source_line = saved_line;
  ast_node* const saved_pseudo = current_pseudo;
  current_pseudo = pseudo_node;

  do_parse_pseudo(opcode);

  /* The expansion is the pseudo-op's line, even when its last operand
     read on to the newline before a child was built. */
  for (ast_node* c = pseudo_node->payload.pseudo.child; c != nullptr; c = c->next)
    c->source_line = saved_line;

  current_pseudo = saved_pseudo;
  ast_append(pseudo_node);
}

static void do_parse_pseudo(int opcode) {
  switch (opcode) {
    case TOK_LI_PSEUDO_OP: {
      /* li DEST, IMM32  →  ori DEST, $0, imm */
      const int target_register = parse_register();
      imm_expr* const immediate = parse_imm32();
      emit_i_free(TOK_ORI_OPCODE, target_register, 0, immediate);
      break;
    }
    case TOK_MOVE_PSEUDO_OP: {
      /* move DEST, SRC1  →  addu DEST, $0, SRC1 */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      emit_r(TOK_ADDU_OPCODE, destination_register, 0, source_register);
      break;
    }
    case TOK_NEG_PSEUDO_OP: {
      /* neg DEST, SRC1  →  sub DEST, $0, SRC1 */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      emit_r(TOK_SUB_OPCODE, destination_register, 0, source_register);
      break;
    }
    case TOK_NEGU_PSEUDO_OP: {
      /* negu DEST, SRC1  →  subu DEST, $0, SRC1 */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      emit_r(TOK_SUBU_OPCODE, destination_register, 0, source_register);
      break;
    }
    case TOK_NOT_PSEUDO_OP: {
      /* not DEST, SRC1  →  nor DEST, SRC1, $0 */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      emit_r(TOK_NOR_OPCODE, destination_register, source_register, 0);
      break;
    }
    case TOK_ROR_PSEUDO_OP:
    case TOK_ROL_PSEUDO_OP: {
      /* Rotate right / left.  Two forms:
           DEST SRC1 SRC2   — register rotate
           DEST SRC1 IMM    — constant rotate */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        /* ROR: subu $at,$0,rt; sllv $at,$at,rs; srlv rd,rt,rs; or rd,rd,$at
           ROL: subu $at,$0,rt; srlv $at,$at,rs; sllv rd,rt,rs; or rd,rd,$at */
        emit_r(TOK_SUBU_OPCODE, 1, 0, target_register);
        if (opcode == TOK_ROR_PSEUDO_OP) {
          emit_r(TOK_SLLV_OPCODE, 1, 1, source_register);
          emit_r(TOK_SRLV_OPCODE, destination_register, target_register, source_register);
        } else {
          emit_r(TOK_SRLV_OPCODE, 1, 1, source_register);
          emit_r(TOK_SLLV_OPCODE, destination_register, target_register, source_register);
        }
        emit_r(TOK_OR_OPCODE, destination_register, destination_register, 1);
      } else {
        const int line = line_no; /* as in parse_imm16 */
        imm_expr* const immediate = parse_imm32();
        const int distance = eval_imm_expr(immediate);
        check_imm_range(immediate, 0, 31, line);
        if (opcode == TOK_ROR_PSEUDO_OP) {
          emit_r_shift(TOK_SLL_OPCODE, 1, source_register, -distance);
          emit_r_shift(TOK_SRL_OPCODE, destination_register, source_register, distance);
        } else {
          emit_r_shift(TOK_SRL_OPCODE, 1, source_register, -distance);
          emit_r_shift(TOK_SLL_OPCODE, destination_register, source_register, distance);
        }
        emit_r(TOK_OR_OPCODE, destination_register, destination_register, 1);
        free(immediate);
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
      const int destination_register = parse_register();
      const int source_register = parse_register();
      int target_register;
      if (scanner_peek() == TOK_REG) {
        target_register = parse_register();
      } else {
        imm_expr* const immediate = parse_imm32();
        if (!is_zero_imm(immediate)) {
          emit_i(TOK_ORI_OPCODE, 1, 0, immediate);
          target_register = 1;
        } else {
          target_register = 0;
        }
        free(immediate);
      }
      switch (opcode) {
        case TOK_SLE_PSEUDO_OP:
        case TOK_SLEU_PSEUDO_OP:
          set_le_inst(opcode, destination_register, source_register, target_register);
          break;
        case TOK_SGT_PSEUDO_OP:
        case TOK_SGTU_PSEUDO_OP:
          set_gt_inst(opcode, destination_register, source_register, target_register);
          break;
        case TOK_SGE_PSEUDO_OP:
        case TOK_SGEU_PSEUDO_OP:
          set_ge_inst(opcode, destination_register, source_register, target_register);
          break;
        case TOK_SEQ_PSEUDO_OP:
        case TOK_SNE_PSEUDO_OP:
          set_eq_inst(opcode, destination_register, source_register, target_register);
          break;
        default:
          /* Never reached: the enclosing case admits only the eight
           * set-on-compare pseudo-ops listed above. */
          fatal_error(
              "internal error: do_parse_pseudo: not a set-on-compare "
              "pseudo-op: %d\n",
              opcode);
      }
      break;
    }
    case TOK_ABS_PSEUDO_OP: {
      /* abs DEST, SRC1: if DEST != SRC1,
         first move; then bgez SRC1 +3; nop; sub DEST, $0, SRC1. */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      if (destination_register != source_register) {
        emit_r(TOK_ADDU_OPCODE, destination_register, 0, source_register);
      }
      emit_i_free(TOK_BGEZ_OPCODE, 0, source_register, branch_offset(3));
      nop_inst();
      emit_r(TOK_SUB_OPCODE, destination_register, 0, source_register);
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
      const int destination_register = parse_register();
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        div_inst(opcode, destination_register, source_register, target_register, 0);
      } else {
        imm_expr* const immediate = parse_imm32();
        if (is_zero_imm(immediate)) {
          parse_error_at("Divide by zero");
        } else {
          emit_i_free(TOK_ORI_OPCODE, 1, 0, immediate);
          div_inst(opcode, destination_register, source_register, 1, 1);
        }
      }
      break;
    }
    case TOK_MULO_PSEUDO_OP:
    case TOK_MULOU_PSEUDO_OP: {
      /* mulo/mulou — MUL_POPS, 3-operand form.. */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        mult_inst(opcode, destination_register, source_register, target_register);
      } else {
        imm_expr* const immediate = parse_imm32();
        if (is_zero_imm(immediate)) {
          /* Optimize: n * 0 == 0 */
          emit_i_free(TOK_ORI_OPCODE, destination_register, 0, immediate);
        } else {
          emit_i_free(TOK_ORI_OPCODE, 1, 0, immediate);
          mult_inst(opcode, destination_register, source_register, 1);
        }
      }
      break;
    }
    case TOK_MFC1_D_PSEUDO_OP: {
      /* mfc1.d REG COP_REG  →  two mfc1 instructions
       */
      const int line = line_no; /* the last operand can consume the newline */
      const int register_number = parse_register();
      const int coprocessor_register =
          scanner_peek() == TOK_FP_REG ? parse_fp_register() : parse_register();
      if (register_number == 31 || coprocessor_register == 31) {
        /* the pair's second register would be 32, past the register file */
        parse_error_on_line("Register pair starts at the last register (31)",
                            line);
        break;
      }
      emit_fp_r(TOK_MFC1_OPCODE, 0, coprocessor_register, register_number);
      emit_fp_r(TOK_MFC1_OPCODE, 0, coprocessor_register + 1, register_number + 1);
      break;
    }
    case TOK_MTC1_D_PSEUDO_OP: {
      /* mtc1.d REG COP_REG  →  two mtc1 instructions
       */
      const int line = line_no; /* the last operand can consume the newline */
      const int register_number = parse_register();
      const int coprocessor_register =
          scanner_peek() == TOK_FP_REG ? parse_fp_register() : parse_register();
      if (register_number == 31 || coprocessor_register == 31) {
        /* the pair's second register would be 32, past the register file */
        parse_error_on_line("Register pair starts at the last register (31)",
                            line);
        break;
      }
      emit_fp_r(TOK_MTC1_OPCODE, 0, coprocessor_register, register_number);
      emit_fp_r(TOK_MTC1_OPCODE, 0, coprocessor_register + 1, register_number + 1);
      break;
    }
    case TOK_LI_D_PSEUDO_OP: {
      /* li.d F_DEST <TOK_FP>  →  load double-precision constant
         via two mtc1. */
      const int floating_point_destination = parse_fp_register();
      if (scanner_peek() != TOK_FP) {
        parse_error_at("Expected FP literal");
        break;
      }
      scanner_advance();
      int32_t words[2]; /* the double's two words, in host order */
      memcpy(words, scan_value.pointer, sizeof words);
      /* emit_i_free (not emit_i): the const_imm_expr node is allocated
         inline with no other owner, so transfer it to be freed. */
      emit_i_free(TOK_ORI_OPCODE, 1, 0, const_imm_expr(words[0]));
      emit_fp_r(TOK_MTC1_OPCODE, 0, floating_point_destination, 1);
      emit_i_free(TOK_ORI_OPCODE, 1, 0, const_imm_expr(words[1]));
      emit_fp_r(TOK_MTC1_OPCODE, 0, floating_point_destination + 1, 1);
      break;
    }
    case TOK_LI_S_PSEUDO_OP: {
      /* li.s F_DEST <TOK_FP>  →  single-precision const via one mtc1
       */
      const int floating_point_destination = parse_fp_register();
      if (scanner_peek() != TOK_FP) {
        parse_error_at("Expected FP literal");
        break;
      }
      scanner_advance();
      float float_value = (float)*((double*)scan_value.pointer);
      int32_t bits; /* the float's bit pattern, not its value */
      memcpy(&bits, &float_value, sizeof bits);
      /* emit_i_free: inline const_imm_expr node has no other owner. */
      emit_i_free(TOK_ORI_OPCODE, 1, 0, const_imm_expr(bits));
      emit_fp_r(TOK_MTC1_OPCODE, 0, floating_point_destination, 1);
      break;
    }
    case TOK_L_D_PSEUDO_OP: {
      /* l.d F_DEST ADDRESS  →  ldc1. */
      const int floating_point_addend = parse_fp_register();
      addr_expr* const address = parse_address();
      emit_i(TOK_LDC1_OPCODE, floating_point_addend, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_L_S_PSEUDO_OP: {
      /* l.s F_DEST ADDRESS  →  lwc1. */
      const int floating_point_addend = parse_fp_register();
      addr_expr* const address = parse_address();
      emit_i(TOK_LWC1_OPCODE, floating_point_addend, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_S_D_PSEUDO_OP: {
      /* s.d F_SRC1 ADDRESS  →  sdc1. */
      const int floating_point_addend = parse_fp_register();
      addr_expr* const address = parse_address();
      emit_i(TOK_SDC1_OPCODE, floating_point_addend, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_S_S_PSEUDO_OP: {
      /* s.s F_SRC1 ADDRESS  →  swc1. */
      const int floating_point_addend = parse_fp_register();
      addr_expr* const address = parse_address();
      emit_i(TOK_SWC1_OPCODE, floating_point_addend, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_LD_PSEUDO_OP: {
      /* ld DEST ADDRESS  — load doubleword pseudo, expands to
         two lw instructions */
      const int line = line_no; /* the address can consume the newline */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      if (target_register == 31) {
        /* the pair's second register would be 32, past the register file */
        parse_error_on_line("Register pair starts at the last register (31)",
                            line);
        free(addr_expr_imm(address));
        free(address);
        break;
      }
      emit_i(TOK_LW_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      emit_i_free(TOK_LW_OPCODE, target_register + 1, addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 4));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_SD_PSEUDO_OP: {
      /* sd SRC1 ADDRESS — store doubleword pseudo. */
      const int line = line_no; /* the address can consume the newline */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      if (target_register == 31) {
        /* the pair's second register would be 32, past the register file */
        parse_error_on_line("Register pair starts at the last register (31)",
                            line);
        free(addr_expr_imm(address));
        free(address);
        break;
      }
      emit_i(TOK_SW_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      emit_i_free(TOK_SW_OPCODE, target_register + 1, addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 4));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_ULW_PSEUDO_OP: {
      /* Unaligned load word. , little-endian
         path (the macOS/Linux x86 default).  Emits LWL+LWR with
         offset bumped by 3 on the LWL. */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      emit_i_free(TOK_LWL_OPCODE, target_register, addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 3));
      emit_i(TOK_LWR_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_ULH_PSEUDO_OP:
    case TOK_ULHU_PSEUDO_OP: {
      /* Unaligned load half (signed/unsigned). LE path. */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      emit_i_free(opcode == TOK_ULH_PSEUDO_OP ? TOK_LB_OPCODE : TOK_LBU_OPCODE, target_register,
                  addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 1));
      emit_i(TOK_LBU_OPCODE, 1, addr_expr_reg(address), addr_expr_imm(address));
      emit_r_shift(TOK_SLL_OPCODE, target_register, target_register, 8);
      emit_r(TOK_OR_OPCODE, target_register, target_register, 1);
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_USW_PSEUDO_OP: {
      /* Unaligned store word.  LE path. */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      emit_i_free(TOK_SWL_OPCODE, target_register, addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 3));
      emit_i(TOK_SWR_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_USH_PSEUDO_OP: {
      /* Unaligned store half.. ROL, store
         high byte, ROR. */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      emit_i(TOK_SB_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      /* ROL SRC, SRC, 8 (via SLL+SRL+OR) */
      emit_r_shift(TOK_SLL_OPCODE, 1, target_register, 24);
      emit_r_shift(TOK_SRL_OPCODE, target_register, target_register, 8);
      emit_r(TOK_OR_OPCODE, target_register, target_register, 1);
      emit_i_free(TOK_SB_OPCODE, target_register, addr_expr_reg(address),
                  incr_expr_offset(addr_expr_imm(address), 1));
      /* ROR SRC, SRC, 8 (via SRL+SLL+OR) */
      emit_r_shift(TOK_SRL_OPCODE, 1, target_register, 24);
      emit_r_shift(TOK_SLL_OPCODE, target_register, target_register, 8);
      emit_r(TOK_OR_OPCODE, target_register, target_register, 1);
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case TOK_LA_PSEUDO_OP: {
      /* la DEST, ADDRESS.  If ADDRESS has a
         base register, emit `addi DEST, base, offset`; else emit
         `ori DEST, $0, offset`. */
      const int target_register = parse_register();
      addr_expr* const address = parse_address();
      if (addr_expr_reg(address)) {
        emit_i(TOK_ADDI_OPCODE, target_register, addr_expr_reg(address), addr_expr_imm(address));
      } else {
        emit_i(TOK_ORI_OPCODE, target_register, 0, addr_expr_imm(address));
      }
      free(addr_expr_imm(address));
      free(address);
      break;
    }

    /* UNARY_BR_POPS: beqz, bnez SRC1 LABEL */
    case TOK_BEQZ_PSEUDO_OP:
    case TOK_BNEZ_PSEUDO_OP: {
      const int source_register = parse_register();
      imm_expr* const target = parse_label();
      const int real_opcode =
          (opcode == TOK_BEQZ_PSEUDO_OP) ? TOK_BEQ_OPCODE : TOK_BNE_OPCODE;
      emit_i_free(real_opcode, 0, source_register, target);
      break;
    }

    /* B_OPS: b, bal LABEL */
    case TOK_B_PSEUDO_OP:
    case TOK_BAL_PSEUDO_OP: {
      imm_expr* const target = parse_label();
      const int real_opcode =
          (opcode == TOK_BAL_PSEUDO_OP) ? TOK_BGEZAL_OPCODE : TOK_BGEZ_OPCODE;
      emit_i_free(real_opcode, 0, 0, target);
      break;
    }

    /* BR_GT_POPS: bgt/bgtu SRC1 (SRC2|IMM) LABEL */
    case TOK_BGT_PSEUDO_OP:
    case TOK_BGTU_PSEUDO_OP: {
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        imm_expr* const target = parse_label();
        emit_r(opcode == TOK_BGT_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1,
               target_register, source_register);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
      } else {
        /* Immediate form — see */
        imm_expr* const immediate = parse_imm32();
        imm_expr* const target = parse_label();
        if (opcode == TOK_BGT_PSEUDO_OP) {
          imm_expr* const immediate_increment = incr_expr_offset(immediate, 1);
          emit_i_free(TOK_SLTI_OPCODE, 1, source_register, immediate_increment);
          emit_i(TOK_BEQ_OPCODE, 0, 1, target);
        } else {
          emit_i(TOK_ORI_OPCODE, 1, 0, immediate);
          emit_i_free(TOK_BEQ_OPCODE, source_register, 1, branch_offset(3));
          emit_r(TOK_SLTU_OPCODE, 1, source_register, 1);
          emit_i(TOK_BEQ_OPCODE, 0, 1, target);
        }
        free(immediate);
        free(target);
      }
      break;
    }

    /* BR_GE_POPS: bge/bgeu SRC1 (SRC2|IMM) LABEL */
    case TOK_BGE_PSEUDO_OP:
    case TOK_BGEU_PSEUDO_OP: {
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        imm_expr* const target = parse_label();
        emit_r(opcode == TOK_BGE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1,
               source_register, target_register);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
      } else {
        imm_expr* const immediate = parse_imm32();
        imm_expr* const target = parse_label();
        emit_i(opcode == TOK_BGE_PSEUDO_OP ? TOK_SLTI_OPCODE : TOK_SLTIU_OPCODE, 1,
               source_register, immediate);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
        free(immediate);
      }
      break;
    }

    /* BR_LT_POPS: blt/bltu SRC1 (SRC2|IMM) LABEL */
    case TOK_BLT_PSEUDO_OP:
    case TOK_BLTU_PSEUDO_OP: {
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        imm_expr* const target = parse_label();
        emit_r(opcode == TOK_BLT_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1,
               source_register, target_register);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
      } else {
        imm_expr* const immediate = parse_imm32();
        imm_expr* const target = parse_label();
        emit_i(opcode == TOK_BLT_PSEUDO_OP ? TOK_SLTI_OPCODE : TOK_SLTIU_OPCODE, 1,
               source_register, immediate);
        emit_i_free(TOK_BNE_OPCODE, 0, 1, target);
        free(immediate);
      }
      break;
    }

    /* BR_LE_POPS: ble/bleu SRC1 (SRC2|IMM) LABEL */
    case TOK_BLE_PSEUDO_OP:
    case TOK_BLEU_PSEUDO_OP: {
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        imm_expr* const target = parse_label();
        emit_r(opcode == TOK_BLE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, 1,
               target_register, source_register);
        emit_i_free(TOK_BEQ_OPCODE, 0, 1, target);
      } else {
        imm_expr* const immediate = parse_imm32();
        imm_expr* const target = parse_label();
        if (opcode == TOK_BLE_PSEUDO_OP) {
          imm_expr* const immediate_increment = incr_expr_offset(immediate, 1);
          emit_i_free(TOK_SLTI_OPCODE, 1, source_register, immediate_increment);
          emit_i(TOK_BNE_OPCODE, 0, 1, target);
        } else {
          emit_i(TOK_ORI_OPCODE, 1, 0, immediate);
          emit_i(TOK_BEQ_OPCODE, source_register, 1, target);
          emit_r(TOK_SLTU_OPCODE, 1, source_register, 1);
          emit_i(TOK_BNE_OPCODE, 0, 1, target);
        }
        free(immediate);
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
  const int opcode = scanner_advance();
  const int type = find_op_type(opcode);

  switch (type) {
    case NOARG_TYPE_INST:
      parse_noarg(opcode);
      break;
    case RD_RS_RT_INST:
      parse_r3(opcode);
      break;
    case RD_RT_SHAMT_INST:
      parse_r2sh(opcode);
      break;
    case RS_INST:
      parse_r1s(opcode);
      break;
    case RT_RS_IMM_INST:
      parse_i2(opcode);
      break;
    case RT_IMM_INST:
      parse_i1t(opcode);
      break;
    case RT_ADDR_INST:
      parse_i2a(opcode);
      break;
    case BRANCH_RS_RT_LABEL_INST:
      parse_b2(opcode);
      break;
    case BRANCH_RS_LABEL_INST:
      parse_b1(opcode);
      break;
    case J_TYPE_INST:
      parse_j(opcode);
      break;
    case PSEUDO_OP:
      parse_pseudo(opcode);
      break;
    case RT_COPREG_INST: {
      /* mfc0/mtc0/etc.: <op> REG COP_REG.  COP_REG accepts
         TOK_REG or TOK_FP_REG. */
      const int register_number = parse_register();
      const int coprocessor_register =
          scanner_peek() == TOK_FP_REG ? parse_fp_register() : parse_register();
      emit_fp_r(opcode, 0, coprocessor_register, register_number);
      break;
    }
    case RD_RS_INST: {
      /* jalr — register-indirect jump-and-link.  Defer to parse_j
         since J_OPS handles the dispatch. */
      parse_j(opcode);
      break;
    }
    case RS_RT_INST: {
      /* Two-source R-type with no destination — covers
         mult/multu (always 2-operand) and div/divu (which ALSO
         accept 3-operand DIV_POPS pseudo forms).

         Dispatch by operand count: count registers after the
         first one. */
      const int r1 = parse_register();
      if (scanner_peek() != TOK_REG && scanner_peek() != TOK_INT &&
          scanner_peek() != TOK_ID) {
        /* Not enough operands?  Fall back to plain 2-form
           anyway — produces a syntax error if anything's wrong. */
        parse_error_at("Expected operand for div/mult");
        break;
      }
      switch (opcode) {
        case TOK_TEQ_OPCODE:
        case TOK_TGE_OPCODE:
        case TOK_TGEU_OPCODE:
        case TOK_TLT_OPCODE:
        case TOK_TLTU_OPCODE:
        case TOK_TNE_OPCODE: {
          /* BINARY_TRAP_OPS: <op> SRC1 SRC2 → emit_r(op, 0, r1, r2) */
          const int r2 = parse_register();
          emit_r(opcode, 0, r1, r2);
          break;
        }
        case TOK_DIV_OPCODE:
        case TOK_DIVU_OPCODE: {
          /* DIV_POPS: can be 2-op (real hardware) or 3-op (pseudo) */
          const int r2 = parse_register();
          switch (scanner_peek()) {
            case TOK_NL:
            case TOK_EOF:
              /* 2-operand form: emit_r(op, 0, r1, r2)
                 — note r1 is rs , r2 is rt (SRC1). */
              emit_r(opcode, 0, r1, r2);
              break;
            case TOK_REG: {
              const int r3 = parse_register();
              div_inst(opcode, r1, r2, r3, 0);
              break;
            }
            default: {
              /* 3-operand form with an immediate divisor. */
              imm_expr* const immediate = parse_imm32();
              if (is_zero_imm(immediate)) {
                parse_error_at("Divide by zero");
              } else {
                emit_i_free(TOK_ORI_OPCODE, 1, 0, immediate);
                div_inst(opcode, r1, r2, 1, 1);
                /* don't free imm again — i_type_free already freed */
                break;
              }
              free(immediate);
              break;
            }
          }
          break;
        }
        default: {
          /* mult/multu and the other two-source ops: always 2-operand */
          const int r2 = parse_register();
          emit_r(opcode, 0, r1, r2);
          break;
        }
      }
      break;
    }
    case RD_INST: {
      /* One-destination-register R-type: mfhi/mflo.  See
       */
      const int destination_register = parse_register();
      emit_r(opcode, destination_register, 0, 0);
      break;
    }
    case RD_RT_RS_INST: {
      /* BINARYIR_OPS: sllv, srav, srlv (variable-register shift).
         DEST SRC1 SRC2 — note the r_type call below has rs/rt
         swapped (rt=SRC2, rs=SRC1).  Also accepts immediate form
         which uses op_to_imm_op for the shamt encoding. */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        emit_r(opcode, destination_register, target_register, source_register);
      } else {
        /* DEST SRC1 TOK_INT → emit_r_shift(op_to_imm_op(op), DEST, SRC1,
         * shamt) */
        if (scanner_peek() != TOK_INT) {
          parse_error_at("Expected register or integer shift");
          break;
        }
        scanner_advance();
        const int shift_amount = scan_value.integer;
        emit_r_shift(op_to_imm_op(opcode), destination_register, source_register, shift_amount);
      }
      break;
    }
    case RS_IMM_INST: {
      /* BINARYI_TRAP_OPS: teqi/tgei/tgeiu/tlti/tltiu/tnei.
         <op> SRC1 IMM16 → emit_i_free(op, 0, rs, imm).
 */
      const int source_register = parse_register();
      imm_expr* const immediate = parse_imm16();
      emit_i_free(opcode, 0, source_register, immediate);
      break;
    }
    case BRANCH_COPROC_COND_INST: {
      /* BR_COP_OPS: bc1f/bc1t/bc1fl/bc1tl.  Two forms:
           <op> LABEL                  → cc=0
           <op> CC_REG LABEL           → cc=TOK_INT
         The RT field is cc_to_rt(cc, nd, tf); RS is 0 (
         uses BIN_RS($1.i) which is the token-number's bits 21-25,
         always 0 for spim's token-number range).  See
        */
      const int nullify_delay = opcode_is_nullified_branch(opcode) ? 1 : 0;
      const int true_false = opcode_is_true_branch(opcode) ? 1 : 0;
      int condition_code = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        condition_code = scan_value.integer;
      }
      imm_expr* const target = parse_label();
      const int target_register = (condition_code << 2) | (nullify_delay << 1) | true_false;
      emit_i_free(opcode, target_register, 0, target);
      break;
    }
    case MOVC_TYPE_INST: {
      /* MOVECC_OPS: movf/movt DEST SRC1 TOK_INT (cc number).
        : emit_r($1, DEST, SRC1, (TOK_INT&7)<<2). */
      const int destination_register = parse_register();
      const int source_register = parse_register();
      int condition_code = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        condition_code = scan_value.integer;
      }
      emit_r(opcode, destination_register, source_register, (condition_code & 0x7) << 2);
      break;
    }

    /* --- FP family --- */
    case FP_FD_FS_INST: {
      /* FP_UNARY_OPS / FP_MOVE_OPS: <op> F_DEST F_SRC2.
         etc.: emit_fp_r($1, $2, $3, 0). */
      const int floating_point_destination = parse_fp_register();
      const int floating_point_source = parse_fp_register();
      emit_fp_r(opcode, floating_point_destination, floating_point_source, 0);
      break;
    }
    case FP_FD_FS_FT_INST: {
      /* FP_BINARY_OPS: <op> F_DEST F_SRC1 F_SRC2.
       */
      const int floating_point_destination = parse_fp_register();
      const int floating_point_source = parse_fp_register();
      const int floating_point_target = parse_fp_register();
      emit_fp_r(opcode, floating_point_destination, floating_point_source, floating_point_target);
      break;
    }
    case FP_CMP_FS_FT_INST: {
      /* FP_CMP_OPS: two forms:
           <op> F_SRC1 F_SRC2                   → cc=0
           <op> CC_REG F_SRC1 F_SRC2            → cc=TOK_INT */
      int condition_code = 0;
      if (scanner_peek() == TOK_INT) {
        scanner_advance();
        condition_code = scan_value.integer;
      }
      const int floating_point_source = parse_fp_register();
      const int floating_point_target = parse_fp_register();
      emit_fp_compare(opcode, floating_point_source, floating_point_target, condition_code);
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
      const int floating_point_destination = parse_fp_register();
      const int floating_point_source = parse_fp_register();
      if (scanner_peek() == TOK_REG) {
        const int target_register = parse_register();
        emit_fp_r(opcode, floating_point_destination, floating_point_source, target_register);
      } else if (scanner_peek() == TOK_INT) {
        scanner_advance();
        const int condition_code = scan_value.integer;
        emit_fp_r(opcode, floating_point_destination, floating_point_source, (condition_code & 0x7) << 2);
      } else {
        /* No third operand (movf/movt with implicit cc=0). */
        emit_fp_r(opcode, floating_point_destination, floating_point_source, 0);
      }
      break;
    }
    case FP_FT_ADDR_INST: {
      /* lwc1 / ldc1 / lwc2 etc.: <op> F_REG ADDRESS.
       */
      const int floating_point_addend = parse_fp_register();
      addr_expr* const address = parse_address();
      emit_i(opcode, floating_point_addend, addr_expr_reg(address), addr_expr_imm(address));
      free(addr_expr_imm(address));
      free(address);
      break;
    }
    case FP_RT_COPREG_INST: {
      /* mfc1/mtc1/cfc0/ctc0/cfc1/ctc1: <op> REG COP_REG.
         COP_REG accepts either TOK_REG OR TOK_FP_REG (
         2574) — both name the coprocessor register number. */
      const int register_number = parse_register();
      const int coprocessor_register =
          scanner_peek() == TOK_FP_REG ? parse_fp_register() : parse_register();
      emit_fp_r(opcode, 0, coprocessor_register, register_number);
      break;
    }
    default:
      parse_error_at("Instruction not yet supported");
      sync_to_nl();
      break;
  }
}

static void parse_directive(int directive_token) {
  switch (directive_token) {
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
static bool is_directive(int token) {
  const int t = find_op_type(token);
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
  char* const symbol = (char*)scan_value.pointer;
  const int separator = scanner_advance(); /* ':' or '=' */

  if (separator == ':') {
    /* The label's address is assigned at emit time (emit_one's
       AST_LABEL_DEF case), where it also conses onto this_line_labels so
       a subsequent .word's alignment can retroactively fix the address
       via fix_current_label_address before the use is resolved. */
    emit_label_normal(symbol);
    free(symbol);
  } else {
    /* ID '=' EXPR : constant label */
    const int v = parse_expression();
    emit_label_const(symbol, v);
    free(symbol);
  }
}

/* LINE / LBL_CMD / CMD */
static void parse_line(void) {
  /* Empty line? */
  if (scanner_peek() == TOK_NL) {
    scanner_advance();
    return;
  } else if (scanner_peek() == TOK_EOF) {
    return;
  } else {
    /* Look ahead for optional label */
    if (scanner_peek() == TOK_ID &&
        (scanner_peek2() == ':' || scanner_peek2() == '=')) {
      parse_opt_label();
    }

    /* Then maybe directive or instruction */
    const int t = scanner_peek();
    if (t == TOK_NL) {
      scanner_advance();
      return;
    } else if (t == TOK_EOF) {
      return;
    } else {
      if (is_directive(t)) {
        const int directive_token = scanner_advance();
        parse_directive(directive_token);
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
      } else {
        /* At TOK_EOF: there is no newline to consume. */
      }
    }
  }
}

/* ---------------- public API ---------------- */

void parser_init(FILE* input, char* file_name) {
  scanner_init(input);
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
  current_file = ast_make_file(file_name);
}

/* The last top-level node of FILE's tree, or nullptr if it has none. */
static const ast_node* last_child(const ast_node* file) {
  if (file == nullptr) return nullptr;
  const ast_node* last = nullptr;
  for (const ast_node* n = file->payload.file.child; n != nullptr; n = n->next)
    last = n;
  return last;
}

int parse_file(void) {
  /* Emit only what this call parses, starting in the segment that was
     current when it began.  The REPL's `.` command parses one line onto
     the tree of the file loaded last, whose nodes were emitted already;
     and the parse itself moves data_directive/text_directive as it meets .data and
     .text, which emit_ast must replay from the start. */
  const ast_node* const emitted = last_child(current_file);
  const bool start_data_directive = data_dir;
  const bool start_text_directive = text_dir;
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
  if (print_ast_after_parse && current_file != nullptr) {
    ast_print(current_file, ast_print_out ? ast_print_out : stderr);
  }

  /* If -show-expansion was requested, dump just the pseudo-op
     wrappers from this file. */
  if (show_expansion_after_parse && current_file != nullptr) {
    print_pseudo_walk(current_file,
                      show_expansion_out ? show_expansion_out : stderr);
  }

  /* If -print-ast-json was requested, dump the tree as JSON. */
  if (print_ast_json_after_parse && current_file != nullptr) {
    ast_print_json(current_file, ast_json_out ? ast_json_out : stderr);
  }

  /* Walk the tree and emit code via the action helpers.  Skipped when
     the user asked to just inspect the tree (-print-ast /
     -show-expansion / -print-ast-json). */
  if (!print_ast_only_ && current_file != nullptr) {
    data_dir = start_data_directive;
    text_dir = start_text_directive;
    emit_ast(current_file, emitted);
  }

  return parse_errors_seen;
}

/* ------- emit_ast — AST walker ------------------------------

   Walks the file's children in source order, calling the action
   helpers (r_type, store_word, record_label, ...) that commit
   each node's effect to the simulator's memory and symbol table.

   This is the only driver for the action helpers: the parse phase
   builds the tree and emit_ast walks it to commit the effects.

   Forward references in `.word LABEL`-style data work via a small
   side channel: the parser attaches the unresolved label to the
   imm_expr in the AST_DATA_* node, and emit_data_int_node here
   records the use at the correct data PC at emit time.
*/

static void emit_one(const ast_node* node);

/* Emit FILE's top-level nodes that come after AFTER (all of them when AFTER is
   nullptr). */
void emit_ast(const ast_node* file, const ast_node* after) {
  if (file == nullptr || file->kind != AST_FILE) return;
  const ast_node* const first =
      after == nullptr ? file->payload.file.child : after->next;
  for (const ast_node* n = first; n != nullptr; n = n->next) {
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
  /* Stop overriding the source annotation; any later direct emission
     (e.g. interactive REPL) should read the live scanner line. */
  emit_source_clear();
}

/* Emit one .byte/.half/.word value list.  For literal-only exprs
   (no symbol), evaluate and store.  For symbol-bearing exprs whose
   symbol is still unresolved at emit time, store the partial value
   and record the use at the (now-correct) data PC; the existing
   resolve_label_uses mechanism patches it when the label is later
   defined. */
static void emit_data_int_node(const ast_node* node, void (*store)(int),
                               int width) {
  for (int i = 0; i < node->payload.data_integer.count; i++) {
    imm_expr* const e = node->payload.data_integer.expressions[i];
    if (e == nullptr) {
      store(0);
      continue;
    }
    if (e->symbol != nullptr && !SYMBOL_IS_DEFINED(e->symbol)) {
      record_data_uses_symbol(current_data_pc(), e->symbol, e->offset, width);
      store(e->offset);
    } else {
      store(eval_imm_expr(e));
    }
  }
}

static void emit_data_fp_node(const ast_node* node, void (*store)(double*)) {
  for (int i = 0; i < node->payload.data_floating_point.count; i++) {
    double temporary = node->payload.data_floating_point.values[i];
    store(&temporary);
  }
}

static void emit_node(const ast_node* node);

/* Emit NODE, recording which instructions a pseudo-op, or a real
   instruction the assembler has to expand, became (expansion_begin). */
static void emit_one(const ast_node* node) {
  if (node == nullptr) return;
  if (node->kind == AST_PSEUDO) {
    expansion_begin(node->payload.pseudo.mnemonic, true);
    emit_node(node);
    expansion_end();
  } else if (node->kind == AST_INST_I) {
    /* The one kind i_type may expand (a label or wide immediate). */
    expansion_begin(op_token_name(node->payload.instruction_i.opcode), false);
    emit_node(node);
    expansion_end();
  } else {
    emit_node(node);
  }
}

static void emit_node(const ast_node* node) {
  /* Restore the per-node source line so listing observers (and any
     parse_error / parse_warn fired from inside an action helper)
     report the right line.  Without this, the scanner's line_no
     stays at the file's last line throughout emit_ast.  The source
     TEXT (for an assembled instruction's annotation) comes from the
     node too — see ast_node.source_text — since source_line() would
     likewise return the stale last line at emit time. */
  line_no = node->source_line;
  emit_source_set(node->source_text);

  switch (node->kind) {
    case AST_FILE:
    case AST_ERROR:
      return; /* shouldn't appear as a direct child of the file root */

    /* Segment changes — mirror parse_dir_data / parse_dir_text. */
    case AST_DIR_TEXT:
      user_kernel_text_segment(false);
      data_dir = false;
      text_dir = true;
      if (node->payload.directive_segment.has_start_address)
        set_text_pc(node->payload.directive_segment.start_address);
      return;
    case AST_DIR_KTEXT:
      user_kernel_text_segment(true);
      data_dir = false;
      text_dir = true;
      if (node->payload.directive_segment.has_start_address)
        set_text_pc(node->payload.directive_segment.start_address);
      return;
    case AST_DIR_DATA:
      user_kernel_data_segment(false);
      data_dir = true;
      text_dir = false;
      enable_data_alignment();
      auto_align = true;
      if (node->payload.directive_segment.has_start_address)
        set_data_pc(node->payload.directive_segment.start_address);
      return;
    case AST_DIR_KDATA:
      user_kernel_data_segment(true);
      data_dir = true;
      text_dir = false;
      enable_data_alignment();
      auto_align = true;
      if (node->payload.directive_segment.has_start_address)
        set_data_pc(node->payload.directive_segment.start_address);
      return;

    case AST_DIR_ALIGN: {
      const int v = node->payload.directive_align.alignment_power;
      if (v == 0) auto_align = false;
      if (text_dir)
        align_text(v);
      else
        align_data(v);
      return;
    }

    case AST_DIR_SPACE:
      increment_data_pc(node->payload.directive_space.size);
      return;

    case AST_DIR_GLOBL:
      make_label_global(node->payload.directive_globl.name);
      return;

    case AST_DIR_EXTERN: {
      const char* const symbol = node->payload.directive_named_size.name;
      make_label_global((char*)symbol);
      if (lookup_label((char*)symbol)->address == 0)
        record_label((char*)symbol, current_data_pc(), true);
      increment_data_pc(node->payload.directive_named_size.size);
      return;
    }

    case AST_DIR_COMM: {
      const char* const symbol = node->payload.directive_named_size.name;
      align_data(2);
      if (lookup_label((char*)symbol)->address == 0)
        record_label((char*)symbol, current_data_pc(), true);
      increment_data_pc(node->payload.directive_named_size.size);
      return;
    }

    /* Data. */
    case AST_DATA_BYTE:
      emit_data_int_node(node, store_byte, 1);
      return;
    case AST_DATA_HALF:
      align_labels_to(1);
      if (data_dir) set_data_alignment(1);
      emit_data_int_node(node, store_half, 2);
      return;
    case AST_DATA_WORD:
      align_labels_to(2);
      if (data_dir) set_data_alignment(2);
      emit_data_int_node(node, store_word, 4);
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
        store_string(node->payload.data_string.bytes, node->payload.data_string.length,
                     node->payload.data_string.null_terminate);
      }
      return;

    /* Labels. */
    case AST_LABEL_DEF:
      if (node->payload.label_definition.kind == AST_LABEL_NORMAL) {
        const mem_addr address = text_dir ? current_text_pc() : current_data_pc();
        label* const l =
            record_label((char*)node->payload.label_definition.name, address, false);
        cons_label(l);
      } else {
        label* l = record_label((char*)node->payload.label_definition.name,
                                (mem_addr)node->payload.label_definition.value, true);
        l->constant_flag = true;
      }
      return;

    /* Instructions — pass copies of imm_expr so the AST keeps its
       own.  The action helpers (i_type, j_type) already
       copy what they store, so a single dup is enough for either
       to drop on the floor / store internally. */
    case AST_INST_R:
      r_type(node->payload.instruction_r.opcode, node->payload.instruction_r.destination_register, node->payload.instruction_r.source_register,
             node->payload.instruction_r.target_register);
      return;
    case AST_INST_R_SHIFT:
      r_sh_type(node->payload.instruction_r_shift.opcode, node->payload.instruction_r_shift.destination_register,
                node->payload.instruction_r_shift.target_register, node->payload.instruction_r_shift.shift_amount);
      return;
    case AST_INST_I: {
      imm_expr* const copy = dup_imm(node->payload.instruction_i.immediate);
      i_type_free(node->payload.instruction_i.opcode, node->payload.instruction_i.target_register, node->payload.instruction_i.source_register,
                  copy);
      return;
    }
    case AST_INST_J: {
      imm_expr* const copy = dup_imm(node->payload.instruction_j.target);
      j_type(node->payload.instruction_j.opcode, copy);
      free(copy);
      return;
    }
    case AST_INST_FP_R:
      r_co_type(node->payload.instruction_floating_point_r.opcode, node->payload.instruction_floating_point_r.floating_point_destination,
                node->payload.instruction_floating_point_r.floating_point_source, node->payload.instruction_floating_point_r.floating_point_target);
      return;
    case AST_INST_FP_COMPARE:
      r_cond_type(node->payload.instruction_floating_point_compare.opcode, node->payload.instruction_floating_point_compare.floating_point_source,
                  node->payload.instruction_floating_point_compare.floating_point_target, node->payload.instruction_floating_point_compare.condition_code);
      return;

    case AST_PSEUDO:
      /* Walk the expansion children — the real instructions the
         parser emitted for this pseudo-op (la, li, bge, ...).  Same
         line-boundary clear_labels rule as the file-level walk. */
      for (const ast_node* c = node->payload.pseudo.child; c != nullptr;
           c = c->next) {
        emit_one(c);
        if (c->kind != AST_LABEL_DEF && c->kind != AST_DIR_GLOBL) {
          clear_labels();
        }
      }
      return;
    default:
      /* Never reached: the cases above list every ast_kind, and the build's
       * -Wswitch-enum fails if a new one is added without a case. */
      fatal_error("internal error: emit_one: unknown AST node kind %d\n",
                  (int)node->kind);
  }
}
