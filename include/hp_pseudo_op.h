/* SPIM S20 MIPS simulator.
   Hand-written-parser side: pseudo-op expansion helpers.

   Relocated from parser.y's postlude.  The bison path keeps
   its own static copy (untouched during Phases 2-4); these
   functions are called only from the hand-written parser.

   Phase 5 will drop the bison copy and rename these to their
   canonical names (without the hp_ prefix).

   Copyright (c) 1990-2026, James R. Larus and contributors.
   BSD 3-Clause.
*/

#ifndef HP_PSEUDO_OP_H
#define HP_PSEUDO_OP_H

#include "spim.h"
#include "inst.h"

/* Pseudo-op expansion — each emits a sequence of real R/I-type
   instructions that implements the user-visible pseudo-op. */

void hp_div_inst(int op, int rd, int rs, int rt, int const_divisor);
void hp_mult_inst(int op, int rd, int rs, int rt);
void hp_set_le_inst(int op, int rd, int rs, int rt);
void hp_set_gt_inst(int op, int rd, int rs, int rt);
void hp_set_ge_inst(int op, int rd, int rs, int rt);
void hp_set_eq_inst(int op, int rd, int rs, int rt);

/* Common building-block helpers. */

void hp_nop_inst(void);
void hp_trap_inst(void);
imm_expr* hp_branch_offset(int n_inst);

/* Range checkers for immediate values. */

void hp_check_imm_range(imm_expr* expr, int32 min, int32 max);
void hp_check_uimm_range(imm_expr* expr, uint32 min, uint32 max);

/* Opcode lookups. */

int hp_op_to_imm_op(int opcode);
int hp_imm_op_to_op(int opcode);

#endif /* HP_PSEUDO_OP_H */
