/* SPIM S20 MIPS simulator.
   Hand-written recursive-descent parser.

   Replaces src/parser.y during Phases 2-4 of the parser
   migration.  Selected at runtime by the `-parser=hand` flag.

   Phase 2 pilot subset covers:
     - LINE / label / directive / instruction dispatch
     - R-type 3-reg arithmetic (add, addu, and, or, etc.)
     - I-type 2-reg+imm (addi, addiu, andi, ori, etc.)
     - Shifts (sll, sra, srl)
     - Load/store with full ADDR (lb, lw, sb, sw, etc.)
     - Branches (beq, bne, bgez, bltz)
     - Jumps (j, jal)
     - NULLARY (syscall, break)
     - LUI
     - Directives: .data, .text, .globl, .word, .byte,
       .asciiz, .ascii, .space, .align, .extern, .kdata,
       .ktext, .half, .float, .double, .comm

   Out of pilot scope (Phase 3):
     - Pseudo-ops: la, li, div, mulo, sle, seq, branch
       pseudo-ops (beqz, bgt, ble, b), nop is supported via
       hp_nop_inst since `nop` keyword expands to `sll $0,$0,0`.
     - FP family.
     - Coprocessor + TLB instructions.
     - Trap family.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause.
*/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "spim.h"
#include "inst.h"
#include "data.h"
#include "sym-tbl.h"
#include "scanner.h"
#include "parser.h"
#include "tokens.h"
#include "spim-utils.h"
#include "hp_parser.h"
#include "hp_pseudo_op.h"

extern int hp_scanner_peek(void);
extern int hp_scanner_peek2(void);
extern int hp_scanner_advance(void);
extern void hp_scanner_init(FILE* in);
extern void hp_scanner_start_line(void);
extern void hp_scanner_force_identifier(void);

/* ------- runtime-visible globals (Phase 5 — sole owner) ----- */

bool data_dir = false;            /* => item in data segment */
bool text_dir = true;             /* => item in text segment */
/* parse_error_occurred / parse_errors_seen live in hp_pseudo_op.c
   so the yyerror funnel (also in hp_pseudo_op.c) sets them without
   pulling hp_parser.o into translation units that only call
   yyerror (sym-tbl.c). */
extern bool parse_error_occurred;
extern int  parse_errors_seen;

/* Source file name, used by yyerror / yywarn for messages.  Exposed
   via the accessor below so hp_pseudo_op.c can read it without
   needing the static directly. */
static char* hp_input_file_name = NULL;
char* hp_input_file_name_get(void) { return hp_input_file_name; }
void hp_set_input_file_name(char* name) { hp_input_file_name = name; }

/* These globals control directive emission.  Local to this TU. */

static bool hp_null_term;
static void (*hp_store_op)(int);
static void (*hp_store_fp_op)(double*);

/* Labels collected on the current line, flushed (resolved + freed)
   at line end.  This mirrors parser.y's `this_line_labels` exactly:
   the list persists across newlines until an ASM_CODE or
   ASM_DIRECTIVE clears it, so that `set_data_alignment` (called
   from inside the directive) can retroactively update label
   addresses via fix-up.  See parser.y CMD action.

   Phase 5 cleanup will merge with parser.y's static. */

typedef struct hp_label_cell {
  label* lab;
  struct hp_label_cell* next;
} hp_label_cell;

static hp_label_cell* hp_this_line_labels = NULL;

/* Mirror of data.c's `enable_data_auto_alignment` static.
   Cleared by `.align 0`, set by `.data`/`.kdata`. */
static bool hp_auto_align = true;

static void hp_cons_label(label* l) {
  hp_label_cell* c = (hp_label_cell*)xmalloc(sizeof(hp_label_cell));
  c->lab = l;
  c->next = hp_this_line_labels;
  hp_this_line_labels = c;
}

/* Resolve and free.  Mirrors parser.y's `clear_labels()`. */
static void hp_clear_labels(void) {
  while (hp_this_line_labels != NULL) {
    hp_label_cell* next = hp_this_line_labels->next;
    resolve_label_uses(hp_this_line_labels->lab);
    free(hp_this_line_labels);
    hp_this_line_labels = next;
  }
}

/* Retroactive label fix-up.  Called both internally (before
   alignment-emitting directives) and externally from data.c /
   inst.c whenever `align_data` / `align_text` advances the PC for
   alignment, so any labels recorded on the current line follow
   the PC to its aligned position. */
void fix_current_label_address(mem_addr new_addr) {
  for (hp_label_cell* c = hp_this_line_labels; c != NULL; c = c->next) {
    c->lab->addr = new_addr;
  }
}
/* Internal call sites kept under the old name to minimise churn. */
#define hp_fix_line_labels fix_current_label_address

/* External spim runtime functions */
extern void r_type_inst(int opcode, int rd, int rs, int rt);
extern void r_sh_type_inst(int opcode, int rd, int rt, int shamt);
extern void r_co_type_inst(int opcode, int rd, int rs, int rt);
extern void r_cond_type_inst(int opcode, int rs, int rt, int cc);
extern void i_type_inst(int opcode, int rt, int rs, imm_expr* expr);
extern void i_type_inst_free(int opcode, int rt, int rs, imm_expr* expr);
extern void j_type_inst(int opcode, imm_expr* target);
extern void store_word(int);
extern void store_half(int);
extern void store_byte(int);
extern void store_double(double*);
extern void store_float(double*);
extern void store_string(char* string, int length, bool null_terminate);
extern void increment_data_pc(int);
extern void increment_text_pc(int);
extern void align_data(int);
extern void align_text(int);
extern void set_data_alignment(int);
extern void enable_data_alignment(void);
extern void user_kernel_data_segment(bool kernel);
extern void user_kernel_text_segment(bool kernel);
extern void set_data_pc(mem_addr);
extern void set_text_pc(mem_addr);
extern mem_addr current_data_pc(void);
extern mem_addr current_text_pc(void);
extern imm_expr* make_imm_expr(int offset, char* symbol, bool branch_relative);
extern imm_expr* const_imm_expr(int value);
extern imm_expr* incr_expr_offset(imm_expr* expr, int delta);
extern addr_expr* make_addr_expr(int offset, char* symbol, int reg);
extern int addr_expr_reg(addr_expr* expr);
extern imm_expr* addr_expr_imm(addr_expr* expr);
extern label* make_label_global(char* name);
extern label* record_label(char* name, mem_addr addr, int locality);
extern label* lookup_label(char* name);
extern void resolve_label_uses(label* sym);
extern void flush_local_labels(int issue_undef_warning);

/* ---------------- error helpers ---------------- */

static void hp_yyerror(const char* msg) {
  parse_error_occurred = true;
  parse_errors_seen += 1;
  error("spim: (parser) %s on line %d of file %s\n",
        msg, line_no, hp_input_file_name ? hp_input_file_name : "(stdin)");
}

/* Skip tokens up to (but NOT past) the next newline.  parse_line's
   tail consumes that newline; consuming it here would leave the
   outer loop pointing at the next line's first token and the
   newline check would mis-fire "Extra tokens after instruction"
   on every line that follows an ignored directive like `.set`. */
static void hp_sync_to_nl(void) {
  while (hp_scanner_peek() != Y_NL && hp_scanner_peek() != Y_EOF) {
    hp_scanner_advance();
  }
}

/* Consume a token and assert it matches `expected`; on
   mismatch, emit an error and DON'T consume. */
static bool hp_expect(int expected, const char* what) {
  if (hp_scanner_peek() == expected) {
    hp_scanner_advance();
    return true;
  }
  hp_yyerror(what);
  return false;
}

/* ---------------- operand parsers ---------------- */

/* Y_REG → register number 0..31 */
static int parse_register(void) {
  if (hp_scanner_peek() == Y_REG) {
    hp_scanner_advance();
    return yylval.i;
  }
  hp_yyerror("Expected register");
  return 0;
}

/* Y_FP_REG → FP register number 0..31 */
static int parse_fp_register(void) {
  if (hp_scanner_peek() == Y_FP_REG) {
    hp_scanner_advance();
    return yylval.i;
  }
  hp_yyerror("Expected FP register");
  return 0;
}

