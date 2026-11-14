//! Setting the machine up, loading files, the program's stack, running,
//! and breakpoints (`spim-utils.c`).

use crate::instruction::InstRef;
use crate::machine::{error, fatal_error, Breakpoint, Machine, Options};
use crate::memory::{
    Memory, DATA_BOTTOM, KERNEL_DATA_BOTTOM, KERNEL_TEXT_BOTTOM, STACK_TOP, TEXT_BOTTOM,
};
use crate::registers::*;
use crate::run::ExceptionCode;

/// The default exception handler, built into the program as the C engine
/// builds it in with `#embed`.
pub const EMBEDDED_EXCEPTIONS: &[u8] = include_bytes!("../../../src/exceptions.s");

/// The name the built-in exception handler is reported by.
pub const EMBEDDED_EXCEPTIONS_NAME: &[u8] = b"<embedded exceptions.s>";

/// Where the exception handler comes from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ExceptionHandler {
    /// The built-in copy of `exceptions.s`.
    Embedded,
    /// Files named on the command line, separated by `;`.
    Files(Vec<u8>),
}

/// The version banner's first line.
pub const SPIM_VERSION: &str = "Version 9.1.24 of August 1, 2023 (final)";

impl Machine {
    /// Set up (or reset) the whole machine: memory, registers, symbols,
    /// and the exception handler (`initialize_world`).
    pub fn initialize_world(&mut self, handler: Option<&ExceptionHandler>, print_message: bool) {
        let options: &Options = &self.options;
        self.mem = Memory::new(
            options.initial_text_size,
            options.initial_data_size,
            options.initial_data_limit,
            options.initial_stack_size,
            options.initial_stack_limit,
            options.initial_kernel_text_size,
            options.initial_kernel_data_size,
            options.initial_kernel_data_limit,
        );
        self.initialize_registers();
        self.symbols.clear();
        self.kernel_text_begins_at_point(KERNEL_TEXT_BOTTOM);
        self.kernel_data_begins_at_point(KERNEL_DATA_BOTTOM);
        self.data_begins_at_point(DATA_BOTTOM);
        self.text_begins_at_point(TEXT_BOTTOM);
        if let Some(handler) = handler {
            // The handler is written for the extended machine.
            let old_bare: bool = self.options.bare_machine;
            let old_accept: bool = self.options.accept_pseudo_instructions;
            self.options.bare_machine = false;
            self.options.accept_pseudo_instructions = true;
            match handler {
                ExceptionHandler::Embedded => {
                    self.parser_initialize(EMBEDDED_EXCEPTIONS.to_vec(), EMBEDDED_EXCEPTIONS_NAME);
                    let _ = self.parse_file();
                    self.flush_local_labels(!self.asm.parse_error_occurred);
                    self.end_of_assembly_file();
                    if print_message {
                        error("Loaded: <embedded exceptions.s>\n");
                    }
                }
                ExceptionHandler::Files(names) => {
                    // Like strtok, skip empty names between separators.
                    let files: Vec<&[u8]> = names
                        .split(|b: &u8| *b == b';')
                        .filter(|name: &&[u8]| !name.is_empty())
                        .collect();
                    for name in files {
                        if !self.read_assembly_file(name) {
                            let mut message: Vec<u8> = b"Cannot read exception handler: ".to_vec();
                            message.extend_from_slice(name);
                            message.push(b'\n');
                            fatal_error(&String::from_utf8_lossy(&message));
                        }
                        if print_message {
                            let mut message: Vec<u8> = b"Loaded: ".to_vec();
                            message.extend_from_slice(name);
                            message.push(b'\n');
                            crate::machine::error_bytes(&message);
                        }
                    }
                }
            }
            self.options.bare_machine = old_bare;
            self.options.accept_pseudo_instructions = old_accept;
            if !self.options.bare_machine {
                // In case `.globl main` was forgotten.
                self.make_label_global("main");
                self.record_label("main", 0, false);
            }
        }
        self.scanner.initialize(Vec::new());
        self.breakpoints.clear();
    }

    /// Print the version banner (`write_startup_message`).
    pub fn write_startup_message(&self) {
        error(&format!("SPIM {SPIM_VERSION}\n"));
        error("Copyright 1990-2023 by James Larus.\n");
        error("All Rights Reserved.\n");
        error("SPIM is distributed under a BSD license.\n");
        error("See the file README for a full copyright notice.\n");
    }

