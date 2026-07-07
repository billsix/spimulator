/* SPIM S20 MIPS simulator.
   Operand-shape type tags used by opcodes.h's X-macro entries.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef OP_TYPES_H
#define OP_TYPES_H

#include <stdint.h>

/* Each OP() entry in opcodes.h carries one of these tags in its third
   argument.  Tables consuming opcodes.h (i_opcode_tbl in
   instruction.c, keyword_tbl in scanner.c, op_type_table in parser.c)
   key per-instruction handling on the value.  uint8_t-backed — values
   fit in 0..42.

   Naming scheme: each tag spells the operand list exactly as the
   student writes it in assembly, left to right, using the MIPS
   register-field names (rd/rs/rt, fd/fs/ft/fr for FP):

       RD_RS_RT_INST     ->  add   rd, rs, rt
       RT_RS_IMM_INST    ->  addiu rt, rs, imm16
       RT_ADDR_INST      ->  lw    rt, address
       RD_RT_SHAMT_INST  ->  sll   rd, rt, shamt
       BRANCH_RS_RT_LABEL_INST -> beq rs, rt, label

   COPREG is a coprocessor register operand (mfc0/mtc1-family).
   MOVC/FP_MOVC are the conditional moves on the FP condition code;
   J covers `<op> label` or `<op> reg` jumps; NOARG covers `<op>`
   alone (syscall, nop) or `<op> imm` (break N).

   Lives in a separate header from opcodes.h because opcodes.h is
   included inside other enum bodies (notably tokens.h's TOK_* enum)
   where a typedef would be a syntax error.  opcodes.h itself is pure
   X-macro content. */
typedef enum op_type : uint8_t {
  ASM_DIR = 0,
  PSEUDO_OP = 1,

  BRANCH_COPROC_COND_INST = 10, /* bc1t/bc1f label            */
  BRANCH_RS_LABEL_INST = 11,    /* bgez rs, label             */
  RS_IMM_INST = 12,             /* tgei rs, imm16 (trap ops)  */
  RT_IMM_INST = 13,             /* lui  rt, imm16             */
  RT_RS_IMM_INST = 14,          /* addi rt, rs, imm16         */
  BRANCH_RS_RT_LABEL_INST = 15, /* beq  rs, rt, label         */
  RT_ADDR_INST = 16,            /* lw   rt, address           */

  RS_INST = 20,          /* jr   rs                    */
  RD_INST = 21,          /* mfhi rd                    */
  RS_RT_INST = 22,       /* mult rs, rt                */
  RD_RS_INST = 23,       /* clz  rd, rs (also jalr)    */
  RT_COPREG_INST = 24,   /* mfc0 rt, coproc-reg        */
  RD_RT_SHAMT_INST = 25, /* sll  rd, rt, shamt         */
  RD_RS_RT_INST = 26,    /* add  rd, rs, rt            */
  RD_RT_RS_INST = 27,    /* sllv rd, rt, rs            */

  FP_FT_ADDR_INST = 30,     /* lwc1  ft, address          */
  FP_FD_FS_INST = 31,       /* mov.s fd, fs               */
  FP_RT_COPREG_INST = 32,   /* mfc1  rt, coproc-reg       */
  FP_CMP_FS_FT_INST = 33,   /* c.eq.s fs, ft              */
  FP_FD_FS_FT_INST = 34,    /* add.s fd, fs, ft           */
  FP_FD_FR_FS_FT_INST = 35, /* madd.s fd, fr, fs, ft      */
  FP_MOVC_TYPE_INST = 36,   /* movt.s fd, fs, cc          */
  MOVC_TYPE_INST = 37,      /* movt rd, rs, cc            */

  J_TYPE_INST = 40,     /* j label / jalr rs          */
  NOARG_TYPE_INST = 42, /* syscall, nop, break N      */
} op_type;

#endif /* OP_TYPES_H */
