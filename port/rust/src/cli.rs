//! The command line and the interactive REPL (`spim.c`).
//!
//! `spimulator-rust -f prog.s` assembles `prog.s` (after the exception
//! handler) and runs it from `__start`.  With no file, it reads commands
//! from its input: `load "prog.s"`, `run`, `step 3`, `print $t0`, ...
//! (`help` lists them).  The flags and messages are the C engine's.

use std::io::Write;

use crate::machine::Machine;
use crate::machine::{catch_run_error, error, error_bytes, install_panic_hook};
use crate::memory::{DATA_BOT, K_TEXT_BOT, TEXT_BOT};
use crate::opcodes::{TOK_EOF, TOK_FP_REG, TOK_ID, TOK_INT, TOK_NL, TOK_REG, TOK_STR};
use crate::spim_utils::{os_path, ExceptionHandler};

/// The usage message printed after an unknown flag.
const USAGE: &str = "Usage: spim
\t-bare\t\t\tBare machine (no pseudo-ops, delayed branches and loads)
\t-asm\t\t\tExtended machine (pseudo-ops, no delayed branches and loads) (default)
\t-delayed_branches\tExecute delayed branches
\t-delayed_loads\t\tExecute delayed loads
\t-exception\t\tLoad exception handler (default)
\t-noexception\t\tDo not load exception handler
\t-exception_file <file>\tSpecify exception handler in place of default
\t-quiet\t\t\tDo not print warnings
\t-noquiet\t\tPrint warnings (default)
\t-explain [N]\t\tNarrate every instruction (teaching mode).
\t\t\t\tLevel N selects detail: 1 = semantic, 2 = + interactive,
\t\t\t\t3 = + bit-layout (default when N omitted; historical behavior),
\t\t\t\t4 = + progressive decoder walkthrough.
\t\t\t\tAlso accepts -explain=N. Short form: -x.
\t-noexplain\t\tDisable teaching mode (default)
\t-mapped_io\t\tEnable memory-mapped IO
\t-nomapped_io\t\tDo not enable memory-mapped IO (default)
\t-file <file> [-file <file> ...] <args>
\t\t\t\tAssembly code file(s) and arguments to program.
\t\t\t\tMultiple -file (or -f) options accumulate into one
\t\t\t\tsymbol table; forward references resolve across files.
\t-assemble\t\tWrite assembled code to <file>.out
\t-listing <file>\t\tWrite assemble-time event trace to <file> (use - for stderr)
\t-print-ast\t\tParse to AST, print it to stderr, and exit without emitting any code
\t-print-ast-json\t\tParse to AST, dump JSON to stdout (for tooling), and exit
\t-show-expansion\t\tParse to AST, print just the pseudo-op wrappers + their expansion, and exit
\t-dump\t\t\tWrite user data and text segments into files
\t-full_dump\t\tWrite user and kernel data and text into files.
";

/// The run never stops for a step count (`DEFAULT_RUN_STEPS`).
const DEFAULT_RUN_STEPS: i32 = i32::MAX;

/// The REPL's commands (`UNKNOWN_CMD`, ...).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Command {
    Unknown,
    Exit,
    Read,
    Run,
    Step,
    Print,
    PrintSymbols,
    PrintAllRegs,
    Reinitialize,
    Asm,
    Redo,
    Nop,
    Help,
    Continue,
    SetBreakpoint,
    DeleteBreakpoint,
    ListBreakpoints,
    DumpNative,
    Dump,
    Disasm,
    Args,
}

/// The engine: the machine plus the settings and REPL state of `spim.c`.
struct Engine {
    machine: Machine,
    /// Load an exception handler (`-exception`/`-noexception`).
    load_exception_handler: bool,
    /// Which handler (`-exception_file`), if any.
    exception_file: Option<ExceptionHandler>,
    /// The program's arguments; the first is its name.
    program_argv: Vec<Vec<u8>>,
    /// The REPL's last command, for a blank line to repeat.
    prev_cmd: Command,
    /// The last `print` operand's token kind.
    prev_token: i32,
    /// The last `run` address.
    run_addr: u32,
    /// The last `step` count.
    step_count: i32,
    /// The last `print` location.
    print_loc: i32,
    /// The last breakpoint address.
    breakpoint_addr: u32,
}

/// The process environment as `NAME=value` strings, in order.
fn environment() -> Vec<Vec<u8>> {
    use std::os::unix::ffi::OsStrExt;
    let mut variables: Vec<Vec<u8>> = Vec::new();
    for (name, value) in std::env::vars_os() {
        let mut entry: Vec<u8> = name.as_bytes().to_vec();
        entry.push(b'=');
        entry.extend_from_slice(value.as_bytes());
        variables.push(entry);
    }
    variables
}

/// True if `s1` is a prefix of `s2` at least `min_match` long
/// (`str_prefix`): `pr` is `print`, `p` is not `print_symbols`.
fn str_prefix(s1: &[u8], s2: &str, min_match: usize) -> bool {
    s1.len() >= min_match && s2.as_bytes().starts_with(s1)
}

