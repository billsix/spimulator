//! Placing data: the data directives' store helpers (`data.c`).
//!
//! The assembler keeps a "next datum" address for the user data segment
//! and one for the kernel data segment.  `.word`, `.half`, `.float` and
//! `.double` first align it to their size (unless `.align 0` turned that
//! off), then store their values there and move it on.
//!
//! The first 64K of the user data segment is the small-data area, which
//! `$gp` points into the middle of, so a 16-bit offset off `$gp` reaches
//! all of it; user data starts after it.

use crate::machine::Machine;
use crate::memory::DATA_BOT;
use crate::registers::REG_GP;

impl Machine {
    /// Send data to the kernel or the user data segment.
    pub fn user_kernel_data_segment(&mut self, to_kernel: bool) {
        self.asm.data_in_kernel = to_kernel;
    }

    /// After a file: back to user data, alignment on.
    pub fn end_of_assembly_file(&mut self) {
        self.asm.data_in_kernel = false;
        self.asm.data_auto_align = true;
    }

    /// Start the user data segment at `addr`, after the 64K small-data
    /// area, and point `$gp` at the middle of that area
    /// (`data_begins_at_point`).
    pub fn data_begins_at_point(&mut self, addr: u32) {
        if self.options.bare_machine {
            self.asm.next_data_pc = addr;
        } else {
            self.asm.next_gp_item_addr = addr;
            self.mem.gp_midpoint = addr + 32 * 1024;
            self.regs.gpr[REG_GP] = self.mem.gp_midpoint as i32;
            self.asm.next_data_pc = addr + 64 * 1024;
        }
    }

    /// Start the kernel data segment at `addr`.
    pub fn k_data_begins_at_point(&mut self, addr: u32) {
        self.asm.next_k_data_pc = addr;
    }

    /// Advance the data address to a multiple of `2^alignment` and move
    /// this line's labels along (`align_data`).  `.align 0` turns the
    /// automatic alignment off instead.
    pub fn align_data(&mut self, alignment: i32) {
        if alignment == 0 {
            self.asm.data_auto_align = false;
            return;
        }
        let round = |pc: u32| -> u32 {
            pc.wrapping_add(1u32.wrapping_shl(alignment as u32))
                .wrapping_sub(1)
                & 0xffff_ffffu32.wrapping_shl(alignment as u32)
        };
        if self.asm.data_in_kernel {
            self.asm.next_k_data_pc = round(self.asm.next_k_data_pc);
            self.fix_current_label_address(self.asm.next_k_data_pc);
        } else {
            self.asm.next_data_pc = round(self.asm.next_data_pc);
            self.fix_current_label_address(self.asm.next_data_pc);
        }
    }

    /// Align the data address unless `.align 0` turned that off.
    pub fn set_data_alignment(&mut self, alignment: i32) {
        if self.asm.data_auto_align {
            self.align_data(alignment);
        }
    }

    /// Turn automatic alignment back on (`.data`).
    pub fn enable_data_alignment(&mut self) {
        self.asm.data_auto_align = true;
    }

    /// Set the data address (`.data ADDR`).
    pub fn set_data_pc(&mut self, addr: u32) {
        if self.asm.data_in_kernel {
            self.asm.next_k_data_pc = addr;
        } else {
            self.asm.next_data_pc = addr;
        }
    }

    /// The address of the next datum (`current_data_pc`).
    pub fn current_data_pc(&self) -> u32 {
        if self.asm.data_in_kernel {
            self.asm.next_k_data_pc
        } else {
            self.asm.next_data_pc
        }
    }

