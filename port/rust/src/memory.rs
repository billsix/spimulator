//! Memory: the five segments, loads and stores, and the memory-mapped
//! console (`memory.c`, `include/memory.h`).
//!
//! The simulated address space has five pieces:
//!
//! | segment     | starts at    | holds                                  |
//! |-------------|--------------|----------------------------------------|
//! | text        | `0x00400000` | the program's instructions             |
//! | data        | `0x10000000` | static data; grows with `sbrk`         |
//! | stack       | below `0x80000000` | grows down automatically         |
//! | kernel text | `0x80000000` | the exception handler                  |
//! | kernel data | `0x90000000` | the exception handler's data           |
//!
//! Any other address is an error, except the four memory-mapped console
//! registers at `0xffff0000`.  Words are stored little-endian, as on the
//! hosts the C engine runs on.

use crate::instruction::{InstRef, Instruction};
use crate::machine::{error, fatal_error, run_error, Machine};
use crate::run::ExcCode;

/// Bottom of the user text segment.
pub const TEXT_BOT: u32 = 0x0040_0000;
/// Bottom of the user data segment.
pub const DATA_BOT: u32 = 0x1000_0000;
/// One past the top of the stack segment.
pub const STACK_TOP: u32 = 0x8000_0000;
/// Bottom of the kernel text segment.
pub const K_TEXT_BOT: u32 = 0x8000_0000;
/// Bottom of the kernel data segment.
pub const K_DATA_BOT: u32 = 0x9000_0000;
/// Bottom of the memory-mapped IO area.
pub const MM_IO_BOT: u32 = 0xffff_0000;

/// The receiver control register (a character is ready).
pub const RECV_CTRL_ADDR: u32 = 0xffff_0000;
/// The receiver data register.
pub const RECV_BUFFER_ADDR: u32 = 0xffff_0004;
/// The transmitter control register (ready to print).
pub const TRANS_CTRL_ADDR: u32 = 0xffff_0008;
/// The transmitter data register.
pub const TRANS_BUFFER_ADDR: u32 = 0xffff_000c;

/// Control register bit: ready.
const READY: u32 = 0x1;
/// Control register bit: interrupt enable.
const INT_ENABLE: u32 = 0x2;
/// Hardware interrupt level of the receiver.
const RECV_INT_LEVEL: u32 = 3;
/// Hardware interrupt level of the transmitter.
const TRANS_INT_LEVEL: u32 = 2;
/// IO intervals a received character stays in the buffer.
const RECV_INTERVAL: i32 = 100;
/// IO intervals it takes to print a character.
const TRANS_LATENCY: i32 = 100;

/// The memory of the machine.
#[derive(Debug, Default)]
pub struct Memory {
    /// The user text segment: one slot per word, an instruction or empty.
    pub text: Vec<Option<InstRef>>,
    /// One past the last text address.
    pub text_top: u32,
    /// The user data segment.
    pub data: Vec<u8>,
    /// One past the last data address.
    pub data_top: u32,
    /// The largest the data segment may grow.
    pub data_limit: i32,
    /// The stack segment; `stack[0]` is the byte at `stack_bot`.
    pub stack: Vec<u8>,
    /// The lowest stack address.
    pub stack_bot: u32,
    /// The largest the stack may grow.
    pub stack_limit: i32,
    /// The kernel text segment.
    pub k_text: Vec<Option<InstRef>>,
    /// One past the last kernel text address.
    pub k_text_top: u32,
    /// The kernel data segment.
    pub k_data: Vec<u8>,
    /// One past the last kernel data address.
    pub k_data_top: u32,
    /// The largest the kernel data segment may grow.
    pub k_data_limit: i32,
    /// The middle of the small data area that `$gp` points to.
    pub gp_midpoint: u32,
}

/// The memory-mapped console's registers (the statics of `memory.c`).
#[derive(Debug)]
pub struct MappedIo {
    recv_control: u32,
    recv_buffer: i32,
    recv_buffer_full_timer: i32,
    trans_control: u32,
    trans_buffer: i32,
    trans_buffer_full_timer: i32,
}

impl Default for MappedIo {
    fn default() -> Self {
        MappedIo {
            recv_control: 0,
            recv_buffer: 0,
            recv_buffer_full_timer: 0,
            trans_control: READY,
            trans_buffer: 0,
            trans_buffer_full_timer: 0,
        }
    }
}

