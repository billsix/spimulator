//! The executor: fetch an instruction, carry it out, repeat (`run.c`).
//!
//! Each step reads the instruction at the program counter, does what its
//! opcode says to the registers and memory, and moves the program counter
//! on.  A fault (an overflow, a bad address, ...) does not stop the loop:
//! like a real MIPS, the machine records the cause in coprocessor 0 and
//! continues at the exception handler, `exceptions.s`, at `0x80000180`.
//!
//! Integer arithmetic says in its spelling whether it wraps or traps:
//! `addu` is `wrapping_add`, `add` is `checked_add` and raises the
//! overflow exception when the sum does not fit.

use std::time::{Duration, Instant};

use crate::arith::{
    f64_to_i32_like_c, join_hi_lo, sign_extend_16, signed_product, split_hi_lo, unsigned_product,
};
use crate::instruction::{InstRef, Instruction};
use crate::machine::{fatal_error, run_error, LoadDest, Machine};
use crate::opcodes::*;
use crate::registers::*;

/// The exception codes of the Cause register (`mips_exc_code`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExcCode {
    /// An interrupt.
    Int = 0,
    /// Address error on a load or an instruction fetch.
    AdEL = 4,
    /// Address error on a store.
    AdES = 5,
    /// Bus error on an instruction fetch.
    Ibe = 6,
    /// Bus error on a data reference.
    Dbe = 7,
    /// `syscall`.
    Sys = 8,
    /// `break`.
    Bp = 9,
    /// Reserved (unimplemented) instruction.
    Ri = 10,
    /// Coprocessor unusable.
    CpU = 11,
    /// Arithmetic overflow.
    Ov = 12,
    /// A trap instruction's condition held.
    Tr = 13,
    /// Floating-point exception.
    Fpe = 15,
}

/// The address of the exception handler.
pub const EXCEPTION_ADDR: u32 = 0x8000_0180;
/// Instructions between polls of the memory-mapped console.
const IO_INTERVAL: i32 = 100;
/// The period of the CP0 Count timer.
const TIMER_TICK: Duration = Duration::from_millis(10);

/// What an instruction tells the run loop to do next.
enum Flow {
    /// Go on to the next instruction.
    Next,
    /// Stop: a breakpoint (`true`, can continue) or an exit (`false`).
    Return(bool),
}

impl Machine {
    /// Raise exception `code` (`raise_exception`): record it in Cause, set
    /// EPC to the faulting instruction unless an exception is already
    /// being handled, and set the exception level bit.  An interrupt is
    /// ignored while interrupts are disabled.
    pub fn raise_exception(&mut self, code: ExcCode) {
        let status: u32 = self.regs.cp0(CP0_STATUS) as u32;
        let interrupts_on: bool = status & STATUS_IE != 0 && status & STATUS_EXL == 0;
        if code == ExcCode::Int && !interrupts_on {
            return;
        }
        self.exception_occurred = true;
        if self.first_bad_exception == -1
            && !matches!(code, ExcCode::Int | ExcCode::Sys | ExcCode::Bp)
        {
            self.first_bad_exception = code as i32;
        }
        if status & STATUS_EXL == 0 {
            if self.run.running_in_delay_slot {
                // The branch's address, and the branch-delay bit.
                let epc: u32 = self.regs.pc.wrapping_sub(4) & !3;
                self.regs.set_cp0(CP0_EPC, epc as i32);
                let cause: u32 = self.regs.cp0(CP0_CAUSE) as u32 | CAUSE_BD;
                self.regs.set_cp0(CP0_CAUSE, cause as i32);
            } else {
                self.regs.set_cp0(CP0_EPC, (self.regs.pc & !3) as i32);
            }
        }
        let cause: u32 = (self.regs.cp0(CP0_CAUSE) as u32 & !CAUSE_EXCCODE) | ((code as u32) << 2);
        self.regs.set_cp0(CP0_CAUSE, cause as i32);
        self.regs.set_cp0(
            CP0_STATUS,
            (self.regs.cp0(CP0_STATUS) as u32 | STATUS_EXL) as i32,
        );
    }

    /// Start the 10 ms timer if it is not running (`start_CP0_timer`).
    fn start_cp0_timer(&mut self) {
        let now: Instant = Instant::now();
        let expired: bool = self
            .run
            .timer_deadline
            .is_none_or(|deadline: Instant| now >= deadline);
        if expired {
            self.run.timer_deadline = Some(now + TIMER_TICK);
        }
    }

    /// If 10 ms have passed, add 1 to Count, and interrupt when it reaches
    /// Compare (`bump_CP0_timer`).
    fn poll_cp0_timer(&mut self) {
        let now: Instant = Instant::now();
        let expired: bool = self
            .run
            .timer_deadline
            .is_none_or(|deadline: Instant| now >= deadline);
        if expired {
            let count: i32 = self.regs.cp0(CP0_COUNT).wrapping_add(1);
            self.regs.set_cp0(CP0_COUNT, count);
            if count == self.regs.cp0(CP0_COMPARE) {
                self.raise_interrupt(7);
            }
            self.start_cp0_timer();
        }
    }

