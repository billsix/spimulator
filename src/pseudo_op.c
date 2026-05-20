/* SPIM S20 MIPS simulator.
   Pseudo-op expansion helpers + runtime error funnel.

   Each pseudo-op expander emits a sequence of real R/I-type
   instructions that implements the user-visible pseudo-op.

   Copyright (c) 1990-2026, James R. Larus and contributors.
   BSD 3-Clause.
*/

#include <stdio.h>

#include "spim.h"
#include "inst.h"
#include "sym-tbl.h" /* SYMBOL_IS_DEFINED */
#include "tokens.h"  /* Y_*_OP, Y_*_POP token values */
#include "pseudo_op.h"

extern int line_no;                     /* from scanner */
extern char* erroneous_line(void);      /* from scanner.c */
extern char* input_file_name_get(void); /* from parser.c */

/* ------------------------------------------------------------------ *
 * Runtime-visible globals: parse-error counters.
 * ------------------------------------------------------------------ */

bool parse_error_occurred = false; /* set for one parse iteration */
int parse_errors_seen = 0;         /* cumulative errors across the file */

/* ------------------------------------------------------------------ *
 * Runtime error funnel.  sym-tbl.c calls parse_error() when it detects a
 * duplicate label; the runtime call site predates the parser
 * rewrite, so the name is kept.  parse_warn() is the unconditional
 * warning channel that parse_error sits on top of.
 * ------------------------------------------------------------------ */

void parse_warn(char* s) {
  error("spim: (parser) %s on line %d of file %s\n%s", s, line_no,
        input_file_name_get() ? input_file_name_get() : "(input)",
        erroneous_line());
}

void parse_error(char* s) {
  parse_error_occurred = true;
  parse_errors_seen += 1;
  parse_warn(s);
}

/* ------------------------------------------------------------------ *
 * imm_op_to_op: needed by src/inst.c's pseudo-op expander.
 * ------------------------------------------------------------------ */

int imm_op_to_op(int opcode) {
  switch (opcode) {
    case TOK_ADDI_OP:
      return TOK_ADD_OP;
    case TOK_ADDIU_OP:
      return TOK_ADDU_OP;
    case TOK_ANDI_OP:
      return TOK_AND_OP;
    case TOK_ORI_OP:
      return TOK_OR_OP;
    case TOK_XORI_OP:
      return TOK_XOR_OP;
    case TOK_SLTI_OP:
      return TOK_SLT_OP;
    case TOK_SLTIU_OP:
      return TOK_SLTU_OP;
    case TOK_J_OP:
      return TOK_JR_OP;
    case TOK_LUI_OP:
      return TOK_ADDU_OP;
    case TOK_SLL_OP:
      return TOK_SLLV_OP;
    case TOK_SRA_OP:
      return TOK_SRAV_OP;
    case TOK_SRL_OP:
      return TOK_SRLV_OP;
    default:
      fatal_error("Can't convert immediate op to op\n");
      return 0;
  }
}

/* Forward declarations for spim runtime calls. */
extern void r_type_inst(int opcode, int rd, int rs, int rt);
extern void r_sh_type_inst(int opcode, int rd, int rt, int shamt);
extern void i_type_inst_free(int opcode, int rt, int rs, imm_expr* expr);
extern imm_expr* const_imm_expr(int value);
extern int32 eval_imm_expr(imm_expr* expr);

void nop_inst(void) {
  r_type_inst(TOK_SLL_OP, 0, 0, 0); /* sll $0, $0, 0 == 0x00000000 */
}

void trap_inst(void) { r_type_inst(TOK_BREAK_OP, 0, 0, 0); }

imm_expr* branch_offset(int n_inst) {
  return const_imm_expr(n_inst << 2); /* later shifted right 2 by the encoder */
}

int op_to_imm_op(int opcode) {
  switch (opcode) {
    case TOK_ADD_OP:
      return TOK_ADDI_OP;
    case TOK_ADDU_OP:
      return TOK_ADDIU_OP;
    case TOK_AND_OP:
      return TOK_ANDI_OP;
    case TOK_OR_OP:
      return TOK_ORI_OP;
    case TOK_XOR_OP:
      return TOK_XORI_OP;
    case TOK_SLT_OP:
      return TOK_SLTI_OP;
    case TOK_SLTU_OP:
      return TOK_SLTIU_OP;
    case TOK_SLLV_OP:
      return TOK_SLL_OP;
    case TOK_SRAV_OP:
      return TOK_SRA_OP;
    case TOK_SRLV_OP:
      return TOK_SRL_OP;
    default:
      fatal_error("Can't convert op to immediate op\n");
      return 0;
  }
}

