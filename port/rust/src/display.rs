//! Printing the machine's contents: the register dump, memory dumps, and
//! the text-segment listing (`display-utils.c`).

use crate::cformat::format_register_column;
use crate::instruction::{bprint, InstRef};
use crate::machine::{error, error_bytes, Machine};
use crate::memory::{DATA_BOTTOM, KERNEL_DATA_BOTTOM, KERNEL_TEXT_BOTTOM, STACK_TOP, TEXT_BOTTOM};
use crate::registers::*;

/// Bytes of memory per line of a memory dump.
const BYTES_PER_LINE: u32 = 16;

impl Machine {
    /// The register dump of `print_all_regs` (`format_registers`).
    pub fn format_registers(
        &self,
        out: &mut Vec<u8>,
        general_purpose_registers_hex: bool,
        floating_point_register_hex: bool,
    ) {
        let registers: &Registers = &self.registers;
        bprint!(out, " PC      = {:08x}   ", registers.program_counter);
        bprint!(
            out,
            "EPC     = {:08x}  ",
            registers.coprocessor_0(COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER) as u32
        );
        bprint!(
            out,
            " Cause   = {:08x}  ",
            registers.coprocessor_0(COPROCESSOR_0_CAUSE) as u32
        );
        bprint!(
            out,
            " BadVAddr= {:08x}\n",
            registers.coprocessor_0(COPROCESSOR_0_BAD_VIRTUAL_ADDRESS) as u32
        );
        bprint!(
            out,
            " Status  = {:08x}   ",
            registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32
        );
        bprint!(out, "HI      = {:08x}  ", registers.hi as u32);
        bprint!(out, " LO      = {:08x}\n", registers.lo as u32);
        out.extend_from_slice(b"\t\t\t\t General Registers\n");
        let general_purpose_registers = |out: &mut Vec<u8>, n: usize| -> () {
            if general_purpose_registers_hex {
                bprint!(
                    out,
                    "R{:<2} ({:>2}) = {:08x}",
                    n,
                    REGISTER_NAMES[n],
                    registers.general_purpose_registers[n] as u32
                );
            } else {
                bprint!(
                    out,
                    "R{:<2} ({:>2}) = {:<10}",
                    n,
                    REGISTER_NAMES[n],
                    registers.general_purpose_registers[n]
                );
            }
        };
        let general_purpose_registers_fill: &[u8] = if general_purpose_registers_hex {
            b"  "
        } else {
            b" "
        };
        for i in 0..8 {
            for column in 0..4 {
                general_purpose_registers(out, i + 8 * column);
                if column < 3 {
                    out.extend_from_slice(general_purpose_registers_fill);
                }
            }
            out.push(b'\n');
        }
        bprint!(
            out,
            "\n FIR    = {:08x}   ",
            registers.cop[1][FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER] as u32
        );
        bprint!(
            out,
            " FCSR    = {:08x}   ",
            registers.floating_point_control_status_register() as u32
        );
        out.extend_from_slice(b"\t\t\t      Double Floating Point Registers\n");
        for i in 0..4 {
            for column in 0..4 {
                let index: usize = i + 4 * column;
                let number: usize = 2 * index;
                if floating_point_register_hex {
                    bprint!(
                        out,
                        "FP{:<2}={:08x},{:08x}",
                        number,
                        registers.floating_point_register[2 * index],
                        registers.floating_point_register[2 * index + 1]
                    );
                } else {
                    bprint!(
                        out,
                        "FP{:<2} = {}",
                        number,
                        format_register_column(registers.double_view(index))
                    );
                }
                out.push(if column < 3 { b' ' } else { b'\n' });
            }
        }
        out.extend_from_slice(b"\t\t\t      Single Floating Point Registers\n");
        for i in 0..8 {
            for column in 0..4 {
                let n: usize = i + 8 * column;
                if floating_point_register_hex {
                    bprint!(
                        out,
                        "FP{:<2}={:08x}",
                        n,
                        registers.floating_point_register[n]
                    );
                } else {
                    bprint!(
                        out,
                        "FP{:<2} = {}",
                        n,
                        format_register_column(f64::from(
                            registers.floating_point_register_single(n)
                        ))
                    );
                }
                out.push(if column < 3 { b' ' } else { b'\n' });
            }
        }
    }