/// The C library's message for an error number (`strerror`).
fn os_error_text(error: &std::io::Error) -> String {
    let text: String = error.to_string();
    // Rust appends " (os error N)" to the C library's message.
    match text.rfind(" (os error ") {
        Some(position) => text[..position].to_string(),
        None => text,
    }
}

impl Engine {
    fn new() -> Engine {
        Engine {
            machine: Machine::new(),
            load_exception_handler: true,
            exception_file: Some(ExceptionHandler::Embedded),
            program_argv: Vec::new(),
            prev_cmd: Command::Nop,
            prev_token: 0,
            run_addr: 0,
            step_count: 0,
            print_loc: 0,
            breakpoint_addr: 0,
        }
    }

    /// The exception handler to load, if any.
    fn handler(&self) -> Option<ExceptionHandler> {
        if self.load_exception_handler {
            self.exception_file.clone()
        } else {
            None
        }
    }

    /// Set the machine up (`initialize_world`).
    fn initialize_world(&mut self) {
        let handler: Option<ExceptionHandler> = self.handler();
        let quiet: bool = self.machine.options.quiet;
        self.machine.initialize_world(handler.as_ref(), !quiet);
    }

    /// Build the program's stack from its arguments and the environment.
    fn initialize_run_stack(&mut self) {
        let argv: Vec<Vec<u8>> = self.program_argv.clone();
        self.machine.initialize_run_stack(&argv, &environment());
    }

    /// Flush the listing trace, then exit.
    fn exit(&mut self, status: i32) -> ! {
        if let Some(listing) = self.machine.listing.as_mut() {
            let _ = listing.flush();
        }
        std::process::exit(status)
    }

    /// Run `body` before there is a top level to return to (while the
    /// world is set up and the `-f` files load): a run-time error there
    /// ends the engine with status 1, after its message.
    fn before_top_level<T>(&mut self, body: impl FnOnce(&mut Engine) -> T) -> T {
        match catch_run_error(|| body(self)) {
            Some(value) => value,
            None => self.exit(1),
        }
    }

    /// Take the next token; at the end of input, exit (`read_token`).
    fn read_token(&mut self) -> i32 {
        let token: i32 = self.machine.advance();
        if token == TOK_EOF {
            self.exit(0);
        }
        token
    }

    /// Skip the rest of the command line (`flush_to_newline`).
    fn flush_to_newline(&mut self) {
        while self.read_token() != TOK_NL {}
    }

    /// An optional integer operand, 0 if none (`get_opt_int`).
    fn get_opt_int(&mut self) -> i32 {
        let token: i32 = self.read_token();
        if token == TOK_INT {
            let value: i32 = self.machine.scanner.scan_value.int();
            self.flush_to_newline();
            value
        } else if token == TOK_NL {
            0
        } else {
            self.flush_to_newline();
            0
        }
    }

    /// Read a command word (`read_assembly_command`).
    fn read_assembly_command(&mut self) -> Command {
        let token: i32 = self.read_token();
        if token == TOK_NL {
            return Command::Redo;
        }
        if token != TOK_ID {
            return Command::Unknown;
        }
        let word: Vec<u8> = self.machine.scanner.scan_value.text();
        let table: [(&str, usize, Command); 19] = [
            ("exit", 2, Command::Exit),
            ("quit", 2, Command::Exit),
            ("print", 1, Command::Print),
            ("print_symbols", 7, Command::PrintSymbols),
            ("print_all_regs", 7, Command::PrintAllRegs),
            ("run", 2, Command::Run),
            ("args", 2, Command::Args),
            ("read", 2, Command::Read),
            ("load", 2, Command::Read),
            ("reinitialize", 6, Command::Reinitialize),
            ("step", 1, Command::Step),
            ("help", 1, Command::Help),
            ("continue", 1, Command::Continue),
            ("breakpoint", 2, Command::SetBreakpoint),
            ("delete", 1, Command::DeleteBreakpoint),
            ("list", 2, Command::ListBreakpoints),
            ("dumpnative", 5, Command::DumpNative),
            ("dump", 4, Command::Dump),
            ("disasm", 3, Command::Disasm),
        ];
        for (name, min_match, command) in table {
            if str_prefix(&word, name, min_match) {
                return command;
            }
        }
        match word.first() {
            Some(b'?') => Command::Help,
            Some(b'.') => Command::Asm,
            _ => Command::Unknown,
        }
    }

    /// Run the program at `addr`; report a breakpoint.
    fn run_and_report(&mut self, addr: u32, steps: i32, display: bool, cont_bkpt: bool) {
        let (breakpoint, _): (bool, bool) =
            self.machine.run_program(addr, steps, display, cont_bkpt);
        if breakpoint {
            error(&format!(
                "Breakpoint encountered at 0x{:08x}\n",
                self.machine.regs.pc
            ));
        }
    }

