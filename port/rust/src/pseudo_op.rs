//! Pseudo-instruction expansions and the parser's error messages
//! (`pseudo-op.c`).
//!
//! A pseudo-instruction is one the assembler accepts but the processor
//! does not have: `rem $t0, $t1, $t2` becomes a check for division by
//! zero, a `div`, and an `mfhi`.  The helpers here append the real
//! instructions to the syntax tree, under the pseudo-instruction's node.

use crate::instruction::ImmExpr;
use crate::machine::{error, error_bytes, fatal_error, Machine};
use crate::opcodes::*;

/// The register-register form of an immediate instruction
/// (`imm_op_to_op`): `addi` to `add`, `ori` to `or`, ...
pub fn imm_op_to_op(opcode: i32) -> i32 {
    match opcode {
        TOK_ADDI_OPCODE => TOK_ADD_OPCODE,
        TOK_ADDIU_OPCODE => TOK_ADDU_OPCODE,
        TOK_ANDI_OPCODE => TOK_AND_OPCODE,
        TOK_ORI_OPCODE => TOK_OR_OPCODE,
        TOK_XORI_OPCODE => TOK_XOR_OPCODE,
        TOK_SLTI_OPCODE => TOK_SLT_OPCODE,
        TOK_SLTIU_OPCODE => TOK_SLTU_OPCODE,
        TOK_J_OPCODE => TOK_JR_OPCODE,
        TOK_LUI_OPCODE => TOK_ADDU_OPCODE,
        TOK_SLL_OPCODE => TOK_SLLV_OPCODE,
        TOK_SRA_OPCODE => TOK_SRAV_OPCODE,
        TOK_SRL_OPCODE => TOK_SRLV_OPCODE,
        _ => fatal_error("Can't convert immediate op to op\n"),
    }
}

/// The immediate form of a register-register instruction (`op_to_imm_op`).
pub fn op_to_imm_op(opcode: i32) -> i32 {
    match opcode {
        TOK_ADD_OPCODE => TOK_ADDI_OPCODE,
        TOK_ADDU_OPCODE => TOK_ADDIU_OPCODE,
        TOK_AND_OPCODE => TOK_ANDI_OPCODE,
        TOK_OR_OPCODE => TOK_ORI_OPCODE,
        TOK_XOR_OPCODE => TOK_XORI_OPCODE,
        TOK_SLT_OPCODE => TOK_SLTI_OPCODE,
        TOK_SLTU_OPCODE => TOK_SLTIU_OPCODE,
        TOK_SLLV_OPCODE => TOK_SLL_OPCODE,
        TOK_SRAV_OPCODE => TOK_SRA_OPCODE,
        TOK_SRLV_OPCODE => TOK_SRL_OPCODE,
        _ => fatal_error("Can't convert op to immediate op\n"),
    }
}

/// A branch offset of `n_inst` instructions (`branch_offset`).
pub fn branch_offset(n_inst: i32) -> ImmExpr {
    ImmExpr::constant(n_inst << 2)
}

impl Machine {
    /// Report an error with the offending line (`parse_error`); used for
    /// errors found while assembling, such as a label defined twice.
    pub fn parse_error(&mut self, message: &str) {
        self.asm.parse_error_occurred = true;
        self.asm.parse_errors_seen += 1;
        self.parse_warn(message);
    }

    /// Print a parser warning with the offending line (`parse_warn`).
    pub fn parse_warn(&mut self, message: &str) {
        let mut text: Vec<u8> = format!(
            "spim: (parser) {message} on line {} of file ",
            self.scanner.line_no()
        )
        .into_bytes();
        text.extend_from_slice(self.asm.input_file_name.as_deref().unwrap_or(b"(input)"));
        text.push(b'\n');
        text.extend_from_slice(&self.scanner.erroneous_line());
        error_bytes(&text);
    }

    /// `nop`: `sll $0, $0, 0`, the all-zero word.
    pub fn nop_inst(&mut self) {
        self.emit_r(TOK_SLL_OPCODE, 0, 0, 0);
    }

    /// `break 0`.
    pub fn trap_inst(&mut self) {
        self.emit_r(TOK_BREAK_OPCODE, 0, 0, 0);
    }

