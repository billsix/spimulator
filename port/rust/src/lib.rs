//! `spimulator-rust`: the spimulator MIPS32 simulator engine in Rust.
//!
//! Milestone 1 of the port: the machine arithmetic where Rust and C
//! differ, C's float formatting, and the opcode table.

pub mod arith;
pub mod cformat;
pub mod opcode_types;
pub mod opcodes;
mod opcodes_table;