/// Round `value` up to a multiple of 4.
fn round_up_4(value: i32) -> i32 {
    value.wrapping_add(3) & !3
}

/// A segment size as a byte count (a negative size is empty).
fn size_of(bytes: i32) -> usize {
    usize::try_from(bytes).unwrap_or(0)
}

/// A little-endian word from four bytes of `segment`.
fn read_le32(segment: &[u8], offset: usize) -> i32 {
    let bytes: [u8; 4] = [
        segment[offset],
        segment[offset + 1],
        segment[offset + 2],
        segment[offset + 3],
    ];
    i32::from_le_bytes(bytes)
}

impl Memory {
    /// Allocate every segment, empty (`make_memory`).
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        text_size: i32,
        data_size: i32,
        data_limit: i32,
        stack_size: i32,
        stack_limit: i32,
        k_text_size: i32,
        k_data_size: i32,
        k_data_limit: i32,
    ) -> Memory {
        // A text segment that can hold no instruction, or a kernel text
        // segment that stops short of the exception vector, would make every
        // exception jump outside its segment, and the run would loop.
        if text_size < 4 {
            fatal_error(&format!(
                "-stext {text_size}: the text segment needs at least one word (4 bytes)\n"
            ));
        }
        if k_text_size < 0x180 + 4 {
            fatal_error(&format!(
                "-sktext {k_text_size}: the kernel text segment must hold the exception \
                 handler at 0x80000180 (at least 388 bytes)\n"
            ));
        }
        // The data segment holds at least the 64K small-data area.
        let data_size: i32 = round_up_4(data_size.max(65536));
        let stack_size: i32 = round_up_4(stack_size);
        let k_data_size: i32 = round_up_4(k_data_size);
        let text_slots: usize = size_of(text_size).div_ceil(4);
        let k_text_slots: usize = size_of(k_text_size).div_ceil(4);
        Memory {
            text: vec![None; text_slots],
            text_top: TEXT_BOT.wrapping_add(text_size as u32),
            data: vec![0; size_of(data_size)],
            data_top: DATA_BOT.wrapping_add(data_size as u32),
            data_limit,
            stack: vec![0; size_of(stack_size)],
            stack_bot: STACK_TOP.wrapping_sub(stack_size as u32),
            stack_limit,
            k_text: vec![None; k_text_slots],
            k_text_top: K_TEXT_BOT.wrapping_add(k_text_size as u32),
            k_data: vec![0; size_of(k_data_size)],
            k_data_top: K_DATA_BOT.wrapping_add(k_data_size as u32),
            k_data_limit,
            gp_midpoint: 0,
        }
    }

    /// The data segment, the stack or the kernel data segment holding
    /// `addr`, and the offset of `addr` in it.
    fn data_segment(&self, addr: u32) -> Option<(&[u8], usize)> {
        if (DATA_BOT..self.data_top).contains(&addr) {
            Some((&self.data, (addr - DATA_BOT) as usize))
        } else if (self.stack_bot..STACK_TOP).contains(&addr) {
            Some((&self.stack, (addr - self.stack_bot) as usize))
        } else if (K_DATA_BOT..self.k_data_top).contains(&addr) {
            Some((&self.k_data, (addr - K_DATA_BOT) as usize))
        } else {
            None
        }
    }

    /// Like [`Memory::data_segment`], to change.
    fn data_segment_mut(&mut self, addr: u32) -> Option<(&mut [u8], usize)> {
        if (DATA_BOT..self.data_top).contains(&addr) {
            Some((&mut self.data, (addr - DATA_BOT) as usize))
        } else if (self.stack_bot..STACK_TOP).contains(&addr) {
            Some((&mut self.stack, (addr - self.stack_bot) as usize))
        } else if (K_DATA_BOT..self.k_data_top).contains(&addr) {
            Some((&mut self.k_data, (addr - K_DATA_BOT) as usize))
        } else {
            None
        }
    }

    /// The text slot of `addr` (word aligned), user or kernel.
    fn text_slot(&self, addr: u32) -> Option<&Option<InstRef>> {
        if addr & 3 != 0 {
            None
        } else if (TEXT_BOT..self.text_top).contains(&addr) {
            self.text.get(((addr - TEXT_BOT) >> 2) as usize)
        } else if (K_TEXT_BOT..self.k_text_top).contains(&addr) {
            self.k_text.get(((addr - K_TEXT_BOT) >> 2) as usize)
        } else {
            None
        }
    }

    /// Like [`Memory::text_slot`], to change.
    fn text_slot_mut(&mut self, addr: u32) -> Option<&mut Option<InstRef>> {
        if addr & 3 != 0 {
            None
        } else if (TEXT_BOT..self.text_top).contains(&addr) {
            self.text.get_mut(((addr - TEXT_BOT) >> 2) as usize)
        } else if (K_TEXT_BOT..self.k_text_top).contains(&addr) {
            self.k_text.get_mut(((addr - K_TEXT_BOT) >> 2) as usize)
        } else {
            None
        }
    }

    /// The bytes from `addr` to the end of the segment that holds it, for
    /// the syscalls that read memory directly (`mem_reference`).
    pub fn bytes_from(&self, addr: u32) -> Option<&[u8]> {
        self.data_segment(addr)
            .map(|(segment, offset): (&[u8], usize)| &segment[offset..])
    }

    /// Like [`Memory::bytes_from`], to change.
    pub fn bytes_from_mut(&mut self, addr: u32) -> Option<&mut [u8]> {
        self.data_segment_mut(addr)
            .map(|(segment, offset): (&mut [u8], usize)| &mut segment[offset..])
    }

    /// True if `addr` is in some segment (`mem_reference` would succeed).
    pub fn is_mapped(&self, addr: u32) -> bool {
        (TEXT_BOT..self.text_top).contains(&addr)
            || (K_TEXT_BOT..self.k_text_top).contains(&addr)
            || self.data_segment(addr).is_some()
    }
}