    /// The listing lines of the instructions from `from` to `to`
    /// (`format_instructions`); empty slots are skipped.
    pub fn format_instructions(&mut self, out: &mut Vec<u8>, from: u32, to: u32) {
        let mut address: u32 = from;
        while address < to {
            let instruction: Option<InstRef> = self.memory_read_instruction(address);
            if instruction.is_some() {
                self.format_an_instruction(out, instruction.as_ref(), address);
            }
            address = address.wrapping_add(4);
        }
    }

    /// The REPL's `disasm`: list the user text segment
    /// (`print_text_listing`).
    pub fn print_text_listing(&mut self) {
        let mut text: Vec<u8> = Vec::new();
        let top: u32 = self.mem.text_top;
        self.format_instructions(&mut text, TEXT_BOTTOM, top);
        if text.is_empty() {
            error("No program loaded.\n");
            return;
        }
        error("\n\tUSER TEXT SEGMENT\n");
        text.push(b'\n');
        error_bytes(&text);
    }

    /// The data, stack and kernel data segments, for `-full_dump`
    /// (`format_data_segments`).
    pub fn format_data_segments(&mut self, out: &mut Vec<u8>) {
        out.extend_from_slice(b"\tDATA\n");
        let data_top: u32 = self.mem.data_top;
        self.format_memory(out, DATA_BOTTOM, data_top);
        out.extend_from_slice(b"\n\tSTACK\n");
        let sp: u32 = self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32 & !3;
        self.format_memory(out, sp, STACK_TOP);
        out.extend_from_slice(b"\n\tKERNEL DATA\n");
        let kernel_data_top: u32 = self.mem.kernel_data_top;
        self.format_memory(out, KERNEL_DATA_BOTTOM, kernel_data_top);
    }

    /// Memory from `from` to `to`, four words a line; four or more zero
    /// words in a row are shown as one range (`format_memory`).
    pub fn format_memory(&mut self, out: &mut Vec<u8>, from: u32, to: u32) {
        let mut i: u32 = from.wrapping_add(3) & !3;
        i = self.format_partial_line(out, i);
        while i < to {
            let mut zeros: u32 = 0;
            while i.wrapping_add(zeros * 4) < to {
                if self.memory_read_word(i.wrapping_add(zeros * 4)) != 0 {
                    break;
                }
                zeros += 1;
            }
            if zeros >= 4 {
                bprint!(
                    out,
                    "[0x{i:08x}]...[0x{:08x}]\t0x00000000\n",
                    i.wrapping_add(zeros * 4)
                );
                i = i.wrapping_add(zeros * 4);
                i = self.format_partial_line(out, i);
            } else {
                bprint!(out, "[0x{i:08x}]\t\t      ");
                loop {
                    let value: i32 = self.memory_read_word(i);
                    bprint!(out, "  0x{:08x}", value as u32);
                    i = i.wrapping_add(4);
                    if i % BYTES_PER_LINE == 0 {
                        break;
                    }
                }
                out.push(b'\n');
            }
        }
    }

    /// The words from `addr` to the end of its 16-byte line, if `addr` is
    /// not at the start of one (`format_partial_line`).
    fn format_partial_line(&mut self, out: &mut Vec<u8>, address: u32) -> u32 {
        let mut address: u32 = address;
        if address % BYTES_PER_LINE != 0 {
            bprint!(out, "[0x{address:08x}]\t\t      ");
            while address % BYTES_PER_LINE != 0 {
                let value: i32 = self.memory_read_word(address);
                bprint!(out, "  0x{:08x}", value as u32);
                address = address.wrapping_add(4);
            }
            out.push(b'\n');
        }
        address
    }

    /// The kernel text listing for `-full_dump`.
    pub fn format_kernel_text(&mut self, out: &mut Vec<u8>) {
        let top: u32 = self.mem.kernel_text_top;
        self.format_instructions(out, KERNEL_TEXT_BOTTOM, top);
    }
}