    /// Execute one REPL command; with `redo`, repeat the previous one.
    /// Returns true if the command was a blank line, asking for a repeat
    /// (`parse_spim_command`).
    fn parse_spim_command(&mut self, redo: bool) -> bool {
        let command: Command = if redo {
            self.prev_cmd
        } else {
            self.read_assembly_command()
        };
        match command {
            Command::Exit => self.exit(0),
            Command::Read => {
                let token: i32 = if redo {
                    self.prev_token
                } else {
                    self.read_token()
                };
                if !redo {
                    self.flush_to_newline();
                }
                if token == TOK_STR {
                    let name: Vec<u8> = self.machine.scanner.scan_value.text();
                    let _ = self.machine.read_assembly_file(&name);
                    self.machine.scanner.pop_source();
                } else {
                    error("Must supply a filename to read\n");
                }
                self.prev_cmd = Command::Read;
            }
            Command::Run => {
                if !redo {
                    self.run_addr = self.get_opt_int() as u32;
                }
                if self.run_addr == 0 {
                    self.run_addr = self.machine.starting_address();
                }
                self.initialize_run_stack();
                if self.run_addr != 0 {
                    if let Some(undefined) = self.machine.undefined_symbol_string() {
                        error("The following symbols are undefined:\n");
                        error(&undefined);
                        error("\n");
                    }
                    self.run_and_report(self.run_addr, DEFAULT_RUN_STEPS, false, false);
                }
                self.prev_cmd = Command::Run;
            }
            Command::Continue => {
                if self.machine.regs.pc != 0 {
                    let pc: u32 = self.machine.regs.pc;
                    self.run_and_report(pc, DEFAULT_RUN_STEPS, false, true);
                }
                self.prev_cmd = Command::Continue;
            }
            Command::Step => {
                if !redo {
                    self.step_count = self.get_opt_int();
                }
                let addr: u32 = if self.machine.regs.pc == 0 {
                    self.machine.starting_address()
                } else {
                    self.machine.regs.pc
                };
                if self.step_count == 0 {
                    self.step_count = 1;
                }
                if addr != 0 {
                    self.run_and_report(addr, self.step_count, true, true);
                }
                self.prev_cmd = Command::Step;
            }
            Command::Print => self.print_command(redo),
            Command::PrintSymbols => {
                self.machine.print_symbols();
                if !redo {
                    self.flush_to_newline();
                }
                self.prev_cmd = Command::Nop;
            }
            Command::PrintAllRegs => {
                let token: i32 = if redo {
                    self.prev_token
                } else {
                    self.read_token()
                };
                let hex: bool = token == TOK_ID && self.machine.scanner.scan_value.text() == b"hex";
                let mut text: Vec<u8> = Vec::new();
                self.machine.format_registers(&mut text, hex, hex);
                text.push(b'\n');
                error_bytes(&text);
                if !redo && token != TOK_NL {
                    self.flush_to_newline();
                }
                self.prev_cmd = Command::Nop;
            }
            Command::Reinitialize => {
                self.flush_to_newline();
                self.initialize_world();
                self.initialize_run_stack();
                self.machine.write_startup_message();
                self.prev_cmd = Command::Nop;
            }
            Command::Asm => {
                let _ = self.machine.parse_file();
                self.prev_cmd = Command::Asm;
            }
            Command::Redo => return true,
            Command::Nop => self.prev_cmd = Command::Nop,
            Command::Args => self.args_command(),
            Command::Help => {
                if !redo {
                    self.flush_to_newline();
                }
                error(HELP);
                self.prev_cmd = Command::Help;
            }
            Command::SetBreakpoint | Command::DeleteBreakpoint => {
                let token: i32 = if redo {
                    self.prev_token
                } else {
                    self.read_token()
                };
                if !redo && token != TOK_NL {
                    self.flush_to_newline();
                }
                if token == TOK_INT {
                    self.breakpoint_addr = if redo {
                        self.breakpoint_addr.wrapping_add(4)
                    } else {
                        self.machine.scanner.scan_value.int() as u32
                    };
                } else if token == TOK_ID {
                    self.breakpoint_addr = if redo {
                        self.breakpoint_addr.wrapping_add(4)
                    } else {
                        let name: String = self.machine.scanner.scan_value.name();
                        self.machine.find_symbol_address(&name)
                    };
                } else {
                    error("Must supply an address for breakpoint\n");
                    return false;
                }
                if command == Command::SetBreakpoint {
                    self.machine.add_breakpoint(self.breakpoint_addr);
                } else {
                    self.machine.delete_breakpoint(self.breakpoint_addr);
                }
                self.prev_cmd = command;
            }
            Command::ListBreakpoints => {
                if !redo {
                    self.flush_to_newline();
                }
                self.machine.list_breakpoints();
                self.prev_cmd = Command::ListBreakpoints;
            }
            Command::Dump | Command::DumpNative => self.dump_command(command, redo),
            Command::Disasm => {
                self.machine.print_text_listing();
                if !redo {
                    self.flush_to_newline();
                }
                self.prev_cmd = Command::Nop;
            }
            Command::Unknown => {
                while self.read_token() != TOK_NL {}
                error("Unknown spim command\n");
            }
        }
        false
    }

