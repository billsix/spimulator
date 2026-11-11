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

use std::cell::RefCell;
use std::rc::Rc;

use crate::arith::sign_extend_16;
use crate::machine::{error, Machine};
use crate::opcode_types::OpType;
use crate::opcodes::*;
use crate::registers::{REG_GP, REG_NAMES};
use crate::symbol_table::LabelId;

/// Append `format!` output to a byte buffer.
macro_rules! bprint {
    ($buf:expr, $($arg:tt)*) => {
        $buf.extend_from_slice(format!($($arg)*).as_bytes())
    };
}
pub(crate) use bprint;

/// An instruction shared between the text segment and the symbol table.
pub type InstRef = Rc<RefCell<Instruction>>;

/// The expression an immediate field comes from: `symbol + offset`
/// (`imm_expr`).  Either part may be missing.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ImmExpr {
    /// The constant part.
    pub offset: i32,
    /// The label, if any.
    pub symbol: Option<LabelId>,
    /// Which bits of the value the instruction wants: positive for the
    /// upper 16 (`lui`), negative for the lower 16, 0 for all 32.
    pub bits: i16,
    /// A branch target: the value is relative to the branch's address.
    pub pc_relative: bool,
}

impl ImmExpr {
    /// A constant (`const_imm_expr`).
    pub fn constant(value: i32) -> ImmExpr {
        ImmExpr {
            offset: value,
            ..ImmExpr::default()
        }
    }

    /// A copy that wants only the upper 16 bits (`upper_bits_of_expr`).
    pub fn upper_bits(self) -> ImmExpr {
        ImmExpr { bits: 1, ..self }
    }

    /// A copy that wants only the lower 16 bits (`lower_bits_of_expr`).
    pub fn lower_bits(self) -> ImmExpr {
        ImmExpr { bits: -1, ..self }
    }