impl Machine {
    /// Grow the data segment by `addl_bytes` (`expand_data`, for `sbrk`
    /// and for data directives that run past the end).
    pub fn expand_data(&mut self, addl_bytes: i32) {
        let delta: i32 = round_up_4(addl_bytes);
        let old_size: i32 = self.mem.data_top.wrapping_sub(DATA_BOT) as i32;
        let new_size: i32 = old_size.wrapping_add(delta);
        if addl_bytes < 0 || new_size > self.mem.data_limit {
            error(&format!(
                "Can't expand data segment by {addl_bytes} bytes to {new_size} bytes\n"
            ));
            run_error(&format!("Use -ldata # with # > {new_size}\n"));
        }
        // The sizes are C ints: a growth that wraps negative passes the limit
        // check, and C's realloc of that size then fails.
        if new_size < 0 {
            fatal_error("realloc failed in expand_data\n");
        }
        self.mem.data.resize(size_of(new_size), 0);
        self.mem.data_top = self.mem.data_top.wrapping_add(delta as u32);
    }

    /// Grow the stack by at least `addl_bytes`, doubling it
    /// (`expand_stack`).  The stack grows down, so the old contents move
    /// to the top of the new segment.
    pub fn expand_stack(&mut self, addl_bytes: i32) {
        let delta: i32 = round_up_4(addl_bytes);
        let old_size: i32 = STACK_TOP.wrapping_sub(self.mem.stack_bot) as i32;
        let new_size: i32 = old_size.wrapping_add(delta.max(old_size));
        if addl_bytes < 0 || new_size > self.mem.stack_limit {
            run_error(&format!(
                "Can't expand stack segment by {addl_bytes} bytes to {new_size} bytes.\nUse -lstack # with # > {new_size}\n"
            ));
        }
        // As in expand_data; C hands the negative int to xmalloc as a size_t.
        if new_size < 0 {
            let request: u64 = i64::from(new_size) as u64;
            fatal_error(&format!("Out of memory at request for {request} bytes.\n"));
        }
        let mut new_stack: Vec<u8> = vec![0; size_of(new_size)];
        let start: usize = size_of(new_size - old_size);
        new_stack[start..].copy_from_slice(&self.mem.stack);
        self.mem.stack = new_stack;
        self.mem.stack_bot = self
            .mem
            .stack_bot
            .wrapping_sub((new_size - old_size) as u32);
    }

    /// Grow the kernel data segment by `addl_bytes` (`expand_k_data`).
    pub fn expand_k_data(&mut self, addl_bytes: i32) {
        let delta: i32 = round_up_4(addl_bytes);
        let old_size: i32 = self.mem.k_data_top.wrapping_sub(K_DATA_BOT) as i32;
        let new_size: i32 = old_size.wrapping_add(delta);
        if addl_bytes < 0 || new_size > self.mem.k_data_limit {
            run_error(&format!(
                "Can't expand kernel data segment by {addl_bytes} bytes to {new_size} bytes.\nUse -lkdata # with # > {new_size}\n"
            ));
        }
        // As in expand_data.
        if new_size < 0 {
            fatal_error("realloc failed in expand_k_data\n");
        }
        self.mem.k_data.resize(size_of(new_size), 0);
        self.mem.k_data_top = self.mem.k_data_top.wrapping_add(delta as u32);
    }

