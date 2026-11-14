//! Instructions: how one is represented, encoded, decoded, printed, and
//! built by the assembler (`instruction.c`, `include/instruction.h`).
//!
//! The text segment does not hold 32-bit words; like the C engine, it
//! holds *instruction objects*: the opcode token, the register and
//! immediate fields, the binary encoding, the expression the immediate
//! came from (`label+4`), and the source line, for listings.  An
//! instruction waiting for a forward reference is shared between the text
//! segment and the symbol table, so it is an [`InstRef`]:
//! `Rc<RefCell<Instruction>>`, Rust's spelling of a shared, mutable object.

use std::cell::{Ref, RefCell, RefMut};
use std::rc::Rc;

use crate::arith::sign_extend_16;
use crate::machine::{error, Machine};
use crate::opcode_types::OpType;
use crate::opcodes::*;
use crate::registers::{GLOBAL_POINTER_REGISTER, REGISTER_NAMES};
use crate::symbol_table::{Label, LabelId};

/// Append `format!` output to a byte buffer.
macro_rules! bprint {
    ($buffer:expr, $($argument:tt)*) => {
        $buffer.extend_from_slice(format!($($argument)*).as_bytes())
    };
}
pub(crate) use bprint;

/// An instruction shared between the text segment and the symbol table.
pub type InstRef = Rc<RefCell<Instruction>>;

/// The expression an immediate field comes from: `symbol + offset`
/// (`imm_expr`).  Either part may be missing.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ImmediateExpression {
    /// The constant part.
    pub offset: i32,
    /// The label, if any.
    pub symbol: Option<LabelId>,
    /// Which bits of the value the instruction wants: positive for the
    /// upper 16 (`lui`), negative for the lower 16, 0 for all 32.
    pub bits: i16,
    /// A branch target: the value is relative to the branch's address.
    pub program_counter_relative: bool,
}

impl ImmediateExpression {
    /// A constant (`const_imm_expr`).
    pub fn constant(value: i32) -> ImmediateExpression {
        ImmediateExpression {
            offset: value,
            ..ImmediateExpression::default()
        }
    }

    /// A copy that wants only the upper 16 bits (`upper_bits_of_expr`).
    pub fn upper_bits(self) -> ImmediateExpression {
        ImmediateExpression { bits: 1, ..self }
    }

    /// A copy that wants only the lower 16 bits (`lower_bits_of_expr`).
    pub fn lower_bits(self) -> ImmediateExpression {
        ImmediateExpression { bits: -1, ..self }
    }

    /// A copy with `value` added to the constant (`incr_expr_offset`).
    pub fn plus(self, value: i32) -> ImmediateExpression {
        ImmediateExpression {
            offset: self.offset.wrapping_add(value),
            ..self
        }
    }

    /// True for the constant 0 (`is_zero_imm`).
    pub fn is_zero(&self) -> bool {
        self.offset == 0 && self.symbol.is_none()
    }
}

/// One instruction (`mips_instruction`).
///
/// The C struct keeps its fields in overlapping unions, close to the real
/// encoding: `rs`, `rt`, then either a 16-bit immediate or the `rd` and
/// `shamt` bytes, and a J-type target that overlays all four bytes.  The
/// port keeps those four bytes as they are (`fields`) and reads them
/// through accessors, so writing one field changes the overlapping ones
/// exactly as in C.  The floating-point names are aliases: `fs` is `rd`,
/// `ft` is `rt`, `fd` is `shamt`.
#[derive(Clone, Debug, Default)]
pub struct Instruction {
    /// The opcode token (`TOKEN_ADD_OPCODE`, ...), or 0 for a word that
    /// decodes to no instruction.
    pub opcode: i32,
    /// The four bytes `rs`, `rt`, `rd`, `shamt` (or the immediate in the
    /// last two, or the target in all four), little-endian.
    fields: [u8; 4],
    /// The binary encoding, or 0 if it has not been computed yet.
    pub encoding: i32,
    /// The expression the immediate or target came from, if any.
    pub expr: Option<ImmediateExpression>,
    /// The source line, as `"12: add $t0, $t1, $t2"`, for listings.
    pub source: Option<Rc<[u8]>>,
}

impl Instruction {
    /// An instruction with just an opcode, every field 0.
    pub fn new(opcode: i32) -> Instruction {
        Instruction {
            opcode,
            ..Instruction::default()
        }
    }

    /// Wrap the instruction to share it.
    pub fn into_ref(self) -> InstRef {
        Rc::new(RefCell::new(self))
    }

    /// The `rs` field (also `base` and the compare `cond`).
    pub fn source_register(&self) -> usize {
        usize::from(self.fields[0])
    }
    /// The `rt` field (also `ft`).
    pub fn target_register(&self) -> usize {
        usize::from(self.fields[1])
    }
    /// The `rd` field (also `fs`).
    pub fn destination_register(&self) -> usize {
        usize::from(self.fields[2])
    }
    /// The `shamt` field (also `fd`).
    pub fn shift_amount(&self) -> usize {
        usize::from(self.fields[3])
    }
    /// The 16-bit immediate, signed.
    pub fn immediate(&self) -> i16 {
        i16::from_le_bytes([self.fields[2], self.fields[3]])
    }
    /// The 26-bit jump target (all four bytes).
    pub fn target(&self) -> u32 {
        u32::from_le_bytes(self.fields)
    }
    /// `fs`, the first floating-point source.
    pub fn fs(&self) -> usize {
        self.destination_register()
    }
    /// `ft`, the second floating-point source.
    pub fn floating_point_target(&self) -> usize {
        self.target_register()
    }
    /// `fd`, the floating-point destination.
    pub fn floating_point_destination(&self) -> usize {
        self.shift_amount()
    }
    /// The condition-code number of a `bc1t` (`rt >> 2`).
    pub fn condition_code(&self) -> i32 {
        (self.target_register() >> 2) as i32
    }

    /// Set `rs`; like C's `(uint8_t)` store, only the low 8 bits are kept.
    pub fn set_source_register(&mut self, value: i32) {
        self.fields[0] = value as u8;
    }
    /// Set `rt`.
    pub fn set_target_register(&mut self, value: i32) {
        self.fields[1] = value as u8;
    }
    /// Set `rd`.
    pub fn set_destination_register(&mut self, value: i32) {
        self.fields[2] = value as u8;
    }
    /// Set `shamt`.
    pub fn set_shift_amount(&mut self, value: i32) {
        self.fields[3] = value as u8;
    }
    /// Set the immediate (its low 16 bits).
    pub fn set_immediate(&mut self, value: i32) {
        let bytes: [u8; 2] = (value as i16).to_le_bytes();
        self.fields[2] = bytes[0];
        self.fields[3] = bytes[1];
    }
    /// Set the jump target.
    pub fn set_target(&mut self, value: u32) {
        self.fields = value.to_le_bytes();
    }

