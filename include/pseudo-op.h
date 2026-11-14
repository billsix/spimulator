/* SPIM S20 MIPS simulator.
   Pseudo-op expansion helpers.

   Copyright (c) 1990-2026, James R. Larus and contributors.
   BSD 3-Clause.
*/

#ifndef HP_PSEUDO_OP_H
#define HP_PSEUDO_OP_H

#include "spim.h"
#include "instruction.h"

/* Pseudo-op expansion — each emits a sequence of real R/I-type
   instructions that implements the user-visible pseudo-op. */

void div_inst(int opcode, int destination_register, int source_register, int target_register, int constant_divisor);
void mult_inst(int opcode, int destination_register, int source_register, int target_register);
void set_le_inst(int opcode, int destination_register, int source_register, int target_register);
void set_gt_inst(int opcode, int destination_register, int source_register, int target_register);
void set_ge_inst(int opcode, int destination_register, int source_register, int target_register);
void set_eq_inst(int opcode, int destination_register, int source_register, int target_register);

/* Common building-block helpers. */

void nop_inst(void);
void trap_inst(void);
imm_expr* branch_offset(int instruction_count);

/* Range checkers for immediate values. */

/* Report an immediate outside [min, max] as a parse error on LINE, the line of
   the instruction it belongs to.  (The scanner's line_no can already be on the
   next line: reading past the immediate scans the newline, which advances it.)
 */
void check_imm_range(const imm_expr* expression, int32_t min, int32_t max, int line);
void check_uimm_range(const imm_expr* expression, uint32_t min, uint32_t max,
                      int line);

/* Opcode lookups. */

int op_to_imm_op(int opcode);

#endif /* HP_PSEUDO_OP_H */