    /// Run the program at `initial_pc` for `steps_to_run` instructions,
    /// printing each one if `display` (`run_spim`).  Returns true if the
    /// program can continue (it stopped at a breakpoint or ran out of
    /// steps), false if it exited.
    pub fn run_spim(&mut self, initial_pc: u32, steps_to_run: i32, display: bool) -> bool {
        self.regs.pc = initial_pc;
        let mapped_io: bool = !self.options.bare_machine && self.options.mapped_io;
        // Without mapped IO, run to completion in one chunk.
        let chunk: i32 = if mapped_io { IO_INTERVAL } else { steps_to_run };
        self.start_cp0_timer();
        let mut steps_left: i32 = steps_to_run;
        while steps_left > 0 {
            let step_size: i32 = chunk.min(steps_left);
            if mapped_io {
                self.check_memory_mapped_io();
            }
            let status: u32 = self.regs.cp0(CP0_STATUS) as u32;
            let cause: u32 = self.regs.cp0(CP0_CAUSE) as u32;
            if status & STATUS_IE != 0
                && status & STATUS_EXL == 0
                && cause & CAUSE_IP & status & STATUS_IM != 0
            {
                // Take the interrupt before the next instruction runs, so
                // EPC points at it.
                self.raise_exception(ExcCode::Int);
                self.handle_exception();
            }
            self.run.force_break = false;
            for _ in 0..step_size {
                if self.run.force_break {
                    return true;
                }
                if let Flow::Return(can_continue) = self.step(display) {
                    return can_continue;
                }
            }
            steps_left -= step_size;
        }
        true
    }

    /// Fetch and execute one instruction.
    fn step(&mut self, display: bool) -> Flow {
        self.regs.gpr[0] = 0;
        self.poll_cp0_timer();
        self.exception_occurred = false;
        let pc: u32 = self.regs.pc;
        let fetched: Option<InstRef> = self.mem_read_inst(pc);
        if self.exception_occurred {
            self.exception_occurred = false;
            self.handle_exception();
            return Flow::Next;
        }
        let Some(fetched) = fetched else {
            run_error(&format!(
                "Attempt to execute non-instruction at 0x{pc:08x}\n"
            ));
        };
        let instruction: Instruction = fetched.borrow().clone();
        if let Some(id) = instruction.expr.and_then(|e| e.symbol) {
            if self.symbols.label(id).addr == 0 {
                let listing: Vec<u8> = self.inst_to_string(pc);
                let mut message: Vec<u8> =
                    format!("Instruction references undefined symbol at 0x{pc:08x}\n  ")
                        .into_bytes();
                message.extend_from_slice(&listing);
                run_error(&String::from_utf8_lossy(&message));
            }
        }
        self.do_delayed_update();
        if let Flow::Return(can_continue) = self.execute(&instruction, display) {
            return Flow::Return(can_continue);
        }
        self.regs.pc = self.regs.pc.wrapping_add(4);
        if display {
            let pc: u32 = self.regs.pc;
            self.print_inst(pc);
        }
        if self.exception_occurred {
            self.handle_exception();
        }
        Flow::Next
    }

    /// Finish the load issued two instructions ago (`DO_DELAYED_UPDATE`).
    fn do_delayed_update(&mut self) {
        if self.options.delayed_loads {
            if let Some((dest, value)) = self.run.delayed_load_2 {
                self.write_load(dest, value);
            }
            self.run.delayed_load_2 = self.run.delayed_load_1.take();
        }
    }

    /// Write a loaded value now, or later with `-delayed_loads`
    /// (`LOAD_INST_BASE`).
    fn load(&mut self, dest: LoadDest, value: i32) {
        if self.options.delayed_loads {
            self.run.delayed_load_1 = Some((dest, value));
        } else {
            self.write_load(dest, value);
        }
    }

    fn write_load(&mut self, dest: LoadDest, value: i32) {
        match dest {
            LoadDest::Gpr(n) => self.regs.gpr[n & 31] = value,
            LoadDest::Fpr(n) => self.regs.set_fpr_w(n, value),
        }
    }

    /// Jump to `target` (`JUMP_INST`).  With delayed branches, the
    /// instruction after the jump (in its delay slot) runs first.
    fn jump(&mut self, target: u32, display: bool) {
        if self.options.delayed_branches {
            self.run.running_in_delay_slot = true;
            let slot: u32 = self.regs.pc.wrapping_add(4);
            let _ = self.run_spim(slot, 1, display);
            self.run.running_in_delay_slot = false;
        }
        // The run loop adds 4 after this instruction.
        self.regs.pc = target.wrapping_sub(4);
    }

    /// A conditional branch (`BRANCH_INST`).  A branch-likely (`nullify`)
    /// that is not taken skips its delay slot.
    fn branch(&mut self, taken: bool, offset: i32, nullify: bool, display: bool) {
        if taken {
            let mut target: u32 = self.regs.pc.wrapping_add(offset as u32);
            if self.options.delayed_branches {
                target = target.wrapping_add(4);
            }
            self.jump(target, display);
        } else if nullify {
            self.regs.pc = self.regs.pc.wrapping_add(4);
        }
    }

    /// The address of a load or store: base register plus the
    /// sign-extended offset.
    fn effective_address(&self, inst: &Instruction) -> u32 {
        (self.regs.gpr[inst.rs() & 31] as u32).wrapping_add(i32::from(inst.imm()) as u32)
    }