    /// `print $N`, `print $fN`, `print ADDR`, `print N($reg)`,
    /// `print LABEL`, `print $pc`...
    fn print_command(&mut self, redo: bool) {
        let token: i32 = if redo {
            self.prev_token
        } else {
            self.read_token()
        };
        let mut consumed_nl: bool = false;
        if token == TOK_REG {
            self.print_loc = if redo {
                self.print_loc.wrapping_add(1)
            } else {
                self.machine.scanner.scan_value.int()
            };
            self.print_reg(self.print_loc);
        } else if token == TOK_FP_REG {
            self.print_loc = if redo {
                self.print_loc.wrapping_add(2)
            } else {
                self.machine.scanner.scan_value.int()
            };
            self.print_fp_reg(self.print_loc);
        } else if token == TOK_INT {
            if redo {
                self.print_loc = self.print_loc.wrapping_add(4);
                self.machine.print_mem(self.print_loc as u32);
            } else {
                let value: i32 = self.machine.scanner.scan_value.int();
                let next: i32 = self.read_token();
                if next == i32::from(b'(') {
                    let reg_token: i32 = self.read_token();
                    let reg: i32 = if reg_token == TOK_REG {
                        self.machine.scanner.scan_value.int()
                    } else {
                        -1
                    };
                    let close: i32 = if reg_token == TOK_REG {
                        self.read_token()
                    } else {
                        0
                    };
                    if reg_token == TOK_NL || close == TOK_NL {
                        consumed_nl = true;
                    }
                    if reg < 0 || close != i32::from(b')') {
                        error("expected $reg) after '(' in print N($reg) form\n");
                    } else {
                        self.print_loc =
                            self.machine.regs.gpr[(reg & 31) as usize].wrapping_add(value);
                        self.machine.print_mem(self.print_loc as u32);
                    }
                } else {
                    self.print_loc = value;
                    self.machine.print_mem(value as u32);
                    if next == TOK_NL {
                        consumed_nl = true;
                    }
                }
            }
        } else if token == TOK_ID {
            let name: Vec<u8> = self.machine.scanner.scan_value.text();
            if !self.print_reg_from_string(&name) {
                if redo {
                    self.print_loc = self.print_loc.wrapping_add(4);
                } else {
                    let label: String = String::from_utf8_lossy(&name).into_owned();
                    self.print_loc = self.machine.find_symbol_address(&label) as i32;
                }
                if self.print_loc != 0 {
                    self.machine.print_mem(self.print_loc as u32);
                } else {
                    let mut message: Vec<u8> = b"Unknown label: ".to_vec();
                    message.extend_from_slice(&name);
                    message.push(b'\n');
                    error_bytes(&message);
                }
            }
        } else {
            error("Print what?\n");
        }
        if !redo && !consumed_nl {
            self.flush_to_newline();
        }
        self.prev_cmd = Command::Print;
        self.prev_token = token;
    }

    /// `Reg N = 0x... (...)` (`print_reg`).
    fn print_reg(&self, n: i32) {
        let value: i32 = usize::try_from(n)
            .ok()
            .and_then(|i: usize| self.machine.regs.gpr.get(i).copied())
            .unwrap_or(0);
        error(&format!("Reg {n} = 0x{:08x} ({value})\n", value as u32));
    }

    /// `FP reg N = ... (double)` and `(single)` (`print_fp_reg`).
    fn print_fp_reg(&self, n: i32) {
        let index: usize = usize::try_from(n).unwrap_or(usize::MAX);
        if n & 1 == 0 {
            let value: f64 = self.machine.regs.fpr_d(index);
            error(&format!(
                "FP reg {n} = {} (double)\n",
                crate::cformat::format_g6(value)
            ));
        }
        let value: f32 = self.machine.regs.fpr_s(index);
        error(&format!(
            "FP reg {n} = {} (single)\n",
            crate::cformat::format_g6(f64::from(value))
        ));
    }

    /// `print $pc`, `print hi`, ... (`print_reg_from_string`).  Returns
    /// false if the name is not one of these registers.
    fn print_reg_from_string(&self, name: &[u8]) -> bool {
        let lower: Vec<u8> = name.iter().take(100).map(u8::to_ascii_lowercase).collect();
        let name: &[u8] = lower.strip_prefix(b"$").unwrap_or(&lower);
        let regs = &self.machine.regs;
        let (label, value): (&str, i32) = match name {
            b"pc" => ("PC", regs.pc as i32),
            b"hi" => ("HI", regs.hi),
            b"lo" => ("LO", regs.lo),
            b"fpcond" => ("FCSR", regs.fcsr()),
            b"cause" => ("Cause", regs.cp0(crate::registers::CP0_CAUSE)),
            b"epc" => ("EPC", regs.cp0(crate::registers::CP0_EPC)),
            b"status" => ("Status", regs.cp0(crate::registers::CP0_STATUS)),
            b"badvaddr" => ("BadVAddr", regs.cp0(crate::registers::CP0_BADVADDR)),
            _ => return false,
        };
        error(&format!("{label} = 0x{:08x} ({value})\n", value as u32));
        true
    }