    /// A copy with `value` added to the constant (`incr_expr_offset`).
    pub fn plus(self, value: i32) -> ImmExpr {
        ImmExpr {
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
    /// The opcode token (`TOK_ADD_OPCODE`, ...), or 0 for a word that
    /// decodes to no instruction.
    pub opcode: i32,
    /// The four bytes `rs`, `rt`, `rd`, `shamt` (or the immediate in the
    /// last two, or the target in all four), little-endian.
    fields: [u8; 4],
    /// The binary encoding, or 0 if it has not been computed yet.
    pub encoding: i32,
    /// The expression the immediate or target came from, if any.
    pub expr: Option<ImmExpr>,
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
    pub fn rs(&self) -> usize {
        usize::from(self.fields[0])
    }
    /// The `rt` field (also `ft`).
    pub fn rt(&self) -> usize {
        usize::from(self.fields[1])
    }
    /// The `rd` field (also `fs`).
    pub fn rd(&self) -> usize {
        usize::from(self.fields[2])
    }
    /// The `shamt` field (also `fd`).
    pub fn shamt(&self) -> usize {
        usize::from(self.fields[3])
    }
    /// The 16-bit immediate, signed.
    pub fn imm(&self) -> i16 {
        i16::from_le_bytes([self.fields[2], self.fields[3]])
    }
    /// The 26-bit jump target (all four bytes).
    pub fn target(&self) -> u32 {
        u32::from_le_bytes(self.fields)
    }
    /// `fs`, the first floating-point source.
    pub fn fs(&self) -> usize {
        self.rd()
    }
    /// `ft`, the second floating-point source.
    pub fn ft(&self) -> usize {
        self.rt()
    }
    /// `fd`, the floating-point destination.
    pub fn fd(&self) -> usize {
        self.shamt()
    }
    /// The condition-code number of a `bc1t` (`rt >> 2`).
    pub fn cc(&self) -> i32 {
        (self.rt() >> 2) as i32
    }

    /// Set `rs`; like C's `(uint8_t)` store, only the low 8 bits are kept.
    pub fn set_rs(&mut self, value: i32) {
        self.fields[0] = value as u8;
    }
    /// Set `rt`.
    pub fn set_rt(&mut self, value: i32) {
        self.fields[1] = value as u8;
    }
    /// Set `rd`.
    pub fn set_rd(&mut self, value: i32) {
        self.fields[2] = value as u8;
    }
    /// Set `shamt`.
    pub fn set_shamt(&mut self, value: i32) {
        self.fields[3] = value as u8;
    }
    /// Set the immediate (its low 16 bits).
    pub fn set_imm(&mut self, value: i32) {
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
        (sign_extend_16(i32::from(self.imm())) as u32).wrapping_shl(2) as i32
    }

    /// The binary encoding of the instruction (`inst_encode`).
    pub fn encode(&self) -> i32 {
        let Some(row) = token_row(self.opcode) else {
            return 0;
        };
        let a_opcode: i32 = row.encoding;
        let reg = |value: usize, shift: u32| -> i32 { ((value & 0x1f) << shift) as i32 };
        let imm16: i32 = i32::from(self.imm()) & 0xffff;
        match row.op_type {
            OpType::BranchCoprocCondInst => a_opcode | reg((self.cc() as usize) << 2, 16) | imm16,
            OpType::BranchRsLabelInst | OpType::RsImmInst => a_opcode | reg(self.rs(), 21) | imm16,
            OpType::RtImmInst
            | OpType::RtRsImmInst
            | OpType::BranchRsRtLabelInst
            | OpType::RtAddrInst
            | OpType::FpFtAddrInst => a_opcode | reg(self.rs(), 21) | reg(self.rt(), 16) | imm16,
            OpType::RsInst => a_opcode | reg(self.rs(), 21),
            OpType::RdInst => a_opcode | reg(self.rd(), 11),
            OpType::RtCopregInst | OpType::FpRtCopregInst => {
                a_opcode | reg(self.rt(), 16) | reg(self.rd(), 11)
            }
            OpType::RsRtInst => a_opcode | reg(self.rs(), 21) | reg(self.rt(), 16),
            OpType::RdRsInst => a_opcode | reg(self.rs(), 21) | reg(self.rd(), 11),
            OpType::RdRtShamtInst => {
                a_opcode | reg(self.rt(), 16) | reg(self.rd(), 11) | reg(self.shamt(), 6)
            }
            OpType::RdRsRtInst | OpType::RdRtRsInst | OpType::MovcTypeInst => {
                a_opcode | reg(self.rs(), 21) | reg(self.rt(), 16) | reg(self.rd(), 11)
            }
            OpType::FpFdFsInst => a_opcode | reg(self.fs(), 11) | reg(self.fd(), 6),
            OpType::FpCmpFsFtInst => {
                a_opcode
                    | reg(self.ft(), 16)
                    | reg(self.fs(), 11)
                    | reg(self.fd(), 6)
                    | self.rs() as i32
            }
            OpType::FpFdFsFtInst | OpType::FpMovcTypeInst => {
                a_opcode | reg(self.ft(), 16) | reg(self.fs(), 11) | reg(self.fd(), 6)
            }
            OpType::JTypeInst => (a_opcode as u32 | self.target()) as i32,
            OpType::NoargTypeInst => a_opcode,
            OpType::AsmDir | OpType::PseudoOp | OpType::FpFdFrFsFtInst => {
                crate::machine::fatal_error("Unknown instruction type in inst_encoding\n")
            }
        }
    }

    /// Decode a 32-bit word into an instruction (`inst_decode`).  A word
    /// that is no instruction decodes to opcode 0.
    pub fn decode(value: i32) -> Instruction {
        let word: u32 = value as u32;
        let mut a_opcode: u32 = word & 0xfc00_0000;
        // Some opcode classes continue the opcode in other fields.
        if a_opcode == 0 || a_opcode == 0x7000_0000 {
            a_opcode |= word & 0x3f; // SPECIAL, SPECIAL2: the funct field
        } else if a_opcode == 0x0400_0000 {
            a_opcode |= word & 0x001f_0000; // REGIMM: the rt field
        } else if a_opcode == 0x4000_0000 {
            a_opcode |= (word & 0x03e0_0000) | (word & 0x1f); // COP0
        } else if a_opcode == 0x4400_0000 {
            a_opcode |= word & 0x03e0_0000; // COP1
            if word & 0xff00_0000 == 0x4500_0000 {
                a_opcode |= word & 0x0001_0000; // bc1f/bc1t
            } else {
                a_opcode |= word & 0x3f;
            }
        } else if a_opcode == 0x4800_0000 || a_opcode == 0x4c00_0000 {
            a_opcode |= word & 0x03e0_0000; // COP2, COP1X
        }

        let field = |shift: u32| -> i32 { ((word >> shift) & 0x1f) as i32 };
        let (rs, rt, rd, shamt): (i32, i32, i32, i32) = (field(21), field(16), field(11), field(6));
        let low16: i32 = (word & 0xffff) as i32;
        let invalid: Instruction = Self::make_r(value, 0, 0, 0, 0, 0);
        let Some(token) = decode_token(a_opcode as i32) else {
            return invalid;
        };
        let Some(op_type) = token_type(token) else {
            return invalid;
        };
        match op_type {
            OpType::BranchCoprocCondInst
            | OpType::RtImmInst
            | OpType::RtRsImmInst
            | OpType::BranchRsRtLabelInst
            | OpType::RtAddrInst
            | OpType::FpFtAddrInst => Self::make_i(value, token, rs, rt, low16),
            OpType::BranchRsLabelInst | OpType::RsImmInst => {
                Self::make_i(value, token, rs, 0, low16)
            }
            OpType::RsInst => Self::make_r(value, token, rs, 0, 0, 0),
            OpType::RdInst => Self::make_r(value, token, 0, 0, rd, 0),
            OpType::RtCopregInst | OpType::FpRtCopregInst => {
                Self::make_r(value, token, 0, rt, rd, 0)
            }
            OpType::RsRtInst => Self::make_r(value, token, rs, rt, 0, 0),
            OpType::RdRsInst => Self::make_r(value, token, rs, 0, rd, 0),
            OpType::RdRtShamtInst => Self::make_r(value, token, 0, rt, rd, shamt),
            OpType::RdRsRtInst | OpType::RdRtRsInst | OpType::MovcTypeInst => {
                Self::make_r(value, token, rs, rt, rd, 0)
            }
            OpType::FpFdFsInst => Self::make_co_r(value, token, rd, 0, shamt),
            OpType::FpCmpFsFtInst => {
                // fs lives in the rd slot; the condition goes in the rs slot.
                let mut instruction: Instruction = Self::make_co_r(value, token, rd, rt, shamt);
                instruction.set_rs(value & 0xf);
                instruction
            }
            OpType::FpFdFsFtInst | OpType::FpMovcTypeInst => {
                Self::make_co_r(value, token, rd, rt, shamt)
            }
            OpType::JTypeInst => {
                let mut instruction: Instruction = Instruction::new(token);
                instruction.set_target(word & 0x03ff_ffff);
                instruction.encoding = value;
                instruction
            }
            OpType::NoargTypeInst => Self::make_r(value, token, 0, 0, 0, 0),
            OpType::AsmDir | OpType::PseudoOp | OpType::FpFdFrFsFtInst => invalid,
        }
    }

    /// A decoded R-type instruction (`mk_r_inst`).
    fn make_r(value: i32, opcode: i32, rs: i32, rt: i32, rd: i32, shamt: i32) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_rs(rs);
        instruction.set_rt(rt);
        instruction.set_rd(rd);
        instruction.set_shamt(shamt);
        instruction.encoding = value;
        instruction
    }

    /// A decoded coprocessor R-type instruction (`mk_co_r_inst`).
    fn make_co_r(value: i32, opcode: i32, fs: i32, ft: i32, fd: i32) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_rd(fs);
        instruction.set_rt(ft);
        instruction.set_shamt(fd);
        instruction.encoding = value;
        instruction
    }