    /// The instruction at `addr`, or `None` for an empty slot
    /// (`mem_read_inst`).  An address outside the text segments raises a
    /// bus error and reads as the instruction 0 (a `nop`).
    pub fn mem_read_inst(&mut self, addr: u32) -> Option<InstRef> {
        match self.mem.text_slot(addr) {
            Some(slot) => slot.clone(),
            None => {
                self.raise_exception(ExcCode::Ibe);
                self.regs
                    .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
                Some(Instruction::decode(0).into_ref())
            }
        }
    }

    /// Put `instruction` in the text slot of `addr` (`mem_write_inst`).
    pub fn mem_write_inst(&mut self, addr: u32, instruction: Option<InstRef>) {
        match self.mem.text_slot_mut(addr) {
            Some(slot) => *slot = instruction,
            None => {
                self.raise_exception(ExcCode::Ibe);
                self.regs
                    .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
                let encoding: i32 = instruction.map_or(0, |inst: InstRef| inst.borrow().encoding);
                self.mem_write_word(addr, encoding);
            }
        }
    }

    /// Load a byte, sign-extended (`mem_read_byte`).
    pub fn mem_read_byte(&mut self, addr: u32) -> i32 {
        match self.mem.data_segment(addr) {
            Some((segment, offset)) => i32::from(segment[offset] as i8),
            None => self.bad_mem_read(addr, 0),
        }
    }