    /// `args A B C`: the arguments of the next run (`ARGS_CMD`).
    fn args_command(&mut self) {
        let argv0: Vec<u8> = self
            .program_argv
            .first()
            .cloned()
            .unwrap_or_else(|| b"<repl>".to_vec());
        let mut arguments: Vec<Vec<u8>> = vec![argv0];
        loop {
            let token: i32 = self.read_token();
            if token == TOK_NL || token == 0 {
                break;
            }
            if token == TOK_STR || token == TOK_ID {
                arguments.push(self.machine.scanner.scan_value.text());
            } else if token == TOK_INT {
                let value: i32 = self.machine.scanner.scan_value.int();
                arguments.push(value.to_string().into_bytes());
            }
        }
        self.program_argv = arguments;
        self.prev_cmd = Command::Args;
    }

    /// `dump ["FILE"]` / `dumpnative ["FILE"]`: write the user program's
    /// instruction words to a file.
    fn dump_command(&mut self, command: Command, redo: bool) {
        let token: i32 = if redo {
            self.prev_token
        } else {
            self.read_token()
        };
        let name: &str = if command == Command::Dump {
            "dump"
        } else {
            "dumpnative"
        };
        let filename: Vec<u8> = if token == TOK_STR {
            self.machine.scanner.scan_value.text()
        } else if token == TOK_NL {
            b"spim.dump".to_vec()
        } else {
            error(&format!("usage: {name} [ \"filename\" ]\n"));
            return;
        };
        let mut file: std::fs::File = match std::fs::File::create(os_path(&filename)) {
            Ok(file) => file,
            Err(failure) => {
                let mut message: Vec<u8> = filename.clone();
                message.extend_from_slice(format!(": {}\n", os_error_text(&failure)).as_bytes());
                error_bytes(&message);
                return;
            }
        };
        self.machine.user_kernel_text_segment(false);
        let start: u32 = self.machine.find_symbol_address("__eoth");
        let end: u32 = self.machine.current_text_pc();
        let mut words: i32 = 0;
        let mut bytes: Vec<u8> = Vec::new();
        let mut addr: u32 = start;
        while addr < end {
            let code: i32 = self
                .machine
                .mem_read_inst(addr)
                .map_or(0, |inst| inst.borrow().encode());
            if command == Command::Dump {
                bytes.extend_from_slice(&code.to_be_bytes());
            } else {
                bytes.extend_from_slice(&code.to_le_bytes());
            }
            words += 1;
            addr = addr.wrapping_add(4);
        }
        let _ = file.write_all(&bytes);
        let mut message: Vec<u8> =
            format!("Dumped {words} words starting at 0x{start:08x} to file ").into_bytes();
        message.extend_from_slice(&filename);
        message.push(b'\n');
        error_bytes(&message);
        self.prev_cmd = command;
    }

    /// The REPL (`top_level`): read a line, execute it, repeat.  A blank
    /// line repeats the previous command.
    fn top_level(&mut self) -> ! {
        self.machine.scanner.init(Vec::new());
        self.machine.asm.input_file_name = Some(b"<standard input>".to_vec());
        let mut redo: bool = false;
        loop {
            let mut pushed: bool = false;
            if !redo {
                let Some(line) = crate::console::read_command_line() else {
                    error("\n");
                    self.exit(0);
                };
                let mut input: Vec<u8> = line;
                input.push(b'\n');
                self.machine.scanner.push_source(input);
                pushed = true;
            }
            let current: bool = redo;
            match catch_run_error(|| self.parse_spim_command(current)) {
                Some(again) => redo = again,
                None => redo = false,
            }
            if pushed {
                self.machine.scanner.pop_source();
            }
        }
    }

    /// `-assemble`: write the assembled words to `NAME.out`
    /// (`write_assembled_code`).  Returns true on failure.
    fn write_assembled_code(&mut self, program_name: &[u8]) -> bool {
        if self.machine.asm.parse_error_occurred {
            return true;
        }
        let mut filename: Vec<u8> = program_name.to_vec();
        filename.extend_from_slice(b".out");
        let mut file: std::fs::File = match std::fs::File::create(os_path(&filename)) {
            Ok(file) => file,
            Err(failure) => {
                let mut message: Vec<u8> = filename.clone();
                message.extend_from_slice(format!(": {}\n", os_error_text(&failure)).as_bytes());
                error_bytes(&message);
                return true;
            }
        };
        let mut out: String = String::new();
        self.machine.user_kernel_text_segment(false);
        let start: u32 = self.machine.find_symbol_address("__eoth");
        let end: u32 = self.machine.current_text_pc();
        out.push_str(&format!(".text # 0x{start:x} .. 0x{end:x}\n.word "));
        let mut addr: u32 = start;
        while addr < end {
            let code: i32 = self
                .machine
                .mem_read_inst(addr)
                .map_or(0, |inst| inst.borrow().encode());
            let separator: &str = if addr != end.wrapping_sub(4) {
                ", "
            } else {
                ""
            };
            out.push_str(&format!("0x{:x}{separator}", code as u32));
            addr = addr.wrapping_add(4);
        }
        out.push('\n');
        self.machine.user_kernel_data_segment(false);
        let start: u32 = if self.machine.options.bare_machine {
            0
        } else {
            DATA_BOT
        };
        let end: u32 = self.machine.current_data_pc();
        if end > start {
            out.push_str(&format!(".data # 0x{start:x} .. 0x{end:x}\n.word "));
            let mut addr: u32 = start;
            while addr < end {
                let code: i32 = self.machine.mem_read_word(addr);
                let separator: &str = if addr != end.wrapping_sub(4) {
                    ", "
                } else {
                    ""
                };
                out.push_str(&format!("0x{:x}{separator}", code as u32));
                addr = addr.wrapping_add(4);
            }
            out.push('\n');
        }
        let _ = file.write_all(out.as_bytes());
        false
    }

