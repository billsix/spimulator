//! The register file (`include/registers.h`, `memory.c`'s register
//! globals, `display-utils.c`'s register names).
//!
//! A MIPS32 processor has 32 general registers, the HI and LO registers
//! that `mult` and `div` fill, the program counter, and coprocessors:
//! coprocessor 0 handles exceptions (Status, Cause, EPC, ...), and
//! coprocessor 1 is the floating-point unit with 32 single-precision
//! registers that pair up into 16 double-precision ones.

use crate::machine::run_error;

/// The names the disassembler prints for the 32 general registers.
/// Register 0 is `r0` (not `zero`), as in the C engine's listings.
pub const REG_NAMES: [&str; 32] = [
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
];

/// `$v0`: syscall number and result.
pub const REG_V0: usize = 2;
/// `$a0`, the first argument register.
pub const REG_A0: usize = 4;
/// `$a1`.
pub const REG_A1: usize = 5;
/// `$a2`.
pub const REG_A2: usize = 6;
/// `$a3`: the syscalls' error flag.
pub const REG_A3: usize = 7;
/// `$f12`, the floating-point argument of `print_float`/`print_double`.
pub const REG_FA0: usize = 12;
/// `$gp`, the global pointer.
pub const REG_GP: usize = 28;
/// `$sp`, the stack pointer.
pub const REG_SP: usize = 29;
/// `$ra`, the return address.
pub const REG_RA: usize = 31;

/// Coprocessor 0 register numbers.
pub const CP0_BADVADDR: usize = 8;
/// The Count register (a timer the C engine bumps every 10 ms).
pub const CP0_COUNT: usize = 9;
/// The Compare register (an interrupt when Count reaches it).
pub const CP0_COMPARE: usize = 11;
/// The Status register.
pub const CP0_STATUS: usize = 12;
/// The Cause register.
pub const CP0_CAUSE: usize = 13;
/// The Exception Program Counter.
pub const CP0_EPC: usize = 14;
/// The Config register.
pub const CP0_CONFIG: usize = 16;

/// Status: coprocessor-usable bits.
pub const STATUS_CU: u32 = 0xf000_0000;
/// Status: the interrupt mask.
pub const STATUS_IM: u32 = 0x0000_ff00;
/// Status: user mode.
pub const STATUS_UM: u32 = 0x0000_0010;
/// Status: exception level (set while handling an exception).
pub const STATUS_EXL: u32 = 0x0000_0002;
/// Status: interrupts enabled.
pub const STATUS_IE: u32 = 0x0000_0001;
/// The Status bits a program may write.
pub const STATUS_MASK: u32 = STATUS_CU | STATUS_UM | STATUS_IM | STATUS_EXL | STATUS_IE;

/// Cause: the exception happened in a branch delay slot.
pub const CAUSE_BD: u32 = 0x8000_0000;
/// Cause: pending interrupts.
pub const CAUSE_IP: u32 = 0x0000_ff00;
/// Cause: hardware interrupt 5 (the timer).
pub const CAUSE_IP7: u32 = 0x0000_8000;
/// Cause: the exception code field.
pub const CAUSE_EXCCODE: u32 = 0x0000_007c;
/// The Cause bits a program may write (the C engine's `CP0_Cause_Mask`).
pub const CAUSE_MASK: u32 = CAUSE_BD | CAUSE_IP | CAUSE_EXCCODE;
/// The Config bits a program may write.
pub const CONFIG_MASK: u32 = 0x0008_0000 | 0x0006_0000 | 0x0001_c000 | 0x0000_0380;

/// The FPU implementation register (read only).
pub const FIR_REG: usize = 0;
/// The FPU control and status register (holds the condition codes).
pub const FCSR_REG: usize = 31;
/// FIR: word, double and single formats are implemented.
pub const FIR_W_D_S: u32 = 0x0008000 | 0x0001000 | 0x0000800;
/// The FCSR bits a program may set (the condition codes).
pub const FCSR_MASK: u32 = 0xfe80_0000;