    /// The branch displacement in bytes: the 16-bit word offset,
    /// sign-extended, times 4 (`BRANCH_OFFSET`).
    pub fn branch_offset(&self) -> i32 {
        (sign_extend_16(i32::from(self.immediate())) as u32).wrapping_shl(2) as i32
    }

    /// The binary encoding of the instruction (`inst_encode`).
    pub fn encode(&self) -> i32 {
        let Some(row) = token_row(self.opcode) else {
            return 0;
        };
        let table_opcode: i32 = row.encoding;
        let register_number =
            |value: usize, shift: u32| -> i32 { ((value & 0x1f) << shift) as i32 };
        let immediate_16: i32 = i32::from(self.immediate()) & 0xffff;
        match row.op_type {
            OpType::BranchCoprocessorConditionInstruction => {
                table_opcode
                    | register_number((self.condition_code() as usize) << 2, 16)
                    | immediate_16
            }
            OpType::BranchSourceRegisterLabelInstruction
            | OpType::SourceRegisterImmediateInstruction => {
                table_opcode | register_number(self.source_register(), 21) | immediate_16
            }
            OpType::TargetRegisterImmediateInstruction
            | OpType::TargetRegisterSourceRegisterImmediateInstruction
            | OpType::BranchSourceRegisterTargetRegisterLabelInstruction
            | OpType::TargetRegisterAddressInstruction
            | OpType::FloatingPointTargetAddressInstruction => {
                table_opcode
                    | register_number(self.source_register(), 21)
                    | register_number(self.target_register(), 16)
                    | immediate_16
            }
            OpType::SourceRegisterInstruction => {
                table_opcode | register_number(self.source_register(), 21)
            }
            OpType::DestinationRegisterInstruction => {
                table_opcode | register_number(self.destination_register(), 11)
            }
            OpType::TargetRegisterCoprocessorRegisterInstruction
            | OpType::FloatingPointTargetRegisterCoprocessorRegisterInstruction => {
                table_opcode
                    | register_number(self.target_register(), 16)
                    | register_number(self.destination_register(), 11)
            }
            OpType::SourceRegisterTargetRegisterInstruction => {
                table_opcode
                    | register_number(self.source_register(), 21)
                    | register_number(self.target_register(), 16)
            }
            OpType::DestinationRegisterSourceRegisterInstruction => {
                table_opcode
                    | register_number(self.source_register(), 21)
                    | register_number(self.destination_register(), 11)
            }
            OpType::DestinationRegisterTargetRegisterShiftAmountInstruction => {
                table_opcode
                    | register_number(self.target_register(), 16)
                    | register_number(self.destination_register(), 11)
                    | register_number(self.shift_amount(), 6)
            }
            OpType::DestinationRegisterSourceRegisterTargetRegisterInstruction
            | OpType::DestinationRegisterTargetRegisterSourceRegisterInstruction
            | OpType::MoveConditionalTypeInstruction => {
                table_opcode
                    | register_number(self.source_register(), 21)
                    | register_number(self.target_register(), 16)
                    | register_number(self.destination_register(), 11)
            }
            OpType::FloatingPointDestinationSourceInstruction => {
                table_opcode
                    | register_number(self.fs(), 11)
                    | register_number(self.floating_point_destination(), 6)
            }
            OpType::FloatingPointCompareSourceTargetInstruction => {
                table_opcode
                    | register_number(self.floating_point_target(), 16)
                    | register_number(self.fs(), 11)
                    | register_number(self.floating_point_destination(), 6)
                    | self.source_register() as i32
            }
            OpType::FloatingPointDestinationSourceTargetInstruction
            | OpType::FloatingPointMoveConditionalTypeInstruction => {
                table_opcode
                    | register_number(self.floating_point_target(), 16)
                    | register_number(self.fs(), 11)
                    | register_number(self.floating_point_destination(), 6)
            }
            OpType::JTypeInstruction => (table_opcode as u32 | self.target()) as i32,
            OpType::NoArgumentTypeInstruction => table_opcode,
            OpType::AssemblerDirective
            | OpType::PseudoOp
            | OpType::FloatingPointDestinationAddendSourceTargetInstruction => {
                crate::machine::fatal_error("Unknown instruction type in inst_encoding\n")
            }
        }
    }

    /// Decode a 32-bit word into an instruction (`inst_decode`).  A word
    /// that is no instruction decodes to opcode 0.
    pub fn decode(value: i32) -> Instruction {
        let word: u32 = value as u32;
        let mut table_opcode: u32 = word & 0xfc00_0000;
        // Some opcode classes continue the opcode in other fields.
        if table_opcode == 0 || table_opcode == 0x7000_0000 {
            table_opcode |= word & 0x3f; // SPECIAL, SPECIAL2: the funct field
        } else if table_opcode == 0x0400_0000 {
            table_opcode |= word & 0x001f_0000; // REGIMM: the rt field
        } else if table_opcode == 0x4000_0000 {
            table_opcode |= (word & 0x03e0_0000) | (word & 0x1f); // COP0
        } else if table_opcode == 0x4400_0000 {
            table_opcode |= word & 0x03e0_0000; // COP1
            if word & 0xff00_0000 == 0x4500_0000 {
                table_opcode |= word & 0x0001_0000; // bc1f/bc1t
            } else {
                table_opcode |= word & 0x3f;
            }
        } else if table_opcode == 0x4800_0000 {
            table_opcode |= word & 0x03e0_0000; // COP2: the rs field
        } else if table_opcode == 0x4c00_0000 {
            table_opcode |= word & 0x3f; // COP1X: the funct field
        }

        let field = |shift: u32| -> i32 { ((word >> shift) & 0x1f) as i32 };
        let (source_register, target_register, destination_register, shift_amount): (
            i32,
            i32,
            i32,
            i32,
        ) = (field(21), field(16), field(11), field(6));
        let low16: i32 = (word & 0xffff) as i32;
        let invalid: Instruction = Self::make_r(value, 0, 0, 0, 0, 0);
        let Some(token) = decode_token(table_opcode as i32) else {
            return invalid;
        };
        let Some(op_type) = token_type(token) else {
            return invalid;
        };
        match op_type {
            OpType::BranchCoprocessorConditionInstruction
            | OpType::TargetRegisterImmediateInstruction
            | OpType::TargetRegisterSourceRegisterImmediateInstruction
            | OpType::BranchSourceRegisterTargetRegisterLabelInstruction
            | OpType::TargetRegisterAddressInstruction
            | OpType::FloatingPointTargetAddressInstruction => {
                Self::make_i(value, token, source_register, target_register, low16)
            }
            OpType::BranchSourceRegisterLabelInstruction
            | OpType::SourceRegisterImmediateInstruction => {
                Self::make_i(value, token, source_register, 0, low16)
            }
            OpType::SourceRegisterInstruction => {
                Self::make_r(value, token, source_register, 0, 0, 0)
            }
            OpType::DestinationRegisterInstruction => {
                Self::make_r(value, token, 0, 0, destination_register, 0)
            }
            OpType::TargetRegisterCoprocessorRegisterInstruction
            | OpType::FloatingPointTargetRegisterCoprocessorRegisterInstruction => {
                Self::make_r(value, token, 0, target_register, destination_register, 0)
            }
            OpType::SourceRegisterTargetRegisterInstruction => {
                Self::make_r(value, token, source_register, target_register, 0, 0)
            }
            OpType::DestinationRegisterSourceRegisterInstruction => {
                Self::make_r(value, token, source_register, 0, destination_register, 0)
            }
            OpType::DestinationRegisterTargetRegisterShiftAmountInstruction => Self::make_r(
                value,
                token,
                0,
                target_register,
                destination_register,
                shift_amount,
            ),
            OpType::DestinationRegisterSourceRegisterTargetRegisterInstruction
            | OpType::DestinationRegisterTargetRegisterSourceRegisterInstruction
            | OpType::MoveConditionalTypeInstruction => Self::make_r(
                value,
                token,
                source_register,
                target_register,
                destination_register,
                0,
            ),
            OpType::FloatingPointDestinationSourceInstruction => {
                Self::make_coprocessor_r(value, token, destination_register, 0, shift_amount)
            }
            OpType::FloatingPointCompareSourceTargetInstruction => {
                // fs lives in the rd slot; the condition goes in the rs slot.
                let mut instruction: Instruction = Self::make_coprocessor_r(
                    value,
                    token,
                    destination_register,
                    target_register,
                    shift_amount,
                );
                instruction.set_source_register(value & 0xf);
                instruction
            }
            OpType::FloatingPointDestinationSourceTargetInstruction
            | OpType::FloatingPointMoveConditionalTypeInstruction => Self::make_coprocessor_r(
                value,
                token,
                destination_register,
                target_register,
                shift_amount,
            ),
            OpType::JTypeInstruction => {
                let mut instruction: Instruction = Instruction::new(token);
                instruction.set_target(word & 0x03ff_ffff);
                instruction.encoding = value;
                instruction
            }
            OpType::NoArgumentTypeInstruction => Self::make_r(value, token, 0, 0, 0, 0),
            OpType::AssemblerDirective
            | OpType::PseudoOp
            | OpType::FloatingPointDestinationAddendSourceTargetInstruction => invalid,
        }
    }