    /// Reset the registers (`initialize_registers`).
    pub fn initialize_registers(&mut self) {
        self.registers.floating_point_register = [0; 32];
        self.registers.general_purpose_registers = [0; 32];
        self.registers.general_purpose_registers[STACK_POINTER_REGISTER] =
            (STACK_TOP - 4 - 4096) as i32;
        self.registers.hi = 0;
        self.registers.lo = 0;
        self.registers.program_counter = 0;
        self.registers
            .set_coprocessor_0(COPROCESSOR_0_BAD_VIRTUAL_ADDRESS, 0);
        self.registers.set_coprocessor_0(COPROCESSOR_0_COUNT, 0);
        self.registers.set_coprocessor_0(COPROCESSOR_0_COMPARE, 0);
        self.registers.set_coprocessor_0(
            COPROCESSOR_0_STATUS,
            ((STATUS_COPROCESSOR_USABLE & 0x3000_0000) | STATUS_INTERRUPT_MASK | STATUS_USER_MODE)
                as i32,
        );
        self.registers.set_coprocessor_0(COPROCESSOR_0_CAUSE, 0);
        self.registers
            .set_coprocessor_0(COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER, 0);
        self.registers.set_coprocessor_0(COPROCESSOR_0_CONFIG, 0);
        self.registers.cop[1][FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER] =
            FLOATING_POINT_IMPLEMENTATION_HAS_WORD_DOUBLE_SINGLE as i32;
        self.registers.cop[1][FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER] = 0;
    }

    /// Assemble the file `name` (`read_assembly_file`).  Returns false if
    /// it cannot be opened; parse errors are counted, not returned.
    pub fn read_assembly_file(&mut self, name: &[u8]) -> bool {
        let path: std::path::PathBuf = os_path(name);
        let Ok(mut file) = std::fs::File::open(&path) else {
            let mut message: Vec<u8> = b"Cannot open file: `".to_vec();
            message.extend_from_slice(name);
            message.extend_from_slice(b"'\n");
            crate::machine::error_bytes(&message);
            return false;
        };
        let mut contents: Vec<u8> = Vec::new();
        // A directory opens but cannot be read: like fgets, see no lines.
        if std::io::Read::read_to_end(&mut file, &mut contents).is_err() {
            contents.clear();
        }
        self.parser_initialize(contents, name);
        let _ = self.parse_file();
        self.flush_local_labels(!self.asm.parse_error_occurred);
        self.end_of_assembly_file();
        true
    }

    /// The address of `__start`, where a run begins (`starting_address`).
    pub fn starting_address(&mut self) -> u32 {
        self.find_symbol_address("__start")
    }

    /// Build the program's stack: the environment strings and the
    /// arguments, then the `envp` and `argv` vectors and `argc`
    /// (`initialize_run_stack`).  `$a0` is argc, `$a1` argv, `$a2` envp.
    pub fn initialize_run_stack(&mut self, argv: &[Vec<u8>], environment: &[Vec<u8>]) {
        self.registers.general_purpose_registers[STACK_POINTER_REGISTER] = (STACK_TOP - 1) as i32;
        let mut addresses: Vec<u32> = Vec::new();
        for variable in environment {
            addresses.push(self.copy_string_to_stack(variable));
        }
        let environment_count: usize = addresses.len();
        for argument in argv {
            addresses.push(self.copy_string_to_stack(argument));
        }
        // Word-align the stack pointer, step to the first free word, and
        // align to a double word.
        let sp: i32 =
            ((self.registers.general_purpose_registers[STACK_POINTER_REGISTER] & !3) - 4) & !7;
        self.registers.general_purpose_registers[STACK_POINTER_REGISTER] = sp;
        self.copy_integer_to_stack(0);
        for i in (0..environment_count).rev() {
            self.registers.general_purpose_registers[ARGUMENT_REGISTER_2] =
                self.copy_integer_to_stack(addresses[i] as i32) as i32;
        }
        self.copy_integer_to_stack(0);
        for i in (environment_count..addresses.len()).rev() {
            self.registers.general_purpose_registers[ARGUMENT_REGISTER_1] =
                self.copy_integer_to_stack(addresses[i] as i32) as i32;
        }
        let argc: i32 = argv.len() as i32;
        self.registers.general_purpose_registers[ARGUMENT_REGISTER_0] = argc;
        let sp: u32 = self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32;
        self.memory_write_word(sp, argc);
    }