/// The machine's registers.
#[derive(Clone, Debug, Default)]
pub struct Registers {
    /// The 32 general registers.  Register 0 always reads 0: the run loop
    /// zeroes it before every instruction.
    pub gpr: [i32; 32],
    /// HI: the high word of a product, or the remainder of a division.
    pub hi: i32,
    /// LO: the low word of a product, or the quotient of a division.
    pub lo: i32,
    /// The program counter.
    pub pc: u32,
    /// The coprocessor registers; `cop[0]` is coprocessor 0 and `cop[1]`
    /// holds the FPU control registers (FIR, FCSR).
    pub cop: [[i32; 32]; 4],
    /// The coprocessor control registers (`cfc0`/`ctc0`).
    pub cop_control: [[i32; 32]; 4],
    /// The 32 floating-point registers as raw bits.  Single register `n` is
    /// `fpr[n]`; double register `n` (even) is `fpr[n]` (low word) and
    /// `fpr[n + 1]` (high word), as on a little-endian host.
    pub fpr: [u32; 32],
}

impl Registers {
    /// Coprocessor 0 register `n`.
    pub fn cp0(&self, n: usize) -> i32 {
        self.cop[0][n]
    }

    /// Set coprocessor 0 register `n`.
    pub fn set_cp0(&mut self, n: usize, value: i32) {
        self.cop[0][n] = value;
    }

    /// The exception code field of Cause.
    pub fn exc_code(&self) -> u32 {
        (self.cp0(CP0_CAUSE) as u32 & CAUSE_EXCCODE) >> 2
    }

    /// The FPU control and status register.
    pub fn fcsr(&self) -> i32 {
        self.cop[1][FCSR_REG]
    }

    /// Single-precision register `n` (`FPR_S`).  A register number past
    /// 31 (the assembler does not check `$f40`) reads as 0.
    pub fn fpr_s(&self, n: usize) -> f32 {
        f32::from_bits(self.fpr_w(n) as u32)
    }

    /// Set single-precision register `n` (`SET_FPR_S`).
    pub fn set_fpr_s(&mut self, n: usize, value: f32) {
        self.set_fpr_w(n, value.to_bits() as i32);
    }

    /// Register `n` read as a 32-bit integer (`FPR_W`).
    pub fn fpr_w(&self, n: usize) -> i32 {
        self.fpr.get(n).map_or(0, |bits: &u32| *bits as i32)
    }

    /// Set register `n` from a 32-bit integer (`SET_FPR_W`).
    pub fn set_fpr_w(&mut self, n: usize, value: i32) {
        if let Some(slot) = self.fpr.get_mut(n) {
            *slot = value as u32;
        }
    }

    /// Double register `n / 2` with no check that `n` is even (the C
    /// engine's `fp_double_view[n / 2]`, used by `print_double`).
    pub fn double_view(&self, index: usize) -> f64 {
        let low: u64 = u64::from(*self.fpr.get(2 * index).unwrap_or(&0));
        let high: u64 = u64::from(*self.fpr.get(2 * index + 1).unwrap_or(&0));
        f64::from_bits((high << 32) | low)
    }

    /// Set double register `n / 2` with no check that `n` is even.
    pub fn set_double_view(&mut self, index: usize, value: f64) {
        let bits: u64 = value.to_bits();
        if 2 * index + 1 < self.fpr.len() {
            self.fpr[2 * index] = bits as u32;
            self.fpr[2 * index + 1] = (bits >> 32) as u32;
        }
    }

    /// Double-precision register `n` (`FPR_D`).  An odd register number
    /// is a run-time error that stops the program, as in the C engine.
    pub fn fpr_d(&self, n: usize) -> f64 {
        if n & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        self.double_view(n / 2)
    }

    /// Set double-precision register `n` (`SET_FPR_D`).
    pub fn set_fpr_d(&mut self, n: usize, value: f64) {
        if n & 1 != 0 {
            run_error("Odd FP double register number\n");
        }
        self.set_double_view(n / 2, value);
    }

    /// The FCSR bit of floating-point condition code `n`: code 0 is bit
    /// 23, codes 1..7 are bits 25..31; any other `n` means code 0.
    fn cc_mask(n: i32) -> u32 {
        if n == 0 || n > 7 {
            1 << 23
        } else {
            1 << (25 + n - 1)
        }
    }

    /// Condition code `n`, 0 or 1 (`fcc`).
    pub fn fcc(&self, n: i32) -> i32 {
        i32::from(self.fcsr() as u32 & Self::cc_mask(n) != 0)
    }

    /// Set condition code `n` to `value` (`set_fcc`).
    pub fn set_fcc(&mut self, n: i32, value: i32) {
        let fcsr: u32 = self.fcsr() as u32;
        let updated: u32 = if value == 0 {
            fcsr & !Self::cc_mask(n)
        } else {
            fcsr | Self::cc_mask(n)
        };
        self.cop[1][FCSR_REG] = updated as i32;
    }
}