    /// A decoded R-type instruction (`mk_r_inst`).
    fn make_r(
        value: i32,
        opcode: i32,
        source_register: i32,
        target_register: i32,
        destination_register: i32,
        shift_amount: i32,
    ) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_source_register(source_register);
        instruction.set_target_register(target_register);
        instruction.set_destination_register(destination_register);
        instruction.set_shift_amount(shift_amount);
        instruction.encoding = value;
        instruction
    }

    /// A decoded coprocessor R-type instruction (`mk_co_r_inst`).
    fn make_coprocessor_r(
        value: i32,
        opcode: i32,
        fs: i32,
        floating_point_target: i32,
        floating_point_destination: i32,
    ) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_destination_register(fs);
        instruction.set_target_register(floating_point_target);
        instruction.set_shift_amount(floating_point_destination);
        instruction.encoding = value;
        instruction
    }

    /// A decoded I-type instruction (`mk_i_inst`).
    fn make_i(
        value: i32,
        opcode: i32,
        source_register: i32,
        target_register: i32,
        offset: i32,
    ) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_source_register(source_register);
        instruction.set_target_register(target_register);
        instruction.set_immediate(offset);
        instruction.encoding = value;
        instruction
    }

    /// An R-type instruction built by the assembler (`make_r_type`).
    pub fn r_type(
        opcode: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_source_register(source_register);
        instruction.set_target_register(target_register);
        instruction.set_destination_register(destination_register);
        instruction
    }
}

/// True for the conditional branches, including the branch
/// pseudo-instructions (`opcode_is_branch`).
pub fn opcode_is_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOKEN_BC1F_OPCODE
            | TOKEN_BC1FL_OPCODE
            | TOKEN_BC1T_OPCODE
            | TOKEN_BC1TL_OPCODE
            | TOKEN_BC2F_OPCODE
            | TOKEN_BC2FL_OPCODE
            | TOKEN_BC2T_OPCODE
            | TOKEN_BC2TL_OPCODE
            | TOKEN_BEQ_OPCODE
            | TOKEN_BEQL_OPCODE
            | TOKEN_BEQZ_PSEUDO_OP
            | TOKEN_BGE_PSEUDO_OP
            | TOKEN_BGEU_PSEUDO_OP
            | TOKEN_BGEZ_OPCODE
            | TOKEN_BGEZAL_OPCODE
            | TOKEN_BGEZALL_OPCODE
            | TOKEN_BGEZL_OPCODE
            | TOKEN_BGT_PSEUDO_OP
            | TOKEN_BGTU_PSEUDO_OP
            | TOKEN_BGTZ_OPCODE
            | TOKEN_BGTZL_OPCODE
            | TOKEN_BLE_PSEUDO_OP
            | TOKEN_BLEU_PSEUDO_OP
            | TOKEN_BLEZ_OPCODE
            | TOKEN_BLEZL_OPCODE
            | TOKEN_BLT_PSEUDO_OP
            | TOKEN_BLTU_PSEUDO_OP
            | TOKEN_BLTZ_OPCODE
            | TOKEN_BLTZAL_OPCODE
            | TOKEN_BLTZALL_OPCODE
            | TOKEN_BLTZL_OPCODE
            | TOKEN_BNE_OPCODE
            | TOKEN_BNEL_OPCODE
            | TOKEN_BNEZ_PSEUDO_OP
    )
}

/// True for the branch-likely forms, which skip their delay slot when not
/// taken (`opcode_is_nullified_branch`).
pub fn opcode_is_nullified_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOKEN_BC1FL_OPCODE
            | TOKEN_BC1TL_OPCODE
            | TOKEN_BC2FL_OPCODE
            | TOKEN_BC2TL_OPCODE
            | TOKEN_BEQL_OPCODE
            | TOKEN_BGEZALL_OPCODE
            | TOKEN_BGEZL_OPCODE
            | TOKEN_BGTZL_OPCODE
            | TOKEN_BLEZL_OPCODE
            | TOKEN_BLTZALL_OPCODE
            | TOKEN_BLTZL_OPCODE
            | TOKEN_BNEL_OPCODE
    )
}

/// True for the coprocessor branches taken on a true condition.
pub fn opcode_is_true_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOKEN_BC1T_OPCODE | TOKEN_BC1TL_OPCODE | TOKEN_BC2T_OPCODE | TOKEN_BC2TL_OPCODE
    )
}