    /// A decoded I-type instruction (`mk_i_inst`).
    fn make_i(value: i32, opcode: i32, rs: i32, rt: i32, offset: i32) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_rs(rs);
        instruction.set_rt(rt);
        instruction.set_imm(offset);
        instruction.encoding = value;
        instruction
    }

    /// An R-type instruction built by the assembler (`make_r_type_inst`).
    pub fn r_type(opcode: i32, rd: i32, rs: i32, rt: i32) -> Instruction {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_rs(rs);
        instruction.set_rt(rt);
        instruction.set_rd(rd);
        instruction
    }
}

/// True for the conditional branches, including the branch
/// pseudo-instructions (`opcode_is_branch`).
pub fn opcode_is_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOK_BC1F_OPCODE
            | TOK_BC1FL_OPCODE
            | TOK_BC1T_OPCODE
            | TOK_BC1TL_OPCODE
            | TOK_BC2F_OPCODE
            | TOK_BC2FL_OPCODE
            | TOK_BC2T_OPCODE
            | TOK_BC2TL_OPCODE
            | TOK_BEQ_OPCODE
            | TOK_BEQL_OPCODE
            | TOK_BEQZ_PSEUDO_OP
            | TOK_BGE_PSEUDO_OP
            | TOK_BGEU_PSEUDO_OP
            | TOK_BGEZ_OPCODE
            | TOK_BGEZAL_OPCODE
            | TOK_BGEZALL_OPCODE
            | TOK_BGEZL_OPCODE
            | TOK_BGT_PSEUDO_OP
            | TOK_BGTU_PSEUDO_OP
            | TOK_BGTZ_OPCODE
            | TOK_BGTZL_OPCODE
            | TOK_BLE_PSEUDO_OP
            | TOK_BLEU_PSEUDO_OP
            | TOK_BLEZ_OPCODE
            | TOK_BLEZL_OPCODE
            | TOK_BLT_PSEUDO_OP
            | TOK_BLTU_PSEUDO_OP
            | TOK_BLTZ_OPCODE
            | TOK_BLTZAL_OPCODE
            | TOK_BLTZALL_OPCODE
            | TOK_BLTZL_OPCODE
            | TOK_BNE_OPCODE
            | TOK_BNEL_OPCODE
            | TOK_BNEZ_PSEUDO_OP
    )
}

/// True for the branch-likely forms, which skip their delay slot when not
/// taken (`opcode_is_nullified_branch`).
pub fn opcode_is_nullified_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOK_BC1FL_OPCODE
            | TOK_BC1TL_OPCODE
            | TOK_BC2FL_OPCODE
            | TOK_BC2TL_OPCODE
            | TOK_BEQL_OPCODE
            | TOK_BGEZALL_OPCODE
            | TOK_BGEZL_OPCODE
            | TOK_BGTZL_OPCODE
            | TOK_BLEZL_OPCODE
            | TOK_BLTZALL_OPCODE
            | TOK_BLTZL_OPCODE
            | TOK_BNEL_OPCODE
    )
}

/// True for the coprocessor branches taken on a true condition.
pub fn opcode_is_true_branch(opcode: i32) -> bool {
    matches!(
        opcode,
        TOK_BC1T_OPCODE | TOK_BC1TL_OPCODE | TOK_BC2T_OPCODE | TOK_BC2TL_OPCODE
    )
}

/// True for `j` and `jal`.
pub fn opcode_is_jump(opcode: i32) -> bool {
    matches!(opcode, TOK_J_OPCODE | TOK_JAL_OPCODE)
}

/// True for the loads and stores.
pub fn opcode_is_load_store(opcode: i32) -> bool {
    matches!(
        opcode,
        TOK_LB_OPCODE
            | TOK_LBU_OPCODE
            | TOK_LH_OPCODE
            | TOK_LHU_OPCODE
            | TOK_LL_OPCODE
            | TOK_LDC1_OPCODE
            | TOK_LDC2_OPCODE
            | TOK_LW_OPCODE
            | TOK_LWC1_OPCODE
            | TOK_LWC2_OPCODE
            | TOK_LWL_OPCODE
            | TOK_LWR_OPCODE
            | TOK_SB_OPCODE
            | TOK_SC_OPCODE
            | TOK_SH_OPCODE
            | TOK_SDC1_OPCODE
            | TOK_SDC2_OPCODE
            | TOK_SW_OPCODE
            | TOK_SWC1_OPCODE
            | TOK_SWC2_OPCODE
            | TOK_SWL_OPCODE
            | TOK_SWR_OPCODE
    )
}

