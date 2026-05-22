/* SPIM S20 MIPS simulator.
   Pseudo-op expansion helpers.

   Copyright (c) 1990-2026, James R. Larus and contributors.
   BSD 3-Clause.
*/

#ifndef HP_PSEUDO_OP_H
#define HP_PSEUDO_OP_H

#include "spim.h"
#include "inst.h"

/* Pseudo-op expansion — each emits a sequence of real R/I-type
   instructions that implements the user-visible pseudo-op. */

void div_inst(int op, int rd, int rs, int rt, int const_divisor);
void mult_inst(int op, int rd, int rs, int rt);
void set_le_inst(int op, int rd, int rs, int rt);
void set_gt_inst(int op, int rd, int rs, int rt);
void set_ge_inst(int op, int rd, int rs, int rt);
void set_eq_inst(int op, int rd, int rs, int rt);

/* Common building-block helpers. */

void nop_inst(void);
void trap_inst(void);
imm_expr* branch_offset(int n_inst);

/* Range checkers for immediate values. */

void check_imm_range(imm_expr* expr, int32_t min, int32_t max);
void check_uimm_range(imm_expr* expr, uint32_t min, uint32_t max);

/* Opcode lookups. */

int op_to_imm_op(int opcode);

#endif /* HP_PSEUDO_OP_H */
