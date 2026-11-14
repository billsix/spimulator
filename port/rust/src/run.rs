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
    f64_to_i32_like_c, join_multiply_divide, sign_extend_16, signed_product, split_multiply_divide,
    unsigned_product,
};
use crate::instruction::{InstRef, Instruction};
use crate::machine::{fatal_error, run_error, LoadDestination, Machine};
use crate::opcodes::*;
use crate::registers::*;

/// The exception codes of the Cause register (`mips_exc_code`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExceptionCode {
    /// Int: An interrupt.
    Interrupt = 0,
    /// AdEL: Address error on a load or an instruction fetch.
    AddressErrorLoad = 4,
    /// AdES: Address error on a store.
    AddressErrorStore = 5,
    /// IBE: Bus error on an instruction fetch.
    InstructionBusError = 6,
    /// DBE: Bus error on a data reference.
    DataBusError = 7,
    /// Sys: `syscall`.
    SystemCall = 8,
    /// Bp: `break`.
    Breakpoint = 9,
    /// RI: Reserved (unimplemented) instruction.
    ReservedInstruction = 10,
    /// CpU: Coprocessor unusable.
    CoprocessorUnusable = 11,
    /// Ov: Arithmetic overflow.
    ArithmeticOverflow = 12,
    /// Tr: A trap instruction's condition held.
    Trap = 13,
    /// FPE: Floating-point exception.
    FloatingPointException = 15,
}