/// The immediates that the hardware sign-extends; a value for them fits
/// in the field if its top 17 bits are all 0 or all 1.
fn is_sign_extended_immediate(opcode: i32, expr: &ImmExpr) -> bool {
    matches!(
        opcode,
        TOK_ADDI_OPCODE
            | TOK_ADDIU_OPCODE
            | TOK_SLTI_OPCODE
            | TOK_SLTIU_OPCODE
            | TOK_TEQI_OPCODE
            | TOK_TGEI_OPCODE
            | TOK_TGEIU_OPCODE
            | TOK_TLTI_OPCODE
            | TOK_TLTIU_OPCODE
            | TOK_TNEI_OPCODE
    ) || (opcode_is_load_store(opcode) && expr.bits == 0)
}

impl Machine {
    /// The value of an immediate expression (`eval_imm_expr`): the label's
    /// address plus the constant, then the upper or lower 16 bits if the
    /// expression asks for them.
    pub fn eval_imm_expr(&self, expr: &ImmExpr) -> i32 {
        let value: i32 = match expr.symbol {
            None => expr.offset,
            Some(id) if self.symbols.label(id).is_defined() => {
                expr.offset.wrapping_add(self.symbols.label(id).addr as i32)
            }
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
    pub fn expr_is_known(&self, expr: &ImmExpr) -> bool {
        expr.symbol
            .is_none_or(|id: LabelId| self.symbols.label(id).is_defined())
    }

    // ----- Where the next instruction goes -----

    /// Start the user text segment at `addr` (`text_begins_at_point`).
    pub fn text_begins_at_point(&mut self, addr: u32) {
        self.asm.next_text_pc = addr;
        self.asm.text_in_kernel = false;
    }

    /// Start the kernel text segment at `addr`.
    pub fn k_text_begins_at_point(&mut self, addr: u32) {
        self.asm.next_k_text_pc = addr;
        self.asm.text_in_kernel = false;
    }

    /// Advance the text PC to a multiple of `2^alignment` (`.align` in
    /// the text segment), moving this line's labels along.
    pub fn align_text(&mut self, alignment: i32) {
        if alignment == 0 {
            return;
        }
        let round = |pc: u32| -> u32 {
            pc.wrapping_add(1u32.wrapping_shl(alignment as u32))
                .wrapping_sub(1)
                & 0xffff_ffffu32.wrapping_shl(alignment as u32)
        };
        if self.asm.text_in_kernel {
            self.asm.next_k_text_pc = round(self.asm.next_k_text_pc);
            self.fix_current_label_address(self.asm.next_k_text_pc);
        } else {
            self.asm.next_text_pc = round(self.asm.next_text_pc);
            self.fix_current_label_address(self.asm.next_text_pc);
        }
    }

    /// Set the text PC (`.text ADDR`).
    pub fn set_text_pc(&mut self, addr: u32) {
        if self.asm.text_in_kernel {
            self.asm.next_k_text_pc = addr;
        } else {
            self.asm.next_text_pc = addr;
        }
    }

    /// The address the next instruction goes to (`current_text_pc`).
    pub fn current_text_pc(&self) -> u32 {
        if self.asm.text_in_kernel {
            self.asm.next_k_text_pc
        } else {
            self.asm.next_text_pc
        }
    }

    /// Step the text PC past an instruction.
    fn increment_text_pc(&mut self, delta: u32) {
        if self.asm.text_in_kernel {
            self.asm.next_k_text_pc = self.asm.next_k_text_pc.wrapping_add(delta);
            if self.mem.k_text_top <= self.asm.next_k_text_pc {
                crate::machine::run_error("Can't expand kernel text segment\n");
            }
        } else {
            self.asm.next_text_pc = self.asm.next_text_pc.wrapping_add(delta);
            if self.mem.text_top <= self.asm.next_text_pc {
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
        } else if self.asm.text_dir {
            let at: u32 = self.current_text_pc();
            self.exception_occurred = false;
            self.mem_write_inst(at, Some(instruction.clone()));
            if self.exception_occurred {
                error(&format!("Invalid address (0x{at:08x}) for instruction\n"));
            } else {
                self.increment_text_pc(4);
            }
            let source: Option<Rc<[u8]>> = if self.asm.emit_source_active {
                self.asm.emit_source_text.clone()
            } else {
                self.scanner.source_line().map(Rc::from)
            };
            let encoding: i32 = {
                let mut inst = instruction.borrow_mut();
                inst.source = source;
                if inst.encoding == 0 {
                    inst.encoding = inst.encode();
                }
                inst.encoding
            };
            self.listing_text_inst(at, encoding as u32);
        }
    }

    /// Build and store an I-type instruction (`i_type_inst`).  The
    /// immediate may not fit in 16 bits; then this produces a sequence of
    /// instructions that builds the value in `$at` first.
    pub fn i_type_inst(&mut self, opcode: i32, rt: i32, rs: i32, expr: &ImmExpr) {
        let mut instruction: Instruction = Instruction::new(opcode);
        instruction.set_rs(rs);
        instruction.set_rt(rt);
        instruction.expr = Some(*expr);
        let instruction: InstRef = instruction.into_ref();
        if self.expr_is_known(expr) {
            let value: i32 = self.eval_imm_expr(expr);
            let bits: u32 = value as u32;
            let too_big: bool = if is_sign_extended_immediate(opcode, expr) {
                bits & 0xffff_8000 != 0 && bits & 0xffff_8000 != 0xffff_8000
            } else {
                bits & 0xffff_0000 != 0
            };
            if !self.options.bare_machine && too_big {
                self.i_type_inst_full_word(opcode, rt, rs, expr, true, value);
                return;
            }
            self.resolve_a_label(&instruction);
        } else if self.options.bare_machine || expr.bits != 0 {
            // The value is unknown, but only 16 bits of it are needed.
            let id: LabelId = expr.symbol.expect("an unknown expression has a label");
            self.record_inst_uses_symbol(&instruction, id);
        } else {
            // All 32 bits of an unknown value: assume it will not fit.
            self.i_type_inst_full_word(opcode, rt, rs, expr, false, 0);
            return;
        }
        self.store_instruction(instruction);
    }

    /// Build an I-type instruction whose immediate does not fit in 16
    /// bits, using `$at` (`i_type_inst_full_word`).
    fn i_type_inst_full_word(
        &mut self,
        opcode: i32,
        rt: i32,
        rs: i32,
        expr: &ImmExpr,
        value_known: bool,
        value: i32,
    ) {
        let gp_offset: Option<i32> = self.gp_relative_offset(expr, rs);
        if opcode_is_load_store(opcode) {
            if let Some(offset) = gp_offset {
                self.i_type_inst(opcode, rt, REG_GP as i32, &ImmExpr::constant(offset));
            } else if value_known {
                let mut high: i32 = (value >> 16) & 0xffff;
                let low: i32 = value & 0xffff;
                if !(high == 0 && low & 0x8000 == 0) && !(high == 0xffff && low & 0x8000 != 0) {
                    // Some of the high 16 bits are set.
                    if low & 0x8000 != 0 {
                        // The load sign-extends the low half; compensate.
                        high += 1;
                    }
                    self.i_type_inst(TOK_LUI_OPCODE, 1, 0, &ImmExpr::constant(high));
                    if rs != 0 {
                        self.r_type_inst(TOK_ADDU_OPCODE, 1, 1, rs);
                    }
                    self.i_type_inst(opcode, rt, 1, &ImmExpr::constant(low).lower_bits());
                } else {
                    // Sign-extending the low half gives the high half.
                    self.i_type_inst(opcode, rt, rs, &ImmExpr::constant(low));
                }
            } else {
                self.i_type_inst(TOK_LUI_OPCODE, 1, 0, &expr.upper_bits());
                if rs != 0 {
                    self.r_type_inst(TOK_ADDU_OPCODE, 1, 1, rs);
                }
                self.i_type_inst(opcode, rt, 1, &expr.lower_bits());
            }
        } else if opcode_is_branch(opcode) {
            // This only allows branches +/- 32K, which is not correct!
            self.i_type_inst(opcode, rt, rs, &expr.lower_bits());
        } else if let Some(offset) = gp_offset {
            let op: i32 = if opcode == TOK_LUI_OPCODE {
                TOK_ADDIU_OPCODE
            } else {
                opcode
            };
            self.i_type_inst(op, rt, REG_GP as i32, &ImmExpr::constant(offset));
        } else if matches!(
            opcode,
            TOK_ORI_OPCODE | TOK_ADDI_OPCODE | TOK_ADDIU_OPCODE | TOK_LUI_OPCODE
        ) && rs == 0
        {
            self.produce_immediate(expr, rt, value_known, value);
        } else {
            self.produce_immediate(expr, 1, value_known, value);
            self.r_type_inst(crate::pseudo_op::imm_op_to_op(opcode), rt, rs, 1);
        }
    }

    /// The offset from `$gp` of an expression on a small-data label, if it
    /// fits in 16 bits and no base register is used.
    fn gp_relative_offset(&self, expr: &ImmExpr, rs: i32) -> Option<i32> {
        let id: LabelId = expr.symbol?;
        let label = self.symbols.label(id);
        if !label.gp || rs != 0 {
            return None;
        }
        let offset: i32 = (label.addr + i64::from(expr.offset)) as i32;
        (-0x8000..=0x7fff).contains(&offset).then_some(offset)
    }

    /// Load a 32-bit value into register `rt` with `lui` and/or `ori`
    /// (`produce_immediate`).
    fn produce_immediate(&mut self, expr: &ImmExpr, rt: i32, value_known: bool, value: i32) {
        if value_known && value & 0xffff == 0 {
            self.i_type_inst(TOK_LUI_OPCODE, rt, 0, &expr.upper_bits());
        } else if value_known && value as u32 & 0xffff_0000 == 0 {
            self.i_type_inst(TOK_ORI_OPCODE, rt, 0, &expr.lower_bits());
        } else {
            self.i_type_inst(TOK_LUI_OPCODE, 1, 0, &expr.upper_bits());
            self.i_type_inst(TOK_ORI_OPCODE, rt, 1, &expr.lower_bits());
        }
    }

    /// Build and store a jump (`j_type_inst`).
    pub fn j_type_inst(&mut self, opcode: i32, target: &ImmExpr) {
        let target: ImmExpr = ImmExpr {
            offset: 0,
            pc_relative: false,
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

    /// Build and store an R-type instruction (`r_type_inst`).
    pub fn r_type_inst(&mut self, opcode: i32, rd: i32, rs: i32, rt: i32) {
        self.store_instruction(Instruction::r_type(opcode, rd, rs, rt).into_ref());
    }

    /// Build and store a coprocessor R-type instruction (`r_co_type_inst`).
    pub fn r_co_type_inst(&mut self, opcode: i32, fd: i32, fs: i32, ft: i32) {
        let mut instruction: Instruction = Instruction::r_type(opcode, fs, 0, ft);
        instruction.set_shamt(fd);
        self.store_instruction(instruction.into_ref());
    }

    /// Build and store a shift by a constant (`r_sh_type_inst`).
    pub fn r_sh_type_inst(&mut self, opcode: i32, rd: i32, rt: i32, shamt: i32) {
        let mut instruction: Instruction = Instruction::r_type(opcode, rd, 0, rt);
        instruction.set_shamt(shamt & 0x1f);
        self.store_instruction(instruction.into_ref());
    }

    /// Build and store a floating-point compare (`r_cond_type_inst`).  The
    /// compare's condition bits say which outcomes (unordered, equal,
    /// less) make it true, and whether NaN operands trap.
    pub fn r_cond_type_inst(&mut self, opcode: i32, fs: i32, ft: i32, cc: i32) {
        const UN: i32 = 0x1;
        const EQ: i32 = 0x2;
        const LT: i32 = 0x4;
        const IN: i32 = 0x8;
        let mut instruction: Instruction = Instruction::r_type(opcode, fs, 0, ft);
        instruction.set_shamt(cc << 2);
        let cond: Option<i32> = match opcode {
            TOK_C_EQ_D_OPCODE | TOK_C_EQ_S_OPCODE => Some(EQ),
            TOK_C_LE_D_OPCODE | TOK_C_LE_S_OPCODE => Some(IN | LT | EQ),
            TOK_C_LT_D_OPCODE | TOK_C_LT_S_OPCODE => Some(IN | LT),
            TOK_C_NGE_D_OPCODE | TOK_C_NGE_S_OPCODE => Some(IN | LT | UN),
            TOK_C_NGLE_D_OPCODE | TOK_C_NGLE_S_OPCODE => Some(IN | UN),
            TOK_C_NGL_D_OPCODE | TOK_C_NGL_S_OPCODE => Some(IN | EQ | UN),
            TOK_C_NGT_D_OPCODE | TOK_C_NGT_S_OPCODE => Some(IN | LT | EQ | UN),
            TOK_C_OLT_D_OPCODE | TOK_C_OLT_S_OPCODE => Some(LT),
            TOK_C_OLE_D_OPCODE | TOK_C_OLE_S_OPCODE => Some(LT | EQ),
            TOK_C_SEQ_D_OPCODE | TOK_C_SEQ_S_OPCODE => Some(IN | EQ),
            TOK_C_SF_D_OPCODE | TOK_C_SF_S_OPCODE => Some(IN),
            TOK_C_F_D_OPCODE | TOK_C_F_S_OPCODE => Some(0),
            TOK_C_UEQ_D_OPCODE | TOK_C_UEQ_S_OPCODE => Some(EQ | UN),
            TOK_C_ULT_D_OPCODE | TOK_C_ULT_S_OPCODE => Some(LT | UN),
            TOK_C_ULE_D_OPCODE | TOK_C_ULE_S_OPCODE => Some(LT | EQ | UN),
            TOK_C_UN_D_OPCODE | TOK_C_UN_S_OPCODE => Some(UN),
            _ => None,
        };
        if let Some(bits) = cond {
            instruction.set_rs(bits);
        }
        self.store_instruction(instruction.into_ref());
    }

    // ----- Filling in labels -----

    /// Fill in the label of an instruction about to be stored at the
    /// current text (or data) address (`resolve_a_label`).
    fn resolve_a_label(&mut self, instruction: &InstRef) {
        let pc: u32 = if self.asm.data_dir {
            self.current_data_pc()
        } else {
            self.current_text_pc()
        };
        self.resolve_a_label_sub(instruction, pc);
    }

    /// Fill in the immediate or target field of the instruction at `pc`
    /// now that its label is known (`resolve_a_label_sub`).
    pub fn resolve_a_label_sub(&mut self, instruction: &InstRef, pc: u32) {
        let (opcode, mut expr): (i32, ImmExpr) = {
            let inst = instruction.borrow();
            (inst.opcode, inst.expr.unwrap_or_default())
        };
        if expr.pc_relative {
            // The instruction may have moved since it was parsed.
            expr.offset = 0i32.wrapping_sub(pc as i32);
            instruction.borrow_mut().expr = Some(expr);
        }
        if !self.expr_is_known(&expr) {
            let name: String = expr.symbol.map_or(String::new(), |id: LabelId| {
                self.symbols.label(id).name.clone()
            });
            error(&format!("Resolving undefined symbol: {name}\n"));
            return;
        }
        let (value, field_mask): (i32, i32) = if opcode_is_branch(opcode) {
            // The whole byte offset: asking for the low 16 bits would
            // truncate a branch more than 32 KB away.
            let whole: ImmExpr = ImmExpr { bits: 0, ..expr };
            let byte_offset: i32 = self.eval_imm_expr(&whole);
            // Drop the low two bits: instructions are on word boundaries.
            let mut word_offset: i32 = byte_offset / 4;
            if self.options.delayed_branches {
                word_offset -= 1;
            }
            if !(-32768..=32767).contains(&word_offset) {
                error(&format!(
                    "Branch target is out of range (more than 128 KB away) at pc 0x{pc:x}\n"
                ));
            }
            (word_offset & 0xffff, 0xffff)
        } else if opcode_is_jump(opcode) {
            let value: i32 = self.eval_imm_expr(&expr);
            if value as u32 & 0xf000_0000 != pc & 0xf000_0000 {
                error(&format!(
                    "Target of jump differs in high-order 4 bits from instruction pc 0x{pc:x}\n"
                ));
            }
            // The top four bits come from the PC, the low two are 0.
            ((value & 0x0fff_ffff) >> 2, -1)
        } else if opcode_is_load_store(opcode) {
            let value: i32 = self.eval_imm_expr(&expr);
            if value & 0x8000 != 0 {
                self.compensate_lui_for_sign_extension(&expr, pc);
            }
            (value, 0xffff)
        } else {
            (self.eval_imm_expr(&expr), 0xffff)
        };
        if value & !field_mask != 0 && value & !field_mask != 0xffff_0000u32 as i32 {
            error("Immediate value is too large for field: ");
            self.print_inst(pc);
        }
        let mut inst = instruction.borrow_mut();
        if opcode_is_jump(opcode) {
            inst.set_target(value as u32);
        } else {
            inst.set_imm(value);
        }
        inst.encoding = inst.encode();
    }

    /// A load or store sign-extends its 16-bit offset, so when the low half
    /// of a label's address has bit 15 set, the `lui` that loads the high
    /// half must add 1.  Find that `lui` just before the instruction (or
    /// two before, past an `addu` of a base register) and adjust it.
    fn compensate_lui_for_sign_extension(&mut self, expr: &ImmExpr, pc: u32) {
        let previous: Option<InstRef> = self.mem_read_inst(pc.wrapping_sub(4));
        let before_previous: Option<InstRef> = self.mem_read_inst(pc.wrapping_sub(8));
        let is_matching_lui = |candidate: &Option<InstRef>| -> bool {
            candidate.as_ref().is_some_and(|inst: &InstRef| {
                let inst = inst.borrow();
                inst.opcode == TOK_LUI_OPCODE
                    && inst.expr.map(|e: ImmExpr| e.symbol) == Some(expr.symbol)
                    && inst.imm() == 0
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
            let mut lui = lui.borrow_mut();
            if let Some(lui_expr) = lui.expr.as_mut() {
                lui_expr.offset = lui_expr.offset.wrapping_add(0x10000);
            }
        }
    }

    // ----- Printing instructions -----

    /// Print the instruction at `addr` on stderr (`print_inst`).
    pub fn print_inst(&mut self, addr: u32) {
        let text: Vec<u8> = self.inst_to_string(addr);
        crate::machine::error_bytes(&text);
    }

    /// The listing line of the instruction at `addr` (`inst_to_string`).
    pub fn inst_to_string(&mut self, addr: u32) -> Vec<u8> {
        self.exception_occurred = false;
        let instruction: Option<InstRef> = self.mem_read_inst(addr);
        if self.exception_occurred {
            error(&format!(
                "Can't print instruction not in text segment (0x{addr:08x})\n"
            ));
            return Vec::new();
        }
        let mut text: Vec<u8> = Vec::new();
        self.format_an_inst(&mut text, instruction.as_ref(), addr);
        text
    }

    /// Append the listing line of `instruction`, stored at `addr`
    /// (`format_an_inst`):
    ///
    /// `[0x00400000]` TAB `0x8fa40000  lw $a0, 0($sp)` ... `; 183: lw $a0 0($sp)`
    ///
    /// The source line starts at byte column 57 (the TAB counts as one)
    /// when the disassembly is shorter.
    pub fn format_an_inst(&mut self, out: &mut Vec<u8>, instruction: Option<&InstRef>, addr: u32) {
        let line_start: usize = out.len();
        if addr != 0 && self.inst_is_breakpoint(addr) {
            self.delete_breakpoint(addr);
            out.push(b'*');
            let original: Option<InstRef> = self.mem_read_inst(addr);
            self.format_an_inst(out, original.as_ref(), addr);
            self.add_breakpoint(addr);
            return;
        }
        bprint!(out, "[0x{addr:08x}]\t");
        let Some(instruction) = instruction else {
            out.extend_from_slice(b"<none>\n");
            return;
        };
        let inst: Instruction = instruction.borrow().clone();
        let Some(row) = token_row(inst.opcode) else {
            bprint!(out, "<unknown instruction {}>\n", inst.opcode);
            return;
        };
        let reg = |n: usize| -> &'static str { REG_NAMES[n & 0x1f] };
        bprint!(out, "0x{:08x}  {}", inst.encoding as u32, row.name);
        match row.op_type {
            OpType::BranchCoprocCondInst => bprint!(out, "{} {}", inst.cc(), inst.branch_offset()),
            OpType::BranchRsLabelInst => {
                bprint!(out, " ${} {}", reg(inst.rs()), inst.branch_offset())
            }
            OpType::RsImmInst => bprint!(out, " ${}, {}", reg(inst.rs()), inst.imm()),
            OpType::RtImmInst => bprint!(out, " ${}, {}", reg(inst.rt()), inst.imm()),
            OpType::RtRsImmInst => {
                bprint!(
                    out,
                    " ${}, ${}, {}",
                    reg(inst.rt()),
                    reg(inst.rs()),
                    inst.imm()
                )
            }
            OpType::BranchRsRtLabelInst => bprint!(
                out,
                " ${}, ${}, {}",
                reg(inst.rs()),
                reg(inst.rt()),
                inst.branch_offset()
            ),
            OpType::RtAddrInst => {
                bprint!(
                    out,
                    " ${}, {}(${})",
                    reg(inst.rt()),
                    inst.imm(),
                    reg(inst.rs())
                )
            }
            OpType::RsInst => bprint!(out, " ${}", reg(inst.rs())),
            OpType::RdInst => bprint!(out, " ${}", reg(inst.rd())),
            OpType::RtCopregInst => bprint!(out, " ${}, ${}", reg(inst.rt()), reg(inst.rd())),
            OpType::RsRtInst => bprint!(out, " ${}, ${}", reg(inst.rs()), reg(inst.rt())),
            OpType::RdRsInst => bprint!(out, " ${}, ${}", reg(inst.rd()), reg(inst.rs())),
            OpType::RdRtShamtInst => {
                if inst.rd() == 0 && inst.rt() == 0 && inst.shamt() == 0 {
                    // `sll $0, $0, 0` is the canonical nop.
                    out.truncate(out.len() - 3);
                    out.extend_from_slice(b"nop");
                } else {
                    bprint!(
                        out,
                        " ${}, ${}, {}",
                        reg(inst.rd()),
                        reg(inst.rt()),
                        inst.shamt()
                    );
                }
            }
            OpType::RdRsRtInst => bprint!(
                out,
                " ${}, ${}, ${}",
                reg(inst.rd()),
                reg(inst.rs()),
                reg(inst.rt())
            ),
            OpType::RdRtRsInst => bprint!(
                out,
                " ${}, ${}, ${}",
                reg(inst.rd()),
                reg(inst.rt()),
                reg(inst.rs())
            ),
            OpType::FpFtAddrInst => {
                bprint!(out, " $f{}, {}(${})", inst.ft(), inst.imm(), reg(inst.rs()))
            }
            OpType::FpFdFsInst => bprint!(out, " $f{}, $f{}", inst.fd(), inst.fs()),
            OpType::FpRtCopregInst => bprint!(out, " ${}, $f{}", reg(inst.rt()), inst.fs()),
            OpType::FpCmpFsFtInst => {
                if inst.fd() == 0 {
                    bprint!(out, " $f{}, $f{}", inst.fs(), inst.ft());
                } else {
                    bprint!(out, " {}, $f{}, $f{}", inst.fd() >> 2, inst.fs(), inst.ft());
                }
            }
            OpType::FpFdFsFtInst => {
                bprint!(out, " $f{}, $f{}, $f{}", inst.fd(), inst.fs(), inst.ft())
            }
            OpType::MovcTypeInst => bprint!(
                out,
                " ${}, ${}, {}",
                reg(inst.rd()),
                reg(inst.rs()),
                inst.rt() >> 2
            ),
            OpType::FpMovcTypeInst => match inst.opcode {
                TOK_MOVN_S_OPCODE | TOK_MOVN_D_OPCODE | TOK_MOVZ_S_OPCODE | TOK_MOVZ_D_OPCODE => {
                    // movn/movz test a general register.
                    bprint!(
                        out,
                        " $f{}, $f{}, ${}",
                        inst.fd(),
                        inst.fs(),
                        reg(inst.rt())
                    )
                }
                _ => bprint!(out, " $f{}, $f{}, {}", inst.fd(), inst.fs(), inst.cc()),
            },
            OpType::JTypeInst => bprint!(out, " 0x{:08x}", inst.target().wrapping_shl(2)),
            OpType::NoargTypeInst => {}
            OpType::AsmDir | OpType::PseudoOp | OpType::FpFdFrFsFtInst => {
                crate::machine::fatal_error("Unknown instruction type in print_inst\n")
            }
        }
        if let Some(expr) = inst.expr.filter(|e: &ImmExpr| e.symbol.is_some()) {
            out.extend_from_slice(b" [");
            let base: Option<usize> = opcode_is_load_store(inst.opcode).then_some(inst.rs());
            self.format_imm_expr(out, &expr, base);
            out.push(b']');
        }
        if let Some(source) = &inst.source {
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

    /// Append `label+offset` for a listing (`format_imm_expr`).
    fn format_imm_expr(&self, out: &mut Vec<u8>, expr: &ImmExpr, base_reg: Option<usize>) {
        if let Some(id) = expr.symbol {
            out.extend_from_slice(self.symbols.label(id).name.as_bytes());
        }
        let offset: i32 = expr.offset;
        if expr.pc_relative {
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
        if let Some(base) = base_reg {
            if expr.symbol.is_some() && !(-10..=10).contains(&offset) {
                bprint!(out, "(${})", REG_NAMES[base & 0x1f]);
            }
        }
    }

    // ----- Breakpoints -----

    /// True if a breakpoint is set at `addr` (`inst_is_breakpoint`).
    pub fn inst_is_breakpoint(&mut self, addr: u32) -> bool {
        let slot: Option<InstRef> = self.mem_read_inst(addr);
        slot.is_some_and(|inst: InstRef| Rc::ptr_eq(&inst, &self.break_inst))
    }

    /// Put the breakpoint instruction at `addr` and return the instruction
    /// it replaced, or `None` if it cannot (`set_breakpoint`).
    pub fn set_breakpoint(&mut self, addr: u32) -> Option<InstRef> {
        self.exception_occurred = false;
        let old: Option<InstRef> = self.mem_read_inst(addr);
        if old
            .as_ref()
            .is_some_and(|inst: &InstRef| Rc::ptr_eq(inst, &self.break_inst))
        {
            return None;
        }
        let breakpoint: InstRef = self.break_inst.clone();
        self.mem_write_inst(addr, Some(breakpoint));
        if self.exception_occurred {
            None
        } else {
            old
        }
    }
}