    /// `div`/`divu`/`rem`/`remu` with a destination (`div_inst`): a check
    /// for a zero divisor (unless the divisor is a constant), the
    /// division, and `mflo` (quotient) or `mfhi` (remainder).
    pub fn div_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32, const_divisor: bool) {
        if rd != 0 && !const_divisor {
            self.emit_i(TOK_BNE_OPCODE, 0, rt, &branch_offset(3));
            self.nop_inst();
            self.trap_inst();
        }
        if op == TOK_DIV_OPCODE || op == TOK_REM_PSEUDO_OP {
            self.emit_r(TOK_DIV_OPCODE, 0, rs, rt);
        } else {
            self.emit_r(TOK_DIVU_OPCODE, 0, rs, rt);
        }
        if rd != 0 {
            if op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE {
                self.emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
            } else {
                self.emit_r(TOK_MFHI_OPCODE, rd, 0, 0);
            }
        }
    }

    /// `mulo`/`mulou` (`mult_inst`): multiply, and trap if the product
    /// does not fit in 32 bits.
    pub fn mult_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        if op == TOK_MULOU_PSEUDO_OP {
            self.emit_r(TOK_MULTU_OPCODE, 0, rs, rt);
        } else {
            self.emit_r(TOK_MULT_OPCODE, 0, rs, rt);
        }
        if op == TOK_MULOU_PSEUDO_OP && rd != 0 {
            self.emit_r(TOK_MFHI_OPCODE, 1, 0, 0);
            self.emit_i(TOK_BEQ_OPCODE, 0, 1, &branch_offset(3));
            self.nop_inst();
            self.trap_inst();
        } else if op == TOK_MULO_PSEUDO_OP && rd != 0 {
            self.emit_r(TOK_MFHI_OPCODE, 1, 0, 0);
            self.emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
            self.emit_r_shift(TOK_SRA_OPCODE, rd, rd, 31);
            self.emit_i(TOK_BEQ_OPCODE, rd, 1, &branch_offset(3));
            self.nop_inst();
            self.trap_inst();
        }
        if rd != 0 {
            self.emit_r(TOK_MFLO_OPCODE, rd, 0, 0);
        }
    }

    /// `sle`/`sleu` (`set_le_inst`).
    pub fn set_le_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        self.emit_i(TOK_BNE_OPCODE, rs, rt, &branch_offset(3));
        self.emit_i(TOK_ORI_OPCODE, rd, 0, &ImmExpr::constant(1));
        self.emit_i(TOK_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_inst();
        let slt: i32 = if op == TOK_SLE_PSEUDO_OP {
            TOK_SLT_OPCODE
        } else {
            TOK_SLTU_OPCODE
        };
        self.emit_r(slt, rd, rs, rt);
    }

    /// `sgt`/`sgtu`: `slt` with the operands swapped (`set_gt_inst`).
    pub fn set_gt_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        let slt: i32 = if op == TOK_SGT_PSEUDO_OP {
            TOK_SLT_OPCODE
        } else {
            TOK_SLTU_OPCODE
        };
        self.emit_r(slt, rd, rt, rs);
    }

    /// `sge`/`sgeu` (`set_ge_inst`).
    pub fn set_ge_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        self.emit_i(TOK_BNE_OPCODE, rs, rt, &branch_offset(3));
        self.emit_i(TOK_ORI_OPCODE, rd, 0, &ImmExpr::constant(1));
        self.emit_i(TOK_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_inst();
        let slt: i32 = if op == TOK_SGE_PSEUDO_OP {
            TOK_SLT_OPCODE
        } else {
            TOK_SLTU_OPCODE
        };
        self.emit_r(slt, rd, rt, rs);
    }

    /// `seq`/`sne` (`set_eq_inst`).
    pub fn set_eq_inst(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        let (if_eq, if_neq): (i32, i32) = if op == TOK_SEQ_PSEUDO_OP {
            (1, 0)
        } else {
            (0, 1)
        };
        self.emit_i(TOK_BEQ_OPCODE, rs, rt, &branch_offset(3));
        self.emit_i(TOK_ORI_OPCODE, rd, 0, &ImmExpr::constant(if_neq));
        self.emit_i(TOK_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_inst();
        self.emit_i(TOK_ORI_OPCODE, rd, 0, &ImmExpr::constant(if_eq));
    }

    /// Warn if a known immediate is outside `min..=max` (`check_imm_range`).
    pub fn check_imm_range(&mut self, expr: &ImmExpr, min: i32, max: i32) {
        if self.expr_is_known(expr) {
            let value: i32 = self.eval_imm_expr(expr);
            if value < min || max < value {
                error(&format!(
                    "spim: (parser) immediate value ({value}) out of range ({min} .. {max}) on line {}\n",
                    self.scanner.line_no()
                ));
            }
        }
    }

    /// Warn if a known immediate is outside `min..=max` as an unsigned
    /// number (`check_uimm_range`).
    pub fn check_uimm_range(&mut self, expr: &ImmExpr, min: u32, max: u32) {
        if self.expr_is_known(expr) {
            let value: u32 = self.eval_imm_expr(expr) as u32;
            if value < min || max < value {
                error(&format!(
                    "spim: (parser) immediate value ({}) out of range ({} .. {}) on line {}\n",
                    value as i32,
                    min as i32,
                    max as i32,
                    self.scanner.line_no()
                ));
            }
        }
    }
}