/// True for `j` and `jal`.
pub fn opcode_is_jump(opcode: i32) -> bool {
    matches!(opcode, TOKEN_J_OPCODE | TOKEN_JAL_OPCODE)
}

/// True for the loads and stores.
pub fn opcode_is_load_store(opcode: i32) -> bool {
    matches!(
        opcode,
        TOKEN_LB_OPCODE
            | TOKEN_LBU_OPCODE
            | TOKEN_LH_OPCODE
            | TOKEN_LHU_OPCODE
            | TOKEN_LL_OPCODE
            | TOKEN_LDC1_OPCODE
            | TOKEN_LDC2_OPCODE
            | TOKEN_LW_OPCODE
            | TOKEN_LWC1_OPCODE
            | TOKEN_LWC2_OPCODE
            | TOKEN_LWL_OPCODE
            | TOKEN_LWR_OPCODE
            | TOKEN_SB_OPCODE
            | TOKEN_SC_OPCODE
            | TOKEN_SH_OPCODE
            | TOKEN_SDC1_OPCODE
            | TOKEN_SDC2_OPCODE
            | TOKEN_SW_OPCODE
            | TOKEN_SWC1_OPCODE
            | TOKEN_SWC2_OPCODE
            | TOKEN_SWL_OPCODE
            | TOKEN_SWR_OPCODE
    )
}

/// The immediates that the hardware sign-extends; a value for them fits
/// in the field if its top 17 bits are all 0 or all 1.
fn is_sign_extended_immediate(opcode: i32, expr: &ImmediateExpression) -> bool {
    matches!(
        opcode,
        TOKEN_ADDI_OPCODE
            | TOKEN_ADDIU_OPCODE
            | TOKEN_SLTI_OPCODE
            | TOKEN_SLTIU_OPCODE
            | TOKEN_TEQI_OPCODE
            | TOKEN_TGEI_OPCODE
            | TOKEN_TGEIU_OPCODE
            | TOKEN_TLTI_OPCODE
            | TOKEN_TLTIU_OPCODE
            | TOKEN_TNEI_OPCODE
    ) || (opcode_is_load_store(opcode) && expr.bits == 0)
}

impl Machine {
    /// The value of an immediate expression (`evaluate_immediate_expression`): the label's
    /// address plus the constant, then the upper or lower 16 bits if the
    /// expression asks for them.
    pub fn evaluate_immediate_expression(&self, expr: &ImmediateExpression) -> i32 {
        let value: i32 = match expr.symbol {
            None => expr.offset,
            Some(id) if self.symbols.label(id).is_defined() => expr
                .offset
                .wrapping_add(self.symbols.label(id).address as i32),
            Some(id) => {
                error(&format!(
                    "Evaluated undefined symbol: {}\n",
                    self.symbols.label(id).name
                ));
                0
            }
        };
        if expr.bits > 0 {
            (value >> 16) & 0xffff
        } else if expr.bits < 0 {
            value & 0xffff
        } else {
            value
        }
    }

    /// True if the expression has no label, or its label is defined.
    pub fn expression_is_known(&self, expr: &ImmediateExpression) -> bool {
        expr.symbol
            .is_none_or(|id: LabelId| self.symbols.label(id).is_defined())
    }

    // ----- Where the next instruction goes -----

    /// Start the user text segment at `addr` (`text_begins_at_point`).
    pub fn text_begins_at_point(&mut self, address: u32) {
        self.asm.next_text_location = address;
        self.asm.text_in_kernel = false;
    }

    /// Start the kernel text segment at `addr`.
    pub fn kernel_text_begins_at_point(&mut self, address: u32) {
        self.asm.next_kernel_text_location = address;
        self.asm.text_in_kernel = false;
    }

    /// Advance the text PC to a multiple of `2^alignment` (`.align` in
    /// the text segment), moving this line's labels along.
    pub fn align_text(&mut self, alignment: i32) {
        if alignment == 0 {
            return;
        }
        let round = |program_counter: u32| -> u32 {
            program_counter
                .wrapping_add(1u32.wrapping_shl(alignment as u32))
                .wrapping_sub(1)
                & 0xffff_ffffu32.wrapping_shl(alignment as u32)
        };
        if self.asm.text_in_kernel {
            self.asm.next_kernel_text_location = round(self.asm.next_kernel_text_location);
            self.fix_current_label_address(self.asm.next_kernel_text_location);
        } else {
            self.asm.next_text_location = round(self.asm.next_text_location);
            self.fix_current_label_address(self.asm.next_text_location);
        }
    }

    /// Set the text PC (`.text ADDR`).
    pub fn set_text_location(&mut self, address: u32) {
        if self.asm.text_in_kernel {
            self.asm.next_kernel_text_location = address;
        } else {
            self.asm.next_text_location = address;
        }
    }

    /// The address the next instruction goes to (`current_text_location`).
    pub fn current_text_location(&self) -> u32 {
        if self.asm.text_in_kernel {
            self.asm.next_kernel_text_location
        } else {
            self.asm.next_text_location
        }
    }

    /// Step the text PC past an instruction.
    fn increment_text_location(&mut self, delta: u32) {
        if self.asm.text_in_kernel {
            self.asm.next_kernel_text_location =
                self.asm.next_kernel_text_location.wrapping_add(delta);
            if self.mem.kernel_text_top <= self.asm.next_kernel_text_location {
                crate::machine::run_error("Can't expand kernel text segment\n");
            }
        } else {
            self.asm.next_text_location = self.asm.next_text_location.wrapping_add(delta);
            if self.mem.text_top <= self.asm.next_text_location {
                crate::machine::run_error("Can't expand text segment\n");
            }
        }
    }

    /// Send the next instructions to the kernel or the user text segment.
    pub fn user_kernel_text_segment(&mut self, to_kernel: bool) {
        self.asm.text_in_kernel = to_kernel;
    }

    // ----- Building instructions -----

    /// Store an instruction at the next text address (`store_instruction`).
    /// In the data segment, an instruction is stored as its encoding.
    fn store_instruction(&mut self, instruction: InstRef) {
        if self.asm.data_dir {
            let encoding: i32 = instruction.borrow().encode();
            self.store_word(encoding);
        } else if self.asm.text_directive {
            let at: u32 = self.current_text_location();
            self.exception_occurred = false;
            self.memory_write_instruction(at, Some(instruction.clone()));
            if self.exception_occurred {
                error(&format!("Invalid address (0x{at:08x}) for instruction\n"));
            } else {
                self.increment_text_location(4);
            }
            let source: Option<Rc<[u8]>> = if self.asm.emit_source_active {
                self.asm.emit_source_text.clone()
            } else {
                self.scanner.source_line().map(Rc::from)
            };
            let encoding: i32 = {
                let mut instruction: RefMut<'_, Instruction> = instruction.borrow_mut();
                instruction.source = source;
                if instruction.encoding == 0 {
                    instruction.encoding = instruction.encode();
                }
                instruction.encoding
            };
            self.listing_text_instruction(at, encoding as u32);
        } else {
            // Never reached: the parser always sets data_dir and text_directive as a pair, one true.
            crate::machine::fatal_error("internal error: store_instruction: neither the data nor the text segment is active\n");
        }
    }