/* ABS_ADDR : Y_INT | Y_INT '+' Y_INT | Y_INT Y_INT (negative sub) */
static int parse_abs_addr(void) {
  if (hp_scanner_peek() != Y_INT) {
    hp_yyerror("Expected integer");
    return 0;
  }
  hp_scanner_advance();
  int v = yylval.i;
  if (hp_scanner_peek() == '+') {
    hp_scanner_advance();
    if (hp_scanner_peek() != Y_INT) {
      hp_yyerror("Expected integer after '+'");
      return v;
    }
    hp_scanner_advance();
    return v + yylval.i;
  }
  /* Y_INT Y_INT (with the second negative) is bison's quirky
     subtraction handling — see scanner-parser-inventory Part 8F. */
  if (hp_scanner_peek() == Y_INT) {
    /* peek the next value */
    hp_scanner_advance();
    if (yylval.i >= 0) hp_yyerror("Syntax error");
    return v - (-yylval.i);
  }
  return v;
}

/* FACTOR : Y_INT | '(' EXPR ')' | ID  (parser.y:2628-2643).
   ID resolves to its label's address; forward references
   record a data-uses-symbol entry for later resolution. */
extern void record_data_uses_symbol(mem_addr location, label* sym);

static int parse_expr(void);  /* forward */
static int parse_factor(void) {
  if (hp_scanner_peek() == Y_INT) {
    hp_scanner_advance();
    return yylval.i;
  }
  if (hp_scanner_peek() == '(') {
    hp_scanner_advance();
    int v = parse_expr();
    hp_expect(')', "Expected ')'");
    return v;
  }
  if (hp_scanner_peek() == Y_ID) {
    hp_scanner_advance();
    char* sym_name = (char*)yylval.p;
    label* l = lookup_label(sym_name);
    if (l->addr == 0) {
      /* Forward reference — record for resolution at label-defn time. */
      record_data_uses_symbol(current_data_pc(), l);
      free(sym_name);
      return 0;
    }
    int addr = (int)l->addr;
    free(sym_name);
    return addr;
  }
  hp_yyerror("Expected expression");
  return 0;
}

/* TRM : FACTOR ( ('*'|'/') FACTOR )* */
static int parse_term(void) {
  int v = parse_factor();
  while (hp_scanner_peek() == '*' || hp_scanner_peek() == '/') {
    int op = hp_scanner_advance();
    int r = parse_factor();
    if (op == '*') v *= r;
    else v = (r == 0) ? 0 : v / r;
  }
  return v;
}

/* EXPR : TRM ( ('+'|'-') TRM )* */
static int parse_expr(void) {
  int v = parse_term();
  while (hp_scanner_peek() == '+' || hp_scanner_peek() == '-') {
    int op = hp_scanner_advance();
    int r = parse_term();
    if (op == '+') v += r;
    else          v -= r;
  }
  return v;
}

/* EXPRESSION: like EXPR but with the only_id flag */
static int parse_expression(void) {
  hp_scanner_force_identifier();
  return parse_expr();
}

/* IMM32 : ABS_ADDR | '(' ABS_ADDR ')' '>' '>' Y_INT
        | Y_ID | Y_ID '+' ABS_ADDR | Y_ID '-' ABS_ADDR */
static imm_expr* parse_imm32(void) {
  hp_scanner_force_identifier();
  /* Y_ID lookahead: maybe followed by + or - */
  if (hp_scanner_peek() == Y_ID) {
    hp_scanner_advance();
    char* sym = (char*)yylval.p;
    if (hp_scanner_peek() == '+') {
      hp_scanner_advance();
      int off = parse_abs_addr();
      imm_expr* r = make_imm_expr(off, sym, false);
      /* Don't free sym — make_imm_expr stores it.  Actually bison
         frees it after — but make_imm_expr likely copies.  Mirror
         the bison action: free here. */
      free(sym);
      return r;
    }
    if (hp_scanner_peek() == '-') {
      hp_scanner_advance();
      int off = parse_abs_addr();
      imm_expr* r = make_imm_expr(-off, sym, false);
      free(sym);
      return r;
    }
    return make_imm_expr(0, sym, false);
    /* sym ownership passes to make_imm_expr; don't free */
  }
  if (hp_scanner_peek() == '(') {
    /* '(' ABS_ADDR ')' '>' '>' Y_INT */
    hp_scanner_advance();
    int v = parse_abs_addr();
    hp_expect(')', "Expected ')'");
    hp_expect('>', "Expected '>'");
    hp_expect('>', "Expected '>'");
    if (hp_scanner_peek() != Y_INT) {
      hp_yyerror("Expected integer after '>>'");
      return make_imm_expr(0, NULL, false);
    }
    hp_scanner_advance();
    int sh = yylval.i;
    return make_imm_expr(v >> sh, NULL, false);
  }
  /* ABS_ADDR */
  int v = parse_abs_addr();
  return make_imm_expr(v, NULL, false);
}

/* IMM16, UIMM16 — IMM32 plus range check */
static imm_expr* parse_imm16(void) {
  imm_expr* e = parse_imm32();
  hp_check_imm_range(e, IMM_MIN, IMM_MAX);
  return e;
}

static imm_expr* parse_uimm16(void) {
  imm_expr* e = parse_imm32();
  hp_check_uimm_range(e, UIMM_MIN, UIMM_MAX);
  return e;
}

/* BR_IMM32 — like IMM32 but with only_id toggle */
static imm_expr* parse_br_imm32(void) {
  return parse_imm32();  /* hp_scanner_force_identifier already set inside */
}

/* LABEL — like ID but produces a PC-relative imm_expr suitable
   for a branch target.  Matches parser.y:2579-2582 exactly:
   the initial offset is `-current_text_pc()`, which gets
   overwritten with `-pc` in resolve_a_label_sub at fix-up
   time, encoding the (target - branch_pc) displacement that
   MIPS branches require. */
static imm_expr* parse_label(void) {
  if (hp_scanner_peek() != Y_ID) {
    hp_yyerror("Expected label");
    return make_imm_expr(0, NULL, true);
  }
  hp_scanner_advance();
  char* sym = (char*)yylval.p;
  imm_expr* r = make_imm_expr(-(int)current_text_pc(), sym, true);
  /* sym ownership: bison-side LABEL action does NOT free $1.p
     (because make_imm_expr stores it).  Mirror that. */
  return r;
}

/* ADDR — the 9-alternative address production.  This is where
   the 25 shift-reduce conflicts collapse into one explicit
   lookahead. */
static addr_expr* parse_address(void) {
  hp_scanner_force_identifier();

  /* '(' REGISTER ')' */
  if (hp_scanner_peek() == '(') {
    hp_scanner_advance();
    int reg = parse_register();
    hp_expect(')', "Expected ')' after register");
    return make_addr_expr(0, NULL, reg);
  }

  /* ABS_ADDR [ '(' REGISTER ')' ] */
  if (hp_scanner_peek() == Y_INT) {
    int imm = parse_abs_addr();
    if (hp_scanner_peek() == '+' && hp_scanner_peek2() == Y_ID) {
      /* ABS_ADDR '+' ID */
      hp_scanner_advance();  /* + */
      hp_scanner_force_identifier();
      hp_scanner_advance();
      char* sym = (char*)yylval.p;
      addr_expr* r = make_addr_expr(imm, sym, 0);
      return r;
    }
    if (hp_scanner_peek() == '(') {
      hp_scanner_advance();
      int reg = parse_register();
      hp_expect(')', "Expected ')'");
      return make_addr_expr(imm, NULL, reg);
    }
    return make_addr_expr(imm, NULL, 0);
  }

  /* Y_ID [ '+' ABS_ADDR | '-' ABS_ADDR ] [ '(' REGISTER ')' ] */
  if (hp_scanner_peek() == Y_ID) {
    hp_scanner_advance();
    char* sym = (char*)yylval.p;
    int off = 0;
    if (hp_scanner_peek() == '+') {
      hp_scanner_advance();
      off = parse_abs_addr();
    } else if (hp_scanner_peek() == '-') {
      hp_scanner_advance();
      off = -parse_abs_addr();
    }
    int reg = 0;
    if (hp_scanner_peek() == '(') {
      hp_scanner_advance();
      reg = parse_register();
      hp_expect(')', "Expected ')'");
    }
    addr_expr* r = make_addr_expr(off, sym, reg);
    free(sym);
    return r;
  }

  hp_yyerror("Expected address");
  return make_addr_expr(0, NULL, 0);
}

