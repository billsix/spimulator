//! The whole simulated machine in one struct, and the ways the engine
//! reports errors.
//!
//! The C engine keeps its state in about a hundred global variables spread
//! over its files.  The port gathers them into one [`Machine`], passed as
//! `&mut self` to the functions that need it; each field is grouped the
//! way the C files group their globals.
//!
//! Errors come in three strengths, as in C:
//! - [`error`] prints a message on stderr and carries on;
//! - [`run_error`] prints and abandons what the machine is doing, back to
//!   the top level (the C engine `longjmp`s there; the port unwinds, and
//!   the top level catches it with [`catch_run_error`]);
//! - [`fatal_error`] prints and exits with status 255.

use std::io::Write;
use std::panic::{self, AssertUnwindSafe};
use std::rc::Rc;
use std::time::Instant;

use crate::ast::AstNode;
use crate::instruction::{ImmExpr, InstRef, Instruction};
use crate::memory::{MappedIo, Memory};
use crate::opcodes::TOK_BREAK_OPCODE;
use crate::registers::Registers;
use crate::scanner::Scanner;
use crate::symbol_table::{LabelId, SymbolTable};

/// The engine's settings from the command line (`spim.c`'s flags).
#[derive(Clone, Debug)]
pub struct Options {
    /// `-bare`: no pseudo-instructions, no `$gp` area.
    pub bare_machine: bool,
    /// Accept pseudo-instructions (`-pseudo`/`-nopseudo`).
    pub accept_pseudo_insts: bool,
    /// `-delayed_branches`: run the instruction after a branch first.
    pub delayed_branches: bool,
    /// `-delayed_loads`: a load's value arrives one instruction late.
    pub delayed_loads: bool,
    /// `-quiet`: no warnings, no exception messages.
    pub quiet: bool,
    /// `-mapped_io`: the memory-mapped console.
    pub mapped_io: bool,
    /// `-stext`: initial size of the text segment.
    pub initial_text_size: i32,
    /// `-sdata`.
    pub initial_data_size: i32,
    /// `-ldata`.
    pub initial_data_limit: i32,
    /// `-sstack`.
    pub initial_stack_size: i32,
    /// `-lstack`.
    pub initial_stack_limit: i32,
    /// `-sktext`.
    pub initial_k_text_size: i32,
    /// `-skdata`.
    pub initial_k_data_size: i32,
    /// `-lkdata`.
    pub initial_k_data_limit: i32,
}

impl Default for Options {
    fn default() -> Self {
        Options {
            bare_machine: false,
            accept_pseudo_insts: true,
            delayed_branches: false,
            delayed_loads: false,
            quiet: false,
            mapped_io: false,
            initial_text_size: 256 * 1024,
            initial_data_size: 256 * 1024,
            initial_data_limit: 1024 * 1024,
            initial_stack_size: 64 * 1024,
            initial_stack_limit: 256 * 1024,
            initial_k_text_size: 64 * 1024,
            initial_k_data_size: 64 * 1024,
            initial_k_data_limit: 1024 * 1024,
        }
    }
}

/// Where the assembled code and data go, and the parser's state
/// (the globals of `instruction.c`, `data.c`, `parser.c`, `pseudo-op.c`).
#[derive(Debug, Default)]
pub struct AsmState {
    /// Address of the next instruction in the user text segment.
    pub next_text_pc: u32,
    /// Address of the next instruction in the kernel text segment.
    pub next_k_text_pc: u32,
    /// Instructions go to the kernel text segment (`.ktext`).
    pub text_in_kernel: bool,
    /// Address of the next datum in the user data segment.
    pub next_data_pc: u32,
    /// Address of the next datum in the kernel data segment.
    pub next_k_data_pc: u32,
    /// Data goes to the kernel data segment (`.kdata`).
    pub data_in_kernel: bool,
    /// Address of the next item in the `$gp` small-data area.
    pub next_gp_item_addr: u32,
    /// Align `.word`, `.half`, ... to their size (off after `.align 0`).
    pub data_auto_align: bool,
    /// How deep the store helpers are nested (one listing event per
    /// directive).
    pub data_store_depth: i32,
    /// While the AST is emitted, the source text to record with each
    /// instruction (the scanner has moved on by then).
    pub emit_source_active: bool,
    /// The text for `emit_source_active`.
    pub emit_source_text: Option<Rc<[u8]>>,
    /// The parser is in a data segment.
    pub data_dir: bool,
    /// The parser is in a text segment.
    pub text_dir: bool,
    /// The name of the file being assembled, for messages.
    pub input_file_name: Option<Vec<u8>>,
    /// `.asciiz` (true) or `.ascii`.
    pub null_term: bool,
    /// `-print-ast`: print the tree on stderr.
    pub print_ast: bool,
    /// `-show-expansion`: print the pseudo-instructions on stderr.
    pub show_expansion: bool,
    /// `-print-ast-json`: print the tree as JSON on stdout.
    pub print_ast_json: bool,
    /// One of the three above was given: do not emit any code.
    pub print_ast_only: bool,
    /// The first undefined label of the expression being parsed.
    pub expr_unresolved_label: Option<LabelId>,
    /// The tree of the file being parsed.
    pub file: Option<AstNode>,
    /// The pseudo-instructions being expanded; their children are
    /// appended to the innermost.
    pub pseudo_stack: Vec<AstNode>,
    /// The values of the `.byte`/`.half`/`.word` being parsed.
    pub data_int_buf: Vec<ImmExpr>,
    /// The values of the `.float`/`.double` being parsed.
    pub data_fp_buf: Vec<f64>,
    /// The source line of the data directive being parsed.
    pub data_directive_line: i32,
    /// The labels defined on the line being emitted, newest first; an
    /// alignment moves them along.
    pub this_line_labels: Vec<LabelId>,
    /// The parser's copy of `data_auto_align`.
    pub auto_align: bool,
    /// The line being parsed has an error.
    pub parse_error_occurred: bool,
    /// Errors in the file so far.
    pub parse_errors_seen: i32,
}

