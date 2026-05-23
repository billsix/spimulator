/* SPIM S20 MIPS simulator.
   Pseudo-op expansion helpers + runtime error funnel.

   Each pseudo-op expander emits a sequence of real R/I-type
   instructions that implements the user-visible pseudo-op.

   Copyright (c) 1990-2026, James R. Larus and contributors.
   BSD 3-Clause.
*/

#include <stdio.h>

#include "spim.h"
#include "instruction.h"
#include "symbol-table.h" /* SYMBOL_IS_DEFINED */
#include "tokens.h"       /* Y_*_OP, Y_*_POP token values */
#include "parser.h"       /* emit_r, emit_r_shift, emit_i_free, ... */
#include "pseudo-op.h"

extern int line_no;                /* from scanner */
extern char* erroneous_line(void); /* from scanner.c */

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
 * imm_op_to_op: needed by src/instruction.c's pseudo-op expander.
 * ------------------------------------------------------------------ */

int imm_op_to_op(int opcode) {
  switch (opcode) {
    case TOK_ADDI_OPCODE:
      return TOK_ADD_OPCODE;
    case TOK_ADDIU_OPCODE:
      return TOK_ADDU_OPCODE;
    case TOK_ANDI_OPCODE:
      return TOK_AND_OPCODE;
    case TOK_ORI_OPCODE:
      return TOK_OR_OPCODE;
    case TOK_XORI_OPCODE:
      return TOK_XOR_OPCODE;
    case TOK_SLTI_OPCODE:
      return TOK_SLT_OPCODE;
    case TOK_SLTIU_OPCODE:
      return TOK_SLTU_OPCODE;
    case TOK_J_OPCODE:
      return TOK_JR_OPCODE;
    case TOK_LUI_OPCODE:
      return TOK_ADDU_OPCODE;
    case TOK_SLL_OPCODE:
      return TOK_SLLV_OPCODE;
    case TOK_SRA_OPCODE:
      return TOK_SRAV_OPCODE;
    case TOK_SRL_OPCODE:
      return TOK_SRLV_OPCODE;
    default:
      fatal_error("Can't convert immediate op to op\n");
      return 0;
  }
}

/* Forward declarations for runtime helpers still provided by instruction.c.
   The instruction-emit calls (r_type_inst, i_type_inst_free, ...) come
   in via parser.h's emit_* dispatch wrappers so they participate in
   AST construction. */
extern imm_expr* const_imm_expr(int value);
extern int32_t eval_imm_expr(imm_expr* expr);

void nop_inst(void) {
  emit_r(TOK_SLL_OPCODE, 0, 0, 0); /* sll $0, $0, 0 == 0x00000000 */
}

void trap_inst(void) { emit_r(TOK_BREAK_OPCODE, 0, 0, 0); }

imm_expr* branch_offset(int n_inst) {
  return const_imm_expr(n_inst << 2); /* later shifted right 2 by the encoder */
}

int op_to_imm_op(int opcode) {
  switch (opcode) {
    case TOK_ADD_OPCODE:
      return TOK_ADDI_OPCODE;
    case TOK_ADDU_OPCODE:
      return TOK_ADDIU_OPCODE;
    case TOK_AND_OPCODE:
      return TOK_ANDI_OPCODE;
    case TOK_OR_OPCODE:
      return TOK_ORI_OPCODE;
    case TOK_XOR_OPCODE:
      return TOK_XORI_OPCODE;
    case TOK_SLT_OPCODE:
      return TOK_SLTI_OPCODE;
    case TOK_SLTU_OPCODE:
      return TOK_SLTIU_OPCODE;
    case TOK_SLLV_OPCODE:
      return TOK_SLL_OPCODE;
    case TOK_SRAV_OPCODE:
      return TOK_SRA_OPCODE;
    case TOK_SRLV_OPCODE:
      return TOK_SRL_OPCODE;
    default:
      fatal_error("Can't convert op to immediate op\n");
      return 0;
  }
}

void div_inst(int op, int rd, int rs, int rt, int const_divisor) {
  if (rd != 0 && !const_divisor) {
    emit_i_free(TOK_BNE_OPCODE, 0, rt, branch_offset(3));
    nop_inst();
    trap_inst();
  }

  if (op == TOK_DIV_OPCODE || op == TOK_REM_PSEUDO_OP)
    emit_r(TOK_DIV_OPCODE, 0, rs, rt);
  else
    emit_r(TOK_DIVU_OPCODE, 0, rs, rt);

  if (rd != 0) {
    if (op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE) /* Quotient */
      emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
    else
      /* Remainder */
      emit_r(TOK_MFHI_OPCODE, rd, 0, 0);
  }
}