    /// `-dump`/`-full_dump`: write `data.asm` and `text.asm`.
    fn dump_segments(&mut self, kernel_also: bool) {
        let mut data: Vec<u8> = Vec::new();
        if kernel_also {
            self.machine.format_data_segs(&mut data);
        } else {
            data.extend_from_slice(b"\tDATA\n");
            let top: u32 = self.machine.mem.data_top;
            self.machine.format_mem(&mut data, DATA_BOT, top);
        }
        let _ = std::fs::write("data.asm", &data);
        let mut text: Vec<u8> = Vec::new();
        let top: u32 = self.machine.mem.text_top;
        if kernel_also {
            self.machine.format_insts(&mut text, TEXT_BOT, top);
            text.extend_from_slice(b"\n\tKERNEL\n");
            let k_top: u32 = self.machine.mem.k_text_top;
            self.machine.format_insts(&mut text, K_TEXT_BOT, k_top);
        } else {
            text.extend_from_slice(b"\n\tUSER TEXT SEGMENT\n");
            self.machine.format_insts(&mut text, TEXT_BOT, top);
        }
        let _ = std::fs::write("text.asm", &text);
    }
}

/// The REPL's `help` text.
const HELP: &str = "
SPIM is a MIPS32 simulator.
Its top-level commands are:
exit  -- Exit the simulator
quit  -- Exit the simulator
read \"FILE\" -- Read FILE containing assembly code into memory
load \"FILE\" -- Same as read
run <ADDR> -- Start the program at (optional) ADDRESS
args <ARG ...> -- Set the args the next `run` will pass to the program;
                  argv[0] stays as the loaded program path.  `args`
                  with no operands clears the args.