    /// Build and store an I-type instruction (`i_type`).  The
    /// immediate may not fit in 16 bits; then this produces a sequence of
    /// instructions that builds the value in `$at` first.
    pub fn i_type(
        &mut self,
        opcode: i32,
        target_register: i32,
        source_register: i32,
        expr: &ImmediateExpression,
    ) {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_source_register(source_register);
        instruction.set_target_register(target_register);
        instruction.expr = Some(*expr);
        let instruction: InstRef = instruction.into_ref();
        if self.expression_is_known(expr) {
            let value: i32 = self.evaluate_immediate_expression(expr);
            let bits: u32 = value as u32;
            let too_big: bool = if is_sign_extended_immediate(opcode, expr) {
                bits & 0xffff_8000 != 0 && bits & 0xffff_8000 != 0xffff_8000
            } else {
                bits & 0xffff_0000 != 0
            };
            if !self.options.bare_machine && too_big {
                self.i_type_full_word(opcode, target_register, source_register, expr, true, value);
                return;
            }
            self.resolve_a_label(&instruction);
        } else if self.options.bare_machine || expr.bits != 0 {
            // The value is unknown, but only 16 bits of it are needed.
            let id: LabelId = expr.symbol.expect("an unknown expression has a label");
            self.record_inst_uses_symbol(&instruction, id);
        } else {
            // All 32 bits of an unknown value: assume it will not fit.
            self.i_type_full_word(opcode, target_register, source_register, expr, false, 0);
            return;
        }
        self.store_instruction(instruction);
    }

    /// Build an I-type instruction whose immediate does not fit in 16
    /// bits, using `$at` (`i_type_full_word`).
    fn i_type_full_word(
        &mut self,
        opcode: i32,
        target_register: i32,
        source_register: i32,
        expr: &ImmediateExpression,
        value_known: bool,
        value: i32,
    ) {
        let global_pointer_offset: Option<i32> =
            self.global_pointer_relative_offset(expr, source_register);
        if opcode_is_load_store(opcode) {
            if let Some(offset) = global_pointer_offset {
                self.i_type(
                    opcode,
                    target_register,
                    GLOBAL_POINTER_REGISTER as i32,
                    &ImmediateExpression::constant(offset),
                );
            } else if value_known {
                let mut high: i32 = (value >> 16) & 0xffff;
                let low: i32 = value & 0xffff;
                if !(high == 0 && low & 0x8000 == 0) && !(high == 0xffff && low & 0x8000 != 0) {
                    // Some of the high 16 bits are set.
                    if low & 0x8000 != 0 {
                        // The load sign-extends the low half; compensate.
                        high += 1;
                    }
                    self.i_type(TOKEN_LUI_OPCODE, 1, 0, &ImmediateExpression::constant(high));
                    if source_register != 0 {
                        self.r_type(TOKEN_ADDU_OPCODE, 1, 1, source_register);
                    }
                    self.i_type(
                        opcode,
                        target_register,
                        1,
                        &ImmediateExpression::constant(low).lower_bits(),
                    );
                } else {
                    // Sign-extending the low half gives the high half.
                    self.i_type(
                        opcode,
                        target_register,
                        source_register,
                        &ImmediateExpression::constant(low),
                    );
                }
            } else {
                self.i_type(TOKEN_LUI_OPCODE, 1, 0, &expr.upper_bits());
                if source_register != 0 {
                    self.r_type(TOKEN_ADDU_OPCODE, 1, 1, source_register);
                }
                self.i_type(opcode, target_register, 1, &expr.lower_bits());
            }
        } else if opcode_is_branch(opcode) {
            // This only allows branches +/- 32K, which is not correct!
            self.i_type(opcode, target_register, source_register, &expr.lower_bits());
        } else if let Some(offset) = global_pointer_offset {
            let op: i32 = if opcode == TOKEN_LUI_OPCODE {
                TOKEN_ADDIU_OPCODE
            } else {
                opcode
            };
            self.i_type(
                op,
                target_register,
                GLOBAL_POINTER_REGISTER as i32,
                &ImmediateExpression::constant(offset),
            );
        } else if matches!(
            opcode,
            TOKEN_ORI_OPCODE | TOKEN_ADDI_OPCODE | TOKEN_ADDIU_OPCODE | TOKEN_LUI_OPCODE
        ) && source_register == 0
        {
            self.produce_immediate(expr, target_register, value_known, value);
        } else {
            self.produce_immediate(expr, 1, value_known, value);
            self.r_type(
                crate::pseudo_op::immediate_opcode_to_opcode(opcode),
                target_register,
                source_register,
                1,
            );
        }
    }

    /// The offset from `$gp` of an expression on a small-data label, if it
    /// fits in 16 bits and no base register is used.
    fn global_pointer_relative_offset(
        &self,
        expr: &ImmediateExpression,
        source_register: i32,
    ) -> Option<i32> {
        let id: LabelId = expr.symbol?;
        let label: &Label = self.symbols.label(id);
        if !label.gp || source_register != 0 {
            return None;
        }
        let offset: i32 = (label.address + i64::from(expr.offset)) as i32;
        (-0x8000..=0x7fff).contains(&offset).then_some(offset)
    }

    /// Load a 32-bit value into register `rt` with `lui` and/or `ori`
    /// (`produce_immediate`).
    fn produce_immediate(
        &mut self,
        expr: &ImmediateExpression,
        target_register: i32,
        value_known: bool,
        value: i32,
    ) {
        if value_known && value & 0xffff == 0 {
            self.i_type(TOKEN_LUI_OPCODE, target_register, 0, &expr.upper_bits());
        } else if value_known && value as u32 & 0xffff_0000 == 0 {
            self.i_type(TOKEN_ORI_OPCODE, target_register, 0, &expr.lower_bits());
        } else {
            self.i_type(TOKEN_LUI_OPCODE, 1, 0, &expr.upper_bits());
            self.i_type(TOKEN_ORI_OPCODE, target_register, 1, &expr.lower_bits());
        }
    }

    /// Build and store a jump (`j_type`).
    pub fn j_type(&mut self, opcode: i32, target: &ImmediateExpression) {
        let target: ImmediateExpression = ImmediateExpression {
            offset: 0,
            program_counter_relative: false,
            ..*target
        };
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.expr = Some(target);
        let instruction: InstRef = instruction.into_ref();
        match target.symbol {
            Some(id) if !self.symbols.label(id).is_defined() => {
                self.record_inst_uses_symbol(&instruction, id);
            }
            _ => self.resolve_a_label(&instruction),
        }
        self.store_instruction(instruction);
    }