/// The address of the exception handler.
pub const EXCEPTION_ADDRESS: u32 = 0x8000_0180;
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
    pub fn raise_exception(&mut self, code: ExceptionCode) {
        let status: u32 = self.registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32;
        let interrupts_on: bool =
            status & STATUS_INTERRUPT_ENABLE != 0 && status & STATUS_EXCEPTION_LEVEL == 0;
        if code == ExceptionCode::Interrupt && !interrupts_on {
            return;
        }
        self.exception_occurred = true;
        if self.first_bad_exception == -1
            && !matches!(
                code,
                ExceptionCode::Interrupt | ExceptionCode::SystemCall | ExceptionCode::Breakpoint
            )
        {
            self.first_bad_exception = code as i32;
        }
        if status & STATUS_EXCEPTION_LEVEL == 0 {
            if self.run.running_in_delay_slot {
                // The branch's address, and the branch-delay bit.
                let exception_program_counter: u32 =
                    self.registers.program_counter.wrapping_sub(4) & !3;
                self.registers.set_coprocessor_0(
                    COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER,
                    exception_program_counter as i32,
                );
                let cause: u32 =
                    self.registers.coprocessor_0(COPROCESSOR_0_CAUSE) as u32 | CAUSE_BD;
                self.registers
                    .set_coprocessor_0(COPROCESSOR_0_CAUSE, cause as i32);
            } else {
                self.registers.set_coprocessor_0(
                    COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER,
                    (self.registers.program_counter & !3) as i32,
                );
            }
        }
        let cause: u32 = (self.registers.coprocessor_0(COPROCESSOR_0_CAUSE) as u32
            & !CAUSE_EXCCODE)
            | ((code as u32) << 2);
        self.registers
            .set_coprocessor_0(COPROCESSOR_0_CAUSE, cause as i32);
        self.registers.set_coprocessor_0(
            COPROCESSOR_0_STATUS,
            (self.registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32 | STATUS_EXCEPTION_LEVEL)
                as i32,
        );
    }

    /// Start the 10 ms timer if it is not running (`start_CP0_timer`).
    fn start_coprocessor_0_timer(&mut self) {
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
    fn poll_coprocessor_0_timer(&mut self) {
        let now: Instant = Instant::now();
        let expired: bool = self
            .run
            .timer_deadline
            .is_none_or(|deadline: Instant| now >= deadline);
        if expired {
            let count: i32 = self
                .registers
                .coprocessor_0(COPROCESSOR_0_COUNT)
                .wrapping_add(1);
            self.registers.set_coprocessor_0(COPROCESSOR_0_COUNT, count);
            if count == self.registers.coprocessor_0(COPROCESSOR_0_COMPARE) {
                self.raise_interrupt(7);
            }
            self.start_coprocessor_0_timer();
        }
    }

    /// Run the program at `initial_program_counter` for `steps_to_run` instructions,
    /// printing each one if `display` (`run_spim`).  Returns true if the
    /// program can continue (it stopped at a breakpoint or ran out of
    /// steps), false if it exited.
    pub fn run_spim(
        &mut self,
        initial_program_counter: u32,
        steps_to_run: i32,
        display: bool,
    ) -> bool {
        self.registers.program_counter = initial_program_counter;
        let mapped_io: bool = !self.options.bare_machine && self.options.mapped_io;
        // Without mapped IO, run to completion in one chunk.
        let chunk: i32 = if mapped_io { IO_INTERVAL } else { steps_to_run };
        self.start_coprocessor_0_timer();
        let mut steps_left: i32 = steps_to_run;
        while steps_left > 0 {
            let step_size: i32 = chunk.min(steps_left);
            if mapped_io {
                self.check_memory_mapped_io();
            }
            let status: u32 = self.registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32;
            let cause: u32 = self.registers.coprocessor_0(COPROCESSOR_0_CAUSE) as u32;
            if status & STATUS_INTERRUPT_ENABLE != 0
                && status & STATUS_EXCEPTION_LEVEL == 0
                && cause & CAUSE_INTERRUPTS_PENDING & status & STATUS_INTERRUPT_MASK != 0
            {
                // Take the interrupt before the next instruction runs, so
                // EPC points at it.
                self.raise_exception(ExceptionCode::Interrupt);
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
        self.registers.general_purpose_registers[0] = 0;
        self.poll_coprocessor_0_timer();
        self.exception_occurred = false;
        let program_counter: u32 = self.registers.program_counter;
        let fetched: Option<InstRef> = self.memory_read_instruction(program_counter);
        if self.exception_occurred {
            self.exception_occurred = false;
            self.handle_exception();
            return Flow::Next;
        }
        let Some(fetched) = fetched else {
            run_error(&format!(
                "Attempt to execute non-instruction at 0x{program_counter:08x}\n"
            ));
        };
        let instruction: Instruction = fetched.borrow().clone();
        if let Some(id) = instruction.expr.and_then(|e| e.symbol) {
            if self.symbols.label(id).address == 0 {
                let listing: Vec<u8> = self.instruction_to_string(program_counter);
                let mut message: Vec<u8> = format!(
                    "Instruction references undefined symbol at 0x{program_counter:08x}\n  "
                )
                .into_bytes();
                message.extend_from_slice(&listing);
                run_error(&String::from_utf8_lossy(&message));
            }
        }
        self.do_delayed_update();
        if let Flow::Return(can_continue) = self.execute(&instruction, display) {
            return Flow::Return(can_continue);
        }
        self.registers.program_counter = self.registers.program_counter.wrapping_add(4);
        if display {
            let program_counter: u32 = self.registers.program_counter;
            self.print_instruction(program_counter);
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
    fn load(&mut self, dest: LoadDestination, value: i32) {
        if self.options.delayed_loads {
            self.run.delayed_load_1 = Some((dest, value));
        } else {
            self.write_load(dest, value);
        }
    }

    fn write_load(&mut self, dest: LoadDestination, value: i32) {
        match dest {
            LoadDestination::GeneralPurposeRegister(n) => {
                self.registers.general_purpose_registers[n & 31] = value
            }
            LoadDestination::FloatingPointRegister(n) => {
                self.registers.set_floating_point_register_word(n, value)
            }
        }
    }

    /// Jump to `target` (`JUMP_INST`).  With delayed branches, the
    /// instruction after the jump (in its delay slot) runs first.
    fn jump(&mut self, target: u32, display: bool) {
        if self.options.delayed_branches {
            self.run.running_in_delay_slot = true;
            let slot: u32 = self.registers.program_counter.wrapping_add(4);
            let _ = self.run_spim(slot, 1, display);
            self.run.running_in_delay_slot = false;
        }
        // The run loop adds 4 after this instruction.
        self.registers.program_counter = target.wrapping_sub(4);
    }

    /// A conditional branch (`BRANCH_INST`).  A branch-likely (`nullify`)
    /// that is not taken skips its delay slot.
    fn branch(&mut self, taken: bool, offset: i32, nullify: bool, display: bool) {
        if taken {
            let mut target: u32 = self.registers.program_counter.wrapping_add(offset as u32);
            if self.options.delayed_branches {
                target = target.wrapping_add(4);
            }
            self.jump(target, display);
        } else if nullify {
            self.registers.program_counter = self.registers.program_counter.wrapping_add(4);
        }
    }

    /// The address of a load or store: base register plus the
    /// sign-extended offset.
    fn effective_address(&self, instruction: &Instruction) -> u32 {
        (self.registers.general_purpose_registers[instruction.source_register() & 31] as u32)
            .wrapping_add(i32::from(instruction.immediate()) as u32)
    }

    /// Set condition code `cc` from a compare (`set_floating_point_unit_condition_code`).
    fn set_floating_point_unit_condition_code(
        &mut self,
        condition: u32,
        condition_code: i32,
        less: bool,
        equal: bool,
        unordered: bool,
    ) {
        let mut result: bool = false;
        if condition & 0x4 != 0 {
            result |= less;
        }
        if condition & 0x2 != 0 {
            result |= equal;
        }
        if condition & 0x1 != 0 {
            result |= unordered;
        }
        self.registers
            .set_floating_point_condition_code(condition_code, i32::from(result));
    }

    /// Carry out one instruction (the big `switch` of `run_spim`).
    fn execute(&mut self, instruction: &Instruction, display: bool) -> Flow {
        let source_register: usize = instruction.source_register() & 31;
        let target_register: usize = instruction.target_register() & 31;
        let destination_register: usize = instruction.destination_register() & 31;
        let (fs, floating_point_target, floating_point_destination): (usize, usize, usize) = (
            instruction.fs(),
            instruction.floating_point_target(),
            instruction.floating_point_destination(),
        );
        let immediate: i32 = i32::from(instruction.immediate());
        let program_counter: u32 = self.registers.program_counter;
        let delay: u32 = if self.options.delayed_branches { 8 } else { 4 };
        let g =
            |machine: &Machine, n: usize| -> i32 { machine.registers.general_purpose_registers[n] };
        match instruction.opcode {
            TOKEN_ADD_OPCODE => {
                match g(self, source_register).checked_add(g(self, target_register)) {
                    Some(sum) => {
                        self.registers.general_purpose_registers[destination_register] = sum
                    }
                    None => self.raise_exception(ExceptionCode::ArithmeticOverflow),
                }
            }
            TOKEN_ADDI_OPCODE => {
                match g(self, source_register).checked_add(sign_extend_16(immediate)) {
                    Some(sum) => self.registers.general_purpose_registers[target_register] = sum,
                    None => self.raise_exception(ExceptionCode::ArithmeticOverflow),
                }
            }
            TOKEN_ADDIU_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    g(self, source_register).wrapping_add(immediate)
            }
            TOKEN_ADDU_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, source_register).wrapping_add(g(self, target_register))
            }
            TOKEN_AND_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, source_register) & g(self, target_register)
            }
            TOKEN_ANDI_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    g(self, source_register) & (immediate & 0xffff)
            }
            TOKEN_BC2F_OPCODE | TOKEN_BC2FL_OPCODE | TOKEN_BC2T_OPCODE | TOKEN_BC2TL_OPCODE => {
                self.raise_exception(ExceptionCode::CoprocessorUnusable)
            }
            TOKEN_BEQ_OPCODE | TOKEN_BEQL_OPCODE => {
                let taken: bool = g(self, source_register) == g(self, target_register);
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BEQL_OPCODE,
                    display,
                );
            }
            TOKEN_BGEZ_OPCODE | TOKEN_BGEZL_OPCODE => {
                let taken: bool = g(self, source_register) >= 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BGEZL_OPCODE,
                    display,
                );
            }
            TOKEN_BGEZAL_OPCODE | TOKEN_BGEZALL_OPCODE => {
                self.registers.general_purpose_registers[31] =
                    program_counter.wrapping_add(delay) as i32;
                let taken: bool = g(self, source_register) >= 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BGEZALL_OPCODE,
                    display,
                );
            }
            TOKEN_BGTZ_OPCODE | TOKEN_BGTZL_OPCODE => {
                let taken: bool = g(self, source_register) > 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BGTZL_OPCODE,
                    display,
                );
            }
            TOKEN_BLEZ_OPCODE | TOKEN_BLEZL_OPCODE => {
                let taken: bool = g(self, source_register) <= 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BLEZL_OPCODE,
                    display,
                );
            }
            TOKEN_BLTZ_OPCODE | TOKEN_BLTZL_OPCODE => {
                let taken: bool = g(self, source_register) < 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BLTZL_OPCODE,
                    display,
                );
            }
            TOKEN_BLTZAL_OPCODE | TOKEN_BLTZALL_OPCODE => {
                self.registers.general_purpose_registers[31] =
                    program_counter.wrapping_add(delay) as i32;
                let taken: bool = g(self, source_register) < 0;
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BLTZALL_OPCODE,
                    display,
                );
            }
            TOKEN_BNE_OPCODE | TOKEN_BNEL_OPCODE => {
                let taken: bool = g(self, source_register) != g(self, target_register);
                self.branch(
                    taken,
                    instruction.branch_offset(),
                    instruction.opcode == TOKEN_BNEL_OPCODE,
                    display,
                );
            }
            TOKEN_BREAK_OPCODE => {
                self.raise_exception(ExceptionCode::Breakpoint);
                if instruction.destination_register() == 1 {
                    // `break 1` is the debugger's breakpoint: stop the run.
                    return Flow::Return(true);
                }
            }
            TOKEN_CACHE_OPCODE | TOKEN_PREF_OPCODE | TOKEN_SYNC_OPCODE => {}
            TOKEN_CFC0_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    self.registers.cop_control[0][destination_register]
            }
            TOKEN_CFC2_OPCODE | TOKEN_COP2_OPCODE | TOKEN_CTC2_OPCODE | TOKEN_MFC2_OPCODE
            | TOKEN_MTC2_OPCODE | TOKEN_LDC2_OPCODE | TOKEN_LWC2_OPCODE | TOKEN_SDC2_OPCODE
            | TOKEN_SWC2_OPCODE => self.raise_exception(ExceptionCode::CoprocessorUnusable),
            TOKEN_CLO_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    (g(self, source_register) as u32).leading_ones() as i32
            }
            TOKEN_CLZ_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    (g(self, source_register) as u32).leading_zeros() as i32
            }
            TOKEN_CTC0_OPCODE => {
                self.registers.cop_control[0][destination_register] = g(self, target_register)
            }
            TOKEN_DIV_OPCODE => {
                // Division by zero, and the one overflowing case
                // i32::MIN / -1, leave HI and LO unchanged, as in C.
                let (dividend, divisor): (i32, i32) =
                    (g(self, source_register), g(self, target_register));
                if divisor != 0 && !(dividend == i32::MIN && divisor == -1) {
                    self.registers.lo = dividend / divisor;
                    self.registers.hi = dividend % divisor;
                }
            }
            TOKEN_DIVU_OPCODE => {
                let (dividend, divisor): (u32, u32) = (
                    g(self, source_register) as u32,
                    g(self, target_register) as u32,
                );
                if let (Some(quotient), Some(remainder)) =
                    (dividend.checked_div(divisor), dividend.checked_rem(divisor))
                {
                    self.registers.lo = quotient as i32;
                    self.registers.hi = remainder as i32;
                }
            }
            TOKEN_ERET_OPCODE => {
                let status: u32 = self.registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32
                    & !STATUS_EXCEPTION_LEVEL;
                self.registers
                    .set_coprocessor_0(COPROCESSOR_0_STATUS, status as i32);
                let exception_program_counter: u32 = self
                    .registers
                    .coprocessor_0(COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER)
                    as u32;
                self.jump(exception_program_counter, display);
            }
            TOKEN_J_OPCODE => {
                let target: u32 =
                    (program_counter & 0xf000_0000) | instruction.target().wrapping_shl(2);
                self.jump(target, display);
            }
            TOKEN_JAL_OPCODE => {
                self.registers.general_purpose_registers[31] =
                    program_counter.wrapping_add(delay) as i32;
                let target: u32 =
                    (program_counter & 0xf000_0000) | instruction.target().wrapping_shl(2);
                self.jump(target, display);
            }
            // The hazard-barrier forms (.hb) run as the plain ones: the simulator has no
            // pipeline hazards for a barrier to clear.
            TOKEN_JALR_OPCODE | TOKEN_JALR_HB_OPCODE => {
                let target: u32 = g(self, source_register) as u32;
                self.registers.general_purpose_registers[destination_register] =
                    program_counter.wrapping_add(delay) as i32;
                self.jump(target, display);
            }
            TOKEN_JR_OPCODE | TOKEN_JR_HB_OPCODE => {
                let target: u32 = g(self, source_register) as u32;
                self.jump(target, display);
            }
            TOKEN_LB_OPCODE => {
                let value: i32 = self.memory_read_byte(self.effective_address(instruction));
                self.load(
                    LoadDestination::GeneralPurposeRegister(target_register),
                    value,
                );
            }
            TOKEN_LBU_OPCODE => {
                let value: i32 = self.memory_read_byte(self.effective_address(instruction));
                self.load(
                    LoadDestination::GeneralPurposeRegister(target_register),
                    value & 0xff,
                );
            }
            TOKEN_LH_OPCODE => {
                let value: i32 = self.memory_read_half(self.effective_address(instruction));
                self.load(
                    LoadDestination::GeneralPurposeRegister(target_register),
                    value,
                );
            }
            TOKEN_LHU_OPCODE => {
                let value: i32 = self.memory_read_half(self.effective_address(instruction));
                self.load(
                    LoadDestination::GeneralPurposeRegister(target_register),
                    value & 0xffff,
                );
            }
            TOKEN_LL_OPCODE | TOKEN_LW_OPCODE => {
                let value: i32 = self.memory_read_word(self.effective_address(instruction));
                self.load(
                    LoadDestination::GeneralPurposeRegister(target_register),
                    value,
                );
            }
            TOKEN_LUI_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    ((immediate as u32) << 16) as i32
            }
            TOKEN_LWL_OPCODE | TOKEN_LWR_OPCODE => {
                self.load_word_part(instruction, target_register)
            }
            TOKEN_MADD_OPCODE | TOKEN_MADDU_OPCODE | TOKEN_MSUB_OPCODE | TOKEN_MSUBU_OPCODE => {
                let (hi, lo): (i32, i32) = if instruction.opcode == TOKEN_MADD_OPCODE
                    || instruction.opcode == TOKEN_MSUB_OPCODE
                {
                    signed_product(g(self, source_register), g(self, target_register))
                } else {
                    unsigned_product(g(self, source_register), g(self, target_register))
                };
                let accumulator: u64 = join_multiply_divide(self.registers.hi, self.registers.lo);
                let product: u64 = join_multiply_divide(hi, lo);
                let result: u64 = if instruction.opcode == TOKEN_MADD_OPCODE
                    || instruction.opcode == TOKEN_MADDU_OPCODE
                {
                    accumulator.wrapping_add(product)
                } else {
                    accumulator.wrapping_sub(product)
                };
                (self.registers.hi, self.registers.lo) = split_multiply_divide(result);
            }
            TOKEN_MFC0_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    *self.registers.cop[0].get(fs).unwrap_or(&0)
            }
            TOKEN_MFHI_OPCODE => {
                self.registers.general_purpose_registers[destination_register] = self.registers.hi
            }
            TOKEN_MFLO_OPCODE => {
                self.registers.general_purpose_registers[destination_register] = self.registers.lo
            }
            TOKEN_MOVN_OPCODE => {
                if g(self, target_register) != 0 {
                    self.registers.general_purpose_registers[destination_register] =
                        g(self, source_register);
                }
            }
            TOKEN_MOVZ_OPCODE => {
                if g(self, target_register) == 0 {
                    self.registers.general_purpose_registers[destination_register] =
                        g(self, source_register);
                }
            }
            TOKEN_MTC0_OPCODE => self.move_to_coprocessor_0(fs, g(self, target_register)),
            TOKEN_MTHI_OPCODE => self.registers.hi = g(self, source_register),
            TOKEN_MTLO_OPCODE => self.registers.lo = g(self, source_register),
            TOKEN_MUL_OPCODE => {
                (self.registers.hi, self.registers.lo) =
                    signed_product(g(self, source_register), g(self, target_register));
                self.registers.general_purpose_registers[destination_register] = self.registers.lo;
            }
            TOKEN_MULT_OPCODE => {
                (self.registers.hi, self.registers.lo) =
                    signed_product(g(self, source_register), g(self, target_register));
            }
            TOKEN_MULTU_OPCODE => {
                (self.registers.hi, self.registers.lo) =
                    unsigned_product(g(self, source_register), g(self, target_register));
            }
            TOKEN_NOR_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    !(g(self, source_register) | g(self, target_register))
            }
            TOKEN_OR_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, source_register) | g(self, target_register)
            }
            TOKEN_ORI_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    g(self, source_register) | (immediate & 0xffff)
            }
            TOKEN_RFE_OPCODE => self.raise_exception(ExceptionCode::ReservedInstruction),
            TOKEN_SB_OPCODE => self.memory_write_byte(
                self.effective_address(instruction),
                g(self, target_register),
            ),
            TOKEN_SC_OPCODE | TOKEN_SW_OPCODE => self.memory_write_word(
                self.effective_address(instruction),
                g(self, target_register),
            ),
            TOKEN_SH_OPCODE => self.memory_write_half(
                self.effective_address(instruction),
                g(self, target_register),
            ),
            TOKEN_SLL_OPCODE => {
                let shift_amount: usize = instruction.shift_amount();
                self.registers.general_purpose_registers[destination_register] =
                    if shift_amount < 32 {
                        ((g(self, target_register) as u32) << shift_amount) as i32
                    } else {
                        g(self, target_register)
                    };
            }
            TOKEN_SLLV_OPCODE => {
                let shift_amount: u32 = (g(self, source_register) & 0x1f) as u32;
                self.registers.general_purpose_registers[destination_register] =
                    ((g(self, target_register) as u32) << shift_amount) as i32;
            }
            TOKEN_SLT_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    i32::from(g(self, source_register) < g(self, target_register))
            }
            TOKEN_SLTI_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    i32::from(g(self, source_register) < immediate)
            }
            TOKEN_SLTIU_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    i32::from((g(self, source_register) as u32) < immediate as u32)
            }
            TOKEN_SLTU_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    i32::from((g(self, source_register) as u32) < (g(self, target_register) as u32))
            }
            TOKEN_SRA_OPCODE => {
                let shift_amount: usize = instruction.shift_amount();
                self.registers.general_purpose_registers[destination_register] =
                    if shift_amount < 32 {
                        g(self, target_register) >> shift_amount
                    } else {
                        g(self, target_register)
                    };
            }
            TOKEN_SRAV_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, target_register) >> (g(self, source_register) & 0x1f)
            }
            TOKEN_SRL_OPCODE => {
                let shift_amount: usize = instruction.shift_amount();
                self.registers.general_purpose_registers[destination_register] =
                    if shift_amount < 32 {
                        ((g(self, target_register) as u32) >> shift_amount) as i32
                    } else {
                        g(self, target_register)
                    };
            }
            TOKEN_SRLV_OPCODE => {
                let shift_amount: u32 = (g(self, source_register) & 0x1f) as u32;
                self.registers.general_purpose_registers[destination_register] =
                    ((g(self, target_register) as u32) >> shift_amount) as i32;
            }
            TOKEN_SUB_OPCODE => {
                match g(self, source_register).checked_sub(g(self, target_register)) {
                    Some(difference) => {
                        self.registers.general_purpose_registers[destination_register] = difference
                    }
                    None => self.raise_exception(ExceptionCode::ArithmeticOverflow),
                }
            }
            TOKEN_SUBU_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, source_register).wrapping_sub(g(self, target_register))
            }
            TOKEN_SWL_OPCODE | TOKEN_SWR_OPCODE => {
                self.store_word_part(instruction, target_register)
            }
            TOKEN_SYSCALL_OPCODE => {
                if !self.do_syscall() {
                    return Flow::Return(false);
                }
            }
            TOKEN_TEQ_OPCODE | TOKEN_TGE_OPCODE | TOKEN_TGEU_OPCODE | TOKEN_TLT_OPCODE
            | TOKEN_TLTU_OPCODE | TOKEN_TNE_OPCODE | TOKEN_TEQI_OPCODE | TOKEN_TGEI_OPCODE
            | TOKEN_TGEIU_OPCODE | TOKEN_TLTI_OPCODE | TOKEN_TLTIU_OPCODE | TOKEN_TNEI_OPCODE => {
                let a: i32 = g(self, source_register);
                let b: i32 = if matches!(
                    instruction.opcode,
                    TOKEN_TEQI_OPCODE
                        | TOKEN_TGEI_OPCODE
                        | TOKEN_TGEIU_OPCODE
                        | TOKEN_TLTI_OPCODE
                        | TOKEN_TLTIU_OPCODE
                        | TOKEN_TNEI_OPCODE
                ) {
                    immediate
                } else {
                    g(self, target_register)
                };
                let trap: bool = match instruction.opcode {
                    TOKEN_TEQ_OPCODE | TOKEN_TEQI_OPCODE => a == b,
                    TOKEN_TNE_OPCODE | TOKEN_TNEI_OPCODE => a != b,
                    TOKEN_TGE_OPCODE | TOKEN_TGEI_OPCODE => a >= b,
                    TOKEN_TLT_OPCODE | TOKEN_TLTI_OPCODE => a < b,
                    TOKEN_TGEU_OPCODE | TOKEN_TGEIU_OPCODE => a as u32 >= b as u32,
                    TOKEN_TLTU_OPCODE | TOKEN_TLTIU_OPCODE => (a as u32) < b as u32,
                    // Never reached: the enclosing arm admits only the twelve
                    // trap opcodes, and each of those has an arm above.
                    _ => fatal_error(&format!(
                        "internal error: run_spim: trap opcode {}\n",
                        instruction.opcode
                    )),
                };
                if trap {
                    self.raise_exception(ExceptionCode::Trap);
                }
            }
            TOKEN_TLBP_OPCODE | TOKEN_TLBR_OPCODE | TOKEN_TLBWI_OPCODE | TOKEN_TLBWR_OPCODE => {
                self.raise_exception(ExceptionCode::ReservedInstruction)
            }
            TOKEN_XOR_OPCODE => {
                self.registers.general_purpose_registers[destination_register] =
                    g(self, source_register) ^ g(self, target_register)
            }
            TOKEN_XORI_OPCODE => {
                self.registers.general_purpose_registers[target_register] =
                    g(self, source_register) ^ (immediate & 0xffff)
            }
            _ => {
                return self.execute_floating_point_unit(
                    instruction,
                    fs,
                    floating_point_target,
                    floating_point_destination,
                    display,
                )
            }
        }
        Flow::Next
    }

    /// `lwl`/`lwr`: load the part of an unaligned word that lies in the
    /// aligned word at the address, merged into the register.
    fn load_word_part(&mut self, instruction: &Instruction, target_register: usize) {
        let address: u32 = self.effective_address(instruction);
        let byte: u32 = address & 3;
        let register_number: u32 = self.registers.general_purpose_registers[target_register] as u32;
        let mut word: u32 = self.memory_read_word(address & !3) as u32;
        if !self.exception_occurred {
            word = if instruction.opcode == TOKEN_LWL_OPCODE {
                match byte {
                    0 => ((word & 0xff) << 24) | (register_number & 0x00ff_ffff),
                    1 => ((word & 0xffff) << 16) | (register_number & 0xffff),
                    2 => ((word & 0x00ff_ffff) << 8) | (register_number & 0xff),
                    _ => word,
                }
            } else {
                match byte {
                    0 => word,
                    1 => (register_number & 0xff00_0000) | ((word & 0xffff_ff00) >> 8),
                    2 => (register_number & 0xffff_0000) | ((word & 0xffff_0000) >> 16),
                    _ => (register_number & 0xffff_ff00) | ((word & 0xff00_0000) >> 24),
                }
            };
        }
        self.load(
            LoadDestination::GeneralPurposeRegister(target_register),
            word as i32,
        );
    }

    /// `swl`/`swr`: store part of a register into the aligned word.
    fn store_word_part(&mut self, instruction: &Instruction, target_register: usize) {
        let address: u32 = self.effective_address(instruction);
        let byte: u32 = address & 3;
        let register_number: u32 = self.registers.general_purpose_registers[target_register] as u32;
        let data: u32 = self.memory_read_word(address & !3) as u32;
        let merged: u32 = if instruction.opcode == TOKEN_SWL_OPCODE {
            match byte {
                0 => (data & 0xffff_ff00) | ((register_number >> 24) & 0xff),
                1 => (data & 0xffff_0000) | ((register_number >> 16) & 0xffff),
                2 => (data & 0xff00_0000) | ((register_number >> 8) & 0x00ff_ffff),
                _ => register_number,
            }
        } else {
            match byte {
                0 => register_number,
                1 => ((register_number << 8) & 0xffff_ff00) | (data & 0xff),
                2 => ((register_number << 16) & 0xffff_0000) | (data & 0xffff),
                _ => ((register_number << 24) & 0xff00_0000) | (data & 0x00ff_ffff),
            }
        };
        self.memory_write_word(address & !3, merged as i32);
    }

    /// `mtc0`: write a coprocessor 0 register; some bits are read only.
    fn move_to_coprocessor_0(&mut self, register: usize, value: i32) {
        let Some(slot) = self.registers.cop[0].get_mut(register) else {
            return;
        };
        *slot = value;
        match register {
            COPROCESSOR_0_COMPARE => {
                // Writing Compare clears the timer interrupt.
                let cause: u32 = self.registers.coprocessor_0(COPROCESSOR_0_CAUSE) as u32
                    & !CAUSE_INTERRUPTS_PENDING_7;
                self.registers
                    .set_coprocessor_0(COPROCESSOR_0_CAUSE, cause as i32);
            }
            COPROCESSOR_0_STATUS => {
                let status: u32 = (value as u32 & STATUS_MASK)
                    | ((STATUS_COPROCESSOR_USABLE & 0x3000_0000) | STATUS_USER_MODE);
                self.registers
                    .set_coprocessor_0(COPROCESSOR_0_STATUS, status as i32);
            }
            COPROCESSOR_0_CAUSE => self
                .registers
                .set_coprocessor_0(COPROCESSOR_0_CAUSE, (value as u32 & CAUSE_MASK) as i32),
            COPROCESSOR_0_CONFIG => self
                .registers
                .set_coprocessor_0(COPROCESSOR_0_CONFIG, (value as u32 & CONFIG_MASK) as i32),
            _ => {}
        }
    }

    /// The floating-point instructions.
    fn execute_floating_point_unit(
        &mut self,
        instruction: &Instruction,
        fs: usize,
        floating_point_target: usize,
        floating_point_destination: usize,
        display: bool,
    ) -> Flow {
        let registers: &Registers = &self.registers;
        let single = |n: usize| -> f32 { registers.floating_point_register_single(n) };
        match instruction.opcode {
            TOKEN_ABS_S_OPCODE => {
                let value: f32 = (f64::from(single(fs)).abs()) as f32;
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_ABS_D_OPCODE => {
                let value: f64 = self.registers.floating_point_register_double(fs).abs();
                self.registers
                    .set_floating_point_register_double(floating_point_destination, value);
            }
            TOKEN_ADD_S_OPCODE => {
                let value: f32 = single(fs) + single(floating_point_target);
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_ADD_D_OPCODE => self.double_operation(
                floating_point_destination,
                fs,
                floating_point_target,
                |a: f64, b: f64| a + b,
            ),
            TOKEN_SUB_S_OPCODE => {
                let value: f32 = single(fs) - single(floating_point_target);
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_SUB_D_OPCODE => self.double_operation(
                floating_point_destination,
                fs,
                floating_point_target,
                |a: f64, b: f64| a - b,
            ),
            TOKEN_MUL_S_OPCODE => {
                let value: f32 = single(fs) * single(floating_point_target);
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_MUL_D_OPCODE => self.double_operation(
                floating_point_destination,
                fs,
                floating_point_target,
                |a: f64, b: f64| a * b,
            ),
            TOKEN_DIV_S_OPCODE => {
                let value: f32 = single(fs) / single(floating_point_target);
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_DIV_D_OPCODE => self.double_operation(
                floating_point_destination,
                fs,
                floating_point_target,
                |a: f64, b: f64| a / b,
            ),
            TOKEN_SQRT_S_OPCODE => {
                let value: f32 = f64::from(single(fs)).sqrt() as f32;
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_SQRT_D_OPCODE => {
                let value: f64 = self.registers.floating_point_register_double(fs).sqrt();
                self.registers
                    .set_floating_point_register_double(floating_point_destination, value);
            }
            TOKEN_NEG_S_OPCODE => {
                let value: f32 = -single(fs);
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_NEG_D_OPCODE => {
                let value: f64 = -self.registers.floating_point_register_double(fs);
                self.registers
                    .set_floating_point_register_double(floating_point_destination, value);
            }
            TOKEN_MOV_S_OPCODE => {
                let bits: i32 = self.registers.floating_point_register_word(fs);
                self.registers
                    .set_floating_point_register_word(floating_point_destination, bits);
            }
            TOKEN_MOV_D_OPCODE => self.move_double(floating_point_destination, fs),
            TOKEN_BC1F_OPCODE | TOKEN_BC1FL_OPCODE | TOKEN_BC1T_OPCODE | TOKEN_BC1TL_OPCODE => {
                let condition_code: i32 = instruction.condition_code();
                let nullify: bool = (instruction.target_register() & 0x2) != 0;
                let true_branch: i32 = (instruction.target_register() & 0x1) as i32;
                let taken: bool =
                    self.registers.floating_point_condition_code(condition_code) == true_branch;
                self.branch(taken, instruction.branch_offset(), nullify, display);
            }
            TOKEN_C_F_S_OPCODE
            | TOKEN_C_UN_S_OPCODE
            | TOKEN_C_EQ_S_OPCODE
            | TOKEN_C_UEQ_S_OPCODE
            | TOKEN_C_OLT_S_OPCODE
            | TOKEN_C_OLE_S_OPCODE
            | TOKEN_C_ULT_S_OPCODE
            | TOKEN_C_ULE_S_OPCODE
            | TOKEN_C_SF_S_OPCODE
            | TOKEN_C_NGLE_S_OPCODE
            | TOKEN_C_SEQ_S_OPCODE
            | TOKEN_C_NGL_S_OPCODE
            | TOKEN_C_LT_S_OPCODE
            | TOKEN_C_NGE_S_OPCODE
            | TOKEN_C_LE_S_OPCODE
            | TOKEN_C_NGT_S_OPCODE => {
                let (a, b): (f32, f32) = (single(fs), single(floating_point_target));
                self.compare(instruction, f64::from(a), f64::from(b));
            }
            TOKEN_C_F_D_OPCODE
            | TOKEN_C_UN_D_OPCODE
            | TOKEN_C_EQ_D_OPCODE
            | TOKEN_C_UEQ_D_OPCODE
            | TOKEN_C_OLT_D_OPCODE
            | TOKEN_C_OLE_D_OPCODE
            | TOKEN_C_ULT_D_OPCODE
            | TOKEN_C_ULE_D_OPCODE
            | TOKEN_C_SF_D_OPCODE
            | TOKEN_C_NGLE_D_OPCODE
            | TOKEN_C_SEQ_D_OPCODE
            | TOKEN_C_NGL_D_OPCODE
            | TOKEN_C_LT_D_OPCODE
            | TOKEN_C_NGE_D_OPCODE
            | TOKEN_C_LE_D_OPCODE
            | TOKEN_C_NGT_D_OPCODE => {
                let a: f64 = self.registers.floating_point_register_double(fs);
                let b: f64 = self
                    .registers
                    .floating_point_register_double(floating_point_target);
                self.compare(instruction, a, b);
            }
            TOKEN_CFC1_OPCODE => {
                self.registers.general_purpose_registers[instruction.target_register() & 31] =
                    *self.registers.cop[1].get(fs).unwrap_or(&0)
            }
            TOKEN_CTC1_OPCODE => {
                let value: i32 =
                    self.registers.general_purpose_registers[instruction.target_register() & 31];
                if let Some(slot) = self.registers.cop[1].get_mut(fs) {
                    *slot = value;
                }
                // FIR is read only (but the C engine writes it anyway).
                if fs == FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER
                    && value as u32 & !FLOATING_POINT_CONTROL_STATUS_MASK != 0
                {
                    // Trying to set a mode the FPU does not support.
                    self.raise_exception(ExceptionCode::FloatingPointException);
                }
            }
            TOKEN_CEIL_W_D_OPCODE => {
                let value: f64 = self.registers.floating_point_register_double(fs);
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value.ceil()),
                );
            }
            TOKEN_CEIL_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value.ceil()),
                );
            }
            TOKEN_FLOOR_W_D_OPCODE => {
                let value: f64 = self.registers.floating_point_register_double(fs);
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value.floor()),
                );
            }
            TOKEN_FLOOR_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value.floor()),
                );
            }
            TOKEN_ROUND_W_D_OPCODE => {
                // The C engine rounds by adding 0.5 and truncating.
                let value: f64 = self.registers.floating_point_register_double(fs);
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value + 0.5),
                );
            }
            TOKEN_ROUND_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value + 0.5),
                );
            }
            TOKEN_TRUNC_W_D_OPCODE | TOKEN_CVT_W_D_OPCODE => {
                let value: f64 = self.registers.floating_point_register_double(fs);
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value),
                );
            }
            TOKEN_TRUNC_W_S_OPCODE | TOKEN_CVT_W_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.registers.set_floating_point_register_word(
                    floating_point_destination,
                    f64_to_i32_like_c(value),
                );
            }
            TOKEN_CVT_D_S_OPCODE => {
                let value: f64 = f64::from(single(fs));
                self.registers
                    .set_floating_point_register_double(floating_point_destination, value);
            }
            TOKEN_CVT_D_W_OPCODE => {
                let value: f64 = f64::from(self.registers.floating_point_register_word(fs));
                self.registers
                    .set_floating_point_register_double(floating_point_destination, value);
            }
            TOKEN_CVT_S_D_OPCODE => {
                let value: f32 = self.registers.floating_point_register_double(fs) as f32;
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_CVT_S_W_OPCODE => {
                let value: f32 = self.registers.floating_point_register_word(fs) as f32;
                self.registers
                    .set_floating_point_register_single(floating_point_destination, value);
            }
            TOKEN_LDC1_OPCODE => {
                let address: u32 = self.effective_address(instruction);
                if address & 3 != 0 {
                    self.raise_exception(ExceptionCode::AddressErrorLoad);
                    self.registers
                        .set_coprocessor_0(COPROCESSOR_0_BAD_VIRTUAL_ADDRESS, address as i32);
                }
                let low: i32 = self.memory_read_word(address);
                self.load(
                    LoadDestination::FloatingPointRegister(floating_point_target),
                    low,
                );
                let high: i32 = self.memory_read_word(address.wrapping_add(4));
                self.load(
                    LoadDestination::FloatingPointRegister(floating_point_target + 1),
                    high,
                );
            }
            TOKEN_LWC1_OPCODE => {
                let value: i32 = self.memory_read_word(self.effective_address(instruction));
                self.load(
                    LoadDestination::FloatingPointRegister(floating_point_target),
                    value,
                );
            }
            TOKEN_SDC1_OPCODE => {
                let bits: u64 = self
                    .registers
                    .floating_point_register_double(instruction.target_register())
                    .to_bits();
                let address: u32 = self.effective_address(instruction);
                if address & 3 != 0 {
                    self.raise_exception(ExceptionCode::AddressErrorLoad);
                    self.registers
                        .set_coprocessor_0(COPROCESSOR_0_BAD_VIRTUAL_ADDRESS, address as i32);
                }
                self.memory_write_word(address, bits as i32);
                self.memory_write_word(address.wrapping_add(4), (bits >> 32) as i32);
            }
            TOKEN_SWC1_OPCODE => {
                let bits: i32 = self
                    .registers
                    .floating_point_register_word(instruction.target_register());
                self.memory_write_word(self.effective_address(instruction), bits);
            }
            TOKEN_MFC1_OPCODE => {
                self.registers.general_purpose_registers[instruction.target_register() & 31] =
                    self.registers.floating_point_register_word(fs)
            }
            TOKEN_MTC1_OPCODE => {
                let bits: i32 =
                    self.registers.general_purpose_registers[instruction.target_register() & 31];
                self.registers.set_floating_point_register_word(fs, bits);
            }
            TOKEN_MOVF_OPCODE | TOKEN_MOVT_OPCODE => {
                let condition: i32 = self
                    .registers
                    .floating_point_condition_code(instruction.condition_code());
                if (condition != 0) == (instruction.opcode == TOKEN_MOVT_OPCODE) {
                    self.registers.general_purpose_registers
                        [instruction.destination_register() & 31] = self
                        .registers
                        .general_purpose_registers[instruction.source_register() & 31];
                }
            }
            TOKEN_MOVF_S_OPCODE | TOKEN_MOVT_S_OPCODE => {
                let condition: i32 = self
                    .registers
                    .floating_point_condition_code(instruction.condition_code());
                if (condition != 0) == (instruction.opcode == TOKEN_MOVT_S_OPCODE) {
                    let bits: i32 = self.registers.floating_point_register_word(fs);
                    self.registers
                        .set_floating_point_register_word(floating_point_destination, bits);
                }
            }
            TOKEN_MOVF_D_OPCODE | TOKEN_MOVT_D_OPCODE => {
                let condition: i32 = self
                    .registers
                    .floating_point_condition_code(instruction.condition_code());
                if (condition != 0) == (instruction.opcode == TOKEN_MOVT_D_OPCODE) {
                    self.move_double(floating_point_destination, fs);
                }
            }
            TOKEN_MOVN_S_OPCODE | TOKEN_MOVZ_S_OPCODE => {
                let zero: bool = self.registers.general_purpose_registers
                    [instruction.target_register() & 31]
                    == 0;
                if zero == (instruction.opcode == TOKEN_MOVZ_S_OPCODE) {
                    let bits: i32 = self.registers.floating_point_register_word(fs);
                    self.registers
                        .set_floating_point_register_word(floating_point_destination, bits);
                }
            }
            TOKEN_MOVN_D_OPCODE | TOKEN_MOVZ_D_OPCODE => {
                let zero: bool = self.registers.general_purpose_registers
                    [instruction.target_register() & 31]
                    == 0;
                if zero == (instruction.opcode == TOKEN_MOVZ_D_OPCODE) {
                    self.move_double(floating_point_destination, fs);
                }
            }
            _ => fatal_error(&format!(
                "Unknown instruction type: {}\n",
                instruction.opcode
            )),
        }
        Flow::Next
    }

    /// `fd = op(fs, ft)` in double precision.
    fn double_operation(
        &mut self,
        floating_point_destination: usize,
        fs: usize,
        floating_point_target: usize,
        op: impl Fn(f64, f64) -> f64,
    ) {
        // As in C, the destination is checked for an odd number first.
        if floating_point_destination & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        let a: f64 = self.registers.floating_point_register_double(fs);
        let b: f64 = self
            .registers
            .floating_point_register_double(floating_point_target);
        self.registers
            .set_floating_point_register_double(floating_point_destination, op(a, b));
    }

    /// `mov.d`: copy a double register's bits.
    fn move_double(&mut self, floating_point_destination: usize, fs: usize) {
        if floating_point_destination & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        let value: f64 = self.registers.floating_point_register_double(fs);
        self.registers
            .set_floating_point_register_double(floating_point_destination, value);
    }

    /// `c.cond.fmt`: compare and set a condition code.  The condition
    /// bits (in the `rs` field) say which outcomes make it true; with the
    /// "invalid" bit, a NaN operand raises the floating-point exception.
    fn compare(&mut self, instruction: &Instruction, a: f64, b: f64) {
        let condition: u32 = instruction.source_register() as u32;
        let condition_code: i32 = (instruction.floating_point_destination() >> 2) as i32;
        if a.is_nan() || b.is_nan() {
            if condition & 0x8 != 0 {
                self.raise_exception(ExceptionCode::FloatingPointException);
                return;
            }
            self.set_floating_point_unit_condition_code(
                condition,
                condition_code,
                false,
                false,
                true,
            );
        } else {
            self.set_floating_point_unit_condition_code(
                condition,
                condition_code,
                a < b,
                a == b,
                false,
            );
        }
    }
}