    /// Set condition code `cc` from a compare (`set_fpu_cc`).
    fn set_fpu_cc(&mut self, cond: u32, cc: i32, less: bool, equal: bool, unordered: bool) {
        let mut result: bool = false;
        if cond & 0x4 != 0 {
            result |= less;
        }
        if cond & 0x2 != 0 {
            result |= equal;
        }
        if cond & 0x1 != 0 {
            result |= unordered;
        }
        self.regs.set_fcc(cc, i32::from(result));
    }

    /// Carry out one instruction (the big `switch` of `run_spim`).
    fn execute(&mut self, inst: &Instruction, display: bool) -> Flow {
        let rs: usize = inst.rs() & 31;
        let rt: usize = inst.rt() & 31;
        let rd: usize = inst.rd() & 31;
        let (fs, ft, fd): (usize, usize, usize) = (inst.fs(), inst.ft(), inst.fd());
        let imm: i32 = i32::from(inst.imm());
        let pc: u32 = self.regs.pc;
        let delay: u32 = if self.options.delayed_branches { 8 } else { 4 };
        let g = |machine: &Machine, n: usize| -> i32 { machine.regs.gpr[n] };
        match inst.opcode {
            TOK_ADD_OPCODE => match g(self, rs).checked_add(g(self, rt)) {
                Some(sum) => self.regs.gpr[rd] = sum,
                None => self.raise_exception(ExcCode::Ov),
            },
            TOK_ADDI_OPCODE => match g(self, rs).checked_add(sign_extend_16(imm)) {
                Some(sum) => self.regs.gpr[rt] = sum,
                None => self.raise_exception(ExcCode::Ov),
            },
            TOK_ADDIU_OPCODE => self.regs.gpr[rt] = g(self, rs).wrapping_add(imm),
            TOK_ADDU_OPCODE => self.regs.gpr[rd] = g(self, rs).wrapping_add(g(self, rt)),
            TOK_AND_OPCODE => self.regs.gpr[rd] = g(self, rs) & g(self, rt),
            TOK_ANDI_OPCODE => self.regs.gpr[rt] = g(self, rs) & (imm & 0xffff),
            TOK_BC2F_OPCODE | TOK_BC2FL_OPCODE | TOK_BC2T_OPCODE | TOK_BC2TL_OPCODE => {
                self.raise_exception(ExcCode::CpU)
            }
            TOK_BEQ_OPCODE | TOK_BEQL_OPCODE => {
                let taken: bool = g(self, rs) == g(self, rt);
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BEQL_OPCODE,
                    display,
                );
            }
            TOK_BGEZ_OPCODE | TOK_BGEZL_OPCODE => {
                let taken: bool = g(self, rs) >= 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BGEZL_OPCODE,
                    display,
                );
            }
            TOK_BGEZAL_OPCODE | TOK_BGEZALL_OPCODE => {
                self.regs.gpr[31] = pc.wrapping_add(delay) as i32;
                let taken: bool = g(self, rs) >= 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BGEZALL_OPCODE,
                    display,
                );
            }
            TOK_BGTZ_OPCODE | TOK_BGTZL_OPCODE => {
                let taken: bool = g(self, rs) > 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BGTZL_OPCODE,
                    display,
                );
            }
            TOK_BLEZ_OPCODE | TOK_BLEZL_OPCODE => {
                let taken: bool = g(self, rs) <= 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BLEZL_OPCODE,
                    display,
                );
            }
            TOK_BLTZ_OPCODE | TOK_BLTZL_OPCODE => {
                let taken: bool = g(self, rs) < 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BLTZL_OPCODE,
                    display,
                );
            }
            TOK_BLTZAL_OPCODE | TOK_BLTZALL_OPCODE => {
                self.regs.gpr[31] = pc.wrapping_add(delay) as i32;
                let taken: bool = g(self, rs) < 0;
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BLTZALL_OPCODE,
                    display,
                );
            }
            TOK_BNE_OPCODE | TOK_BNEL_OPCODE => {
                let taken: bool = g(self, rs) != g(self, rt);
                self.branch(
                    taken,
                    inst.branch_offset(),
                    inst.opcode == TOK_BNEL_OPCODE,
                    display,
                );
            }
            TOK_BREAK_OPCODE => {
                self.raise_exception(ExcCode::Bp);
                if inst.rd() == 1 {
                    // `break 1` is the debugger's breakpoint: stop the run.
                    return Flow::Return(true);
                }
            }
            TOK_CACHE_OPCODE | TOK_PREF_OPCODE | TOK_SYNC_OPCODE => {}
            TOK_CFC0_OPCODE => self.regs.gpr[rt] = self.regs.cop_control[0][rd],
            TOK_CFC2_OPCODE | TOK_COP2_OPCODE | TOK_CTC2_OPCODE | TOK_MFC2_OPCODE
            | TOK_MTC2_OPCODE | TOK_LDC2_OPCODE | TOK_LWC2_OPCODE | TOK_SDC2_OPCODE
            | TOK_SWC2_OPCODE => self.raise_exception(ExcCode::CpU),
            TOK_CLO_OPCODE => self.regs.gpr[rd] = (g(self, rs) as u32).leading_ones() as i32,
            TOK_CLZ_OPCODE => self.regs.gpr[rd] = (g(self, rs) as u32).leading_zeros() as i32,
            TOK_CTC0_OPCODE => self.regs.cop_control[0][rd] = g(self, rt),
            TOK_DIV_OPCODE => {
                // Division by zero, and the one overflowing case
                // i32::MIN / -1, leave HI and LO unchanged, as in C.
                let (dividend, divisor): (i32, i32) = (g(self, rs), g(self, rt));
                if divisor != 0 && !(dividend == i32::MIN && divisor == -1) {
                    self.regs.lo = dividend / divisor;
                    self.regs.hi = dividend % divisor;
                }
            }
            TOK_DIVU_OPCODE => {
                let (dividend, divisor): (u32, u32) = (g(self, rs) as u32, g(self, rt) as u32);
                if let (Some(quotient), Some(remainder)) =
                    (dividend.checked_div(divisor), dividend.checked_rem(divisor))
                {
                    self.regs.lo = quotient as i32;
                    self.regs.hi = remainder as i32;
                }
            }
            TOK_ERET_OPCODE => {
                let status: u32 = self.regs.cp0(CP0_STATUS) as u32 & !STATUS_EXL;
                self.regs.set_cp0(CP0_STATUS, status as i32);
                let epc: u32 = self.regs.cp0(CP0_EPC) as u32;
                self.jump(epc, display);
            }
            TOK_J_OPCODE => {
                let target: u32 = (pc & 0xf000_0000) | inst.target().wrapping_shl(2);
                self.jump(target, display);
            }
            TOK_JAL_OPCODE => {
                self.regs.gpr[31] = pc.wrapping_add(delay) as i32;
                let target: u32 = (pc & 0xf000_0000) | inst.target().wrapping_shl(2);
                self.jump(target, display);
            }
            // The hazard-barrier forms (.hb) run as the plain ones: the simulator has no
            // pipeline hazards for a barrier to clear.
            TOK_JALR_OPCODE | TOK_JALR_HB_OPCODE => {
                let target: u32 = g(self, rs) as u32;
                self.regs.gpr[rd] = pc.wrapping_add(delay) as i32;
                self.jump(target, display);
            }
            TOK_JR_OPCODE | TOK_JR_HB_OPCODE => {
                let target: u32 = g(self, rs) as u32;
                self.jump(target, display);
            }
            TOK_LB_OPCODE => {
                let value: i32 = self.mem_read_byte(self.effective_address(inst));
                self.load(LoadDest::Gpr(rt), value);
            }
            TOK_LBU_OPCODE => {
                let value: i32 = self.mem_read_byte(self.effective_address(inst));
                self.load(LoadDest::Gpr(rt), value & 0xff);
            }
            TOK_LH_OPCODE => {
                let value: i32 = self.mem_read_half(self.effective_address(inst));
                self.load(LoadDest::Gpr(rt), value);
            }
            TOK_LHU_OPCODE => {
                let value: i32 = self.mem_read_half(self.effective_address(inst));
                self.load(LoadDest::Gpr(rt), value & 0xffff);
            }
            TOK_LL_OPCODE | TOK_LW_OPCODE => {
                let value: i32 = self.mem_read_word(self.effective_address(inst));
                self.load(LoadDest::Gpr(rt), value);
            }
            TOK_LUI_OPCODE => self.regs.gpr[rt] = ((imm as u32) << 16) as i32,
            TOK_LWL_OPCODE | TOK_LWR_OPCODE => self.load_word_part(inst, rt),
            TOK_MADD_OPCODE | TOK_MADDU_OPCODE | TOK_MSUB_OPCODE | TOK_MSUBU_OPCODE => {
                let (hi, lo): (i32, i32) =
                    if inst.opcode == TOK_MADD_OPCODE || inst.opcode == TOK_MSUB_OPCODE {
                        signed_product(g(self, rs), g(self, rt))
                    } else {
                        unsigned_product(g(self, rs), g(self, rt))
                    };
                let accumulator: u64 = join_hi_lo(self.regs.hi, self.regs.lo);
                let product: u64 = join_hi_lo(hi, lo);
                let result: u64 =
                    if inst.opcode == TOK_MADD_OPCODE || inst.opcode == TOK_MADDU_OPCODE {
                        accumulator.wrapping_add(product)
                    } else {
                        accumulator.wrapping_sub(product)
                    };
                (self.regs.hi, self.regs.lo) = split_hi_lo(result);
            }
            TOK_MFC0_OPCODE => self.regs.gpr[rt] = *self.regs.cop[0].get(fs).unwrap_or(&0),
            TOK_MFHI_OPCODE => self.regs.gpr[rd] = self.regs.hi,
            TOK_MFLO_OPCODE => self.regs.gpr[rd] = self.regs.lo,
            TOK_MOVN_OPCODE => {
                if g(self, rt) != 0 {
                    self.regs.gpr[rd] = g(self, rs);
                }
            }
            TOK_MOVZ_OPCODE => {
                if g(self, rt) == 0 {
                    self.regs.gpr[rd] = g(self, rs);
                }
            }
            TOK_MTC0_OPCODE => self.move_to_cp0(fs, g(self, rt)),
            TOK_MTHI_OPCODE => self.regs.hi = g(self, rs),
            TOK_MTLO_OPCODE => self.regs.lo = g(self, rs),
            TOK_MUL_OPCODE => {
                (self.regs.hi, self.regs.lo) = signed_product(g(self, rs), g(self, rt));
                self.regs.gpr[rd] = self.regs.lo;
            }
            TOK_MULT_OPCODE => {
                (self.regs.hi, self.regs.lo) = signed_product(g(self, rs), g(self, rt));
            }
            TOK_MULTU_OPCODE => {
                (self.regs.hi, self.regs.lo) = unsigned_product(g(self, rs), g(self, rt));
            }
            TOK_NOR_OPCODE => self.regs.gpr[rd] = !(g(self, rs) | g(self, rt)),
            TOK_OR_OPCODE => self.regs.gpr[rd] = g(self, rs) | g(self, rt),
            TOK_ORI_OPCODE => self.regs.gpr[rt] = g(self, rs) | (imm & 0xffff),
            TOK_RFE_OPCODE => self.raise_exception(ExcCode::Ri),
            TOK_SB_OPCODE => self.mem_write_byte(self.effective_address(inst), g(self, rt)),
            TOK_SC_OPCODE | TOK_SW_OPCODE => {
                self.mem_write_word(self.effective_address(inst), g(self, rt))
            }
            TOK_SH_OPCODE => self.mem_write_half(self.effective_address(inst), g(self, rt)),
            TOK_SLL_OPCODE => {
                let shamt: usize = inst.shamt();
                self.regs.gpr[rd] = if shamt < 32 {
                    ((g(self, rt) as u32) << shamt) as i32
                } else {
                    g(self, rt)
                };
            }
            TOK_SLLV_OPCODE => {
                let shamt: u32 = (g(self, rs) & 0x1f) as u32;
                self.regs.gpr[rd] = ((g(self, rt) as u32) << shamt) as i32;
            }
            TOK_SLT_OPCODE => self.regs.gpr[rd] = i32::from(g(self, rs) < g(self, rt)),
            TOK_SLTI_OPCODE => self.regs.gpr[rt] = i32::from(g(self, rs) < imm),
            TOK_SLTIU_OPCODE => self.regs.gpr[rt] = i32::from((g(self, rs) as u32) < imm as u32),
            TOK_SLTU_OPCODE => {
                self.regs.gpr[rd] = i32::from((g(self, rs) as u32) < (g(self, rt) as u32))
            }
            TOK_SRA_OPCODE => {
                let shamt: usize = inst.shamt();
                self.regs.gpr[rd] = if shamt < 32 {
                    g(self, rt) >> shamt
                } else {
                    g(self, rt)
                };
            }
            TOK_SRAV_OPCODE => self.regs.gpr[rd] = g(self, rt) >> (g(self, rs) & 0x1f),
            TOK_SRL_OPCODE => {
                let shamt: usize = inst.shamt();
                self.regs.gpr[rd] = if shamt < 32 {
                    ((g(self, rt) as u32) >> shamt) as i32
                } else {
                    g(self, rt)
                };
            }
            TOK_SRLV_OPCODE => {
                let shamt: u32 = (g(self, rs) & 0x1f) as u32;
                self.regs.gpr[rd] = ((g(self, rt) as u32) >> shamt) as i32;
            }
            TOK_SUB_OPCODE => match g(self, rs).checked_sub(g(self, rt)) {
                Some(difference) => self.regs.gpr[rd] = difference,
                None => self.raise_exception(ExcCode::Ov),
            },
            TOK_SUBU_OPCODE => self.regs.gpr[rd] = g(self, rs).wrapping_sub(g(self, rt)),
            TOK_SWL_OPCODE | TOK_SWR_OPCODE => self.store_word_part(inst, rt),
            TOK_SYSCALL_OPCODE => {
                if !self.do_syscall() {
                    return Flow::Return(false);
                }
            }
            TOK_TEQ_OPCODE | TOK_TGE_OPCODE | TOK_TGEU_OPCODE | TOK_TLT_OPCODE
            | TOK_TLTU_OPCODE | TOK_TNE_OPCODE | TOK_TEQI_OPCODE | TOK_TGEI_OPCODE
            | TOK_TGEIU_OPCODE | TOK_TLTI_OPCODE | TOK_TLTIU_OPCODE | TOK_TNEI_OPCODE => {
                let a: i32 = g(self, rs);
                let b: i32 = if matches!(
                    inst.opcode,
                    TOK_TEQI_OPCODE
                        | TOK_TGEI_OPCODE
                        | TOK_TGEIU_OPCODE
                        | TOK_TLTI_OPCODE
                        | TOK_TLTIU_OPCODE
                        | TOK_TNEI_OPCODE
                ) {
                    imm
                } else {
                    g(self, rt)
                };
                let trap: bool = match inst.opcode {
                    TOK_TEQ_OPCODE | TOK_TEQI_OPCODE => a == b,
                    TOK_TNE_OPCODE | TOK_TNEI_OPCODE => a != b,
                    TOK_TGE_OPCODE | TOK_TGEI_OPCODE => a >= b,
                    TOK_TLT_OPCODE | TOK_TLTI_OPCODE => a < b,
                    TOK_TGEU_OPCODE | TOK_TGEIU_OPCODE => a as u32 >= b as u32,
                    TOK_TLTU_OPCODE | TOK_TLTIU_OPCODE => (a as u32) < b as u32,
                    // Never reached: the enclosing arm admits only the twelve
                    // trap opcodes, and each of those has an arm above.
                    _ => fatal_error(&format!(
                        "internal error: run_spim: trap opcode {}\n",
                        inst.opcode
                    )),
                };
                if trap {
                    self.raise_exception(ExcCode::Tr);
                }
            }
            TOK_TLBP_OPCODE | TOK_TLBR_OPCODE | TOK_TLBWI_OPCODE | TOK_TLBWR_OPCODE => {
                self.raise_exception(ExcCode::Ri)
            }
            TOK_XOR_OPCODE => self.regs.gpr[rd] = g(self, rs) ^ g(self, rt),
            TOK_XORI_OPCODE => self.regs.gpr[rt] = g(self, rs) ^ (imm & 0xffff),
            _ => return self.execute_fpu(inst, fs, ft, fd, display),
        }
        Flow::Next
    }

    /// `lwl`/`lwr`: load the part of an unaligned word that lies in the
    /// aligned word at the address, merged into the register.
    fn load_word_part(&mut self, inst: &Instruction, rt: usize) {
        let addr: u32 = self.effective_address(inst);
        let byte: u32 = addr & 3;
        let reg: u32 = self.regs.gpr[rt] as u32;
        let mut word: u32 = self.mem_read_word(addr & !3) as u32;
        if !self.exception_occurred {
            word = if inst.opcode == TOK_LWL_OPCODE {
                match byte {
                    0 => ((word & 0xff) << 24) | (reg & 0x00ff_ffff),
                    1 => ((word & 0xffff) << 16) | (reg & 0xffff),
                    2 => ((word & 0x00ff_ffff) << 8) | (reg & 0xff),
                    _ => word,
                }
            } else {
                match byte {
                    0 => word,
                    1 => (reg & 0xff00_0000) | ((word & 0xffff_ff00) >> 8),
                    2 => (reg & 0xffff_0000) | ((word & 0xffff_0000) >> 16),
                    _ => (reg & 0xffff_ff00) | ((word & 0xff00_0000) >> 24),
                }
            };
        }
        self.load(LoadDest::Gpr(rt), word as i32);
    }

    /// `swl`/`swr`: store part of a register into the aligned word.
    fn store_word_part(&mut self, inst: &Instruction, rt: usize) {
        let addr: u32 = self.effective_address(inst);
        let byte: u32 = addr & 3;
        let reg: u32 = self.regs.gpr[rt] as u32;
        let data: u32 = self.mem_read_word(addr & !3) as u32;
        let merged: u32 = if inst.opcode == TOK_SWL_OPCODE {
            match byte {
                0 => (data & 0xffff_ff00) | ((reg >> 24) & 0xff),
                1 => (data & 0xffff_0000) | ((reg >> 16) & 0xffff),
                2 => (data & 0xff00_0000) | ((reg >> 8) & 0x00ff_ffff),
                _ => reg,
            }
        } else {
            match byte {
                0 => reg,
                1 => ((reg << 8) & 0xffff_ff00) | (data & 0xff),
                2 => ((reg << 16) & 0xffff_0000) | (data & 0xffff),
                _ => ((reg << 24) & 0xff00_0000) | (data & 0x00ff_ffff),
            }
        };
        self.mem_write_word(addr & !3, merged as i32);
    }

    /// `mtc0`: write a coprocessor 0 register; some bits are read only.
    fn move_to_cp0(&mut self, register: usize, value: i32) {
        let Some(slot) = self.regs.cop[0].get_mut(register) else {
            return;
        };
        *slot = value;
        match register {
            CP0_COMPARE => {
                // Writing Compare clears the timer interrupt.
                let cause: u32 = self.regs.cp0(CP0_CAUSE) as u32 & !CAUSE_IP7;
                self.regs.set_cp0(CP0_CAUSE, cause as i32);
            }
            CP0_STATUS => {
                let status: u32 =
                    (value as u32 & STATUS_MASK) | ((STATUS_CU & 0x3000_0000) | STATUS_UM);
                self.regs.set_cp0(CP0_STATUS, status as i32);
            }
            CP0_CAUSE => self
                .regs
                .set_cp0(CP0_CAUSE, (value as u32 & CAUSE_MASK) as i32),
            CP0_CONFIG => self
                .regs
                .set_cp0(CP0_CONFIG, (value as u32 & CONFIG_MASK) as i32),
            _ => {}
        }
    }

    /// The floating-point instructions.
    fn execute_fpu(
        &mut self,
        inst: &Instruction,
        fs: usize,
        ft: usize,
        fd: usize,
        display: bool,
    ) -> Flow {
        let regs: &Registers = &self.regs;
        let single = |n: usize| -> f32 { regs.fpr_s(n) };
        match inst.opcode {
            TOK_ABS_S_OPCODE => {
                let value: f32 = (f64::from(single(fs)).abs()) as f32;
                self.regs.set_fpr_s(fd, value);
            }
            TOK_ABS_D_OPCODE => {
                let value: f64 = self.regs.fpr_d(fs).abs();
                self.regs.set_fpr_d(fd, value);
            }
            TOK_ADD_S_OPCODE => {
                let value: f32 = single(fs) + single(ft);
                self.regs.set_fpr_s(fd, value);
            }
            TOK_ADD_D_OPCODE => self.double_op(fd, fs, ft, |a: f64, b: f64| a + b),
            TOK_SUB_S_OPCODE => {
                let value: f32 = single(fs) - single(ft);
                self.regs.set_fpr_s(fd, value);
            }
            TOK_SUB_D_OPCODE => self.double_op(fd, fs, ft, |a: f64, b: f64| a - b),
            TOK_MUL_S_OPCODE => {
                let value: f32 = single(fs) * single(ft);
                self.regs.set_fpr_s(fd, value);
            }
            TOK_MUL_D_OPCODE => self.double_op(fd, fs, ft, |a: f64, b: f64| a * b),
            TOK_DIV_S_OPCODE => {
                let value: f32 = single(fs) / single(ft);
                self.regs.set_fpr_s(fd, value);
            }
            TOK_DIV_D_OPCODE => self.double_op(fd, fs, ft, |a: f64, b: f64| a / b),
            TOK_SQRT_S_OPCODE => {
                let value: f32 = f64::from(single(fs)).sqrt() as f32;
                self.regs.set_fpr_s(fd, value);
            }
            TOK_SQRT_D_OPCODE => {
                let value: f64 = self.regs.fpr_d(fs).sqrt();
                self.regs.set_fpr_d(fd, value);
            }
            TOK_NEG_S_OPCODE => {
                let value: f32 = -single(fs);
                self.regs.set_fpr_s(fd, value);
            }
            TOK_NEG_D_OPCODE => {
                let value: f64 = -self.regs.fpr_d(fs);
                self.regs.set_fpr_d(fd, value);
            }
            TOK_MOV_S_OPCODE => {
                let bits: i32 = self.regs.fpr_w(fs);
                self.regs.set_fpr_w(fd, bits);
            }
            TOK_MOV_D_OPCODE => self.move_double(fd, fs),
            TOK_BC1F_OPCODE | TOK_BC1FL_OPCODE | TOK_BC1T_OPCODE | TOK_BC1TL_OPCODE => {
                let cc: i32 = inst.cc();
                let nullify: bool = (inst.rt() & 0x2) != 0;
                let true_branch: i32 = (inst.rt() & 0x1) as i32;
                let taken: bool = self.regs.fcc(cc) == true_branch;
                self.branch(taken, inst.branch_offset(), nullify, display);
            }
            TOK_C_F_S_OPCODE | TOK_C_UN_S_OPCODE | TOK_C_EQ_S_OPCODE | TOK_C_UEQ_S_OPCODE
            | TOK_C_OLT_S_OPCODE | TOK_C_OLE_S_OPCODE | TOK_C_ULT_S_OPCODE | TOK_C_ULE_S_OPCODE
            | TOK_C_SF_S_OPCODE | TOK_C_NGLE_S_OPCODE | TOK_C_SEQ_S_OPCODE | TOK_C_NGL_S_OPCODE
            | TOK_C_LT_S_OPCODE | TOK_C_NGE_S_OPCODE | TOK_C_LE_S_OPCODE | TOK_C_NGT_S_OPCODE => {
                let (a, b): (f32, f32) = (single(fs), single(ft));
                self.compare(inst, f64::from(a), f64::from(b));
            }
            TOK_C_F_D_OPCODE | TOK_C_UN_D_OPCODE | TOK_C_EQ_D_OPCODE | TOK_C_UEQ_D_OPCODE
            | TOK_C_OLT_D_OPCODE | TOK_C_OLE_D_OPCODE | TOK_C_ULT_D_OPCODE | TOK_C_ULE_D_OPCODE
            | TOK_C_SF_D_OPCODE | TOK_C_NGLE_D_OPCODE | TOK_C_SEQ_D_OPCODE | TOK_C_NGL_D_OPCODE
            | TOK_C_LT_D_OPCODE | TOK_C_NGE_D_OPCODE | TOK_C_LE_D_OPCODE | TOK_C_NGT_D_OPCODE => {
                let a: f64 = self.regs.fpr_d(fs);
                let b: f64 = self.regs.fpr_d(ft);
                self.compare(inst, a, b);
            }
            TOK_CFC1_OPCODE => {
                self.regs.gpr[inst.rt() & 31] = *self.regs.cop[1].get(fs).unwrap_or(&0)
            }
            TOK_CTC1_OPCODE => {
                let value: i32 = self.regs.gpr[inst.rt() & 31];
                if let Some(slot) = self.regs.cop[1].get_mut(fs) {
                    *slot = value;
                }
                // FIR is read only (but the C engine writes it anyway).
                if fs == FCSR_REG && value as u32 & !FCSR_MASK != 0 {
                    // Trying to set a mode the FPU does not support.
                    self.raise_exception(ExcCode::Fpe);
                }
            }
            TOK_CEIL_W_D_OPCODE => {
                let value: f64 = self.regs.fpr_d(fs);
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value.ceil()));
            }
            TOK_CEIL_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value.ceil()));
            }
            TOK_FLOOR_W_D_OPCODE => {
                let value: f64 = self.regs.fpr_d(fs);
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value.floor()));
            }
            TOK_FLOOR_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value.floor()));
            }
            TOK_ROUND_W_D_OPCODE => {
                // The C engine rounds by adding 0.5 and truncating.
                let value: f64 = self.regs.fpr_d(fs);
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value + 0.5));
            }
            TOK_ROUND_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value + 0.5));
            }
            TOK_TRUNC_W_D_OPCODE | TOK_CVT_W_D_OPCODE => {
                let value: f64 = self.regs.fpr_d(fs);
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value));
            }
            TOK_TRUNC_W_S_OPCODE | TOK_CVT_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.regs.set_fpr_w(fd, f64_to_i32_like_c(value));
            }
            TOK_CVT_D_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.regs.set_fpr_d(fd, value);
            }
            TOK_CVT_D_W_OPCODE => {
                let value: f64 = f64::from(self.regs.fpr_w(fs));
                self.regs.set_fpr_d(fd, value);
            }
            TOK_CVT_S_D_OPCODE => {
                let value: f32 = self.regs.fpr_d(fs) as f32;
                self.regs.set_fpr_s(fd, value);
            }
            TOK_CVT_S_W_OPCODE => {
                let value: f32 = self.regs.fpr_w(fs) as f32;
                self.regs.set_fpr_s(fd, value);
            }
            TOK_LDC1_OPCODE => {
                let addr: u32 = self.effective_address(inst);
                if addr & 3 != 0 {
                    self.raise_exception(ExcCode::AdEL);
                    self.regs.set_cp0(CP0_BADVADDR, addr as i32);
                }
                let low: i32 = self.mem_read_word(addr);
                self.load(LoadDest::Fpr(ft), low);
                let high: i32 = self.mem_read_word(addr.wrapping_add(4));
                self.load(LoadDest::Fpr(ft + 1), high);
            }
            TOK_LWC1_OPCODE => {
                let value: i32 = self.mem_read_word(self.effective_address(inst));
                self.load(LoadDest::Fpr(ft), value);
            }
            TOK_SDC1_OPCODE => {
                let bits: u64 = self.regs.fpr_d(inst.rt()).to_bits();
                let addr: u32 = self.effective_address(inst);
                if addr & 3 != 0 {
                    self.raise_exception(ExcCode::AdEL);
                    self.regs.set_cp0(CP0_BADVADDR, addr as i32);
                }
                self.mem_write_word(addr, bits as i32);
                self.mem_write_word(addr.wrapping_add(4), (bits >> 32) as i32);
            }
            TOK_SWC1_OPCODE => {
                let bits: i32 = self.regs.fpr_w(inst.rt());
                self.mem_write_word(self.effective_address(inst), bits);
            }
            TOK_MFC1_OPCODE => self.regs.gpr[inst.rt() & 31] = self.regs.fpr_w(fs),
            TOK_MTC1_OPCODE => {
                let bits: i32 = self.regs.gpr[inst.rt() & 31];
                self.regs.set_fpr_w(fs, bits);
            }
            TOK_MOVF_OPCODE | TOK_MOVT_OPCODE => {
                let condition: i32 = self.regs.fcc(inst.cc());
                if (condition != 0) == (inst.opcode == TOK_MOVT_OPCODE) {
                    self.regs.gpr[inst.rd() & 31] = self.regs.gpr[inst.rs() & 31];
                }
            }
            TOK_MOVF_S_OPCODE | TOK_MOVT_S_OPCODE => {
                let condition: i32 = self.regs.fcc(inst.cc());
                if (condition != 0) == (inst.opcode == TOK_MOVT_S_OPCODE) {
                    let bits: i32 = self.regs.fpr_w(fs);
                    self.regs.set_fpr_w(fd, bits);
                }
            }
            TOK_MOVF_D_OPCODE | TOK_MOVT_D_OPCODE => {
                let condition: i32 = self.regs.fcc(inst.cc());
                if (condition != 0) == (inst.opcode == TOK_MOVT_D_OPCODE) {
                    self.move_double(fd, fs);
                }
            }
            TOK_MOVN_S_OPCODE | TOK_MOVZ_S_OPCODE => {
                let zero: bool = self.regs.gpr[inst.rt() & 31] == 0;
                if zero == (inst.opcode == TOK_MOVZ_S_OPCODE) {
                    let bits: i32 = self.regs.fpr_w(fs);
                    self.regs.set_fpr_w(fd, bits);
                }
            }
            TOK_MOVN_D_OPCODE | TOK_MOVZ_D_OPCODE => {
                let zero: bool = self.regs.gpr[inst.rt() & 31] == 0;
                if zero == (inst.opcode == TOK_MOVZ_D_OPCODE) {
                    self.move_double(fd, fs);
                }
            }
            _ => fatal_error(&format!("Unknown instruction type: {}\n", inst.opcode)),
        }
        Flow::Next
    }

    /// `fd = op(fs, ft)` in double precision.
    fn double_op(&mut self, fd: usize, fs: usize, ft: usize, op: impl Fn(f64, f64) -> f64) {
        // As in C, the destination is checked for an odd number first.
        if fd & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        let a: f64 = self.regs.fpr_d(fs);
        let b: f64 = self.regs.fpr_d(ft);
        self.regs.set_fpr_d(fd, op(a, b));
    }

    /// `mov.d`: copy a double register's bits.
    fn move_double(&mut self, fd: usize, fs: usize) {
        if fd & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        let value: f64 = self.regs.fpr_d(fs);
        self.regs.set_fpr_d(fd, value);
    }

    /// `c.cond.fmt`: compare and set a condition code.  The condition
    /// bits (in the `rs` field) say which outcomes make it true; with the
    /// "invalid" bit, a NaN operand raises the floating-point exception.
    fn compare(&mut self, inst: &Instruction, a: f64, b: f64) {
        let cond: u32 = inst.rs() as u32;
        let cc: i32 = (inst.fd() >> 2) as i32;
        if a.is_nan() || b.is_nan() {
            if cond & 0x8 != 0 {
                self.raise_exception(ExcCode::Fpe);
                return;
            }
            self.set_fpu_cc(cond, cc, false, false, true);
        } else {
            self.set_fpu_cc(cond, cc, a < b, a == b, false);
        }
    }
}