    /// Build and store an R-type instruction (`r_type`).
    pub fn r_type(
        &mut self,
        opcode: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        self.store_instruction(
            Instruction::r_type(
                opcode,
                destination_register,
                source_register,
                target_register,
            )
            .into_ref(),
        );
    }

    /// Build and store a coprocessor R-type instruction (`r_coprocessor_type`).
    pub fn r_coprocessor_type(
        &mut self,
        opcode: i32,
        floating_point_destination: i32,
        fs: i32,
        floating_point_target: i32,
    ) {
        let mut instruction: Instruction =
            Instruction::r_type(opcode, fs, 0, floating_point_target);
        instruction.set_shift_amount(floating_point_destination);
        self.store_instruction(instruction.into_ref());
    }

    /// Build and store a shift by a constant (`r_shift_amount_type`).
    pub fn r_shift_amount_type(
        &mut self,
        opcode: i32,
        destination_register: i32,
        target_register: i32,
        shift_amount: i32,
    ) {
        let mut instruction: Instruction =
            Instruction::r_type(opcode, destination_register, 0, target_register);
        instruction.set_shift_amount(shift_amount & 0x1f);
        self.store_instruction(instruction.into_ref());
    }

    /// Build and store a floating-point compare (`r_condition_type`).  The
    /// compare's condition bits say which outcomes (unordered, equal,
    /// less) make it true, and whether NaN operands trap.
    pub fn r_condition_type(
        &mut self,
        opcode: i32,
        fs: i32,
        floating_point_target: i32,
        condition_code: i32,
    ) {
        const UN: i32 = 0x1;
        const EQ: i32 = 0x2;
        const LT: i32 = 0x4;
        const IN: i32 = 0x8;
        let mut instruction: Instruction =
            Instruction::r_type(opcode, fs, 0, floating_point_target);
        instruction.set_shift_amount(condition_code << 2);
        let condition: i32 = match opcode {
            TOKEN_C_EQ_D_OPCODE | TOKEN_C_EQ_S_OPCODE => EQ,
            TOKEN_C_LE_D_OPCODE | TOKEN_C_LE_S_OPCODE => IN | LT | EQ,
            TOKEN_C_LT_D_OPCODE | TOKEN_C_LT_S_OPCODE => IN | LT,
            TOKEN_C_NGE_D_OPCODE | TOKEN_C_NGE_S_OPCODE => IN | LT | UN,
            TOKEN_C_NGLE_D_OPCODE | TOKEN_C_NGLE_S_OPCODE => IN | UN,
            TOKEN_C_NGL_D_OPCODE | TOKEN_C_NGL_S_OPCODE => IN | EQ | UN,
            TOKEN_C_NGT_D_OPCODE | TOKEN_C_NGT_S_OPCODE => IN | LT | EQ | UN,
            TOKEN_C_OLT_D_OPCODE | TOKEN_C_OLT_S_OPCODE => LT,
            TOKEN_C_OLE_D_OPCODE | TOKEN_C_OLE_S_OPCODE => LT | EQ,
            TOKEN_C_SEQ_D_OPCODE | TOKEN_C_SEQ_S_OPCODE => IN | EQ,
            TOKEN_C_SF_D_OPCODE | TOKEN_C_SF_S_OPCODE => IN,
            TOKEN_C_F_D_OPCODE | TOKEN_C_F_S_OPCODE => 0,
            TOKEN_C_UEQ_D_OPCODE | TOKEN_C_UEQ_S_OPCODE => EQ | UN,
            TOKEN_C_ULT_D_OPCODE | TOKEN_C_ULT_S_OPCODE => LT | UN,
            TOKEN_C_ULE_D_OPCODE | TOKEN_C_ULE_S_OPCODE => LT | EQ | UN,
            TOKEN_C_UN_D_OPCODE | TOKEN_C_UN_S_OPCODE => UN,
            // Never reached: only FP_CMP_FS_FT_INST rows reach here, and the arms above
            // are all 32 of them (16 conditions, .s and .d).
            _ => crate::machine::fatal_error(&format!(
                "internal error: r_cond_type: not an FP compare: {opcode}\n"
            )),
        };
        instruction.set_source_register(condition);
        self.store_instruction(instruction.into_ref());
    }

    // ----- Filling in labels -----

    /// Fill in the label of an instruction about to be stored at the
    /// current text (or data) address (`resolve_a_label`).
    fn resolve_a_label(&mut self, instruction: &InstRef) {
        let program_counter: u32 = if self.asm.data_dir {
            self.current_data_location()
        } else {
            self.current_text_location()
        };
        self.resolve_a_label_subtract(instruction, program_counter);
    }

    /// Fill in the immediate or target field of the instruction at `pc`
    /// now that its label is known (`resolve_a_label_subtract`).
    pub fn resolve_a_label_subtract(&mut self, instruction: &InstRef, program_counter: u32) {
        let (opcode, mut expr): (i32, ImmediateExpression) = {
            let instruction: Ref<'_, Instruction> = instruction.borrow();
            (instruction.opcode, instruction.expr.unwrap_or_default())
        };
        if expr.program_counter_relative {
            // The instruction may have moved since it was parsed.
            expr.offset = 0i32.wrapping_sub(program_counter as i32);
            instruction.borrow_mut().expr = Some(expr);
        }
        if !self.expression_is_known(&expr) {
            let name: String = expr.symbol.map_or(String::new(), |id: LabelId| {
                self.symbols.label(id).name.clone()
            });
            error(&format!("Resolving undefined symbol: {name}\n"));
            return;
        }
        let (value, field_mask): (i32, i32) = if opcode_is_branch(opcode) {
            // The whole byte offset: asking for the low 16 bits would
            // truncate a branch more than 32 KB away.
            let whole: ImmediateExpression = ImmediateExpression { bits: 0, ..expr };
            let byte_offset: i32 = self.evaluate_immediate_expression(&whole);
            // Drop the low two bits: instructions are on word boundaries.
            let mut word_offset: i32 = byte_offset / 4;
            if self.options.delayed_branches {
                word_offset -= 1;
            }
            if !(-32768..=32767).contains(&word_offset) {
                error(&format!(
                    "Branch target is out of range (more than 128 KB away) at pc 0x{program_counter:x}\n"
                ));
            }
            (word_offset & 0xffff, 0xffff)
        } else if opcode_is_jump(opcode) {
            let value: i32 = self.evaluate_immediate_expression(&expr);
            if value as u32 & 0xf000_0000 != program_counter & 0xf000_0000 {
                error(&format!(
                    "Target of jump differs in high-order 4 bits from instruction pc 0x{program_counter:x}\n"
                ));
            }
            // The top four bits come from the PC, the low two are 0.
            ((value & 0x0fff_ffff) >> 2, -1)
        } else if opcode_is_load_store(opcode) {
            let value: i32 = self.evaluate_immediate_expression(&expr);
            if value & 0x8000 != 0 {
                self.compensate_lui_for_sign_extension(&expr, program_counter);
            }
            (value, 0xffff)
        } else {
            (self.evaluate_immediate_expression(&expr), 0xffff)
        };
        if value & !field_mask != 0 && value & !field_mask != 0xffff_0000u32 as i32 {
            error("Immediate value is too large for field: ");
            self.print_instruction(program_counter);
        }
        let mut instruction: RefMut<'_, Instruction> = instruction.borrow_mut();
        if opcode_is_jump(opcode) {
            instruction.set_target(value as u32);
        } else {
            instruction.set_immediate(value);
        }
        instruction.encoding = instruction.encode();
    }