/* ---------------- instruction parsers ---------------- */

/* Return true if `op` is a BINARYI_OPS opcode (has both reg-reg
   and reg-imm forms — add, addu, and, or, xor, slt, sltu).
   See parser.y's BINARYI_OPS production. */
static bool op_has_imm_form(int op) {
  switch (op) {
    case Y_ADD_OP:
    case Y_ADDU_OP:
    case Y_AND_OP:
    case Y_OR_OP:
    case Y_XOR_OP:
    case Y_SLT_OP:
    case Y_SLTU_OP:
      return true;
    default:
      return false;
  }
}

/* R3_TYPE_INST: <op> DEST, SRC1, SRC2.  For BINARYI_OPS opcodes,
   also accepts <op> DEST, SRC1, IMM and <op> DEST, IMM (see
   parser.y:941-956).  SUB_OPS (sub, subu) similarly accept an
   immediate, converted to addi/addiu with a negated value
   (parser.y:1068-1103). */
extern int32 eval_imm_expr(imm_expr* expr);

static void parse_r3(int op) {
  int rd = parse_register();
  if (op == Y_CLO_OP || op == Y_CLZ_OP) {
    /* COUNT_LEADING_OPS DEST SRC1 — RT must equal RD.
       parser.y:928-932. */
    int rs = parse_register();
    r_type_inst(op, rd, rs, rd);
    return;
  }
  if (op == Y_MUL_OP) {
    /* MULT_OPS3 DEST SRC1 (SRC2|IMM).  3-reg or 3-with-imm
       (parser.y:1159-1170).  Imm form uses $at + ori. */
    int rs = parse_register();
    if (hp_scanner_peek() == Y_REG) {
      int rt = parse_register();
      r_type_inst(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      i_type_inst_free(Y_ORI_OP, 1, 0, imm);
      r_type_inst(op, rd, rs, 1);
    }
    return;
  }
  if (op == Y_SUB_OP || op == Y_SUBU_OP) {
    /* SUB_OPS: 3-reg, 3-with-imm, or 2-with-imm.  Convert imm
       form to addi/addiu with negated value. */
    if (hp_scanner_peek() != Y_REG) {
      imm_expr* imm = parse_imm32();
      int val = eval_imm_expr(imm);
      i_type_inst(op == Y_SUB_OP ? Y_ADDI_OP : Y_ADDIU_OP,
                  rd, rd, make_imm_expr(-val, NULL, false));
      free(imm);
      return;
    }
    int rs = parse_register();
    if (hp_scanner_peek() == Y_REG) {
      int rt = parse_register();
      r_type_inst(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      int val = eval_imm_expr(imm);
      i_type_inst(op == Y_SUB_OP ? Y_ADDI_OP : Y_ADDIU_OP,
                  rd, rs, make_imm_expr(-val, NULL, false));
      free(imm);
    }
    return;
  }
  if (op_has_imm_form(op)) {
    /* Two-operand form: <op> DEST, IMM */
    if (hp_scanner_peek() != Y_REG) {
      imm_expr* imm = parse_imm32();
      i_type_inst_free(hp_op_to_imm_op(op), rd, rd, imm);
      return;
    }
    int rs = parse_register();
    /* Three-operand form: <op> DEST, SRC1, SRC2 (reg) or
       <op> DEST, SRC1, IMM (immediate) */
    if (hp_scanner_peek() == Y_REG) {
      int rt = parse_register();
      r_type_inst(op, rd, rs, rt);
    } else {
      imm_expr* imm = parse_imm32();
      i_type_inst_free(hp_op_to_imm_op(op), rd, rs, imm);
    }
    return;
  }
  /* Plain R3 (no immediate form): three registers */
  int rs = parse_register();
  int rt = parse_register();
  r_type_inst(op, rd, rs, rt);
}

/* R2sh_TYPE_INST: <op> DEST, SRC1, SHAMT (immediate 0..31).
   ssnop is a special no-operand case emitting `sll $0, $0, 1`. */
static void parse_r2sh(int op) {
  if (op == Y_SSNOP_OP) {
    r_sh_type_inst(Y_SLL_OP, 0, 0, 1);
    return;
  }
  int rd = parse_register();
  int rs = parse_register();  /* this is rt in MIPS shift encoding */
  if (hp_scanner_peek() != Y_INT) {
    hp_yyerror("Expected integer shift amount");
    return;
  }
  hp_scanner_advance();
  int sh = yylval.i;
  r_sh_type_inst(op, rd, rs, sh);
}

/* R2ds_TYPE_INST: <op> DEST, SRC1 (mfhi/mflo etc.) — out of pilot */
/* R1s_TYPE_INST: <op> SRC1 (jr) */
static void parse_r1s(int op) {
  int rs = parse_register();
  r_type_inst(op, 0, rs, 0);
}

/* I2_TYPE_INST: <op> DEST, SRC1, IMM16
   Also accepts the 2-operand shorthand `<op> DEST, IMM16` for
   andi/ori/xori (parser.y:991-994 BINARY_LOGICALI_OPS DEST UIMM16),
   where the missing SRC1 defaults to DEST — exception handlers use
   `ori $k0, 0x1`. */
static void parse_i2(int op) {
  int rt = parse_register();
  int rs;
  if (hp_scanner_peek() == Y_REG) {
    rs = parse_register();
  } else {
    rs = rt;
  }
  /* andi/ori/xori take unsigned; addi/addiu/slti signed.  The
     range check happens inside parse_imm16/parse_uimm16. */
  imm_expr* imm;
  if (op == Y_ANDI_OP || op == Y_ORI_OP || op == Y_XORI_OP) {
    imm = parse_uimm16();
  } else {
    imm = parse_imm16();
  }
  i_type_inst_free(op, rt, rs, imm);
}

/* I1t_TYPE_INST: <op> DEST, UIMM16 (lui) */
static void parse_i1t(int op) {
  int rt = parse_register();
  imm_expr* imm = parse_uimm16();
  i_type_inst_free(op, rt, 0, imm);
}

/* I2a_TYPE_INST: <op> DEST, ADDRESS — loads and stores */
static void parse_i2a(int op) {
  int rt = parse_register();
  addr_expr* addr = parse_address();
  i_type_inst(op, rt, addr_expr_reg(addr), addr_expr_imm(addr));
  /* Bison frees the addr_expr's inner imm + the addr_expr; do the
     same.  spim runtime exposes free_imm_expr but not
     free_addr_expr — see inst.h.  Use free() since that's what
     parser.y does. */
  free(addr_expr_imm(addr));
  free(addr);
}

/* B2_TYPE_INST: <op> SRC1, SRC2, LABEL (beq, bne) — reg/reg form.
   Also <op> SRC1, IMM, LABEL (parser.y:1327-1346) — reg/imm form
   which uses $at + ori to materialize the immediate, then beq/bne. */
static void parse_b2(int op) {
  int src1 = parse_register();
  if (hp_scanner_peek() == Y_REG) {
    int src2 = parse_register();
    imm_expr* target = parse_label();
    i_type_inst_free(op, src2, src1, target);
  } else {
    /* Immediate form (parser.y:1327-1346) */
    imm_expr* imm = parse_imm32();
    imm_expr* target = parse_label();
    extern bool is_zero_imm(imm_expr* expr);
    if (is_zero_imm(imm)) {
      /* Special case: comparing against literal 0 → use $0 directly */
      i_type_inst(op, src1, 0, target);
    } else {
      /* Use $at: ori $at, $0, imm; <op> src1, $at, target */
      i_type_inst(Y_ORI_OP, 1, 0, imm);
      i_type_inst(op, src1, 1, target);
    }
    free(imm);
    free(target);
  }
}

/* B1_TYPE_INST: <op> SRC1, LABEL (bgez, bltz, etc.) */
static void parse_b1(int op) {
  int rs = parse_register();
  imm_expr* target = parse_label();
  i_type_inst_free(op, 0, rs, target);
}

/* J_TYPE_INST: <op> LABEL  OR  <op> SRC1 (j/jal/jr/jalr).
   Matches parser.y:1443-1462.  J_OPS covers j, jal, jr, jalr;
   with a label argument we emit j_type_inst with the right
   absolute-jump opcode; with a register argument we emit
   r_type_inst with the indirect-jump opcode. */
static void parse_j(int op) {
  if (hp_scanner_peek() == Y_REG) {
    /* J_OPS SRC1  or  J_OPS DEST SRC1 — register-indirect jump.
       parser.y:1452-1466. */
    int r1 = parse_register();
    if (hp_scanner_peek() == Y_REG) {
      /* DEST SRC1 form */
      int r2 = parse_register();
      if (op == Y_J_OP || op == Y_JR_OP)
        r_type_inst(Y_JR_OP, 0, r2, 0);
      else if (op == Y_JAL_OP || op == Y_JALR_OP)
        r_type_inst(Y_JALR_OP, r1, r2, 0);
      return;
    }
    /* SRC1-only form */
    if (op == Y_J_OP || op == Y_JR_OP)
      r_type_inst(Y_JR_OP, 0, r1, 0);
    else if (op == Y_JAL_OP || op == Y_JALR_OP)
      r_type_inst(Y_JALR_OP, 31, r1, 0);
    return;
  }
  /* J_OPS LABEL — absolute-target jump */
  imm_expr* target = parse_label();
  if (op == Y_J_OP || op == Y_JR_OP)
    j_type_inst(Y_J_OP, target);
  else if (op == Y_JAL_OP || op == Y_JALR_OP)
    j_type_inst(Y_JAL_OP, target);
  free(target);
}

/* NOARG_TYPE_INST: <op>  (syscall) or <op> IMM (break N, sync N).
   See parser.y:855-866. */
static void parse_noarg(int op) {
  if (hp_scanner_peek() == Y_INT) {
    /* break N or sync N — encode the int in the rd slot.
       Bison: r_type_inst(op, $2.i, 0, 0). */
    hp_scanner_advance();
    int n = yylval.i;
    if (op == Y_BREAK_OP && n == 1) {
      hp_yyerror("Breakpoint 1 is reserved for debugger");
    }
    r_type_inst(op, n, 0, 0);
  } else {
    r_type_inst(op, 0, 0, 0);
  }
}

/* ---------------- directive parsers ---------------- */

static void parse_dir_data(bool kernel) {
  user_kernel_data_segment(kernel);
  data_dir = true; text_dir = false;
  enable_data_alignment();
  hp_auto_align = true;   /* mirror data.c's enable_data_auto_alignment */
  if (hp_scanner_peek() == Y_INT) {
    hp_scanner_advance();
    set_data_pc(yylval.i);
  }
}

static void parse_dir_text(bool kernel) {
  user_kernel_text_segment(kernel);
  data_dir = false; text_dir = true;
  if (hp_scanner_peek() == Y_INT) {
    hp_scanner_advance();
    set_text_pc(yylval.i);
  }
}

static void parse_dir_globl(void) {
  hp_scanner_force_identifier();
  if (hp_scanner_peek() != Y_ID) {
    hp_yyerror("Expected identifier after .globl");
    return;
  }
  hp_scanner_advance();
  make_label_global((char*)yylval.p);
  free((char*)yylval.p);
}

static void parse_dir_align(void) {
  int v = parse_expression();
  if (v == 0) hp_auto_align = false;   /* mirror data.c's flag */
  if (text_dir) align_text(v);
  else          align_data(v);
}

static void parse_dir_extern(void) {
  hp_scanner_force_identifier();
  if (hp_scanner_peek() != Y_ID) { hp_yyerror("Expected ID"); return; }
  hp_scanner_advance();
  char* sym = (char*)yylval.p;
  int sz = parse_expression();
  make_label_global(sym);
  if (lookup_label(sym)->addr == 0) {
    record_label(sym, current_data_pc(), 1);
  }
  free(sym);
  increment_data_pc(sz);
}

static void parse_dir_comm(void) {
  hp_scanner_force_identifier();
  if (hp_scanner_peek() != Y_ID) { hp_yyerror("Expected ID"); return; }
  hp_scanner_advance();
  char* sym = (char*)yylval.p;
  int sz = parse_expression();
  align_data(2);
  if (lookup_label(sym)->addr == 0) {
    record_label(sym, current_data_pc(), 1);
  }
  free(sym);
  increment_data_pc(sz);
}

static void parse_dir_space(void) {
  int v = parse_expression();
  if (text_dir) { hp_yyerror("Can't put data in text segment"); return; }
  increment_data_pc(v);
}

/* EXPR_LST: emit each expression value via hp_store_op */
static void parse_expr_list(void) {
  for (;;) {
    int v = parse_expression();
    hp_store_op(v);
    /* commas are skipped by the scanner; same with whitespace */
    if (hp_scanner_peek() == Y_NL || hp_scanner_peek() == Y_EOF) break;
    /* Otherwise continue to next expression */
  }
}

/* Pre-fix any labels currently on this_line_labels to the
   address they'll occupy AFTER `set_data_alignment(N)` aligns
   the data PC.  parser.y achieves this via `align_data` →
   `fix_current_label_address`, which sees bison's static
   `this_line_labels`; we maintain our own and pre-fix here.
   Respects the same auto-align gate that data.c uses. */
static void hp_align_labels_to(int alignment) {
  if (!data_dir || !hp_auto_align || hp_this_line_labels == NULL) return;
  mem_addr cur = current_data_pc();
  mem_addr aligned = (cur + (1u << alignment) - 1) & (~0u << alignment);
  hp_fix_line_labels(aligned);
}

/* FP_EXPR_LST: consume Y_FP tokens, emit via hp_store_fp_op. */
static void parse_fp_expr_list(void) {
  for (;;) {
    if (hp_scanner_peek() != Y_FP) {
      hp_yyerror("Expected floating-point literal");
      return;
    }
    hp_scanner_advance();
    double* val = (double*)yylval.p;
    hp_store_fp_op(val);
    if (hp_scanner_peek() == Y_NL || hp_scanner_peek() == Y_EOF) break;
  }
}

static void parse_dir_float(void) {
  hp_align_labels_to(2);
  hp_store_fp_op = store_float;
  if (data_dir) set_data_alignment(2);
  parse_fp_expr_list();
}

static void parse_dir_double(void) {
  hp_align_labels_to(3);
  hp_store_fp_op = store_double;
  if (data_dir) set_data_alignment(3);
  parse_fp_expr_list();
}

static void parse_dir_word(void)  {
  hp_align_labels_to(2);
  hp_store_op = store_word;
  if (data_dir) set_data_alignment(2);
  parse_expr_list();
}
static void parse_dir_half(void)  {
  hp_align_labels_to(1);
  hp_store_op = store_half;
  if (data_dir) set_data_alignment(1);
  parse_expr_list();
}
static void parse_dir_byte(void)  { hp_store_op = store_byte; parse_expr_list(); }

/* STR_LST: emit each string via store_string */
static void parse_string_list(void) {
  for (;;) {
    if (hp_scanner_peek() != Y_STR) {
      hp_yyerror("Expected string literal");
      return;
    }
    hp_scanner_advance();
    char* s = (char*)yylval.p;
    int len = (int)strlen(s);
    if (text_dir) {
      hp_yyerror("Can't put data in text segment");
    } else {
      store_string(s, len, hp_null_term);
    }
    free(s);
    if (hp_scanner_peek() == Y_NL || hp_scanner_peek() == Y_EOF) break;
  }
}

static void parse_dir_ascii(void)  { hp_null_term = false; parse_string_list(); }
static void parse_dir_asciiz(void) { hp_null_term = true;  parse_string_list(); }

/* ---------------- top-level dispatch ---------------- */

/* Look up an opcode's TYPE field (from op.h's X-macro).  We
   rebuild a small table here keyed on opcode-token value. */

typedef struct { int op; int type; } op_type_entry;

static op_type_entry op_type_table[] = {
#define OP(NAME, OPCODE, TYPE, R_OPCODE) {OPCODE, TYPE},
#include "op.h"
};

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
static void parse_pseudo(int op) {
  switch (op) {
    case Y_LI_POP: {
      /* li DEST, IMM32  →  ori DEST, $0, imm  (parser.y:648-651) */
      int rt = parse_register();
      imm_expr* imm = parse_imm32();
      i_type_inst_free(Y_ORI_OP, rt, 0, imm);
      break;
    }
    case Y_MOVE_POP: {
      /* move DEST, SRC1  →  addu DEST, $0, SRC1  (parser.y:910-913) */
      int rd = parse_register();
      int rs = parse_register();
      r_type_inst(Y_ADDU_OP, rd, 0, rs);
      break;
    }
    case Y_NEG_POP: {
      /* neg DEST, SRC1  →  sub DEST, $0, SRC1  (parser.y:892-895) */
      int rd = parse_register();
      int rs = parse_register();
      r_type_inst(Y_SUB_OP, rd, 0, rs);
      break;
    }
    case Y_NEGU_POP: {
      /* negu DEST, SRC1  →  subu DEST, $0, SRC1  (parser.y:898-901) */
      int rd = parse_register();
      int rs = parse_register();
      r_type_inst(Y_SUBU_OP, rd, 0, rs);
      break;
    }
    case Y_NOT_POP: {
      /* not DEST, SRC1  →  nor DEST, SRC1, $0  (parser.y:904-907) */
      int rd = parse_register();
      int rs = parse_register();
      r_type_inst(Y_NOR_OP, rd, rs, 0);
      break;
    }
    case Y_ROR_POP:
    case Y_ROL_POP: {
      /* Rotate right / left.  Two forms (parser.y:1173-1211):
           DEST SRC1 SRC2   — register rotate
           DEST SRC1 IMM    — constant rotate */
      int rd = parse_register();
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        /* ROR: subu $at,$0,rt; sllv $at,$at,rs; srlv rd,rt,rs; or rd,rd,$at
           ROL: subu $at,$0,rt; srlv $at,$at,rs; sllv rd,rt,rs; or rd,rd,$at */
        r_type_inst(Y_SUBU_OP, 1, 0, rt);
        if (op == Y_ROR_POP) {
          r_type_inst(Y_SLLV_OP, 1, 1, rs);
          r_type_inst(Y_SRLV_OP, rd, rt, rs);
        } else {
          r_type_inst(Y_SRLV_OP, 1, 1, rs);
          r_type_inst(Y_SLLV_OP, rd, rt, rs);
        }
        r_type_inst(Y_OR_OP, rd, rd, 1);
      } else {
        imm_expr* imm = parse_imm32();
        long dist = eval_imm_expr(imm);
        hp_check_imm_range(imm, 0, 31);
        if (op == Y_ROR_POP) {
          r_sh_type_inst(Y_SLL_OP, 1, rs, -dist);
          r_sh_type_inst(Y_SRL_OP, rd, rs, dist);
        } else {
          r_sh_type_inst(Y_SRL_OP, 1, rs, -dist);
          r_sh_type_inst(Y_SLL_OP, rd, rs, dist);
        }
        r_type_inst(Y_OR_OP, rd, rd, 1);
        free(imm);
      }
      break;
    }
    case Y_SLE_POP:
    case Y_SLEU_POP:
    case Y_SGT_POP:
    case Y_SGTU_POP:
    case Y_SGE_POP:
    case Y_SGEU_POP:
    case Y_SEQ_POP:
    case Y_SNE_POP: {
      /* SET_*_POPS: DEST SRC1 (SRC2|IMM32).
         (parser.y:1221-1283).  IMM form uses $at + ori when imm != 0. */
      int rd = parse_register();
      int rs = parse_register();
      int rt;
      if (hp_scanner_peek() == Y_REG) {
        rt = parse_register();
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr*);
        if (!is_zero_imm(imm)) {
          i_type_inst(Y_ORI_OP, 1, 0, imm);
          rt = 1;
        } else {
          rt = 0;
        }
        free(imm);
      }
      switch (op) {
        case Y_SLE_POP:
        case Y_SLEU_POP: hp_set_le_inst(op, rd, rs, rt); break;
        case Y_SGT_POP:
        case Y_SGTU_POP: hp_set_gt_inst(op, rd, rs, rt); break;
        case Y_SGE_POP:
        case Y_SGEU_POP: hp_set_ge_inst(op, rd, rs, rt); break;
        case Y_SEQ_POP:
        case Y_SNE_POP: hp_set_eq_inst(op, rd, rs, rt); break;
      }
      break;
    }
    case Y_ABS_POP: {
      /* abs DEST, SRC1  (parser.y:881-889): if DEST != SRC1,
         first move; then bgez SRC1 +3; nop; sub DEST, $0, SRC1. */
      int rd = parse_register();
      int rs = parse_register();
      if (rd != rs) {
        r_type_inst(Y_ADDU_OP, rd, 0, rs);
      }
      i_type_inst_free(Y_BGEZ_OP, 0, rs, hp_branch_offset(3));
      hp_nop_inst();
      r_type_inst(Y_SUB_OP, rd, 0, rs);
      break;
    }
    case Y_NOP_POP: {
      hp_nop_inst();
      break;
    }
    case Y_REM_POP:
    case Y_REMU_POP: {
      /* rem/remu — DIV_POPS pseudo, 3-operand form only.
         parser.y:1115-1131. */
      int rd = parse_register();
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        hp_div_inst(op, rd, rs, rt, 0);
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr* expr);
        if (is_zero_imm(imm)) {
          hp_yyerror("Divide by zero");
        } else {
          i_type_inst_free(Y_ORI_OP, 1, 0, imm);
          hp_div_inst(op, rd, rs, 1, 1);
        }
      }
      break;
    }
    case Y_MULO_POP:
    case Y_MULOU_POP: {
      /* mulo/mulou — MUL_POPS, 3-operand form. parser.y:1134-1150. */
      int rd = parse_register();
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        hp_mult_inst(op, rd, rs, rt);
      } else {
        imm_expr* imm = parse_imm32();
        extern bool is_zero_imm(imm_expr* expr);
        if (is_zero_imm(imm)) {
          /* Optimize: n * 0 == 0 */
          i_type_inst_free(Y_ORI_OP, rd, 0, imm);
        } else {
          i_type_inst_free(Y_ORI_OP, 1, 0, imm);
          hp_mult_inst(op, rd, rs, 1);
        }
      }
      break;
    }
    case Y_MFC1_D_POP: {
      /* mfc1.d REG COP_REG  →  two mfc1 instructions
         (parser.y:1560-1563). */
      int reg = parse_register();
      int copreg;
      if (hp_scanner_peek() == Y_FP_REG) copreg = parse_fp_register();
      else copreg = parse_register();
      r_co_type_inst(Y_MFC1_OP, 0, copreg, reg);
      r_co_type_inst(Y_MFC1_OP, 0, copreg + 1, reg + 1);
      break;
    }
    case Y_MTC1_D_POP: {
      /* mtc1.d REG COP_REG  →  two mtc1 instructions
         (parser.y:1565-1569). */
      int reg = parse_register();
      int copreg;
      if (hp_scanner_peek() == Y_FP_REG) copreg = parse_fp_register();
      else copreg = parse_register();
      r_co_type_inst(Y_MTC1_OP, 0, copreg, reg);
      r_co_type_inst(Y_MTC1_OP, 0, copreg + 1, reg + 1);
      break;
    }
    case Y_LI_D_POP: {
      /* li.d F_DEST <Y_FP>  →  load double-precision constant
         via two mtc1 (parser.y:654-662). */
      int fd = parse_fp_register();
      if (hp_scanner_peek() != Y_FP) {
        hp_yyerror("Expected FP literal");
        break;
      }
      hp_scanner_advance();
      int* x = (int*)yylval.p;
      i_type_inst(Y_ORI_OP, 1, 0, const_imm_expr(*x));
      r_co_type_inst(Y_MTC1_OP, 0, fd, 1);
      i_type_inst(Y_ORI_OP, 1, 0, const_imm_expr(*(x + 1)));
      r_co_type_inst(Y_MTC1_OP, 0, fd + 1, 1);
      break;
    }
    case Y_LI_S_POP: {
      /* li.s F_DEST <Y_FP>  →  single-precision const via one mtc1
         (parser.y:665-672). */
      int fd = parse_fp_register();
      if (hp_scanner_peek() != Y_FP) {
        hp_yyerror("Expected FP literal");
        break;
      }
      hp_scanner_advance();
      float fval = (float)*((double*)yylval.p);
      int* y = (int*)&fval;
      i_type_inst(Y_ORI_OP, 1, 0, const_imm_expr(*y));
      r_co_type_inst(Y_MTC1_OP, 0, fd, 1);
      break;
    }
    case Y_L_D_POP: {
      /* l.d F_DEST ADDRESS  →  ldc1 (parser.y:1668). */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_LDC1_OP, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_L_S_POP: {
      /* l.s F_DEST ADDRESS  →  lwc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_LWC1_OP, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_S_D_POP: {
      /* s.d F_SRC1 ADDRESS  →  sdc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_SDC1_OP, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_S_S_POP: {
      /* s.s F_SRC1 ADDRESS  →  swc1. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_SWC1_OP, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_LD_POP: {
      /* ld DEST ADDRESS  — load doubleword pseudo, expands to
         two lw instructions (parser.y:592-606 first alternative). */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_LW_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      i_type_inst_free(Y_LW_OP, rt + 1, addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 4));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_SD_POP: {
      /* sd SRC1 ADDRESS — store doubleword pseudo. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_SW_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      i_type_inst_free(Y_SW_OP, rt + 1, addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 4));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_ULW_POP: {
      /* Unaligned load word.  parser.y:675-696, little-endian
         path (the macOS/Linux x86 default).  Emits LWL+LWR with
         offset bumped by 3 on the LWL. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst_free(Y_LWL_OP, rt, addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 3));
      i_type_inst(Y_LWR_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_ULH_POP:
    case Y_ULHU_POP: {
      /* Unaligned load half (signed/unsigned). parser.y:699-724 LE path. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst_free(op == Y_ULH_POP ? Y_LB_OP : Y_LBU_OP, rt,
                       addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 1));
      i_type_inst(Y_LBU_OP, 1, addr_expr_reg(addr), addr_expr_imm(addr));
      r_sh_type_inst(Y_SLL_OP, rt, rt, 8);
      r_type_inst(Y_OR_OP, rt, rt, 1);
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_USW_POP: {
      /* Unaligned store word.  parser.y:760-781 LE path. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst_free(Y_SWL_OP, rt, addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 3));
      i_type_inst(Y_SWR_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_USH_POP: {
      /* Unaligned store half.  parser.y:784-806.  ROL, store
         high byte, ROR. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      i_type_inst(Y_SB_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      /* ROL SRC, SRC, 8 (via SLL+SRL+OR) */
      r_sh_type_inst(Y_SLL_OP, 1, rt, 24);
      r_sh_type_inst(Y_SRL_OP, rt, rt, 8);
      r_type_inst(Y_OR_OP, rt, rt, 1);
      i_type_inst_free(Y_SB_OP, rt, addr_expr_reg(addr),
                       incr_expr_offset(addr_expr_imm(addr), 1));
      /* ROR SRC, SRC, 8 (via SRL+SLL+OR) */
      r_sh_type_inst(Y_SRL_OP, 1, rt, 24);
      r_sh_type_inst(Y_SLL_OP, rt, rt, 8);
      r_type_inst(Y_OR_OP, rt, rt, 1);
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case Y_LA_POP: {
      /* la DEST, ADDRESS  (parser.y:634-645).  If ADDRESS has a
         base register, emit `addi DEST, base, offset`; else emit
         `ori DEST, $0, offset`. */
      int rt = parse_register();
      addr_expr* addr = parse_address();
      if (addr_expr_reg(addr)) {
        i_type_inst(Y_ADDI_OP, rt, addr_expr_reg(addr), addr_expr_imm(addr));
      } else {
        i_type_inst(Y_ORI_OP, rt, 0, addr_expr_imm(addr));
      }
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }

    /* UNARY_BR_POPS: beqz, bnez SRC1 LABEL  (parser.y:1315-1319) */
    case Y_BEQZ_POP:
    case Y_BNEZ_POP: {
      int rs = parse_register();
      imm_expr* target = parse_label();
      int real_op = (op == Y_BEQZ_POP) ? Y_BEQ_OP : Y_BNE_OP;
      i_type_inst_free(real_op, 0, rs, target);
      break;
    }

    /* B_OPS: b, bal LABEL  (parser.y:1469-1473) */
    case Y_B_POP:
    case Y_BAL_POP: {
      imm_expr* target = parse_label();
      int real_op = (op == Y_BAL_POP) ? Y_BGEZAL_OP : Y_BGEZ_OP;
      i_type_inst_free(real_op, 0, 0, target);
      break;
    }

    /* BR_GT_POPS: bgt/bgtu SRC1 (SRC2|IMM) LABEL (parser.y:1351-1378) */
    case Y_BGT_POP:
    case Y_BGTU_POP: {
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        r_type_inst(op == Y_BGT_POP ? Y_SLT_OP : Y_SLTU_OP, 1, rt, rs);
        i_type_inst_free(Y_BNE_OP, 0, 1, target);
      } else {
        /* Immediate form — see parser.y:1358-1378 */
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        if (op == Y_BGT_POP) {
          imm_expr* imm_inc = incr_expr_offset(imm, 1);
          i_type_inst_free(Y_SLTI_OP, 1, rs, imm_inc);
          i_type_inst(Y_BEQ_OP, 0, 1, target);
        } else {
          i_type_inst(Y_ORI_OP, 1, 0, imm);
          i_type_inst_free(Y_BEQ_OP, rs, 1, hp_branch_offset(3));
          r_type_inst(Y_SLTU_OP, 1, rs, 1);
          i_type_inst(Y_BEQ_OP, 0, 1, target);
        }
        free(imm);
        free(target);
      }
      break;
    }

    /* BR_GE_POPS: bge/bgeu SRC1 (SRC2|IMM) LABEL (parser.y:1381-1394) */
    case Y_BGE_POP:
    case Y_BGEU_POP: {
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        r_type_inst(op == Y_BGE_POP ? Y_SLT_OP : Y_SLTU_OP, 1, rs, rt);
        i_type_inst_free(Y_BEQ_OP, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        i_type_inst(op == Y_BGE_POP ? Y_SLTI_OP : Y_SLTIU_OP, 1, rs, imm);
        i_type_inst_free(Y_BEQ_OP, 0, 1, target);
        free(imm);
      }
      break;
    }

    /* BR_LT_POPS: blt/bltu SRC1 (SRC2|IMM) LABEL (parser.y:1397-1410) */
    case Y_BLT_POP:
    case Y_BLTU_POP: {
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        r_type_inst(op == Y_BLT_POP ? Y_SLT_OP : Y_SLTU_OP, 1, rs, rt);
        i_type_inst_free(Y_BNE_OP, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        i_type_inst(op == Y_BLT_POP ? Y_SLTI_OP : Y_SLTIU_OP, 1, rs, imm);
        i_type_inst_free(Y_BNE_OP, 0, 1, target);
        free(imm);
      }
      break;
    }

    /* BR_LE_POPS: ble/bleu SRC1 (SRC2|IMM) LABEL (parser.y:1413-1440) */
    case Y_BLE_POP:
    case Y_BLEU_POP: {
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        imm_expr* target = parse_label();
        r_type_inst(op == Y_BLE_POP ? Y_SLT_OP : Y_SLTU_OP, 1, rt, rs);
        i_type_inst_free(Y_BEQ_OP, 0, 1, target);
      } else {
        imm_expr* imm = parse_imm32();
        imm_expr* target = parse_label();
        if (op == Y_BLE_POP) {
          imm_expr* imm_inc = incr_expr_offset(imm, 1);
          i_type_inst_free(Y_SLTI_OP, 1, rs, imm_inc);
          i_type_inst(Y_BNE_OP, 0, 1, target);
        } else {
          i_type_inst(Y_ORI_OP, 1, 0, imm);
          i_type_inst(Y_BEQ_OP, rs, 1, target);
          r_type_inst(Y_SLTU_OP, 1, rs, 1);
          i_type_inst(Y_BNE_OP, 0, 1, target);
        }
        free(imm);
        free(target);
      }
      break;
    }

    default:
      hp_yyerror("Pseudo-op not yet supported in hand-written parser pilot");
      hp_sync_to_nl();
      break;
  }
}