void div_inst(int op, int rd, int rs, int rt, int const_divisor) {
  if (rd != 0 && !const_divisor) {
    i_type_inst_free(TOK_BNE_OP, 0, rt, branch_offset(3));
    nop_inst();
    trap_inst();
  }

  if (op == TOK_DIV_OP || op == TOK_REM_POP)
    r_type_inst(TOK_DIV_OP, 0, rs, rt);
  else
    r_type_inst(TOK_DIVU_OP, 0, rs, rt);

  if (rd != 0) {
    if (op == TOK_DIV_OP || op == TOK_DIVU_OP) /* Quotient */
      r_type_inst(TOK_MFLO_OP, rd, 0, 0);
    else
      /* Remainder */
      r_type_inst(TOK_MFHI_OP, rd, 0, 0);
  }
}

void mult_inst(int op, int rd, int rs, int rt) {
  if (op == TOK_MULOU_POP)
    r_type_inst(TOK_MULTU_OP, 0, rs, rt);
  else
    r_type_inst(TOK_MULT_OP, 0, rs, rt);

  if (op == TOK_MULOU_POP && rd != 0) {
    r_type_inst(TOK_MFHI_OP, 1, 0, 0); /* Use $at */
    i_type_inst_free(TOK_BEQ_OP, 0, 1, branch_offset(3));
    nop_inst();
    trap_inst();
  } else if (op == TOK_MULO_POP && rd != 0) {
    r_type_inst(TOK_MFHI_OP, 1, 0, 0); /* use $at */
    r_type_inst(TOK_MFLO_OP, rd, 0, 0);
    r_sh_type_inst(TOK_SRA_OP, rd, rd, 31);
    i_type_inst_free(TOK_BEQ_OP, rd, 1, branch_offset(3));
    nop_inst();
    trap_inst();
  }
  if (rd != 0) r_type_inst(TOK_MFLO_OP, rd, 0, 0);
}

void set_le_inst(int op, int rd, int rs, int rt) {
  i_type_inst_free(TOK_BNE_OP, rs, rt, branch_offset(3));
  i_type_inst_free(TOK_ORI_OP, rd, 0, const_imm_expr(1));
  i_type_inst_free(TOK_BEQ_OP, 0, 0, branch_offset(3));
  nop_inst();
  r_type_inst((op == TOK_SLE_POP ? TOK_SLT_OP : TOK_SLTU_OP), rd, rs, rt);
}

void set_gt_inst(int op, int rd, int rs, int rt) {
  r_type_inst(op == TOK_SGT_POP ? TOK_SLT_OP : TOK_SLTU_OP, rd, rt, rs);
}

void set_ge_inst(int op, int rd, int rs, int rt) {
  i_type_inst_free(TOK_BNE_OP, rs, rt, branch_offset(3));
  i_type_inst_free(TOK_ORI_OP, rd, 0, const_imm_expr(1));
  i_type_inst_free(TOK_BEQ_OP, 0, 0, branch_offset(3));
  nop_inst();
  r_type_inst(op == TOK_SGE_POP ? TOK_SLT_OP : TOK_SLTU_OP, rd, rt, rs);
}

void set_eq_inst(int op, int rd, int rs, int rt) {
  imm_expr *if_eq, *if_neq;

  if (op == TOK_SEQ_POP) {
    if_eq = const_imm_expr(1);
    if_neq = const_imm_expr(0);
  } else {
    if_eq = const_imm_expr(0);
    if_neq = const_imm_expr(1);
  }

  i_type_inst_free(TOK_BEQ_OP, rs, rt, branch_offset(3));
  /* RD <- 0 (if not equal) */
  i_type_inst_free(TOK_ORI_OP, rd, 0, if_neq);
  i_type_inst_free(TOK_BEQ_OP, 0, 0, branch_offset(3)); /* Branch always */
  nop_inst();
  /* RD <- 1 */
  i_type_inst_free(TOK_ORI_OP, rd, 0, if_eq);
}

void check_imm_range(imm_expr* expr, int32 min, int32 max) {
  if (expr->symbol == nullptr || SYMBOL_IS_DEFINED(expr->symbol)) {
    /* If expression can be evaluated, compare against limits. */
    int32 value = eval_imm_expr(expr);

    if (value < min || max < value) {
      char str[200];
      sprintf(str, "immediate value (%d) out of range (%d .. %d)", value, min,
              max);
      error("spim: (parser) %s on line %d\n", str, line_no);
    }
  }
}

void check_uimm_range(imm_expr* expr, uint32 min, uint32 max) {
  if (expr->symbol == nullptr || SYMBOL_IS_DEFINED(expr->symbol)) {
    uint32 value = (uint32)eval_imm_expr(expr);

    if (value < min || max < value) {
      char str[200];
      sprintf(str, "immediate value (%d) out of range (%d .. %d)", (int32)value,
              (int32)min, (int32)max);
      error("spim: (parser) %s on line %d\n", str, line_no);
    }
  }
}