    /// A load or store sign-extends its 16-bit offset, so when the low half
    /// of a label's address has bit 15 set, the `lui` that loads the high
    /// half must add 1.  Find that `lui` just before the instruction (or
    /// two before, past an `addu` of a base register) and adjust it.
    fn compensate_lui_for_sign_extension(
        &mut self,
        expr: &ImmediateExpression,
        program_counter: u32,
    ) {
        let previous: Option<InstRef> =
            self.memory_read_instruction(program_counter.wrapping_sub(4));
        let before_previous: Option<InstRef> =
            self.memory_read_instruction(program_counter.wrapping_sub(8));
        let is_matching_lui = |candidate: &Option<InstRef>| -> bool {
            candidate.as_ref().is_some_and(|instruction: &InstRef| {
                let instruction: Ref<'_, Instruction> = instruction.borrow();
                instruction.opcode == TOKEN_LUI_OPCODE
                    && instruction.expr.map(|e: ImmediateExpression| e.symbol) == Some(expr.symbol)
                    && instruction.immediate() == 0
            })
        };
        let target: Option<InstRef> = if is_matching_lui(&previous) {
            previous
        } else if is_matching_lui(&before_previous) {
            before_previous
        } else {
            None
        };
        if let Some(lui) = target {
            let mut lui: RefMut<'_, Instruction> = lui.borrow_mut();
            if let Some(lui_expression) = lui.expr.as_mut() {
                lui_expression.offset = lui_expression.offset.wrapping_add(0x10000);
            }
        }
    }

    // ----- Printing instructions -----

    /// Print the instruction at `addr` on stderr (`print_instruction`).
    pub fn print_instruction(&mut self, address: u32) {
        let text: Vec<u8> = self.instruction_to_string(address);
        crate::machine::error_bytes(&text);
    }

    /// The listing line of the instruction at `addr` (`instruction_to_string`).
    pub fn instruction_to_string(&mut self, address: u32) -> Vec<u8> {
        // A side-effect-free read: showing an instruction must not raise,
        // or clear, the exception the program's own step raised.
        let Some(instruction) = self.memory_peek_instruction(address) else {
            error(&format!(
                "Can't print instruction not in text segment (0x{address:08x})\n"
            ));
            return Vec::new();
        };
        let mut text: Vec<u8> = Vec::new();
        self.format_an_instruction(&mut text, instruction.as_ref(), address);
        text
    }

