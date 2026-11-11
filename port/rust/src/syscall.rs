//! System calls and the exception messages (`syscall.c`).
//!
//! A program asks the simulator for a service with `syscall`: the service
//! number in `$v0`, arguments in `$a0`..`$a2` (or `$f12`), a result in
//! `$v0` (or `$f0`).
//!
//! | `$v0` | service      | `$v0` | service       |
//! |-------|--------------|-------|---------------|
//! | 1     | print_int    | 10    | exit          |
//! | 2     | print_float  | 11    | print_char    |
//! | 3     | print_double | 12    | read_char     |
//! | 4     | print_string | 13    | open          |
//! | 5     | read_int     | 14    | read          |
//! | 6     | read_float   | 15    | write         |
//! | 7     | read_double  | 16    | close         |
//! | 8     | read_string  | 17    | exit2 (`$a0`) |
//! | 9     | sbrk         |       |               |

use crate::arith::c_atof;
use crate::cformat::{format_f, format_g};
use crate::console::{close_fd, open_path, read_fd, read_input, read_int_input, write_fd};
use crate::machine::{error, run_error, write_stdout, Machine};
use crate::registers::*;
use crate::run::{ExcCode, EXCEPTION_ADDR};

impl Machine {
    /// The memory at `addr` to the end of its segment, for the syscalls
    /// that read or write memory directly (`mem_reference`); a run-time
    /// error if `addr` is in no segment.  The text segment holds
    /// instructions, not bytes, so it reads as empty.
    fn mem_reference(&self, addr: u32) -> &[u8] {
        if !self.mem.is_mapped(addr) {
            run_error("Memory address out of bounds\n");
        }
        self.mem.bytes_from(addr).unwrap_or(&[])
    }

    /// Check that `addr` is in some segment, as `mem_reference` does.
    fn check_mapped(&self, addr: u32) {
        if !self.mem.is_mapped(addr) {
            run_error("Memory address out of bounds\n");
        }
    }

    /// The NUL-terminated string at `addr`.
    fn string_at(&self, addr: u32) -> Vec<u8> {
        let bytes: &[u8] = self.mem_reference(addr);
        let end: usize = bytes
            .iter()
            .position(|b: &u8| *b == 0)
            .unwrap_or(bytes.len());
        bytes[..end].to_vec()
    }

