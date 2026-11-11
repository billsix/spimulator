//! `spimulator-rust`: the spimulator MIPS32 simulator engine in Rust.
//!
//! spimulator is a fork of James Larus's SPIM.  This crate is a port of
//! its C engine (the standard it is tested against, byte for byte): the
//! same command line, the same output, the same exit status.  The modules
//! mirror the C files, so the two can be read side by side:
//!
//! | module          | C file(s)                         | what it does                     |
//! |-----------------|-----------------------------------|----------------------------------|
//! | [`cli`]         | `spim.c`                          | flags, the REPL                  |
//! | [`scanner`]     | `scanner.c`                       | source text to tokens            |
//! | [`parser`]      | `parser.c`                        | tokens to a syntax tree; emit it |
//! | [`pseudo_op`]   | `pseudo-op.c`                     | pseudo-instruction expansions    |
//! | [`ast`]         | `ast.c`                           | the tree and its printers        |
//! | [`instruction`] | `instruction.c`                   | encode, decode, disassemble      |
//! | [`symbol_table`]| `symbol-table.c`                  | labels and forward references    |
//! | [`data`]        | `data.c`                          | data directives                  |
//! | [`memory`]      | `memory.c`                        | segments, loads and stores       |
//! | [`run`]         | `run.c`                           | the fetch-execute loop           |
//! | [`syscall`]     | `syscall.c`                       | system calls, exception messages |
//! | [`spim_utils`]  | `spim-utils.c`                    | set-up, stack, breakpoints       |
//! | [`display`]     | `display-utils.c`                 | register and memory dumps        |
//! | [`registers`]   | `registers.h`                     | the register file                |
//! | [`opcodes`]     | `opcodes.h`, `tokens.h`           | the opcode table                 |
//! | [`listing`]     | `assembler-event.c`               | the `-listing` trace             |
//! | [`console`]     | IO in `spim.c`                    | unbuffered input and output      |
//! | [`cformat`]     | (glibc `printf`)                  | C's float formatting             |
//! | [`arith`]       | (C conversions)                   | where Rust and C differ          |
//! | [`machine`]     | (the globals)                     | the machine; error reporting     |
//!
//! The teaching mode (`-explain`, `explain.c`) is not ported.

pub mod arith;
pub mod ast;
pub mod cformat;
pub mod cli;
pub mod console;
pub mod data;
pub mod display;
pub mod instruction;
pub mod listing;
pub mod machine;
pub mod memory;
pub mod opcode_types;
pub mod opcodes;
mod opcodes_table;
pub mod parser;
pub mod pseudo_op;
pub mod registers;
pub mod run;
pub mod scanner;
pub mod spim_utils;
pub mod symbol_table;
pub mod syscall;
