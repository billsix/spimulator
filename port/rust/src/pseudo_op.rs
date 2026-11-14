//! Pseudo-instruction expansions and the parser's error messages
//! (`pseudo-op.c`).
//!
//! A pseudo-instruction is one the assembler accepts but the processor
//! does not have: `rem $t0, $t1, $t2` becomes a check for division by
//! zero, a `div`, and an `mfhi`.  The helpers here append the real
//! instructions to the syntax tree, under the pseudo-instruction's node.

use crate::instruction::ImmediateExpression;
use crate::machine::{error, error_bytes, fatal_error, Machine};
use crate::opcodes::*;

/// The register-register form of an immediate instruction
/// (`immediate_opcode_to_opcode`): `addi` to `add`, `ori` to `or`, ...
pub fn immediate_opcode_to_opcode(opcode: i32) -> i32 {
    match opcode {
        TOKEN_ADDI_OPCODE => TOKEN_ADD_OPCODE,
        TOKEN_ADDIU_OPCODE => TOKEN_ADDU_OPCODE,
        TOKEN_ANDI_OPCODE => TOKEN_AND_OPCODE,
        TOKEN_ORI_OPCODE => TOKEN_OR_OPCODE,
        TOKEN_XORI_OPCODE => TOKEN_XOR_OPCODE,
        TOKEN_SLTI_OPCODE => TOKEN_SLT_OPCODE,
        TOKEN_SLTIU_OPCODE => TOKEN_SLTU_OPCODE,
        TOKEN_J_OPCODE => TOKEN_JR_OPCODE,
        TOKEN_LUI_OPCODE => TOKEN_ADDU_OPCODE,
        TOKEN_SLL_OPCODE => TOKEN_SLLV_OPCODE,
        TOKEN_SRA_OPCODE => TOKEN_SRAV_OPCODE,
        TOKEN_SRL_OPCODE => TOKEN_SRLV_OPCODE,
        _ => fatal_error("Can't convert immediate op to op\n"),
    }
}

/// The immediate form of a register-register instruction (`opcode_to_immediate_opcode`).
pub fn opcode_to_immediate_opcode(opcode: i32) -> i32 {
    match opcode {
        TOKEN_ADD_OPCODE => TOKEN_ADDI_OPCODE,
        TOKEN_ADDU_OPCODE => TOKEN_ADDIU_OPCODE,
        TOKEN_AND_OPCODE => TOKEN_ANDI_OPCODE,
        TOKEN_OR_OPCODE => TOKEN_ORI_OPCODE,
        TOKEN_XOR_OPCODE => TOKEN_XORI_OPCODE,
        TOKEN_SLT_OPCODE => TOKEN_SLTI_OPCODE,
        TOKEN_SLTU_OPCODE => TOKEN_SLTIU_OPCODE,
        TOKEN_SLLV_OPCODE => TOKEN_SLL_OPCODE,
        TOKEN_SRAV_OPCODE => TOKEN_SRA_OPCODE,
        TOKEN_SRLV_OPCODE => TOKEN_SRL_OPCODE,
        _ => fatal_error("Can't convert op to immediate op\n"),
    }
}

