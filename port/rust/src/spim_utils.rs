//! Setting the machine up, loading files, the program's stack, running,
//! and breakpoints (`spim-utils.c`).

use crate::machine::{error, fatal_error, Breakpoint, Machine};
use crate::memory::{Memory, DATA_BOT, K_DATA_BOT, K_TEXT_BOT, STACK_TOP, TEXT_BOT};
use crate::registers::*;
use crate::run::ExcCode;

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
        let options = &self.options;
        self.mem = Memory::new(
            options.initial_text_size,
            options.initial_data_size,
            options.initial_data_limit,
            options.initial_stack_size,
            options.initial_stack_limit,
            options.initial_k_text_size,
            options.initial_k_data_size,
            options.initial_k_data_limit,
        );
        self.initialize_registers();
        self.symbols.clear();
        self.k_text_begins_at_point(K_TEXT_BOT);
        self.k_data_begins_at_point(K_DATA_BOT);
        self.data_begins_at_point(DATA_BOT);
        self.text_begins_at_point(TEXT_BOT);
        if let Some(handler) = handler {
            // The handler is written for the extended machine.
            let old_bare: bool = self.options.bare_machine;
            let old_accept: bool = self.options.accept_pseudo_insts;
            self.options.bare_machine = false;
            self.options.accept_pseudo_insts = true;
            match handler {
                ExceptionHandler::Embedded => {
                    self.parser_init(EMBEDDED_EXCEPTIONS.to_vec(), EMBEDDED_EXCEPTIONS_NAME);
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
            self.options.accept_pseudo_insts = old_accept;
            if !self.options.bare_machine {
                // In case `.globl main` was forgotten.
                self.make_label_global("main");
                self.record_label("main", 0, false);
            }
        }
        self.scanner.init(Vec::new());
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
        self.regs.fpr = [0; 32];
        self.regs.gpr = [0; 32];
        self.regs.gpr[REG_SP] = (STACK_TOP - 4 - 4096) as i32;
        self.regs.hi = 0;
        self.regs.lo = 0;
        self.regs.pc = 0;
        self.regs.set_cp0(CP0_BADVADDR, 0);
        self.regs.set_cp0(CP0_COUNT, 0);
        self.regs.set_cp0(CP0_COMPARE, 0);
        self.regs.set_cp0(
            CP0_STATUS,
            ((STATUS_CU & 0x3000_0000) | STATUS_IM | STATUS_UM) as i32,
        );
        self.regs.set_cp0(CP0_CAUSE, 0);
        self.regs.set_cp0(CP0_EPC, 0);
        self.regs.set_cp0(CP0_CONFIG, 0);
        self.regs.cop[1][FIR_REG] = FIR_W_D_S as i32;
        self.regs.cop[1][FCSR_REG] = 0;
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
        self.parser_init(contents, name);
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
        self.regs.gpr[REG_SP] = (STACK_TOP - 1) as i32;
        let mut addresses: Vec<u32> = Vec::new();
        for variable in environment {
            addresses.push(self.copy_str_to_stack(variable));
        }
        let env_count: usize = addresses.len();
        for argument in argv {
            addresses.push(self.copy_str_to_stack(argument));
        }
        // Word-align the stack pointer, step to the first free word, and
        // align to a double word.
        let sp: i32 = ((self.regs.gpr[REG_SP] & !3) - 4) & !7;
        self.regs.gpr[REG_SP] = sp;
        self.copy_int_to_stack(0);
        for i in (0..env_count).rev() {
            self.regs.gpr[REG_A2] = self.copy_int_to_stack(addresses[i] as i32) as i32;
        }
        self.copy_int_to_stack(0);
        for i in (env_count..addresses.len()).rev() {
            self.regs.gpr[REG_A1] = self.copy_int_to_stack(addresses[i] as i32) as i32;
        }
        let argc: i32 = argv.len() as i32;
        self.regs.gpr[REG_A0] = argc;
        let sp: u32 = self.regs.gpr[REG_SP] as u32;
        self.mem_write_word(sp, argc);
    }

    /// Push a string and its NUL, last byte first; returns its address.
    fn copy_str_to_stack(&mut self, text: &[u8]) -> u32 {
        let mut bytes: Vec<u8> = text.to_vec();
        bytes.push(0);
        for &byte in bytes.iter().rev() {
            let sp: u32 = self.regs.gpr[REG_SP] as u32;
            self.mem_write_byte(sp, i32::from(byte as i8));
            self.regs.gpr[REG_SP] = self.regs.gpr[REG_SP].wrapping_sub(1);
        }
        (self.regs.gpr[REG_SP] as u32).wrapping_add(1)
    }

    /// Push a word; returns its address.
    fn copy_int_to_stack(&mut self, value: i32) -> u32 {
        let sp: u32 = self.regs.gpr[REG_SP] as u32;
        self.mem_write_word(sp, value);
        self.regs.gpr[REG_SP] = self.regs.gpr[REG_SP].wrapping_sub(4);
        (self.regs.gpr[REG_SP] as u32).wrapping_add(4)
    }

    /// Run from `pc` for `steps` instructions (`run_program`).  With
    /// `cont_bkpt`, first step over a breakpoint at `pc`.  Returns
    /// `(stopped at a breakpoint, can continue)`.
    pub fn run_program(
        &mut self,
        pc: u32,
        steps: i32,
        display: bool,
        cont_bkpt: bool,
    ) -> (bool, bool) {
        let mut pc: u32 = pc;
        let mut steps: i32 = steps;
        if cont_bkpt && self.inst_is_breakpoint(pc) {
            let addr: u32 = if self.regs.pc == 0 { pc } else { self.regs.pc };
            self.delete_breakpoint(addr);
            self.exception_occurred = false;
            let _ = self.run_spim(addr, 1, display);
            self.add_breakpoint(addr);
            steps -= 1;
            pc = self.regs.pc;
        }
        self.exception_occurred = false;
        let continuable: bool = self.run_spim(pc, steps, display);
        if self.exception_occurred && self.regs.exc_code() == ExcCode::Bp as u32 {
            // The breakpoint is handled here, not by MIPS code: clear the
            // exception level so later interrupts set EPC again.
            let status: u32 = self.regs.cp0(CP0_STATUS) as u32 & !STATUS_EXL;
            self.regs.set_cp0(CP0_STATUS, status as i32);
            (true, continuable)
        } else {
            (false, continuable)
        }
    }

    /// Set a breakpoint at `addr` (`add_breakpoint`).
    pub fn add_breakpoint(&mut self, addr: u32) {
        let instruction = self.set_breakpoint(addr);
        if instruction.is_some() {
            self.breakpoints.insert(0, Breakpoint { addr, instruction });
        } else if self.exception_occurred {
            error(&format!(
                "Cannot put a breakpoint at address 0x{addr:08x}\n"
            ));
        } else {
            error(&format!(
                "No instruction to breakpoint at address 0x{addr:08x}\n"
            ));
        }
    }

    /// Remove the breakpoints at `addr`, putting the old instructions back
    /// (`delete_breakpoint`).
    pub fn delete_breakpoint(&mut self, addr: u32) {
        let mut deleted: bool = false;
        let mut index: usize = 0;
        while index < self.breakpoints.len() {
            if self.breakpoints[index].addr == addr {
                let breakpoint: Breakpoint = self.breakpoints.remove(index);
                self.mem_write_inst(addr, breakpoint.instruction);
                deleted = true;
            } else {
                index += 1;
            }
        }
        if !deleted {
            error(&format!("No breakpoint to delete at 0x{addr:08x}\n"));
        }
    }

    /// List the breakpoints (`list_breakpoints`).
    pub fn list_breakpoints(&self) {
        if self.breakpoints.is_empty() {
            error("No breakpoints set\n");
        }
        for breakpoint in &self.breakpoints {
            error(&format!("Breakpoint at 0x{:08x}\n", breakpoint.addr));
        }
    }
}

/// A file name from the command line (bytes, as Unix has them) as a path.
pub fn os_path(name: &[u8]) -> std::path::PathBuf {
    use std::os::unix::ffi::OsStrExt;
    std::path::PathBuf::from(std::ffi::OsStr::from_bytes(name))
}