    /// Move the data address on by `delta` bytes, growing the segment in
    /// 64K steps if it runs past the end (`increment_data_pc`).
    pub fn increment_data_pc(&mut self, delta: i32) {
        let round_64k = |bytes: u32| -> i32 { (bytes.wrapping_add(0xffff) & !0xffff) as i32 };
        if self.asm.data_in_kernel {
            self.asm.next_k_data_pc = self.asm.next_k_data_pc.wrapping_add(delta as u32);
            if self.mem.k_data_top <= self.asm.next_k_data_pc {
                let missing: u32 = self.asm.next_k_data_pc - self.mem.k_data_top + 1;
                self.expand_k_data(round_64k(missing));
            }
        } else {
            self.asm.next_data_pc = self.asm.next_data_pc.wrapping_add(delta as u32);
            if self.mem.data_top <= self.asm.next_data_pc {
                let missing: u32 = self.asm.next_data_pc - self.mem.data_top + 1;
                self.expand_data(round_64k(missing));
            }
        }
    }

    /// Store a string, and a NUL after it for `.asciiz` (`store_string`).
    pub fn store_string(&mut self, bytes: &[u8], null_terminate: bool) {
        let at: u32 = self.current_data_pc();
        self.asm.data_store_depth += 1;
        for &byte in bytes {
            self.store_byte(i32::from(byte as i8));
        }
        if null_terminate {
            self.store_byte(0);
        }
        self.asm.data_store_depth -= 1;
        if self.asm.data_store_depth == 0 {
            self.listing_data_string(at, bytes.len(), null_terminate);
        }
    }

    /// Store a byte (`store_byte`).
    pub fn store_byte(&mut self, value: i32) {
        let at: u32 = self.current_data_pc();
        self.mem_write_byte(at, value);
        self.increment_data_pc(1);
        if self.asm.data_store_depth == 0 {
            self.listing_data_byte(at, value);
        }
    }

    /// Store a halfword; at an odd address, as two bytes (`store_half`).
    pub fn store_half(&mut self, value: i32) {
        let at: u32 = self.current_data_pc();
        let outer: bool = self.asm.data_store_depth == 0;
        self.asm.data_store_depth += 1;
        if at & 1 != 0 {
            self.store_byte(value & 0xff);
            self.store_byte((value >> 8) & 0xff);
        } else {
            self.mem_write_half(at, value);
            self.increment_data_pc(2);
        }
        self.asm.data_store_depth -= 1;
        if outer {
            self.listing_data_half(at, value);
        }
    }

    /// Store a word; at an unaligned address, as two halves (`store_word`).
    pub fn store_word(&mut self, value: i32) {
        let at: u32 = self.current_data_pc();
        let outer: bool = self.asm.data_store_depth == 0;
        self.asm.data_store_depth += 1;
        if at & 3 != 0 {
            self.store_half(value & 0xffff);
            self.store_half((value >> 16) & 0xffff);
        } else {
            self.mem_write_word(at, value);
            self.increment_data_pc(4);
        }
        self.asm.data_store_depth -= 1;
        if outer {
            self.listing_data_word(at, value);
        }
    }

    /// Store a double as its two words, low word first (`store_double`).
    pub fn store_double(&mut self, value: f64) {
        let at: u32 = self.current_data_pc();
        let outer: bool = self.asm.data_store_depth == 0;
        self.asm.data_store_depth += 1;
        let bits: u64 = value.to_bits();
        let (low, high): (i32, i32) = (bits as i32, (bits >> 32) as i32);
        if at & 7 != 0 {
            self.store_word(low);
            self.store_word(high);
        } else {
            let pc: u32 = self.current_data_pc();
            self.mem_write_word(pc, low);
            self.increment_data_pc(4);
            let pc: u32 = self.current_data_pc();
            self.mem_write_word(pc, high);
            self.increment_data_pc(4);
        }
        self.asm.data_store_depth -= 1;
        if outer {
            self.listing_data_double(at, value);
        }
    }

    /// Store a float, rounded from the literal's double (`store_float`).
    pub fn store_float(&mut self, value: f64) {
        let bits: i32 = (value as f32).to_bits() as i32;
        let at: u32 = self.current_data_pc();
        if at & 3 != 0 {
            self.store_half(bits & 0xffff);
            self.store_half((bits >> 16) & 0xffff);
        } else {
            self.mem_write_word(at, bits);
            self.increment_data_pc(4);
        }
    }

    /// The first address of user data (where a `-bare` dump starts).
    pub fn data_bot(&self) -> u32 {
        DATA_BOT
    }
}