static void parse_asm_code(void) {
  int op = hp_scanner_advance();
  int type = find_op_type(op);

  switch (type) {
    case NOARG_TYPE_INST: parse_noarg(op); break;
    case R3_TYPE_INST:    parse_r3(op);    break;
    case R2sh_TYPE_INST:  parse_r2sh(op);  break;
    case R1s_TYPE_INST:   parse_r1s(op);   break;
    case I2_TYPE_INST:    parse_i2(op);    break;
    case I1t_TYPE_INST:   parse_i1t(op);   break;
    case I2a_TYPE_INST:   parse_i2a(op);   break;
    case B2_TYPE_INST:    parse_b2(op);    break;
    case B1_TYPE_INST:    parse_b1(op);    break;
    case J_TYPE_INST:     parse_j(op);     break;
    case PSEUDO_OP:       parse_pseudo(op); break;
    case R2td_TYPE_INST: {
      /* mfc0/mtc0/etc.: <op> REG COP_REG.  COP_REG accepts
         Y_REG or Y_FP_REG (parser.y:2572-2574). */
      int reg = parse_register();
      int copreg;
      if (hp_scanner_peek() == Y_FP_REG) {
        copreg = parse_fp_register();
      } else {
        copreg = parse_register();
      }
      r_co_type_inst(op, 0, copreg, reg);
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
         accept 3-operand DIV_POPS pseudo forms).  See parser.y
         lines 1106-1131 (div pseudo forms) and 1153 (mult).

         Dispatch by operand count: count registers after the
         first one. */
      int r1 = parse_register();
      if (hp_scanner_peek() != Y_REG && hp_scanner_peek() != Y_INT
          && hp_scanner_peek() != Y_ID) {
        /* Not enough operands?  Fall back to plain 2-form
           anyway — produces a syntax error if anything's wrong. */
        hp_yyerror("Expected operand for div/mult");
        break;
      }
      if (op == Y_TEQ_OP || op == Y_TGE_OP || op == Y_TGEU_OP
          || op == Y_TLT_OP || op == Y_TLTU_OP || op == Y_TNE_OP) {
        /* BINARY_TRAP_OPS: <op> SRC1 SRC2 → r_type_inst(op, 0, r1, r2)
           (parser.y:1482-1485) */
        int r2 = parse_register();
        r_type_inst(op, 0, r1, r2);
      } else if (op == Y_DIV_OP || op == Y_DIVU_OP) {
        /* DIV_POPS: can be 2-op (real hardware) or 3-op (pseudo) */
        int r2 = parse_register();
        if (hp_scanner_peek() == Y_NL || hp_scanner_peek() == Y_EOF) {
          /* 2-operand form: r_type_inst(op, 0, r1, r2)
             — note r1 is rs (DEST in bison's grammar), r2 is rt (SRC1). */
          r_type_inst(op, 0, r1, r2);
        } else if (hp_scanner_peek() == Y_REG) {
          int r3 = parse_register();
          hp_div_inst(op, r1, r2, r3, 0);
        } else {
          imm_expr* imm = parse_imm32();
          extern bool is_zero_imm(imm_expr* expr);
          if (is_zero_imm(imm)) {
            hp_yyerror("Divide by zero");
          } else {
            i_type_inst_free(Y_ORI_OP, 1, 0, imm);
            hp_div_inst(op, r1, r2, 1, 1);
            /* don't free imm again — i_type_inst_free already freed */
            break;
          }
          free(imm);
        }
      } else {
        /* mult/multu: always 2-operand */
        int r2 = parse_register();
        r_type_inst(op, 0, r1, r2);
      }
      break;
    }
    case R1d_TYPE_INST: {
      /* One-destination-register R-type: mfhi/mflo.  See
         parser.y MOVE_FROM_HILO_OPS (~line 1919). */
      int rd = parse_register();
      r_type_inst(op, rd, 0, 0);
      break;
    }
    case R3sh_TYPE_INST: {
      /* BINARYIR_OPS: sllv, srav, srlv (variable-register shift).
         DEST SRC1 SRC2 — note bison's r_type_inst($1, DEST, SRC2, SRC1)
         has rs/rt swapped (parser.y:959-962).  Also accepts immediate
         form which uses op_to_imm_op for the shamt encoding. */
      int rd = parse_register();
      int rs = parse_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        r_type_inst(op, rd, rt, rs);
      } else {
        /* DEST SRC1 Y_INT → r_sh_type_inst(op_to_imm_op(op), DEST, SRC1, shamt) */
        if (hp_scanner_peek() != Y_INT) {
          hp_yyerror("Expected register or integer shift");
          break;
        }
        hp_scanner_advance();
        int shamt = yylval.i;
        r_sh_type_inst(hp_op_to_imm_op(op), rd, rs, shamt);
      }
      break;
    }
    case I1s_TYPE_INST: {
      /* BINARYI_TRAP_OPS: teqi/tgei/tgeiu/tlti/tltiu/tnei.
         <op> SRC1 IMM16 → i_type_inst_free(op, 0, rs, imm).
         (parser.y:1476-1479) */
      int rs = parse_register();
      imm_expr* imm = parse_imm16();
      i_type_inst_free(op, 0, rs, imm);
      break;
    }
    case BC_TYPE_INST: {
      /* BR_COP_OPS: bc1f/bc1t/bc1fl/bc1tl.  Two forms:
           <op> LABEL                  → cc=0
           <op> CC_REG LABEL           → cc=Y_INT
         The RT field is cc_to_rt(cc, nd, tf); RS is 0 (bison
         uses BIN_RS($1.i) which is the token-number's bits 21-25,
         always 0 for spim's token-number range).  See
         parser.y:1286-1306. */
      extern bool opcode_is_nullified_branch(int);
      extern bool opcode_is_true_branch(int);
      int nd = opcode_is_nullified_branch(op) ? 1 : 0;
      int tf = opcode_is_true_branch(op) ? 1 : 0;
      int cc = 0;
      if (hp_scanner_peek() == Y_INT) {
        hp_scanner_advance();
        cc = yylval.i;
      }
      imm_expr* target = parse_label();
      int rt = (cc << 2) | (nd << 1) | tf;
      i_type_inst_free(op, rt, 0, target);
      break;
    }
    case MOVC_TYPE_INST: {
      /* MOVECC_OPS: movf/movt DEST SRC1 Y_INT (cc number).
         parser.y:1506-1512: r_type_inst($1, DEST, SRC1, (Y_INT&7)<<2). */
      int rd = parse_register();
      int rs = parse_register();
      int cc = 0;
      if (hp_scanner_peek() == Y_INT) {
        hp_scanner_advance();
        cc = yylval.i;
      }
      r_type_inst(op, rd, rs, (cc & 0x7) << 2);
      break;
    }

    /* --- FP family (parser.y:1587-2120) --- */
    case FP_R2ds_TYPE_INST: {
      /* FP_UNARY_OPS / FP_MOVE_OPS: <op> F_DEST F_SRC2.
         parser.y:1587-1591 etc.: r_co_type_inst($1, $2, $3, 0). */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      r_co_type_inst(op, fd, fs, 0);
      break;
    }
    case FP_R3_TYPE_INST: {
      /* FP_BINARY_OPS: <op> F_DEST F_SRC1 F_SRC2.
         parser.y FP_BINARY_OPS production: r_co_type_inst. */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      int ft = parse_fp_register();
      r_co_type_inst(op, fd, fs, ft);
      break;
    }
    case FP_CMP_TYPE_INST: {
      /* FP_CMP_OPS: two forms (parser.y:1617-1626):
           <op> F_SRC1 F_SRC2                   → cc=0
           <op> CC_REG F_SRC1 F_SRC2            → cc=Y_INT */
      int cc = 0;
      if (hp_scanner_peek() == Y_INT) {
        hp_scanner_advance();
        cc = yylval.i;
      }
      int fs = parse_fp_register();
      int ft = parse_fp_register();
      r_cond_type_inst(op, fs, ft, cc);
      break;
    }
    case FP_MOVC_TYPE_INST: {
      /* FP_MOVC_TYPE covers two families with different operand
         shapes (parser.y:1515-1536):
           FP_MOVEC_OPS  (movn/movz.{s,d}) : F_DEST F_SRC1 REG
              → r_co_type_inst(op, fd, fs, rt)
           FP_MOVECC_OPS (movf/movt.{s,d}) : F_DEST F_SRC1 [CC_REG]
              → r_co_type_inst(op, fd, fs, cc_to_rt(cc, 0, 0))
         Disambiguate by inspecting the third operand. */
      int fd = parse_fp_register();
      int fs = parse_fp_register();
      if (hp_scanner_peek() == Y_REG) {
        int rt = parse_register();
        r_co_type_inst(op, fd, fs, rt);
      } else if (hp_scanner_peek() == Y_INT) {
        hp_scanner_advance();
        int cc = yylval.i;
        r_co_type_inst(op, fd, fs, (cc & 0x7) << 2);
      } else {
        /* No third operand (movf/movt with implicit cc=0). */
        r_co_type_inst(op, fd, fs, 0);
      }
      break;
    }
    case FP_I2a_TYPE_INST: {
      /* lwc1 / ldc1 / lwc2 etc.: <op> F_REG ADDRESS.
         parser.y LOADFP_OPS / STOREFP_OPS productions. */
      int fr = parse_fp_register();
      addr_expr* addr = parse_address();
      i_type_inst(op, fr, addr_expr_reg(addr), addr_expr_imm(addr));
      free(addr_expr_imm(addr));
      free(addr);
      break;
    }
    case FP_R2ts_TYPE_INST: {
      /* mfc1/mtc1/cfc0/ctc0/cfc1/ctc1: <op> REG COP_REG.
         COP_REG accepts either Y_REG OR Y_FP_REG (parser.y:2572-
         2574) — both name the coprocessor register number. */
      int reg = parse_register();
      int copreg;
      if (hp_scanner_peek() == Y_FP_REG) {
        copreg = parse_fp_register();
      } else {
        copreg = parse_register();
      }
      r_co_type_inst(op, 0, copreg, reg);
      break;
    }
    default:
      hp_yyerror("Instruction not yet supported in hand-written parser pilot");
      hp_sync_to_nl();
      break;
  }
}

