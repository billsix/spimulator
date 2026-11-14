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
use crate::run::ExceptionCode;

/// Bottom of the user text segment.
pub const TEXT_BOTTOM: u32 = 0x0040_0000;
/// Bottom of the user data segment.
pub const DATA_BOTTOM: u32 = 0x1000_0000;
/// One past the top of the stack segment.
pub const STACK_TOP: u32 = 0x8000_0000;
/// Bottom of the kernel text segment.
pub const KERNEL_TEXT_BOTTOM: u32 = 0x8000_0000;
/// Bottom of the kernel data segment.
pub const KERNEL_DATA_BOTTOM: u32 = 0x9000_0000;
/// Bottom of the memory-mapped IO area.
pub const MEMORY_MAPPED_IO_BOTTOM: u32 = 0xffff_0000;

/// The receiver control register (a character is ready).
pub const RECEIVER_CONTROL_ADDRESS: u32 = 0xffff_0000;
/// The receiver data register.
pub const RECEIVER_BUFFER_ADDRESS: u32 = 0xffff_0004;
/// The transmitter control register (ready to print).
pub const TRANSMITTER_CONTROL_ADDRESS: u32 = 0xffff_0008;
/// The transmitter data register.
pub const TRANSMITTER_BUFFER_ADDRESS: u32 = 0xffff_000c;

/// Control register bit: ready.
const READY: u32 = 0x1;
/// Control register bit: interrupt enable.
const INTERRUPT_ENABLE: u32 = 0x2;
/// Hardware interrupt level of the receiver.
const RECEIVER_INTERRUPT_LEVEL: u32 = 3;
/// Hardware interrupt level of the transmitter.
const TRANSMITTER_INTERRUPT_LEVEL: u32 = 2;
/// IO intervals a received character stays in the buffer.
const RECEIVER_INTERVAL: i32 = 100;
/// IO intervals it takes to print a character.
const TRANSMITTER_LATENCY: i32 = 100;

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
    /// The stack segment; `stack[0]` is the byte at `stack_bottom`.
    pub stack: Vec<u8>,
    /// The lowest stack address.
    pub stack_bottom: u32,
    /// The largest the stack may grow.
    pub stack_limit: i32,
    /// The kernel text segment.
    pub kernel_text: Vec<Option<InstRef>>,
    /// One past the last kernel text address.
    pub kernel_text_top: u32,
    /// The kernel data segment.
    pub kernel_data: Vec<u8>,
    /// One past the last kernel data address.
    pub kernel_data_top: u32,
    /// The largest the kernel data segment may grow.
    pub kernel_data_limit: i32,
    /// The middle of the small data area that `$gp` points to.
    pub global_pointer_midpoint: u32,
}

/// The memory-mapped console's registers (the statics of `memory.c`).
#[derive(Debug)]
pub struct MappedIo {
    receiver_control: u32,
    receiver_buffer: i32,
    receiver_buffer_full_timer: i32,
    transmitter_control: u32,
    transmitter_buffer: i32,
    transmitter_buffer_full_timer: i32,
}