    /// Carry out the syscall in `$v0` (`do_syscall`).  Returns false if the
    /// program exits.
    pub fn do_syscall(&mut self) -> bool {
        let a0: i32 = self.regs.gpr[REG_A0];
        let a1: i32 = self.regs.gpr[REG_A1];
        let a2: i32 = self.regs.gpr[REG_A2];
        match self.regs.gpr[REG_V0] {
            1 => write_stdout(a0.to_string().as_bytes()),
            2 => {
                let value: f32 = self.regs.fpr_s(REG_FA0);
                write_stdout(format_f(8, f64::from(value)).as_bytes());
            }
            3 => {
                let value: f64 = self.regs.double_view(REG_FA0 / 2);
                write_stdout(format_g(false, 18, value).as_bytes());
            }
            4 => {
                let text: Vec<u8> = self.string_at(a0 as u32);
                write_stdout(&text);
            }
            5 => {
                let value: Option<i64> = read_int_input();
                self.regs.gpr[REG_V0] = value.unwrap_or(0) as i32;
                self.regs.gpr[REG_A3] = i32::from(value.is_none());
            }
            6 => {
                let mut text: [u8; 256] = [0; 256];
                let count: usize = read_input(&mut text, 256);
                let value: f32 = c_atof(&text[..count]) as f32;
                self.regs.set_fpr_s(0, value);
            }
            7 => {
                let mut text: [u8; 256] = [0; 256];
                let count: usize = read_input(&mut text, 256);
                self.regs.set_double_view(0, c_atof(&text[..count]));
            }
            8 => {
                let addr: u32 = a0 as u32;
                self.check_mapped(addr);
                let count: usize = match self.mem.bytes_from_mut(addr) {
                    Some(memory) => read_input(memory, a1),
                    None => {
                        let mut scratch: Vec<u8> = Vec::new();
                        read_input(&mut scratch, a1)
                    }
                };
                self.regs.gpr[REG_A3] = i32::from(count == 0);
            }
            9 => {
                let old_top: u32 = self.mem.data_top;
                self.expand_data(a0);
                self.regs.gpr[REG_V0] = old_top as i32;
            }
            10 => {
                self.return_value = 0;
                return false;
            }
            11 => write_stdout(&[a0 as u8]),
            12 => {
                let mut text: [u8; 2] = [0; 2];
                let count: usize = read_input(&mut text, 2);
                // The byte as 0..255, so it never looks like the -1 of EOF.
                self.regs.gpr[REG_V0] = if count == 0 { -1 } else { i32::from(text[0]) };
            }
            13 => {
                let path: Vec<u8> = self.string_at(a0 as u32);
                self.regs.gpr[REG_V0] = open_path(&path, a1, a2);
            }
            14 => {
                self.check_mapped((a1 as u32).wrapping_add(a2 as u32).wrapping_sub(1));
                self.check_mapped(a1 as u32);
                let length: usize = a2 as u32 as usize;
                let result: isize = match self.mem.bytes_from_mut(a1 as u32) {
                    Some(memory) => {
                        let end: usize = length.min(memory.len());
                        read_fd(a0, &mut memory[..end])
                    }
                    None => read_fd(a0, &mut []),
                };
                self.regs.gpr[REG_V0] = result as i32;
            }
            15 => {
                self.check_mapped((a1 as u32).wrapping_add(a2 as u32).wrapping_sub(1));
                let memory: &[u8] = self.mem_reference(a1 as u32);
                let length: usize = (a2 as u32 as usize).min(memory.len());
                let result: isize = write_fd(a0, &memory[..length]);
                self.regs.gpr[REG_V0] = result as i32;
            }
            16 => self.regs.gpr[REG_V0] = close_fd(a0),
            17 => {
                self.return_value = a0;
                return false;
            }
            number => run_error(&format!("Unknown system call: {number}\n")),
        }
        true
    }

    /// Report an exception on stderr and send the program to the exception
    /// handler (`handle_exception`).  A breakpoint is left for the REPL.
    pub fn handle_exception(&mut self) {
        let code: u32 = self.regs.exc_code();
        let quiet: bool = self.options.quiet;
        if !quiet && code != ExcCode::Int as u32 {
            error(&format!(
                "Exception occurred at PC=0x{:08x}\n",
                self.regs.cp0(CP0_EPC) as u32
            ));
        }
        self.exception_occurred = false;
        self.regs.pc = EXCEPTION_ADDR;
        let bad_vaddr: u32 = self.regs.cp0(CP0_BADVADDR) as u32;
        let message: Option<String> = match code {
            0 => None,
            4 => Some(format!(
                "  Unaligned address in instruction/data fetch: 0x{bad_vaddr:08x}\n"
            )),
            5 => Some(format!("  Unaligned address in store: 0x{bad_vaddr:08x}\n")),
            6 => Some(format!("  Bad address in text read: 0x{bad_vaddr:08x}\n")),
            7 => Some(format!(
                "  Bad address in data/stack read: 0x{bad_vaddr:08x}\n"
            )),
            8 => Some("  Error in syscall\n".to_string()),
            9 => {
                self.exception_occurred = false;
                return;
            }
            10 => Some("  Reserved instruction execution\n".to_string()),
            // The C engine's message spells it "unuable".
            11 => Some("  Coprocessor unuable\n".to_string()),
            12 => Some("  Arithmetic overflow\n".to_string()),
            13 => Some("  Trap\n".to_string()),
            15 => Some("  Floating point\n".to_string()),
            other => Some(format!("Unknown exception: {other}\n")),
        };
        if let Some(text) = message {
            if !quiet {
                error(&text);
            }
        }
    }
}