    /// Load a halfword, sign-extended (`mem_read_half`).
    pub fn mem_read_half(&mut self, addr: u32) -> i32 {
        if addr & 1 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment(addr) {
                let bytes: [u8; 2] = [segment[offset], segment[offset + 1]];
                return i32::from(i16::from_le_bytes(bytes));
            }
        }
        self.bad_mem_read(addr, 1)
    }

    /// Load a word (`mem_read_word`).
    pub fn mem_read_word(&mut self, addr: u32) -> i32 {
        if addr & 3 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment(addr) {
                return read_le32(segment, offset);
            }
        }
        self.bad_mem_read(addr, 3)
    }

    /// Store a byte (`mem_write_byte`).
    pub fn mem_write_byte(&mut self, addr: u32, value: i32) {
        match self.mem.data_segment_mut(addr) {
            Some((segment, offset)) => segment[offset] = value as u8,
            None => self.bad_mem_write(addr, value, 0),
        }
    }

    /// Store a halfword (`mem_write_half`).
    pub fn mem_write_half(&mut self, addr: u32, value: i32) {
        if addr & 1 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment_mut(addr) {
                segment[offset..offset + 2].copy_from_slice(&(value as i16).to_le_bytes());
                return;
            }
        }
        self.bad_mem_write(addr, value, 1);
    }

    /// Store a word (`mem_write_word`).
    pub fn mem_write_word(&mut self, addr: u32, value: i32) {
        if addr & 3 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment_mut(addr) {
                segment[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
                return;
            }
        }
        self.bad_mem_write(addr, value, 3);
    }

    /// The unusual loads (`bad_mem_read`): misaligned (an address error),
    /// from the text segment (the encoding of the instruction there), just
    /// below the stack (the stack grows), from the console registers, or
    /// from nowhere (a bus error).  `mask` is 0, 1 or 3 for a byte, half
    /// or word.
    fn bad_mem_read(&mut self, addr: u32, mask: u32) -> i32 {
        if addr & mask != 0 {
            self.raise_exception(ExcCode::AdEL);
            self.regs
                .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
        } else if (TEXT_BOT..self.mem.text_top).contains(&addr) {
            let slot: Option<InstRef> = self.mem.text[((addr - TEXT_BOT) >> 2) as usize].clone();
            // A word holding no instruction reads as 0.
            let encoding: u32 = slot.map_or(0, |inst: InstRef| inst.borrow().encoding as u32);
            return match mask {
                0 => ((encoding >> (8 * (addr & 3))) & 0xff) as i32,
                1 => ((encoding >> (8 * (addr & 2))) & 0xffff) as i32,
                _ => encoding as i32,
            };
        } else if addr > self.mem.data_top
            && addr < self.mem.stack_bot
            && addr > self.mem.stack_bot.wrapping_sub(16 * 1024 * 1024)
        {
            // Within 16 MB below the stack: grow the stack.
            self.expand_stack(self.mem.stack_bot.wrapping_sub(addr).wrapping_add(4) as i32);
            return 0;
        } else if addr >= MM_IO_BOT {
            return self.read_memory_mapped_io(addr);
        } else {
            self.raise_exception(ExcCode::Dbe);
            self.regs
                .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
        }
        0
    }

    /// The unusual stores (`bad_mem_write`): misaligned, into the text
    /// segment (the word is decoded into a new instruction), just below
    /// the stack, into the console registers, or nowhere.
    fn bad_mem_write(&mut self, addr: u32, value: i32, mask: u32) {
        if addr & mask != 0 {
            self.raise_exception(ExcCode::AdES);
            self.regs
                .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
        } else if (TEXT_BOT..self.mem.text_top).contains(&addr) {
            let index: usize = ((addr - TEXT_BOT) >> 2) as usize;
            let word: i32 = match &self.mem.text[index] {
                // An empty slot gets an instruction only from a full word.
                None => {
                    if mask == 3 {
                        value
                    } else {
                        0
                    }
                }
                Some(inst) => {
                    let old: u32 = inst.borrow().encoding as u32;
                    let value: u32 = value as u32;
                    match mask {
                        0 => {
                            let shift: u32 = 8 * (addr & 3);
                            ((old & !(0xff << shift)) | ((value & 0xff) << shift)) as i32
                        }
                        1 => {
                            let shift: u32 = 8 * (addr & 2);
                            ((old & !(0xffff << shift)) | ((value & 0xffff) << shift)) as i32
                        }
                        _ => value as i32,
                    }
                }
            };
            self.mem.text[index] = Some(Instruction::decode(word).into_ref());
        } else if addr > self.mem.data_top
            && addr < self.mem.stack_bot
            && addr > self.mem.stack_bot.wrapping_sub(16 * 1024 * 1024)
        {
            self.expand_stack(self.mem.stack_bot.wrapping_sub(addr).wrapping_add(4) as i32);
            if addr >= self.mem.stack_bot {
                let offset: usize = (addr - self.mem.stack_bot) as usize;
                match mask {
                    0 => self.mem.stack[offset] = value as u8,
                    1 => self.mem.stack[offset..offset + 2]
                        .copy_from_slice(&(value as i16).to_le_bytes()),
                    _ => self.mem.stack[offset..offset + 4].copy_from_slice(&value.to_le_bytes()),
                }
            } else {
                self.raise_exception(ExcCode::Dbe);
                self.regs
                    .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
            }
        } else if addr >= MM_IO_BOT {
            self.write_memory_mapped_io(addr, value);
        } else {
            self.raise_exception(ExcCode::Dbe);
            self.regs
                .set_cp0(crate::registers::CP0_BADVADDR, addr as i32);
        }
    }

    /// Poll the console every IO interval (`check_memory_mapped_IO`): take
    /// a typed character into the receiver, finish a pending print.
    pub fn check_memory_mapped_io(&mut self) {
        if self.mmio.recv_buffer_full_timer > 0 {
            self.mmio.recv_buffer_full_timer -= 1;
        } else if crate::console::console_input_available(self.options.mapped_io) {
            self.mmio.recv_buffer = i32::from(crate::console::get_console_char() as i8);
            self.mmio.recv_control |= READY;
            self.mmio.recv_buffer_full_timer = RECV_INTERVAL;
            if self.mmio.recv_control & INT_ENABLE != 0 {
                self.raise_interrupt(RECV_INT_LEVEL);
            }
        }
        if self.mmio.trans_buffer_full_timer > 0 {
            self.mmio.trans_buffer_full_timer -= 1;
        } else if self.mmio.trans_control & READY == 0 {
            self.mmio.trans_control |= READY;
            if self.mmio.trans_control & INT_ENABLE != 0 {
                self.raise_interrupt(TRANS_INT_LEVEL);
            }
        }
    }

    /// A store to a console register (`write_memory_mapped_IO`).
    fn write_memory_mapped_io(&mut self, addr: u32, value: i32) {
        let value: u32 = value as u32;
        match addr {
            TRANS_CTRL_ADDR => {
                // A program may set only the interrupt-enable bit.
                if value & INT_ENABLE != 0 {
                    self.mmio.trans_control |= INT_ENABLE;
                    if self.mmio.trans_control & READY != 0 {
                        self.raise_interrupt(TRANS_INT_LEVEL);
                    }
                } else {
                    self.mmio.trans_control &= !INT_ENABLE;
                    self.clear_interrupt(TRANS_INT_LEVEL);
                }
            }
            TRANS_BUFFER_ADDR => {
                // A write while the device is busy is lost.
                if self.mmio.trans_control & READY != 0 {
                    self.mmio.trans_buffer = (value & 0xff) as i32;
                    crate::console::put_console_char(self.mmio.trans_buffer as u8);
                    self.mmio.trans_control &= !READY;
                    self.mmio.trans_buffer_full_timer = TRANS_LATENCY;
                    self.clear_interrupt(TRANS_INT_LEVEL);
                }
            }
            RECV_CTRL_ADDR => {
                if value & INT_ENABLE != 0 {
                    self.mmio.recv_control |= INT_ENABLE;
                    if self.mmio.recv_control & READY != 0 {
                        self.raise_interrupt(RECV_INT_LEVEL);
                    }
                } else {
                    self.mmio.recv_control &= !INT_ENABLE;
                    self.clear_interrupt(RECV_INT_LEVEL);
                }
            }
            RECV_BUFFER_ADDR => {}
            _ => run_error(&format!(
                "Write to unused memory-mapped IO address (0x{addr:x})\n"
            )),
        }
    }

    /// A load from a console register (`read_memory_mapped_IO`).
    fn read_memory_mapped_io(&mut self, addr: u32) -> i32 {
        match addr {
            TRANS_CTRL_ADDR => self.mmio.trans_control as i32,
            TRANS_BUFFER_ADDR => self.mmio.trans_buffer & 0xff,
            RECV_CTRL_ADDR => self.mmio.recv_control as i32,
            RECV_BUFFER_ADDR => {
                self.mmio.recv_control &= !READY;
                self.mmio.recv_buffer_full_timer = 0;
                self.clear_interrupt(RECV_INT_LEVEL);
                self.mmio.recv_buffer & 0xff
            }
            _ => run_error(&format!(
                "Read from unused memory-mapped IO address (0x{addr:x})\n"
            )),
        }
    }

    /// Set the pending bit of hardware interrupt `level` in Cause.
    pub fn raise_interrupt(&mut self, level: u32) {
        let cause: u32 = self.regs.cp0(crate::registers::CP0_CAUSE) as u32;
        self.regs.set_cp0(
            crate::registers::CP0_CAUSE,
            (cause | (1 << (level + 8))) as i32,
        );
    }

    /// Clear the pending bit of interrupt `level` in Cause.
    pub fn clear_interrupt(&mut self, level: u32) {
        let cause: u32 = self.regs.cp0(crate::registers::CP0_CAUSE) as u32;
        self.regs.set_cp0(
            crate::registers::CP0_CAUSE,
            (cause & !(1 << (level + 8))) as i32,
        );
    }

    /// Print the word or instruction at `addr` (`print_mem`, the REPL's
    /// `print ADDR`).
    pub fn print_mem(&mut self, addr: u32) {
        let addr: u32 = addr & !3;
        let data_name: Option<&str> = if (TEXT_BOT..self.mem.text_top).contains(&addr) {
            None
        } else if (DATA_BOT..self.mem.data_top).contains(&addr) {
            Some("Data seg")
        } else if (self.mem.stack_bot..STACK_TOP).contains(&addr) {
            Some("Stack seg")
        } else if (K_TEXT_BOT..self.mem.k_text_top).contains(&addr) {
            None
        } else if (K_DATA_BOT..self.mem.k_data_top).contains(&addr) {
            Some("Kernel Data seg")
        } else {
            error(&format!(
                "Address 0x{addr:08x} ({}) to print_mem is out of bounds\n",
                addr as i32
            ));
            return;
        };
        match data_name {
            None => self.print_inst(addr),
            Some(name) => {
                let value: i32 = self.mem_read_word(addr);
                error(&format!(
                    "{name} @ 0x{addr:08x} ({}) = 0x{:08x} ({value})\n",
                    addr as i32, value as u32
                ));
            }
        }
    }
}
