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
#include "scanner.h" /* line_no, erroneous_line */

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

void parse_warn(const char* s) {
  error("spim: (parser) %s on line %d of file %s\n%s", s, line_no,
        input_file_name_get() ? input_file_name_get() : "(input)",
        erroneous_line());
}

void parse_error(const char* s) {
  parse_error_occurred = true;
  parse_errors_seen += 1;
  parse_warn(s);
}

/* ------------------------------------------------------------------ *
 * immediate_opcode_to_opcode: needed by src/instruction.c's pseudo-op expander.
 * ------------------------------------------------------------------ */

int immediate_opcode_to_opcode(int opcode) {
  switch (opcode) {
    case TOKEN_ADDI_OPCODE:
      return TOKEN_ADD_OPCODE;
    case TOKEN_ADDIU_OPCODE:
      return TOKEN_ADDU_OPCODE;
    case TOKEN_ANDI_OPCODE:
      return TOKEN_AND_OPCODE;
    case TOKEN_ORI_OPCODE:
      return TOKEN_OR_OPCODE;
    case TOKEN_XORI_OPCODE:
      return TOKEN_XOR_OPCODE;
    case TOKEN_SLTI_OPCODE:
      return TOKEN_SLT_OPCODE;
    case TOKEN_SLTIU_OPCODE:
      return TOKEN_SLTU_OPCODE;
    case TOKEN_J_OPCODE:
      return TOKEN_JR_OPCODE;
    case TOKEN_LUI_OPCODE:
      return TOKEN_ADDU_OPCODE;
    case TOKEN_SLL_OPCODE:
      return TOKEN_SLLV_OPCODE;
    case TOKEN_SRA_OPCODE:
      return TOKEN_SRAV_OPCODE;
    case TOKEN_SRL_OPCODE:
      return TOKEN_SRLV_OPCODE;
    default:
      fatal_error("Can't convert immediate op to op\n");
      return 0;
  }
}

/* Forward declarations for runtime helpers still provided by instruction.c.
   The instruction-emit calls (r_type, i_type_free, ...) come
   in via parser.h's emit_* dispatch wrappers so they participate in
   AST construction. */

void nop_instruction(void) {
  emit_r(TOKEN_SLL_OPCODE, 0, 0, 0); /* sll $0, $0, 0 == 0x00000000 */
}

void trap_instruction(void) { emit_r(TOKEN_BREAK_OPCODE, 0, 0, 0); }

immediate_expression* branch_offset(int instruction_count) {
  return constant_immediate_expression(
      instruction_count << 2); /* later shifted right 2 by the encoder */
}

int opcode_to_immediate_opcode(int opcode) {
  switch (opcode) {
    case TOKEN_ADD_OPCODE:
      return TOKEN_ADDI_OPCODE;
    case TOKEN_ADDU_OPCODE:
      return TOKEN_ADDIU_OPCODE;
    case TOKEN_AND_OPCODE:
      return TOKEN_ANDI_OPCODE;
    case TOKEN_OR_OPCODE:
      return TOKEN_ORI_OPCODE;
    case TOKEN_XOR_OPCODE:
      return TOKEN_XORI_OPCODE;
    case TOKEN_SLT_OPCODE:
      return TOKEN_SLTI_OPCODE;
    case TOKEN_SLTU_OPCODE:
      return TOKEN_SLTIU_OPCODE;
    case TOKEN_SLLV_OPCODE:
      return TOKEN_SLL_OPCODE;
    case TOKEN_SRAV_OPCODE:
      return TOKEN_SRA_OPCODE;
    case TOKEN_SRLV_OPCODE:
      return TOKEN_SRL_OPCODE;
    default:
      fatal_error("Can't convert op to immediate op\n");
      return 0;
  }
}

void divide_instruction(int opcode, int destination_register,
                        int source_register, int target_register,
                        int constant_divisor) {
  if (destination_register != 0 && !constant_divisor) {
    emit_i_free(TOKEN_BNE_OPCODE, 0, target_register, branch_offset(3));
    nop_instruction();
    trap_instruction();
  }

  if (opcode == TOKEN_DIV_OPCODE || opcode == TOKEN_REM_PSEUDO_OP)
    emit_r(TOKEN_DIV_OPCODE, 0, source_register, target_register);
  else
    emit_r(TOKEN_DIVU_OPCODE, 0, source_register, target_register);

  if (destination_register != 0) {
    if (opcode == TOKEN_DIV_OPCODE ||
        opcode == TOKEN_DIVU_OPCODE) /* Quotient */
      emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
    else
      /* Remainder */
      emit_r(TOKEN_MFHI_OPCODE, destination_register, 0, 0);
  }
}