/// Where a delayed load will write (`-delayed_loads`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadDest {
    /// A general register.
    Gpr(usize),
    /// A floating-point register, as a raw word.
    Fpr(usize),
}

/// The executor's state that lives between instructions (`run.c`).
#[derive(Debug, Default)]
pub struct RunState {
    /// The instruction running is in a branch delay slot.
    pub running_in_delay_slot: bool,
    /// The load issued by the current instruction (`-delayed_loads`).
    pub delayed_load_1: Option<(LoadDest, i32)>,
    /// The load issued by the previous instruction.
    pub delayed_load_2: Option<(LoadDest, i32)>,
    /// When the CP0 timer next ticks (it ticks every 10 ms of real time).
    pub timer_deadline: Option<Instant>,
    /// Stop the run loop at the next instruction.
    pub force_break: bool,
}

/// A breakpoint: where it is and the instruction it replaced.
#[derive(Clone, Debug)]
pub struct Breakpoint {
    /// The address.
    pub addr: u32,
    /// The instruction the breakpoint instruction replaced.
    pub instruction: Option<InstRef>,
}

/// The simulated machine.
pub struct Machine {
    /// Command-line settings.
    pub options: Options,
    /// The registers.
    pub regs: Registers,
    /// The memory segments.
    pub mem: Memory,
    /// The memory-mapped console.
    pub mmio: MappedIo,
    /// The labels.
    pub symbols: SymbolTable,
    /// The scanner reading the current assembly source.
    pub scanner: Scanner,
    /// The assembler's and parser's state.
    pub asm: AsmState,
    /// The executor's state.
    pub run: RunState,
    /// An exception was raised by the current instruction.
    pub exception_occurred: bool,
    /// The code of the first exception that was a fault (not an interrupt,
    /// a syscall or a breakpoint), or -1; the exit status reports it.
    pub first_bad_exception: i32,
    /// The single instruction every breakpoint uses (`break 1`).
    pub break_inst: InstRef,
    /// The breakpoints, newest first.
    pub breakpoints: Vec<Breakpoint>,
    /// The engine's exit status.
    pub return_value: i32,
    /// `-listing FILE`: the assembly event trace.
    pub listing: Option<Box<dyn Write>>,
}

impl Default for Machine {
    fn default() -> Self {
        Self::new()
    }
}

impl Machine {
    /// A machine with no memory yet; `initialize_world` sets it up.
    pub fn new() -> Machine {
        Machine {
            options: Options::default(),
            regs: Registers::default(),
            mem: Memory::default(),
            mmio: MappedIo::default(),
            symbols: SymbolTable::default(),
            scanner: Scanner::default(),
            asm: AsmState {
                text_dir: true,
                data_auto_align: true,
                auto_align: true,
                ..AsmState::default()
            },
            run: RunState::default(),
            exception_occurred: false,
            first_bad_exception: -1,
            break_inst: Instruction::r_type(TOK_BREAK_OPCODE, 1, 0, 0).into_ref(),
            breakpoints: Vec::new(),
            return_value: 0,
            listing: None,
        }
    }
}

/// Write bytes to a stream and flush them at once: the C engine flushes
/// after every message, so the order of stdout and stderr is kept.
fn write_flushed(stream: &mut dyn Write, bytes: &[u8]) {
    // Like the C engine, ignore a failure to write (a closed pipe).
    let _ = stream.write_all(bytes);
    let _ = stream.flush();
}

/// Print a message on stderr (`error`).
pub fn error(message: &str) {
    error_bytes(message.as_bytes());
}

/// Print raw bytes on stderr.
pub fn error_bytes(message: &[u8]) {
    write_flushed(&mut std::io::stderr().lock(), message);
}

/// Print the program's output on stdout.
pub fn write_stdout(bytes: &[u8]) {
    write_flushed(&mut std::io::stdout().lock(), bytes);
}

/// The value a run-time error unwinds with.
#[derive(Debug)]
pub struct RunErrorUnwind;

/// Print `message` on stderr and abandon the current command (`run_error`).
pub fn run_error(message: &str) -> ! {
    error(message);
    panic::panic_any(RunErrorUnwind)
}

/// Print `message` on stderr and exit with status 255 (`fatal_error`).
pub fn fatal_error(message: &str) -> ! {
    error(message);
    std::process::exit(255)
}

/// Run `body`; if it stops with a [`run_error`], return `None` instead of
/// unwinding further (the C engine's `setjmp`).  Any other panic is a bug
/// in the port and keeps unwinding.
pub fn catch_run_error<T>(body: impl FnOnce() -> T) -> Option<T> {
    match panic::catch_unwind(AssertUnwindSafe(body)) {
        Ok(value) => Some(value),
        Err(payload) if payload.is::<RunErrorUnwind>() => None,
        Err(payload) => panic::resume_unwind(payload),
    }
}

/// Keep the default panic message for real panics, but say nothing for a
/// [`run_error`], which has printed its own message.
pub fn install_panic_hook() {
    let default_hook = panic::take_hook();
    panic::set_hook(Box::new(move |info: &panic::PanicHookInfo<'_>| {
        if !info.payload().is::<RunErrorUnwind>() {
            default_hook(info);
        }
    }));
}
