-- | Operand-shape tags for the rows of the opcode table
-- (C: include/opcode-types.h).
--
-- Each tag spells the operand list as the student writes it, left to right:
-- 'RD_RS_RT_INST' is @add rd, rs, rt@, 'RT_ADDR_INST' is @lw rt, address@.
-- The constructor names are the C enumerator names, so the two sources can
-- be read side by side.
module OpcodeTypes (OpType (..)) where

-- | The operand shape of one keyword.
data OpType
  = ASM_DIR -- ^ an assembler directive (@.word@, @.text@, ...)
  | PSEUDO_OP -- ^ a pseudo-instruction the assembler expands (@li@, @la@, ...)
  | BRANCH_COPROC_COND_INST -- ^ @bc1t label@
  | BRANCH_RS_LABEL_INST -- ^ @bgez rs, label@
  | RS_IMM_INST -- ^ @tgei rs, imm16@
  | RT_IMM_INST -- ^ @lui rt, imm16@
  | RT_RS_IMM_INST -- ^ @addi rt, rs, imm16@
  | BRANCH_RS_RT_LABEL_INST -- ^ @beq rs, rt, label@
  | RT_ADDR_INST -- ^ @lw rt, address@
  | RS_INST -- ^ @jr rs@
  | RD_INST -- ^ @mfhi rd@
  | RS_RT_INST -- ^ @mult rs, rt@
  | RD_RS_INST -- ^ @clz rd, rs@ (also @jalr@)
  | RT_COPREG_INST -- ^ @mfc0 rt, coprocessor-register@
  | RD_RT_SHAMT_INST -- ^ @sll rd, rt, shamt@
  | RD_RS_RT_INST -- ^ @add rd, rs, rt@
  | RD_RT_RS_INST -- ^ @sllv rd, rt, rs@
  | FP_FT_ADDR_INST -- ^ @lwc1 ft, address@
  | FP_FD_FS_INST -- ^ @mov.s fd, fs@
  | FP_RT_COPREG_INST -- ^ @mfc1 rt, coprocessor-register@
  | FP_CMP_FS_FT_INST -- ^ @c.eq.s fs, ft@
  | FP_FD_FS_FT_INST -- ^ @add.s fd, fs, ft@
  | FP_FD_FR_FS_FT_INST -- ^ @madd.s fd, fr, fs, ft@
  | FP_MOVC_TYPE_INST -- ^ @movt.s fd, fs, cc@
  | MOVC_TYPE_INST -- ^ @movt rd, rs, cc@
  | J_TYPE_INST -- ^ @j label@
  | NOARG_TYPE_INST -- ^ @syscall@, @nop@, @break N@
  deriving (Eq, Show)