void multiply_instruction(int opcode, int destination_register,
                          int source_register, int target_register) {
  if (opcode == TOKEN_MULOU_PSEUDO_OP)
    emit_r(TOKEN_MULTU_OPCODE, 0, source_register, target_register);
  else
    emit_r(TOKEN_MULT_OPCODE, 0, source_register, target_register);

  if (opcode == TOKEN_MULOU_PSEUDO_OP && destination_register != 0) {
    emit_r(TOKEN_MFHI_OPCODE, 1, 0, 0); /* Use $at */
    emit_i_free(TOKEN_BEQ_OPCODE, 0, 1, branch_offset(3));
    nop_instruction();
    trap_instruction();
  } else if (opcode == TOKEN_MULO_PSEUDO_OP && destination_register != 0) {
    emit_r(TOKEN_MFHI_OPCODE, 1, 0, 0); /* use $at */
    emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
    emit_r_shift(TOKEN_SRA_OPCODE, destination_register, destination_register,
                 31);
    emit_i_free(TOKEN_BEQ_OPCODE, destination_register, 1, branch_offset(3));
    nop_instruction();
    trap_instruction();
  } else {
    /* Any other op, or a result into $zero: no overflow check. */
  }
  if (destination_register != 0)
    emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
}

void set_less_or_equal_instruction(int opcode, int destination_register,
                                   int source_register, int target_register) {
  emit_i_free(TOKEN_BNE_OPCODE, source_register, target_register,
              branch_offset(3));
  emit_i_free(TOKEN_ORI_OPCODE, destination_register, 0,
              constant_immediate_expression(1));
  emit_i_free(TOKEN_BEQ_OPCODE, 0, 0, branch_offset(3));
  nop_instruction();
  emit_r((opcode == TOKEN_SLE_PSEUDO_OP ? TOKEN_SLT_OPCODE : TOKEN_SLTU_OPCODE),
         destination_register, source_register, target_register);
}

void set_greater_than_instruction(int opcode, int destination_register,
                                  int source_register, int target_register) {
  emit_r(opcode == TOKEN_SGT_PSEUDO_OP ? TOKEN_SLT_OPCODE : TOKEN_SLTU_OPCODE,
         destination_register, target_register, source_register);
}

void set_greater_or_equal_instruction(int opcode, int destination_register,
                                      int source_register,
                                      int target_register) {
  emit_i_free(TOKEN_BNE_OPCODE, source_register, target_register,
              branch_offset(3));
  emit_i_free(TOKEN_ORI_OPCODE, destination_register, 0,
              constant_immediate_expression(1));
  emit_i_free(TOKEN_BEQ_OPCODE, 0, 0, branch_offset(3));
  nop_instruction();
  emit_r(opcode == TOKEN_SGE_PSEUDO_OP ? TOKEN_SLT_OPCODE : TOKEN_SLTU_OPCODE,
         destination_register, target_register, source_register);
}

void set_equal_instruction(int opcode, int destination_register,
                           int source_register, int target_register) {
  immediate_expression *if_equal, *if_not_equal;

  if (opcode == TOKEN_SEQ_PSEUDO_OP) {
    if_equal = constant_immediate_expression(1);
    if_not_equal = constant_immediate_expression(0);
  } else {
    if_equal = constant_immediate_expression(0);
    if_not_equal = constant_immediate_expression(1);
  }

  emit_i_free(TOKEN_BEQ_OPCODE, source_register, target_register,
              branch_offset(3));
  /* RD <- 0 (if not equal) */
  emit_i_free(TOKEN_ORI_OPCODE, destination_register, 0, if_not_equal);
  emit_i_free(TOKEN_BEQ_OPCODE, 0, 0, branch_offset(3)); /* Branch always */
  nop_instruction();
  /* RD <- 1 */
  emit_i_free(TOKEN_ORI_OPCODE, destination_register, 0, if_equal);
}

void check_immediate_range(const immediate_expression* expression, int32_t min,
                           int32_t max, int line) {
  if (expression->symbol == nullptr || SYMBOL_IS_DEFINED(expression->symbol)) {
    /* If expression can be evaluated, compare against limits. */
    const int32_t value = evaluate_immediate_expression(expression);

    if (value < min || max < value) {
      char string[200];
      snprintf(string, sizeof string,
               "immediate value (%d) out of range (%d .. %d)", value, min, max);
      error("spim: (parser) %s on line %d\n", string, line);
    }
  }
}

void check_unsigned_immediate_range(const immediate_expression* expression,
                                    uint32_t min, uint32_t max, int line) {
  if (expression->symbol == nullptr || SYMBOL_IS_DEFINED(expression->symbol)) {
    const uint32_t value = (uint32_t)evaluate_immediate_expression(expression);

    if (value < min || max < value) {
      char string[200];
      snprintf(string, sizeof string,
               "immediate value (%d) out of range (%d .. %d)", (int32_t)value,
               (int32_t)min, (int32_t)max);
      error("spim: (parser) %s on line %d\n", string, line);
    }
  }
}