void mult_inst(int op, int rd, int rs, int rt) {
  if (op == TOK_MULOU_PSEUDO_OP)
    emit_r(TOK_MULTU_OPCODE, 0, rs, rt);
  else
    emit_r(TOK_MULT_OPCODE, 0, rs, rt);

  if (op == TOK_MULOU_PSEUDO_OP && rd != 0) {
    emit_r(TOK_MFHI_OPCODE, 1, 0, 0); /* Use $at */
    emit_i_free(TOK_BEQ_OPCODE, 0, 1, branch_offset(3));
    nop_inst();
    trap_inst();
  } else if (op == TOK_MULO_PSEUDO_OP && rd != 0) {
    emit_r(TOK_MFHI_OPCODE, 1, 0, 0); /* use $at */
    emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
    emit_r_shift(TOK_SRA_OPCODE, rd, rd, 31);
    emit_i_free(TOK_BEQ_OPCODE, rd, 1, branch_offset(3));
    nop_inst();
    trap_inst();
  }
  if (rd != 0) emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
}

void set_le_inst(int op, int rd, int rs, int rt) {
  emit_i_free(TOK_BNE_OPCODE, rs, rt, branch_offset(3));
  emit_i_free(TOK_ORI_OPCODE, rd, 0, const_imm_expr(1));
  emit_i_free(TOK_BEQ_OPCODE, 0, 0, branch_offset(3));
  nop_inst();
  emit_r((op == TOK_SLE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE), rd, rs,
         rt);
}

void set_gt_inst(int op, int rd, int rs, int rt) {
  emit_r(op == TOK_SGT_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, rd, rt,
         rs);
}

void set_ge_inst(int op, int rd, int rs, int rt) {
  emit_i_free(TOK_BNE_OPCODE, rs, rt, branch_offset(3));
  emit_i_free(TOK_ORI_OPCODE, rd, 0, const_imm_expr(1));
  emit_i_free(TOK_BEQ_OPCODE, 0, 0, branch_offset(3));
  nop_inst();
  emit_r(op == TOK_SGE_PSEUDO_OP ? TOK_SLT_OPCODE : TOK_SLTU_OPCODE, rd, rt,
         rs);
}

void set_eq_inst(int op, int rd, int rs, int rt) {
  imm_expr *if_eq, *if_neq;

  if (op == TOK_SEQ_PSEUDO_OP) {
    if_eq = const_imm_expr(1);
    if_neq = const_imm_expr(0);
  } else {
    if_eq = const_imm_expr(0);
    if_neq = const_imm_expr(1);
  }

  emit_i_free(TOK_BEQ_OPCODE, rs, rt, branch_offset(3));
  /* RD <- 0 (if not equal) */
  emit_i_free(TOK_ORI_OPCODE, rd, 0, if_neq);
  emit_i_free(TOK_BEQ_OPCODE, 0, 0, branch_offset(3)); /* Branch always */
  nop_inst();
  /* RD <- 1 */
  emit_i_free(TOK_ORI_OPCODE, rd, 0, if_eq);
}

void check_imm_range(imm_expr* expr, int32_t min, int32_t max) {
  if (expr->symbol == nullptr || SYMBOL_IS_DEFINED(expr->symbol)) {
    /* If expression can be evaluated, compare against limits. */
    int32_t value = eval_imm_expr(expr);

    if (value < min || max < value) {
      char str[200];
      sprintf(str, "immediate value (%d) out of range (%d .. %d)", value, min,
              max);
      error("spim: (parser) %s on line %d\n", str, line_no);
    }
  }
}

void check_uimm_range(imm_expr* expr, uint32_t min, uint32_t max) {
  if (expr->symbol == nullptr || SYMBOL_IS_DEFINED(expr->symbol)) {
    uint32_t value = (uint32_t)eval_imm_expr(expr);

    if (value < min || max < value) {
      char str[200];
      sprintf(str, "immediate value (%d) out of range (%d .. %d)",
              (int32_t)value, (int32_t)min, (int32_t)max);
      error("spim: (parser) %s on line %d\n", str, line_no);
    }
  }
}
