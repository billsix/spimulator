//! Operand-shape tags of the opcode table (`include/opcode-types.h`).
//!
//! Every row of the opcode table carries one of these tags.  The parser
//! uses it to know which operands to read, the encoder and decoder to know
//! where each field lives in the 32-bit word, and the disassembler to know
//! how to print the instruction.  Each name spells the operand list as the
//! student writes it: `RdRsRtInst` is `add rd, rs, rt`.

/// The operand shape of an opcode-table row.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpType {
    /// An assembler directive (`.data`, `.word`, ...).
    AsmDir,
    /// A pseudo-instruction the parser expands (`li`, `la`, `bge`, ...).
    PseudoOp,
    /// `bc1t label`.
    BranchCoprocCondInst,
    /// `bgez rs, label`.
    BranchRsLabelInst,
    /// `tgei rs, imm16` (the trap-immediate instructions).
    RsImmInst,
    /// `lui rt, imm16`.
    RtImmInst,
    /// `addi rt, rs, imm16`.
    RtRsImmInst,
    /// `beq rs, rt, label`.
    BranchRsRtLabelInst,
    /// `lw rt, address`.
    RtAddrInst,
    /// `jr rs`.
    RsInst,
    /// `mfhi rd`.
    RdInst,
    /// `mult rs, rt`.
    RsRtInst,
    /// `clz rd, rs` (also `jalr`).
    RdRsInst,
    /// `mfc0 rt, coprocessor-register`.
    RtCopregInst,
    /// `sll rd, rt, shamt`.
    RdRtShamtInst,
    /// `add rd, rs, rt`.
    RdRsRtInst,
    /// `sllv rd, rt, rs`.
    RdRtRsInst,
    /// `lwc1 ft, address`.
    FpFtAddrInst,
    /// `mov.s fd, fs`.
    FpFdFsInst,
    /// `mfc1 rt, fs`.
    FpRtCopregInst,
    /// `c.eq.s fs, ft`.
    FpCmpFsFtInst,
    /// `add.s fd, fs, ft`.
    FpFdFsFtInst,
    /// `madd.s fd, fr, fs, ft`.
    FpFdFrFsFtInst,
    /// `movt.s fd, fs, cc` (and `movn.s fd, fs, rt`).
    FpMovcTypeInst,
    /// `movt rd, rs, cc`.
    MovcTypeInst,
    /// `j label`.
    JTypeInst,
    /// `syscall`, `nop`, `break n`.
    NoargTypeInst,
}