/// A branch offset of `instruction_count` instructions (`branch_offset`).
pub fn branch_offset(instruction_count: i32) -> ImmediateExpression {
    ImmediateExpression::constant(instruction_count << 2)
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
    pub fn nop_instruction(&mut self) {
        self.emit_r(TOKEN_SLL_OPCODE, 0, 0, 0);
    }

    /// `break 0`.
    pub fn trap_instruction(&mut self) {
        self.emit_r(TOKEN_BREAK_OPCODE, 0, 0, 0);
    }

    /// `div`/`divu`/`rem`/`remu` with a destination (`divide_instruction`): a check
    /// for a zero divisor (unless the divisor is a constant), the
    /// division, and `mflo` (quotient) or `mfhi` (remainder).
    pub fn divide_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
        constant_divisor: bool,
    ) {
        if destination_register != 0 && !constant_divisor {
            self.emit_i(TOKEN_BNE_OPCODE, 0, target_register, &branch_offset(3));
            self.nop_instruction();
            self.trap_instruction();
        }
        if op == TOKEN_DIV_OPCODE || op == TOKEN_REM_PSEUDO_OP {
            self.emit_r(TOKEN_DIV_OPCODE, 0, source_register, target_register);
        } else {
            self.emit_r(TOKEN_DIVU_OPCODE, 0, source_register, target_register);
        }
        if destination_register != 0 {
            if op == TOKEN_DIV_OPCODE || op == TOKEN_DIVU_OPCODE {
                self.emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
            } else {
                self.emit_r(TOKEN_MFHI_OPCODE, destination_register, 0, 0);
            }
        }
    }

    /// `mulo`/`mulou` (`multiply_instruction`): multiply, and trap if the product
    /// does not fit in 32 bits.
    pub fn multiply_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        if op == TOKEN_MULOU_PSEUDO_OP {
            self.emit_r(TOKEN_MULTU_OPCODE, 0, source_register, target_register);
        } else {
            self.emit_r(TOKEN_MULT_OPCODE, 0, source_register, target_register);
        }
        if op == TOKEN_MULOU_PSEUDO_OP && destination_register != 0 {
            self.emit_r(TOKEN_MFHI_OPCODE, 1, 0, 0);
            self.emit_i(TOKEN_BEQ_OPCODE, 0, 1, &branch_offset(3));
            self.nop_instruction();
            self.trap_instruction();
        } else if op == TOKEN_MULO_PSEUDO_OP && destination_register != 0 {
            self.emit_r(TOKEN_MFHI_OPCODE, 1, 0, 0);
            self.emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
            self.emit_r_shift(
                TOKEN_SRA_OPCODE,
                destination_register,
                destination_register,
                31,
            );
            self.emit_i(TOKEN_BEQ_OPCODE, destination_register, 1, &branch_offset(3));
            self.nop_instruction();
            self.trap_instruction();
        }
        if destination_register != 0 {
            self.emit_r(TOKEN_MFLO_OPCODE, destination_register, 0, 0);
        }
    }

    /// `sle`/`sleu` (`set_less_or_equal_instruction`).
    pub fn set_less_or_equal_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        self.emit_i(
            TOKEN_BNE_OPCODE,
            source_register,
            target_register,
            &branch_offset(3),
        );
        self.emit_i(
            TOKEN_ORI_OPCODE,
            destination_register,
            0,
            &ImmediateExpression::constant(1),
        );
        self.emit_i(TOKEN_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_instruction();
        let slt: i32 = if op == TOKEN_SLE_PSEUDO_OP {
            TOKEN_SLT_OPCODE
        } else {
            TOKEN_SLTU_OPCODE
        };
        self.emit_r(slt, destination_register, source_register, target_register);
    }

    /// `sgt`/`sgtu`: `slt` with the operands swapped (`set_greater_than_instruction`).
    pub fn set_greater_than_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        let slt: i32 = if op == TOKEN_SGT_PSEUDO_OP {
            TOKEN_SLT_OPCODE
        } else {
            TOKEN_SLTU_OPCODE
        };
        self.emit_r(slt, destination_register, target_register, source_register);
    }

    /// `sge`/`sgeu` (`set_greater_or_equal_instruction`).
    pub fn set_greater_or_equal_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        self.emit_i(
            TOKEN_BNE_OPCODE,
            source_register,
            target_register,
            &branch_offset(3),
        );
        self.emit_i(
            TOKEN_ORI_OPCODE,
            destination_register,
            0,
            &ImmediateExpression::constant(1),
        );
        self.emit_i(TOKEN_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_instruction();
        let slt: i32 = if op == TOKEN_SGE_PSEUDO_OP {
            TOKEN_SLT_OPCODE
        } else {
            TOKEN_SLTU_OPCODE
        };
        self.emit_r(slt, destination_register, target_register, source_register);
    }

    /// `seq`/`sne` (`set_equal_instruction`).
    pub fn set_equal_instruction(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        let (if_eq, if_neq): (i32, i32) = if op == TOKEN_SEQ_PSEUDO_OP {
            (1, 0)
        } else {
            (0, 1)
        };
        self.emit_i(
            TOKEN_BEQ_OPCODE,
            source_register,
            target_register,
            &branch_offset(3),
        );
        self.emit_i(
            TOKEN_ORI_OPCODE,
            destination_register,
            0,
            &ImmediateExpression::constant(if_neq),
        );
        self.emit_i(TOKEN_BEQ_OPCODE, 0, 0, &branch_offset(3));
        self.nop_instruction();
        self.emit_i(
            TOKEN_ORI_OPCODE,
            destination_register,
            0,
            &ImmediateExpression::constant(if_eq),
        );
    }

    /// Warn if a known immediate is outside `min..=max` (`check_immediate_range`),
    /// on `line`, the instruction's line: the scanner's line can already be
    /// the next one, as reading past the immediate scans the newline.
    pub fn check_immediate_range(
        &mut self,
        expr: &ImmediateExpression,
        min: i32,
        max: i32,
        line: i32,
    ) {
        if self.expression_is_known(expr) {
            let value: i32 = self.evaluate_immediate_expression(expr);
            if value < min || max < value {
                error(&format!(
                    "spim: (parser) immediate value ({value}) out of range ({min} .. {max}) on line {line}\n"
                ));
            }
        }
    }

    /// Warn if a known immediate is outside `min..=max` as an unsigned
    /// number (`check_unsigned_immediate_range`), on `line`.
    pub fn check_unsigned_immediate_range(
        &mut self,
        expr: &ImmediateExpression,
        min: u32,
        max: u32,
        line: i32,
    ) {
        if self.expression_is_known(expr) {
            let value: u32 = self.evaluate_immediate_expression(expr) as u32;
            if value < min || max < value {
                error(&format!(
                    "spim: (parser) immediate value ({}) out of range ({} .. {}) on line {line}\n",
                    value as i32, min as i32, max as i32,
                ));
            }
        }
    }
}