    /// Push a string and its NUL, last byte first; returns its address.
    fn copy_string_to_stack(&mut self, text: &[u8]) -> u32 {
        let mut bytes: Vec<u8> = text.to_vec();
        bytes.push(0);
        for &byte in bytes.iter().rev() {
            let sp: u32 = self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32;
            self.memory_write_byte(sp, i32::from(byte as i8));
            self.registers.general_purpose_registers[STACK_POINTER_REGISTER] =
                self.registers.general_purpose_registers[STACK_POINTER_REGISTER].wrapping_sub(1);
        }
        (self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32).wrapping_add(1)
    }

    /// Push a word; returns its address.
    fn copy_integer_to_stack(&mut self, value: i32) -> u32 {
        let sp: u32 = self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32;
        self.memory_write_word(sp, value);
        self.registers.general_purpose_registers[STACK_POINTER_REGISTER] =
            self.registers.general_purpose_registers[STACK_POINTER_REGISTER].wrapping_sub(4);
        (self.registers.general_purpose_registers[STACK_POINTER_REGISTER] as u32).wrapping_add(4)
    }

    /// Run from `pc` for `steps` instructions (`run_program`).  With
    /// `continue_breakpoint`, first step over a breakpoint at `pc`.  Returns
    /// `(stopped at a breakpoint, can continue)`.
    pub fn run_program(
        &mut self,
        program_counter: u32,
        steps: i32,
        display: bool,
        continue_breakpoint: bool,
    ) -> (bool, bool) {
        let mut program_counter: u32 = program_counter;
        let mut steps: i32 = steps;
        if continue_breakpoint && self.instruction_is_breakpoint(program_counter) {
            let address: u32 = if self.registers.program_counter == 0 {
                program_counter
            } else {
                self.registers.program_counter
            };
            self.delete_breakpoint(address);
            self.exception_occurred = false;
            let _ = self.run_spim(address, 1, display);
            self.add_breakpoint(address);
            steps = steps.wrapping_sub(1);
            program_counter = self.registers.program_counter;
        }
        self.exception_occurred = false;
        let continuable: bool = self.run_spim(program_counter, steps, display);
        if self.exception_occurred
            && self.registers.exception_code() == ExceptionCode::Breakpoint as u32
        {
            // The breakpoint is handled here, not by MIPS code: clear the
            // exception level so later interrupts set EPC again.
            let status: u32 =
                self.registers.coprocessor_0(COPROCESSOR_0_STATUS) as u32 & !STATUS_EXCEPTION_LEVEL;
            self.registers
                .set_coprocessor_0(COPROCESSOR_0_STATUS, status as i32);
            (true, continuable)
        } else {
            (false, continuable)
        }
    }

    /// Set a breakpoint at `addr` (`add_breakpoint`).
    pub fn add_breakpoint(&mut self, address: u32) {
        let instruction: Option<InstRef> = self.set_breakpoint(address);
        if instruction.is_some() {
            self.breakpoints.insert(
                0,
                Breakpoint {
                    address,
                    instruction,
                },
            );
        } else if self.exception_occurred {
            error(&format!(
                "Cannot put a breakpoint at address 0x{address:08x}\n"
            ));
        } else {
            error(&format!(
                "No instruction to breakpoint at address 0x{address:08x}\n"
            ));
        }
    }

    /// Remove the breakpoints at `addr`, putting the old instructions back
    /// (`delete_breakpoint`).
    pub fn delete_breakpoint(&mut self, address: u32) {
        let mut deleted: bool = false;
        let mut index: usize = 0;
        while index < self.breakpoints.len() {
            if self.breakpoints[index].address == address {
                let breakpoint: Breakpoint = self.breakpoints.remove(index);
                self.memory_write_instruction(address, breakpoint.instruction);
                deleted = true;
            } else {
                index += 1;
            }
        }
        if !deleted {
            error(&format!("No breakpoint to delete at 0x{address:08x}\n"));
        }
    }

    /// List the breakpoints (`list_breakpoints`).
    pub fn list_breakpoints(&self) {
        if self.breakpoints.is_empty() {
            error("No breakpoints set\n");
        }
        for breakpoint in &self.breakpoints {
            error(&format!("Breakpoint at 0x{:08x}\n", breakpoint.address));
        }
    }
}

/// A file name from the command line (bytes, as Unix has them) as a path.
pub fn os_path(name: &[u8]) -> std::path::PathBuf {
    use std::os::unix::ffi::OsStrExt;
    std::path::PathBuf::from(std::ffi::OsStr::from_bytes(name))
}