    /// Append the listing line of `instruction`, stored at `addr`
    /// (`format_an_instruction`):
    ///
    /// `[0x00400000]` TAB `0x8fa40000  lw $a0, 0($sp)` ... `; 183: lw $a0 0($sp)`
    ///
    /// The source line starts at byte column 57 (the TAB counts as one)
    /// when the disassembly is shorter.
    pub fn format_an_instruction(
        &mut self,
        out: &mut Vec<u8>,
        instruction: Option<&InstRef>,
        address: u32,
    ) {
        let line_start: usize = out.len();
        if address != 0 && self.instruction_is_breakpoint(address) {
            self.delete_breakpoint(address);
            out.push(b'*');
            let original: Option<InstRef> = self.memory_read_instruction(address);
            self.format_an_instruction(out, original.as_ref(), address);
            self.add_breakpoint(address);
            return;
        }
        bprint!(out, "[0x{address:08x}]\t");
        let Some(instruction) = instruction else {
            out.extend_from_slice(b"<none>\n");
            return;
        };
        let instruction: Instruction = instruction.borrow().clone();
        let Some(row) = token_row(instruction.opcode) else {
            bprint!(out, "<unknown instruction {}>\n", instruction.opcode);
            return;
        };
        let register_number = |n: usize| -> &'static str { REGISTER_NAMES[n & 0x1f] };
        bprint!(out, "0x{:08x}  {}", instruction.encoding as u32, row.name);
        match row.op_type {
            OpType::BranchCoprocessorConditionInstruction => {
                bprint!(
                    out,
                    "{} {}",
                    instruction.condition_code(),
                    instruction.branch_offset()
                )
            }
            OpType::BranchSourceRegisterLabelInstruction => {
                bprint!(
                    out,
                    " ${} {}",
                    register_number(instruction.source_register()),
                    instruction.branch_offset()
                )
            }
            OpType::SourceRegisterImmediateInstruction => {
                bprint!(
                    out,
                    " ${}, {}",
                    register_number(instruction.source_register()),
                    instruction.immediate()
                )
            }
            OpType::TargetRegisterImmediateInstruction => {
                bprint!(
                    out,
                    " ${}, {}",
                    register_number(instruction.target_register()),
                    instruction.immediate()
                )
            }
            OpType::TargetRegisterSourceRegisterImmediateInstruction => {
                bprint!(
                    out,
                    " ${}, ${}, {}",
                    register_number(instruction.target_register()),
                    register_number(instruction.source_register()),
                    instruction.immediate()
                )
            }
            OpType::BranchSourceRegisterTargetRegisterLabelInstruction => bprint!(
                out,
                " ${}, ${}, {}",
                register_number(instruction.source_register()),
                register_number(instruction.target_register()),
                instruction.branch_offset()
            ),
            OpType::TargetRegisterAddressInstruction => {
                bprint!(
                    out,
                    " ${}, {}(${})",
                    register_number(instruction.target_register()),
                    instruction.immediate(),
                    register_number(instruction.source_register())
                )
            }
            OpType::SourceRegisterInstruction => {
                bprint!(out, " ${}", register_number(instruction.source_register()))
            }
            OpType::DestinationRegisterInstruction => bprint!(
                out,
                " ${}",
                register_number(instruction.destination_register())
            ),
            OpType::TargetRegisterCoprocessorRegisterInstruction => {
                bprint!(
                    out,
                    " ${}, ${}",
                    register_number(instruction.target_register()),
                    register_number(instruction.destination_register())
                )
            }
            OpType::SourceRegisterTargetRegisterInstruction => {
                bprint!(
                    out,
                    " ${}, ${}",
                    register_number(instruction.source_register()),
                    register_number(instruction.target_register())
                )
            }
            OpType::DestinationRegisterSourceRegisterInstruction => {
                bprint!(
                    out,
                    " ${}, ${}",
                    register_number(instruction.destination_register()),
                    register_number(instruction.source_register())
                )
            }
            OpType::DestinationRegisterTargetRegisterShiftAmountInstruction => {
                if instruction.destination_register() == 0
                    && instruction.target_register() == 0
                    && instruction.shift_amount() == 0
                {
                    // `sll $0, $0, 0` is the canonical nop.
                    out.truncate(out.len() - 3);
                    out.extend_from_slice(b"nop");
                } else {
                    bprint!(
                        out,
                        " ${}, ${}, {}",
                        register_number(instruction.destination_register()),
                        register_number(instruction.target_register()),
                        instruction.shift_amount()
                    );
                }
            }
            OpType::DestinationRegisterSourceRegisterTargetRegisterInstruction => bprint!(
                out,
                " ${}, ${}, ${}",
                register_number(instruction.destination_register()),
                register_number(instruction.source_register()),
                register_number(instruction.target_register())
            ),
            OpType::DestinationRegisterTargetRegisterSourceRegisterInstruction => bprint!(
                out,
                " ${}, ${}, ${}",
                register_number(instruction.destination_register()),
                register_number(instruction.target_register()),
                register_number(instruction.source_register())
            ),
            OpType::FloatingPointTargetAddressInstruction => {
                bprint!(
                    out,
                    " $f{}, {}(${})",
                    instruction.floating_point_target(),
                    instruction.immediate(),
                    register_number(instruction.source_register())
                )
            }
            OpType::FloatingPointDestinationSourceInstruction => {
                bprint!(
                    out,
                    " $f{}, $f{}",
                    instruction.floating_point_destination(),
                    instruction.fs()
                )
            }
            OpType::FloatingPointTargetRegisterCoprocessorRegisterInstruction => {
                bprint!(
                    out,
                    " ${}, $f{}",
                    register_number(instruction.target_register()),
                    instruction.fs()
                )
            }
            OpType::FloatingPointCompareSourceTargetInstruction => {
                if instruction.floating_point_destination() == 0 {
                    bprint!(
                        out,
                        " $f{}, $f{}",
                        instruction.fs(),
                        instruction.floating_point_target()
                    );
                } else {
                    bprint!(
                        out,
                        " {}, $f{}, $f{}",
                        instruction.floating_point_destination() >> 2,
                        instruction.fs(),
                        instruction.floating_point_target()
                    );
                }
            }
            OpType::FloatingPointDestinationSourceTargetInstruction => {
                bprint!(
                    out,
                    " $f{}, $f{}, $f{}",
                    instruction.floating_point_destination(),
                    instruction.fs(),
                    instruction.floating_point_target()
                )
            }
            OpType::MoveConditionalTypeInstruction => bprint!(
                out,
                " ${}, ${}, {}",
                register_number(instruction.destination_register()),
                register_number(instruction.source_register()),
                instruction.target_register() >> 2
            ),
            OpType::FloatingPointMoveConditionalTypeInstruction => match instruction.opcode {
                TOKEN_MOVN_S_OPCODE | TOKEN_MOVN_D_OPCODE | TOKEN_MOVZ_S_OPCODE
                | TOKEN_MOVZ_D_OPCODE => {
                    // movn/movz test a general register.
                    bprint!(
                        out,
                        " $f{}, $f{}, ${}",
                        instruction.floating_point_destination(),
                        instruction.fs(),
                        register_number(instruction.target_register())
                    )
                }
                _ => bprint!(
                    out,
                    " $f{}, $f{}, {}",
                    instruction.floating_point_destination(),
                    instruction.fs(),
                    instruction.condition_code()
                ),
            },
            OpType::JTypeInstruction => {
                bprint!(out, " 0x{:08x}", instruction.target().wrapping_shl(2))
            }
            OpType::NoArgumentTypeInstruction => {}
            OpType::AssemblerDirective
            | OpType::PseudoOp
            | OpType::FloatingPointDestinationAddendSourceTargetInstruction => {
                crate::machine::fatal_error("Unknown instruction type in print_inst\n")
            }
        }
        if let Some(expr) = instruction
            .expr
            .filter(|e: &ImmediateExpression| e.symbol.is_some())
        {
            out.extend_from_slice(b" [");
            let base: Option<usize> =
                opcode_is_load_store(instruction.opcode).then_some(instruction.source_register());
            self.format_immediate_expression(out, &expr, base);
            out.push(b']');
        }
        if let Some(source) = &instruction.source {
            // The source line starts at byte column 57 of this line.
            let used: usize = out.len() - line_start;
            for _ in used..57 {
                out.push(b' ');
            }
            out.extend_from_slice(b"; ");
            out.extend_from_slice(source);
        }
        out.push(b'\n');
    }

    /// Append `label+offset` for a listing (`format_immediate_expression`).
    fn format_immediate_expression(
        &self,
        out: &mut Vec<u8>,
        expr: &ImmediateExpression,
        base_register: Option<usize>,
    ) {
        if let Some(id) = expr.symbol {
            out.extend_from_slice(self.symbols.label(id).name.as_bytes());
        }
        let offset: i32 = expr.offset;
        if expr.program_counter_relative {
            bprint!(out, "-0x{:08x}", offset.wrapping_neg() as u32);
        } else if offset < -10 {
            bprint!(
                out,
                "-{} (-0x{:08x})",
                offset.wrapping_neg(),
                offset.wrapping_neg() as u32
            );
        } else if offset > 10 {
            bprint!(out, "+{} (0x{:08x})", offset, offset as u32);
        }
        if let Some(base) = base_register {
            if expr.symbol.is_some() && !(-10..=10).contains(&offset) {
                bprint!(out, "(${})", REGISTER_NAMES[base & 0x1f]);
            }
        }
    }

    // ----- Breakpoints -----

    /// True if a breakpoint is set at `addr` (`instruction_is_breakpoint`).
    pub fn instruction_is_breakpoint(&mut self, address: u32) -> bool {
        let slot: Option<InstRef> = self.memory_read_instruction(address);
        slot.is_some_and(|instruction: InstRef| Rc::ptr_eq(&instruction, &self.break_instruction))
    }

    /// Put the breakpoint instruction at `addr` and return the instruction
    /// it replaced, or `None` if it cannot (`set_breakpoint`).
    pub fn set_breakpoint(&mut self, address: u32) -> Option<InstRef> {
        self.exception_occurred = false;
        let old: Option<InstRef> = self.memory_read_instruction(address);
        if old
            .as_ref()
            .is_some_and(|instruction: &InstRef| Rc::ptr_eq(instruction, &self.break_instruction))
        {
            return None;
        }
        let breakpoint: InstRef = self.break_instruction.clone();
        self.memory_write_instruction(address, Some(breakpoint));
        if self.exception_occurred {
            None
        } else {
            old
        }
    }
}