step <N> -- Step the program for N instructions (default 1)
continue -- Continue program execution without stepping
print $N -- Print register N
print $fN -- Print floating point register N
print ADDR -- Print contents of memory at ADDRESS
print_symbols -- Print all global symbols
print_all_regs -- Print all MIPS registers
print_all_regs hex -- Print all MIPS registers in hex
reinitialize -- Clear the memory and registers
breakpoint <ADDR> -- Set a breakpoint at address ADDR
delete <ADDR> -- Delete breakpoint at address ADDR
list -- List all breakpoints
dump [ \"FILE\" ] -- Dump binary code to spim.dump or FILE in network byte order
dumpnative [ \"FILE\" ] -- Dump binary code to spim.dump or FILE in host byte order
disasm -- Print a disassembly listing of the loaded program's text segment
. -- Rest of line is assembly instruction to execute
<cr> -- Newline reexecutes previous command
? -- Print this message

Most commands can be abbreviated to their unique prefix
e.g., ex(it), re(ad), l(oad), ru(n), s(tep), p(rint)

";

/// The flags that set a segment size or limit.
#[derive(Clone, Copy, Debug)]
enum SizeFlag {
    TextSize,
    DataSize,
    DataLimit,
    StackSize,
    StackLimit,
    KTextSize,
    KDataSize,
    KDataLimit,
}

/// Which size flag `arg` is (`-stext`/`-st`, ...), if any.
fn size_flag(arg: &[u8]) -> Option<SizeFlag> {
    let flags: [(&str, &str, SizeFlag); 8] = [
        ("-stext", "-st", SizeFlag::TextSize),
        ("-sdata", "-sd", SizeFlag::DataSize),
        ("-ldata", "-ld", SizeFlag::DataLimit),
        ("-sstack", "-ss", SizeFlag::StackSize),
        ("-lstack", "-ls", SizeFlag::StackLimit),
        ("-sktext", "-skt", SizeFlag::KTextSize),
        ("-skdata", "-skd", SizeFlag::KDataSize),
        ("-lkdata", "-lkd", SizeFlag::KDataLimit),
    ];
    flags
        .iter()
        .find(|(long, short, _)| arg == long.as_bytes() || arg == short.as_bytes())
        .map(|(_, _, flag)| *flag)
}

/// True, after saying so, if the flag `args[i]`, which takes a value, is
/// the last argument (`flag_value_missing`).
fn flag_value_missing(args: &[Vec<u8>], i: usize) -> bool {
    if i + 1 < args.len() {
        return false;
    }
    let mut message: Vec<u8> = b"spimulator: ".to_vec();
    message.extend_from_slice(&args[i]);
    message.extend_from_slice(b" requires a value\n");
    error_bytes(&message);
    true
}

/// Refuse the teaching mode, which this port does not have.
fn explain_not_supported() -> ! {
    error("spimulator-rust: the teaching mode (-explain) is not supported by this port\n");
    std::process::exit(2)
}

/// The engine's `main`: parse the flags, load the files, then run the
/// program (or the REPL).  Returns the exit status.
pub fn main() -> i32 {
    use std::os::unix::ffi::OsStrExt;
    install_panic_hook();
    crate::console::restore_sigpipe();
    let args: Vec<Vec<u8>> = std::env::args_os()
        .map(|arg: std::ffi::OsString| arg.as_bytes().to_vec())
        .collect();
    let mut engine: Engine = Engine::new();
    if let Some(handler) = std::env::var_os("SPIM_EXCEPTION_HANDLER") {
        engine.exception_file = Some(ExceptionHandler::Files(handler.as_bytes().to_vec()));
    }
    let mut assemble: bool = false;
    let mut dump_user: bool = false;
    let mut dump_all: bool = false;
    let mut print_usage: bool = false;
    let mut file_loaded: bool = false;
    let mut file_attempted: bool = false;
    let mut world_initialized: bool = false;
    let mut last_file: Option<usize> = None;
    let argc: usize = args.len();
    let mut i: usize = 1;
    while i < argc {
        let arg: &[u8] = &args[i];
        let is: &dyn Fn(&str, &str) -> bool =
            &|long: &str, short: &str| arg == long.as_bytes() || arg == short.as_bytes();
        let options = &mut engine.machine.options;
        if is("-asm", "-a") {
            options.bare_machine = false;
            options.delayed_branches = false;
            options.delayed_loads = false;
        } else if is("-bare", "-b") {
            options.bare_machine = true;
            options.delayed_branches = true;
            options.delayed_loads = true;
            options.quiet = true;
        } else if is("-delayed_branches", "-db") {
            options.delayed_branches = true;
        } else if is("-delayed_loads", "-dl") {
            options.delayed_loads = true;
        } else if is("-exception", "-e") || is("-trap", "-t") {
            engine.load_exception_handler = true;
        } else if is("-noexception", "-ne") || is("-notrap", "-nt") {
            engine.load_exception_handler = false;
        } else if is("-exception_file", "-ef") || is("-trap_file", "-tf") {
            if flag_value_missing(&args, i) {
                print_usage = true;
            } else {
                i += 1;
                engine.exception_file = Some(ExceptionHandler::Files(args[i].clone()));
                engine.load_exception_handler = true;
            }
        } else if is("-mapped_io", "-mio") {
            options.mapped_io = true;
        } else if is("-nomapped_io", "-nmio") {
            options.mapped_io = false;
        } else if is("-pseudo", "-p") {
            options.accept_pseudo_insts = true;
        } else if is("-nopseudo", "-np") {
            options.accept_pseudo_insts = false;
        } else if is("-quiet", "-q") {
            options.quiet = true;
        } else if is("-noquiet", "-nq") {
            options.quiet = false;
        } else if is("-noexplain", "-nx") {
            // The teaching mode is off already.
        } else if arg.starts_with(b"-explain=") || arg.starts_with(b"-x=") {
            let position: usize = arg.iter().position(|b: &u8| *b == b'=').unwrap_or(0);
            let level: &[u8] = &arg[position + 1..];
            if level.len() == 1 && (b'0'..=b'4').contains(&level[0]) {
                if level[0] != b'0' {
                    explain_not_supported();
                }
            } else {
                let mut message: Vec<u8> = b"spimulator: ".to_vec();
                message.extend_from_slice(arg);
                message.extend_from_slice(b" requires a level in 0..4\n");
                error_bytes(&message);
                print_usage = true;
            }
        } else if is("-explain", "-x") {
            let next: Option<&Vec<u8>> = args.get(i + 1);
            match next {
                Some(level) if level.len() == 1 && (b'0'..=b'4').contains(&level[0]) => {
                    i += 1;
                    if level[0] != b'0' {
                        explain_not_supported();
                    }
                }
                _ => explain_not_supported(),
            }
        } else if let Some(field) = size_flag(arg) {
            // -stext N, -sdata N, ...: a size or limit (C's atoi).
            if flag_value_missing(&args, i) {
                print_usage = true;
            } else {
                i += 1;
                let value: i32 = crate::arith::c_atoi(&args[i]);
                match field {
                    SizeFlag::TextSize => options.initial_text_size = value,
                    SizeFlag::DataSize => options.initial_data_size = value,
                    SizeFlag::DataLimit => options.initial_data_limit = value,
                    SizeFlag::StackSize => options.initial_stack_size = value,
                    SizeFlag::StackLimit => options.initial_stack_limit = value,
                    SizeFlag::KTextSize => options.initial_k_text_size = value,
                    SizeFlag::KDataSize => options.initial_k_data_size = value,
                    SizeFlag::KDataLimit => options.initial_k_data_limit = value,
                }
            }
        } else if (is("-file", "-f") && i + 1 < argc) || arg.first() != Some(&b'-') {
            // `-f FILE` may repeat; a bare FILE is both the program and
            // its argv[0], and everything after it is the program's argv.
            let explicit: bool = arg.first() == Some(&b'-');
            if !explicit && file_loaded {
                break;
            }
            let file_index: usize = if explicit { i + 1 } else { i };
            if !world_initialized {
                engine.before_top_level(Engine::initialize_world);
                world_initialized = true;
            }
            file_attempted = true;
            let name: Vec<u8> = args[file_index].clone();
            let loaded: bool = engine
                .before_top_level(|engine: &mut Engine| engine.machine.read_assembly_file(&name));
            if !loaded {
                break;
            }
            file_loaded = true;
            last_file = Some(file_index);
            if explicit {
                i = file_index + 1;
                continue;
            }
            engine.program_argv = args[file_index..].to_vec();
            break;
        } else if arg == b"-assemble" {
            assemble = true;
        } else if arg == b"-print-ast" {
            engine.machine.asm.print_ast = true;
            engine.machine.asm.print_ast_only = true;
        } else if arg == b"-show-expansion" {
            engine.machine.asm.show_expansion = true;
            engine.machine.asm.print_ast_only = true;
        } else if arg == b"-print-ast-json" {
            engine.machine.asm.print_ast_json = true;
            engine.machine.asm.print_ast_only = true;
        } else if arg == b"-listing" {
            if i + 1 >= argc {
                error("\n-listing requires a filename argument\n");
                print_usage = true;
                i += 1;
                continue;
            }
            i += 1;
            let path: &[u8] = &args[i];
            if path == b"-" {
                engine.machine.listing = Some(Box::new(std::io::stderr()));
            } else {
                match std::fs::File::create(os_path(path)) {
                    Ok(file) => {
                        engine.machine.listing = Some(Box::new(std::io::BufWriter::new(file)));
                    }
                    Err(_) => {
                        let mut message: Vec<u8> = b"\nCannot open listing file '".to_vec();
                        message.extend_from_slice(path);
                        message.extend_from_slice(b"'\n");
                        error_bytes(&message);
                        print_usage = true;
                    }
                }
            }
        } else if arg == b"-dump" {
            dump_user = true;
        } else if arg == b"-full_dump" {
            dump_all = true;
        } else {
            let mut message: Vec<u8> = b"\nUnknown argument: ".to_vec();
            message.extend_from_slice(arg);
            message.extend_from_slice(b" (ignored)\n");
            error_bytes(&message);
            print_usage = true;
        }
        i += 1;
    }

    // With only `-f` files, argv[0] is the last file loaded.
    if file_loaded && engine.program_argv.is_empty() {
        if let Some(index) = last_file {
            engine.program_argv = args[index..].to_vec();
        }
    }
    if print_usage {
        error(USAGE);
    }
    if !file_loaded {
        if file_attempted {
            // The file could not be opened: do not wait for REPL input.
            let status: i32 = engine.machine.return_value;
            engine.exit(if status != 0 { status } else { 2 });
        }
        engine.before_top_level(Engine::initialize_world);
        engine.before_top_level(Engine::initialize_run_stack);
        engine.top_level();
    }
    if engine.machine.asm.parse_errors_seen > 0 {
        let status: i32 = engine.machine.return_value;
        engine.exit(if status != 0 { status } else { 2 });
    }
    if engine.machine.asm.print_ast_only {
        engine.exit(0);
    }
    if assemble {
        let name: Vec<u8> = engine.program_argv.first().cloned().unwrap_or_default();
        let failed: bool = engine.write_assembled_code(&name);
        engine.exit(i32::from(failed));
    } else if dump_user {
        engine.dump_segments(false);
    } else if dump_all {
        engine.dump_segments(true);
    } else {
        engine.before_top_level(Engine::initialize_run_stack);
        let undefined: Option<String> = engine.machine.undefined_symbol_string();
        if let Some(undefined) = undefined {
            error("The following symbols are undefined:\n");
            error(&undefined);
            error("\n");
            let status: i32 = engine.machine.return_value;
            engine.exit(if status != 0 { status } else { 1 });
        }
        let _ = catch_run_error(|| {
            let start: u32 = engine.machine.find_symbol_address("__start");
            engine
                .machine
                .run_program(start, DEFAULT_RUN_STEPS, false, false)
        });
        // A fault the program did not report itself through exit2 becomes
        // the exit status 128 + its exception code.
        if engine.machine.first_bad_exception != -1 && engine.machine.return_value == 0 {
            engine.machine.return_value = 128 + engine.machine.first_bad_exception;
        }
    }
    let status: i32 = engine.machine.return_value;
    engine.exit(status)
}
