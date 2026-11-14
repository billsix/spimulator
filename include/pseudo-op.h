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

void divide_instruction(int opcode, int destination_register, int source_register, int target_register, int constant_divisor);
void multiply_instruction(int opcode, int destination_register, int source_register, int target_register);
void set_less_or_equal_instruction(int opcode, int destination_register, int source_register, int target_register);
void set_greater_than_instruction(int opcode, int destination_register, int source_register, int target_register);
void set_greater_or_equal_instruction(int opcode, int destination_register, int source_register, int target_register);
void set_equal_instruction(int opcode, int destination_register, int source_register, int target_register);

/* Common building-block helpers. */

void nop_instruction(void);
void trap_instruction(void);
immediate_expression* branch_offset(int instruction_count);

/* Range checkers for immediate values. */

/* Report an immediate outside [min, max] as a parse error on LINE, the line of
   the instruction it belongs to.  (The scanner's line_no can already be on the
   next line: reading past the immediate scans the newline, which advances it.)
 */
void check_immediate_range(const immediate_expression* expression, int32_t min, int32_t max, int line);
void check_unsigned_immediate_range(const immediate_expression* expression, uint32_t min, uint32_t max,
                      int line);

/* Opcode lookups. */

int opcode_to_immediate_opcode(int opcode);

#endif /* HP_PSEUDO_OP_H */