impl Default for MappedIo {
    fn default() -> Self {
        MappedIo {
            receiver_control: 0,
            receiver_buffer: 0,
            receiver_buffer_full_timer: 0,
            transmitter_control: READY,
            transmitter_buffer: 0,
            transmitter_buffer_full_timer: 0,
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
fn read_little_endian_32(segment: &[u8], offset: usize) -> i32 {
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
        kernel_text_size: i32,
        kernel_data_size: i32,
        kernel_data_limit: i32,
    ) -> Memory {
        // A text segment that can hold no instruction, or a kernel text
        // segment that stops short of the exception vector, would make every
        // exception jump outside its segment, and the run would loop.
        if text_size < 4 {
            fatal_error(&format!(
                "-stext {text_size}: the text segment needs at least one word (4 bytes)\n"
            ));
        }
        if kernel_text_size < 0x180 + 4 {
            fatal_error(&format!(
                "-sktext {kernel_text_size}: the kernel text segment must hold the exception \
                 handler at 0x80000180 (at least 388 bytes)\n"
            ));
        }
        // The data segment holds at least the 64K small-data area.
        let data_size: i32 = round_up_4(data_size.max(65536));
        let stack_size: i32 = round_up_4(stack_size);
        let kernel_data_size: i32 = round_up_4(kernel_data_size);
        let text_slots: usize = size_of(text_size).div_ceil(4);
        let kernel_text_slots: usize = size_of(kernel_text_size).div_ceil(4);
        Memory {
            text: vec![None; text_slots],
            text_top: TEXT_BOTTOM.wrapping_add(text_size as u32),
            data: vec![0; size_of(data_size)],
            data_top: DATA_BOTTOM.wrapping_add(data_size as u32),
            data_limit,
            stack: vec![0; size_of(stack_size)],
            stack_bottom: STACK_TOP.wrapping_sub(stack_size as u32),
            stack_limit,
            kernel_text: vec![None; kernel_text_slots],
            kernel_text_top: KERNEL_TEXT_BOTTOM.wrapping_add(kernel_text_size as u32),
            kernel_data: vec![0; size_of(kernel_data_size)],
            kernel_data_top: KERNEL_DATA_BOTTOM.wrapping_add(kernel_data_size as u32),
            kernel_data_limit,
            global_pointer_midpoint: 0,
        }
    }

    /// The data segment, the stack or the kernel data segment holding
    /// `addr`, and the offset of `addr` in it.
    fn data_segment(&self, address: u32) -> Option<(&[u8], usize)> {
        if (DATA_BOTTOM..self.data_top).contains(&address) {
            Some((&self.data, (address - DATA_BOTTOM) as usize))
        } else if (self.stack_bottom..STACK_TOP).contains(&address) {
            Some((&self.stack, (address - self.stack_bottom) as usize))
        } else if (KERNEL_DATA_BOTTOM..self.kernel_data_top).contains(&address) {
            Some((&self.kernel_data, (address - KERNEL_DATA_BOTTOM) as usize))
        } else {
            None
        }
    }

    /// Like [`Memory::data_segment`], to change.
    fn data_segment_mut(&mut self, address: u32) -> Option<(&mut [u8], usize)> {
        if (DATA_BOTTOM..self.data_top).contains(&address) {
            Some((&mut self.data, (address - DATA_BOTTOM) as usize))
        } else if (self.stack_bottom..STACK_TOP).contains(&address) {
            Some((&mut self.stack, (address - self.stack_bottom) as usize))
        } else if (KERNEL_DATA_BOTTOM..self.kernel_data_top).contains(&address) {
            Some((
                &mut self.kernel_data,
                (address - KERNEL_DATA_BOTTOM) as usize,
            ))
        } else {
            None
        }
    }

    /// The text slot of `addr` (word aligned), user or kernel.
    fn text_slot(&self, address: u32) -> Option<&Option<InstRef>> {
        if address & 3 != 0 {
            None
        } else if (TEXT_BOTTOM..self.text_top).contains(&address) {
            self.text.get(((address - TEXT_BOTTOM) >> 2) as usize)
        } else if (KERNEL_TEXT_BOTTOM..self.kernel_text_top).contains(&address) {
            self.kernel_text
                .get(((address - KERNEL_TEXT_BOTTOM) >> 2) as usize)
        } else {
            None
        }
    }

    /// Like [`Memory::text_slot`], to change.
    fn text_slot_mut(&mut self, address: u32) -> Option<&mut Option<InstRef>> {
        if address & 3 != 0 {
            None
        } else if (TEXT_BOTTOM..self.text_top).contains(&address) {
            self.text.get_mut(((address - TEXT_BOTTOM) >> 2) as usize)
        } else if (KERNEL_TEXT_BOTTOM..self.kernel_text_top).contains(&address) {
            self.kernel_text
                .get_mut(((address - KERNEL_TEXT_BOTTOM) >> 2) as usize)
        } else {
            None
        }
    }

    /// The bytes from `addr` to the end of the segment that holds it, for
    /// the syscalls that read memory directly (`memory_reference`).
    pub fn bytes_from(&self, address: u32) -> Option<&[u8]> {
        self.data_segment(address)
            .map(|(segment, offset): (&[u8], usize)| &segment[offset..])
    }

    /// Like [`Memory::bytes_from`], to change.
    pub fn bytes_from_mut(&mut self, address: u32) -> Option<&mut [u8]> {
        self.data_segment_mut(address)
            .map(|(segment, offset): (&mut [u8], usize)| &mut segment[offset..])
    }

    /// True if `addr` is in some segment (`memory_reference` would succeed).
    pub fn is_mapped(&self, address: u32) -> bool {
        (TEXT_BOTTOM..self.text_top).contains(&address)
            || (KERNEL_TEXT_BOTTOM..self.kernel_text_top).contains(&address)
            || self.data_segment(address).is_some()
    }
}

impl Machine {
    /// Grow the data segment by `additional_bytes` (`expand_data`, for `sbrk`
    /// and for data directives that run past the end).
    pub fn expand_data(&mut self, additional_bytes: i32) {
        let delta: i32 = round_up_4(additional_bytes);
        let old_size: i32 = self.mem.data_top.wrapping_sub(DATA_BOTTOM) as i32;
        let new_size: i32 = old_size.wrapping_add(delta);
        if additional_bytes < 0 || new_size > self.mem.data_limit {
            error(&format!(
                "Can't expand data segment by {additional_bytes} bytes to {new_size} bytes\n"
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

    /// Grow the stack by at least `additional_bytes`, doubling it
    /// (`expand_stack`).  The stack grows down, so the old contents move
    /// to the top of the new segment.
    pub fn expand_stack(&mut self, additional_bytes: i32) {
        let delta: i32 = round_up_4(additional_bytes);
        let old_size: i32 = STACK_TOP.wrapping_sub(self.mem.stack_bottom) as i32;
        let new_size: i32 = old_size.wrapping_add(delta.max(old_size));
        if additional_bytes < 0 || new_size > self.mem.stack_limit {
            run_error(&format!(
                "Can't expand stack segment by {additional_bytes} bytes to {new_size} bytes.\nUse -lstack # with # > {new_size}\n"
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
        self.mem.stack_bottom = self
            .mem
            .stack_bottom
            .wrapping_sub((new_size - old_size) as u32);
    }

    /// Grow the kernel data segment by `additional_bytes` (`expand_kernel_data`).
    pub fn expand_kernel_data(&mut self, additional_bytes: i32) {
        let delta: i32 = round_up_4(additional_bytes);
        let old_size: i32 = self.mem.kernel_data_top.wrapping_sub(KERNEL_DATA_BOTTOM) as i32;
        let new_size: i32 = old_size.wrapping_add(delta);
        if additional_bytes < 0 || new_size > self.mem.kernel_data_limit {
            run_error(&format!(
                "Can't expand kernel data segment by {additional_bytes} bytes to {new_size} bytes.\nUse -lkdata # with # > {new_size}\n"
            ));
        }
        // As in expand_data.
        if new_size < 0 {
            fatal_error("realloc failed in expand_k_data\n");
        }
        self.mem.kernel_data.resize(size_of(new_size), 0);
        self.mem.kernel_data_top = self.mem.kernel_data_top.wrapping_add(delta as u32);
    }

    /// The text slot of `addr`, read without side effects
    /// (`memory_peek_instruction`): no exception is raised and nothing grows.
    /// `None` when `addr` is not an aligned location in a text segment.
    pub fn memory_peek_instruction(&self, address: u32) -> Option<Option<InstRef>> {
        self.mem.text_slot(address).cloned()
    }

    /// The instruction at `addr`, or `None` for an empty slot
    /// (`memory_read_instruction`).  An address outside the text segments raises a
    /// bus error and reads as the instruction 0 (a `nop`).
    pub fn memory_read_instruction(&mut self, address: u32) -> Option<InstRef> {
        match self.mem.text_slot(address) {
            Some(slot) => slot.clone(),
            None => {
                self.raise_exception(ExceptionCode::InstructionBusError);
                self.registers.set_coprocessor_0(
                    crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                    address as i32,
                );
                Some(Instruction::decode(0).into_ref())
            }
        }
    }

    /// Put `instruction` in the text slot of `addr` (`memory_write_instruction`).
    pub fn memory_write_instruction(&mut self, address: u32, instruction: Option<InstRef>) {
        match self.mem.text_slot_mut(address) {
            Some(slot) => *slot = instruction,
            None => {
                self.raise_exception(ExceptionCode::InstructionBusError);
                self.registers.set_coprocessor_0(
                    crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                    address as i32,
                );
                let encoding: i32 =
                    instruction.map_or(0, |instruction: InstRef| instruction.borrow().encoding);
                self.memory_write_word(address, encoding);
            }
        }
    }

    /// Load a byte, sign-extended (`memory_read_byte`).
    pub fn memory_read_byte(&mut self, address: u32) -> i32 {
        match self.mem.data_segment(address) {
            Some((segment, offset)) => i32::from(segment[offset] as i8),
            None => self.bad_memory_read(address, 0),
        }
    }

    /// Load a halfword, sign-extended (`memory_read_half`).
    pub fn memory_read_half(&mut self, address: u32) -> i32 {
        if address & 1 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment(address) {
                let bytes: [u8; 2] = [segment[offset], segment[offset + 1]];
                return i32::from(i16::from_le_bytes(bytes));
            }
        }
        self.bad_memory_read(address, 1)
    }

    /// Load a word (`memory_read_word`).
    pub fn memory_read_word(&mut self, address: u32) -> i32 {
        if address & 3 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment(address) {
                return read_little_endian_32(segment, offset);
            }
        }
        self.bad_memory_read(address, 3)
    }

    /// Store a byte (`memory_write_byte`).
    pub fn memory_write_byte(&mut self, address: u32, value: i32) {
        match self.mem.data_segment_mut(address) {
            Some((segment, offset)) => segment[offset] = value as u8,
            None => self.bad_memory_write(address, value, 0),
        }
    }

    /// Store a halfword (`memory_write_half`).
    pub fn memory_write_half(&mut self, address: u32, value: i32) {
        if address & 1 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment_mut(address) {
                segment[offset..offset + 2].copy_from_slice(&(value as i16).to_le_bytes());
                return;
            }
        }
        self.bad_memory_write(address, value, 1);
    }

    /// Store a word (`memory_write_word`).
    pub fn memory_write_word(&mut self, address: u32, value: i32) {
        if address & 3 == 0 {
            if let Some((segment, offset)) = self.mem.data_segment_mut(address) {
                segment[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
                return;
            }
        }
        self.bad_memory_write(address, value, 3);
    }

    /// The unusual loads (`bad_memory_read`): misaligned (an address error),
    /// from the text segment (the encoding of the instruction there), just
    /// below the stack (the stack grows), from the console registers, or
    /// from nowhere (a bus error).  `mask` is 0, 1 or 3 for a byte, half
    /// or word.
    fn bad_memory_read(&mut self, address: u32, mask: u32) -> i32 {
        if address & mask != 0 {
            self.raise_exception(ExceptionCode::AddressErrorLoad);
            self.registers.set_coprocessor_0(
                crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                address as i32,
            );
        } else if (TEXT_BOTTOM..self.mem.text_top).contains(&address) {
            let slot: Option<InstRef> =
                self.mem.text[((address - TEXT_BOTTOM) >> 2) as usize].clone();
            // A word holding no instruction reads as 0.
            let encoding: u32 = slot.map_or(0, |instruction: InstRef| {
                instruction.borrow().encoding as u32
            });
            return match mask {
                0 => ((encoding >> (8 * (address & 3))) & 0xff) as i32,
                1 => ((encoding >> (8 * (address & 2))) & 0xffff) as i32,
                _ => encoding as i32,
            };
        } else if address > self.mem.data_top
            && address < self.mem.stack_bottom
            && address > self.mem.stack_bottom.wrapping_sub(16 * 1024 * 1024)
        {
            // Within 16 MB below the stack: grow the stack.
            self.expand_stack(self.mem.stack_bottom.wrapping_sub(address).wrapping_add(4) as i32);
            return 0;
        } else if address >= MEMORY_MAPPED_IO_BOTTOM {
            return self.read_memory_mapped_io(address);
        } else {
            self.raise_exception(ExceptionCode::DataBusError);
            self.registers.set_coprocessor_0(
                crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                address as i32,
            );
        }
        0
    }

    /// The unusual stores (`bad_memory_write`): misaligned, into the text
    /// segment (the word is decoded into a new instruction), just below
    /// the stack, into the console registers, or nowhere.
    fn bad_memory_write(&mut self, address: u32, value: i32, mask: u32) {
        if address & mask != 0 {
            self.raise_exception(ExceptionCode::AddressErrorStore);
            self.registers.set_coprocessor_0(
                crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                address as i32,
            );
        } else if (TEXT_BOTTOM..self.mem.text_top).contains(&address) {
            let index: usize = ((address - TEXT_BOTTOM) >> 2) as usize;
            let word: i32 = match &self.mem.text[index] {
                // An empty slot gets an instruction only from a full word.
                None => {
                    if mask == 3 {
                        value
                    } else {
                        0
                    }
                }
                Some(instruction) => {
                    let old: u32 = instruction.borrow().encoding as u32;
                    let value: u32 = value as u32;
                    match mask {
                        0 => {
                            let shift: u32 = 8 * (address & 3);
                            ((old & !(0xff << shift)) | ((value & 0xff) << shift)) as i32
                        }
                        1 => {
                            let shift: u32 = 8 * (address & 2);
                            ((old & !(0xffff << shift)) | ((value & 0xffff) << shift)) as i32
                        }
                        _ => value as i32,
                    }
                }
            };
            self.mem.text[index] = Some(Instruction::decode(word).into_ref());
        } else if address > self.mem.data_top
            && address < self.mem.stack_bottom
            && address > self.mem.stack_bottom.wrapping_sub(16 * 1024 * 1024)
        {
            self.expand_stack(self.mem.stack_bottom.wrapping_sub(address).wrapping_add(4) as i32);
            if address >= self.mem.stack_bottom {
                let offset: usize = (address - self.mem.stack_bottom) as usize;
                match mask {
                    0 => self.mem.stack[offset] = value as u8,
                    1 => self.mem.stack[offset..offset + 2]
                        .copy_from_slice(&(value as i16).to_le_bytes()),
                    _ => self.mem.stack[offset..offset + 4].copy_from_slice(&value.to_le_bytes()),
                }
            } else {
                self.raise_exception(ExceptionCode::DataBusError);
                self.registers.set_coprocessor_0(
                    crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                    address as i32,
                );
            }
        } else if address >= MEMORY_MAPPED_IO_BOTTOM {
            self.write_memory_mapped_io(address, value);
        } else {
            self.raise_exception(ExceptionCode::DataBusError);
            self.registers.set_coprocessor_0(
                crate::registers::COPROCESSOR_0_BAD_VIRTUAL_ADDRESS,
                address as i32,
            );
        }
    }

    /// Poll the console every IO interval (`check_memory_mapped_IO`): take
    /// a typed character into the receiver, finish a pending print.
    pub fn check_memory_mapped_io(&mut self) {
        if self.mmio.receiver_buffer_full_timer > 0 {
            self.mmio.receiver_buffer_full_timer -= 1;
        } else if crate::console::console_input_available(self.options.mapped_io) {
            self.mmio.receiver_buffer = i32::from(crate::console::get_console_char() as i8);
            self.mmio.receiver_control |= READY;
            self.mmio.receiver_buffer_full_timer = RECEIVER_INTERVAL;
            if self.mmio.receiver_control & INTERRUPT_ENABLE != 0 {
                self.raise_interrupt(RECEIVER_INTERRUPT_LEVEL);
            }
        }
        if self.mmio.transmitter_buffer_full_timer > 0 {
            self.mmio.transmitter_buffer_full_timer -= 1;
        } else if self.mmio.transmitter_control & READY == 0 {
            self.mmio.transmitter_control |= READY;
            if self.mmio.transmitter_control & INTERRUPT_ENABLE != 0 {
                self.raise_interrupt(TRANSMITTER_INTERRUPT_LEVEL);
            }
        }
    }

    /// A store to a console register (`write_memory_mapped_IO`).
    fn write_memory_mapped_io(&mut self, address: u32, value: i32) {
        let value: u32 = value as u32;
        match address {
            TRANSMITTER_CONTROL_ADDRESS => {
                // A program may set only the interrupt-enable bit.
                if value & INTERRUPT_ENABLE != 0 {
                    self.mmio.transmitter_control |= INTERRUPT_ENABLE;
                    if self.mmio.transmitter_control & READY != 0 {
                        self.raise_interrupt(TRANSMITTER_INTERRUPT_LEVEL);
                    }
                } else {
                    self.mmio.transmitter_control &= !INTERRUPT_ENABLE;
                    self.clear_interrupt(TRANSMITTER_INTERRUPT_LEVEL);
                }
            }
            TRANSMITTER_BUFFER_ADDRESS => {
                // A write while the device is busy is lost.
                if self.mmio.transmitter_control & READY != 0 {
                    self.mmio.transmitter_buffer = (value & 0xff) as i32;
                    crate::console::put_console_char(self.mmio.transmitter_buffer as u8);
                    self.mmio.transmitter_control &= !READY;
                    self.mmio.transmitter_buffer_full_timer = TRANSMITTER_LATENCY;
                    self.clear_interrupt(TRANSMITTER_INTERRUPT_LEVEL);
                }
            }
            RECEIVER_CONTROL_ADDRESS => {
                if value & INTERRUPT_ENABLE != 0 {
                    self.mmio.receiver_control |= INTERRUPT_ENABLE;
                    if self.mmio.receiver_control & READY != 0 {
                        self.raise_interrupt(RECEIVER_INTERRUPT_LEVEL);
                    }
                } else {
                    self.mmio.receiver_control &= !INTERRUPT_ENABLE;
                    self.clear_interrupt(RECEIVER_INTERRUPT_LEVEL);
                }
            }
            RECEIVER_BUFFER_ADDRESS => {}
            _ => run_error(&format!(
                "Write to unused memory-mapped IO address (0x{address:x})\n"
            )),
        }
    }

    /// A load from a console register (`read_memory_mapped_IO`).
    fn read_memory_mapped_io(&mut self, address: u32) -> i32 {
        match address {
            TRANSMITTER_CONTROL_ADDRESS => self.mmio.transmitter_control as i32,
            TRANSMITTER_BUFFER_ADDRESS => self.mmio.transmitter_buffer & 0xff,
            RECEIVER_CONTROL_ADDRESS => self.mmio.receiver_control as i32,
            RECEIVER_BUFFER_ADDRESS => {
                self.mmio.receiver_control &= !READY;
                self.mmio.receiver_buffer_full_timer = 0;
                self.clear_interrupt(RECEIVER_INTERRUPT_LEVEL);
                self.mmio.receiver_buffer & 0xff
            }
            _ => run_error(&format!(
                "Read from unused memory-mapped IO address (0x{address:x})\n"
            )),
        }
    }

    /// Set the pending bit of hardware interrupt `level` in Cause.
    pub fn raise_interrupt(&mut self, level: u32) {
        let cause: u32 = self
            .registers
            .coprocessor_0(crate::registers::COPROCESSOR_0_CAUSE) as u32;
        self.registers.set_coprocessor_0(
            crate::registers::COPROCESSOR_0_CAUSE,
            (cause | (1 << (level + 8))) as i32,
        );
    }

    /// Clear the pending bit of interrupt `level` in Cause.
    pub fn clear_interrupt(&mut self, level: u32) {
        let cause: u32 = self
            .registers
            .coprocessor_0(crate::registers::COPROCESSOR_0_CAUSE) as u32;
        self.registers.set_coprocessor_0(
            crate::registers::COPROCESSOR_0_CAUSE,
            (cause & !(1 << (level + 8))) as i32,
        );
    }

    /// Print the word or instruction at `addr` (`print_memory`, the REPL's
    /// `print ADDR`).
    pub fn print_memory(&mut self, address: u32) {
        let address: u32 = address & !3;
        let data_name: Option<&str> = if (TEXT_BOTTOM..self.mem.text_top).contains(&address) {
            None
        } else if (DATA_BOTTOM..self.mem.data_top).contains(&address) {
            Some("Data seg")
        } else if (self.mem.stack_bottom..STACK_TOP).contains(&address) {
            Some("Stack seg")
        } else if (KERNEL_TEXT_BOTTOM..self.mem.kernel_text_top).contains(&address) {
            None
        } else if (KERNEL_DATA_BOTTOM..self.mem.kernel_data_top).contains(&address) {
            Some("Kernel Data seg")
        } else {
            error(&format!(
                "Address 0x{address:08x} ({}) to print_mem is out of bounds\n",
                address as i32
            ));
            return;
        };
        match data_name {
            None => self.print_instruction(address),
            Some(name) => {
                let value: i32 = self.memory_read_word(address);
                error(&format!(
                    "{name} @ 0x{address:08x} ({}) = 0x{:08x} ({value})\n",
                    address as i32, value as u32
                ));
            }
        }
    }
}
