/* SPIM S20 MIPS simulator.
   Operand-shape type tags used by op.h's X-macro entries.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef OP_TYPES_H
#define OP_TYPES_H

#include <stdint.h>

/* Each OP() entry in op.h carries one of these tags in its third
   argument.  Tables consuming op.h (i_opcode_tbl in instruction.c,
   keyword_tbl in scanner.c, op_type_table in parser.c) key per-
   instruction handling on the value.  uint8_t-backed — values
   fit in 0..42.

   Lives in a separate header from op.h because op.h is included
   inside other enum bodies (notably tokens.h's TOK_* enum) where
   a typedef would be a syntax error.  op.h itself is pure X-macro
   content. */
typedef enum op_type : uint8_t {
  ASM_DIR = 0,
  PSEUDO_OP = 1,

  BC_TYPE_INST = 10,
  B1_TYPE_INST = 11,
  I1s_TYPE_INST = 12,
  I1t_TYPE_INST = 13,
  I2_TYPE_INST = 14,
  B2_TYPE_INST = 15,
  I2a_TYPE_INST = 16,

  R1s_TYPE_INST = 20,
  R1d_TYPE_INST = 21,
  R2st_TYPE_INST = 22,
  R2ds_TYPE_INST = 23,
  R2td_TYPE_INST = 24,
  R2sh_TYPE_INST = 25,
  R3_TYPE_INST = 26,
  R3sh_TYPE_INST = 27,

  FP_I2a_TYPE_INST = 30,
  FP_R2ds_TYPE_INST = 31,
  FP_R2ts_TYPE_INST = 32,
  FP_CMP_TYPE_INST = 33,
  FP_R3_TYPE_INST = 34,
  FP_R4_TYPE_INST = 35,
  FP_MOVC_TYPE_INST = 36,
  MOVC_TYPE_INST = 37,

  J_TYPE_INST = 40,
  NOARG_TYPE_INST = 42,
} op_type;

#endif /* OP_TYPES_H */
