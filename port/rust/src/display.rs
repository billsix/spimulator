//! Printing the machine's contents: the register dump, memory dumps, and
//! the text-segment listing (`display-utils.c`).

use crate::cformat::format_register_column;
use crate::instruction::{bprint, InstRef};
use crate::machine::{error, error_bytes, Machine};
use crate::memory::{DATA_BOT, K_DATA_BOT, K_TEXT_BOT, STACK_TOP, TEXT_BOT};
use crate::registers::*;

/// Bytes of memory per line of a memory dump.
const BYTES_PER_LINE: u32 = 16;

impl Machine {
    /// The register dump of `print_all_regs` (`format_registers`).
    pub fn format_registers(&self, out: &mut Vec<u8>, gpr_hex: bool, fpr_hex: bool) {
        let regs: &Registers = &self.regs;
        bprint!(out, " PC      = {:08x}   ", regs.pc);
        bprint!(out, "EPC     = {:08x}  ", regs.cp0(CP0_EPC) as u32);
        bprint!(out, " Cause   = {:08x}  ", regs.cp0(CP0_CAUSE) as u32);
        bprint!(out, " BadVAddr= {:08x}\n", regs.cp0(CP0_BADVADDR) as u32);
        bprint!(out, " Status  = {:08x}   ", regs.cp0(CP0_STATUS) as u32);
        bprint!(out, "HI      = {:08x}  ", regs.hi as u32);
        bprint!(out, " LO      = {:08x}\n", regs.lo as u32);
        out.extend_from_slice(b"\t\t\t\t General Registers\n");
        let gpr = |out: &mut Vec<u8>, n: usize| {
            if gpr_hex {
                bprint!(
                    out,
                    "R{:<2} ({:>2}) = {:08x}",
                    n,
                    REG_NAMES[n],
                    regs.gpr[n] as u32
                );
            } else {
                bprint!(out, "R{:<2} ({:>2}) = {:<10}", n, REG_NAMES[n], regs.gpr[n]);
            }
        };
        let gpr_fill: &[u8] = if gpr_hex { b"  " } else { b" " };
        for i in 0..8 {
            for column in 0..4 {
                gpr(out, i + 8 * column);
                if column < 3 {
                    out.extend_from_slice(gpr_fill);
                }
            }
            out.push(b'\n');
        }
        bprint!(out, "\n FIR    = {:08x}   ", regs.cop[1][FIR_REG] as u32);
        bprint!(out, " FCSR    = {:08x}   ", regs.fcsr() as u32);
        out.extend_from_slice(b"\t\t\t      Double Floating Point Registers\n");
        for i in 0..4 {
            for column in 0..4 {
                let index: usize = i + 4 * column;
                let number: usize = 2 * index;
                if fpr_hex {
                    bprint!(
                        out,
                        "FP{:<2}={:08x},{:08x}",
                        number,
                        regs.fpr[2 * index],
                        regs.fpr[2 * index + 1]
                    );
                } else {
                    bprint!(
                        out,
                        "FP{:<2} = {}",
                        number,
                        format_register_column(regs.double_view(index))
                    );
                }
                out.push(if column < 3 { b' ' } else { b'\n' });
            }
        }
        out.extend_from_slice(b"\t\t\t      Single Floating Point Registers\n");
        for i in 0..8 {
            for column in 0..4 {
                let n: usize = i + 8 * column;
                if fpr_hex {
                    bprint!(out, "FP{:<2}={:08x}", n, regs.fpr[n]);
                } else {
                    bprint!(
                        out,
                        "FP{:<2} = {}",
                        n,
                        format_register_column(f64::from(regs.fpr_s(n)))
                    );
                }
                out.push(if column < 3 { b' ' } else { b'\n' });
            }
        }
    }

    /// The listing lines of the instructions from `from` to `to`
    /// (`format_insts`); empty slots are skipped.
    pub fn format_insts(&mut self, out: &mut Vec<u8>, from: u32, to: u32) {
        let mut addr: u32 = from;
        while addr < to {
            let instruction: Option<InstRef> = self.mem_read_inst(addr);
            if instruction.is_some() {
                self.format_an_inst(out, instruction.as_ref(), addr);
            }
            addr = addr.wrapping_add(4);
        }
    }

    /// The REPL's `disasm`: list the user text segment
    /// (`print_text_listing`).
    pub fn print_text_listing(&mut self) {
        let mut text: Vec<u8> = Vec::new();
        let top: u32 = self.mem.text_top;
        self.format_insts(&mut text, TEXT_BOT, top);
        if text.is_empty() {
            error("No program loaded.\n");
            return;
        }
        error("\n\tUSER TEXT SEGMENT\n");
        text.push(b'\n');
        error_bytes(&text);
    }

    /// The data, stack and kernel data segments, for `-full_dump`
    /// (`format_data_segs`).
    pub fn format_data_segs(&mut self, out: &mut Vec<u8>) {
        out.extend_from_slice(b"\tDATA\n");
        let data_top: u32 = self.mem.data_top;
        self.format_mem(out, DATA_BOT, data_top);
        out.extend_from_slice(b"\n\tSTACK\n");
        let sp: u32 = self.regs.gpr[REG_SP] as u32 & !3;
        self.format_mem(out, sp, STACK_TOP);
        out.extend_from_slice(b"\n\tKERNEL DATA\n");
        let k_data_top: u32 = self.mem.k_data_top;
        self.format_mem(out, K_DATA_BOT, k_data_top);
    }

    /// Memory from `from` to `to`, four words a line; four or more zero
    /// words in a row are shown as one range (`format_mem`).
    pub fn format_mem(&mut self, out: &mut Vec<u8>, from: u32, to: u32) {
        let mut i: u32 = from.wrapping_add(3) & !3;
        i = self.format_partial_line(out, i);
        while i < to {
            let mut zeros: u32 = 0;
            while i.wrapping_add(zeros * 4) < to {
                if self.mem_read_word(i.wrapping_add(zeros * 4)) != 0 {
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
                    let value: i32 = self.mem_read_word(i);
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
    fn format_partial_line(&mut self, out: &mut Vec<u8>, addr: u32) -> u32 {
        let mut addr: u32 = addr;
        if addr % BYTES_PER_LINE != 0 {
            bprint!(out, "[0x{addr:08x}]\t\t      ");
            while addr % BYTES_PER_LINE != 0 {
                let value: i32 = self.mem_read_word(addr);
                bprint!(out, "  0x{:08x}", value as u32);
                addr = addr.wrapping_add(4);
            }
            out.push(b'\n');
        }
        addr
    }

    /// The kernel text listing for `-full_dump`.
    pub fn format_kernel_text(&mut self, out: &mut Vec<u8>) {
        let top: u32 = self.mem.k_text_top;
        self.format_insts(out, K_TEXT_BOT, top);
    }
}