static void parse_directive(int dir_tok) {
  switch (dir_tok) {
    case Y_DATA_DIR:     parse_dir_data(false); break;
    case Y_K_DATA_DIR:   parse_dir_data(true);  break;
    case Y_TEXT_DIR:     parse_dir_text(false); break;
    case Y_K_TEXT_DIR:   parse_dir_text(true);  break;
    case Y_GLOBAL_DIR:   parse_dir_globl();     break;
    case Y_WORD_DIR:     parse_dir_word();      break;
    case Y_HALF_DIR:     parse_dir_half();      break;
    case Y_BYTE_DIR:     parse_dir_byte();      break;
    case Y_ASCII_DIR:    parse_dir_ascii();     break;
    case Y_ASCIIZ_DIR:   parse_dir_asciiz();    break;
    case Y_FLOAT_DIR:    parse_dir_float();     break;
    case Y_DOUBLE_DIR:   parse_dir_double();    break;
    case Y_SPACE_DIR:    parse_dir_space();     break;
    case Y_ALIGN_DIR:    parse_dir_align();     break;
    case Y_COMM_DIR:     parse_dir_comm();      break;
    case Y_EXTERN_DIR:   parse_dir_extern();    break;
    case Y_ERR_DIR:      hp_yyerror(".err directive"); break;
    /* Metadata directives — swallow the rest of the line. */
    case Y_FILE_DIR:
    case Y_LOC_DIR:
    case Y_FRAME_DIR:
    case Y_MASK_DIR:
    case Y_FMASK_DIR:
    case Y_ENT_DIR:
    case Y_END_DIR:
    case Y_LABEL_DIR:
    case Y_LIVEREG_DIR:
    case Y_OPTIONS_DIR:
    case Y_BGNB_DIR:
    case Y_ENDB_DIR:
    case Y_ENDR_DIR:
    case Y_ASM0_DIR:
    case Y_ALIAS_DIR:
    case Y_SET_DIR:
      hp_sync_to_nl();
      break;
    default:
      hp_yyerror("Directive not yet supported in hand-written parser pilot");
      hp_sync_to_nl();
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
     We know peek == Y_ID and peek2 == ':' or '='.  Don't call
     hp_scanner_force_identifier here — the Y_ID was already
     classified at the peek site, so the flag would
     incorrectly affect a LATER token (e.g., misclassifying
     `buf: .word 42`'s `.word` as Y_ID). */
  hp_scanner_advance();  /* consume Y_ID */
  char* sym = (char*)yylval.p;
  int sep = hp_scanner_advance();  /* ':' or '=' */

  if (sep == ':') {
    label* l = record_label(sym,
                            text_dir ? current_text_pc() : current_data_pc(),
                            0);
    /* Cons onto this_line_labels — DO NOT resolve uses
       immediately.  If a subsequent .word (etc.) on the next
       line bumps the data PC for alignment, hp_fix_line_labels
       must be able to update this label's addr first.  Mirrors
       parser.y's deferred-resolution behavior. */
    hp_cons_label(l);
    free(sym);
  } else {
    /* ID '=' EXPR : constant label */
    int v = parse_expression();
    label* l = record_label(sym, (mem_addr)v, 1);
    l->const_flag = 1;
    free(sym);
  }
}

/* LINE / LBL_CMD / CMD */
static void parse_line(void) {
  /* Empty line? */
  if (hp_scanner_peek() == Y_NL) {
    hp_scanner_advance();
    return;
  }
  if (hp_scanner_peek() == Y_EOF) {
    return;
  }

  /* Look ahead for optional label */
  if (hp_scanner_peek() == Y_ID
      && (hp_scanner_peek2() == ':' || hp_scanner_peek2() == '=')) {
    parse_opt_label();
  }

  /* Then maybe directive or instruction */
  int t = hp_scanner_peek();
  if (t == Y_NL) { hp_scanner_advance(); return; }
  if (t == Y_EOF) { return; }

  if (is_directive(t)) {
    int dt = hp_scanner_advance();
    parse_directive(dt);
    hp_clear_labels();   /* mirrors parser.y's CMD: ASM_DIRECTIVE { clear_labels() } */
  } else {
    parse_asm_code();
    hp_clear_labels();   /* mirrors parser.y's CMD: ASM_CODE { clear_labels() } */
  }

  /* Expect newline or EOF */
  if (parse_error_occurred) {
    hp_sync_to_nl();
  } else if (hp_scanner_peek() == Y_NL) {
    hp_scanner_advance();
  } else if (hp_scanner_peek() != Y_EOF) {
    hp_yyerror("Extra tokens after instruction");
    hp_sync_to_nl();
  }
}

/* ---------------- public API ---------------- */

void hp_initialize_parser(FILE* in, char* file_name) {
  hp_scanner_init(in);
  hp_input_file_name = file_name;
  parse_errors_seen = 0;
  parse_error_occurred = false;
  data_dir = false;
  text_dir = true;
}

int hp_parse_file(void) {
  while (hp_scanner_peek() != Y_EOF) {
    parse_error_occurred = false;
    hp_scanner_start_line();
    parse_line();
  }
  /* Mirror parser.y's `TERM: Y_EOF { clear_labels(); ... }` —
     if the last lines were bare-label definitions, their uses
     need resolving here. */
  hp_clear_labels();
  return parse_errors_seen;
}

/* Flex/bison-name compatibility shims for the REPL's ASM_CMD
   path and any caller that still uses the old surface.  Phase 5
   removed the generated yyparse(); spim.c's `case ASM_CMD: yyparse();`
   now lands here. */
int yyparse(void) {
  return hp_parse_file();
}
