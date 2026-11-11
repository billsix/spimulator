//! The opcode table and the lookups built on it (`include/tokens.h`, and
//! the tables of `scanner.c` and `instruction.c`).
//!
//! `include/opcodes.tsv` is the single list of every mnemonic.
//! `tools/gen_opcodes.py` generates the C engine's tables from it, and this
//! port's (`opcodes_table.rs` and `opcode_types.rs`); the port answers every
//! question from those arrays.

use std::cmp::Ordering;

use crate::opcode_types::OpType;
pub use crate::opcodes_table::*;

/// One row of `include/opcodes.tsv`.
#[derive(Clone, Copy, Debug)]
pub struct OpcodeRow {
    /// The mnemonic as the student types it (`"addu"`, `".word"`).
    pub name: &'static str,
    /// The token the scanner returns for it.
    pub token: i32,
    /// Which operands it takes.
    pub op_type: OpType,
    /// The fixed bits of its binary encoding, or -1 for a directive or a
    /// pseudo-instruction.
    pub encoding: i32,
}

// The structural tokens of `tokens.h`.  They start at 256 so they never
// collide with a punctuation token, which is the character itself
// (`'+' as i32`).

/// End of input.
pub const TOK_EOF: i32 = 256;
/// End of a statement: a newline or a `;`.
pub const TOK_NL: i32 = 257;
/// An integer (or a character literal, or a defined constant).
pub const TOK_INT: i32 = 258;
/// An identifier (a label name, or a REPL command word).
pub const TOK_ID: i32 = 259;
/// A general register, `$t0` or `$8`.
pub const TOK_REG: i32 = 260;
/// A floating-point register, `$f2`.
pub const TOK_FP_REG: i32 = 261;
/// A string literal.
pub const TOK_STR: i32 = 262;
/// A floating-point literal, `3.25`.
pub const TOK_FP: i32 = 263;
/// The token of the first row of the opcode table.
pub const FIRST_KEYWORD_TOKEN: i32 = 264;

/// `bsearch` exactly as glibc implements it: probe the middle, halve.
///
/// The C engine looks keywords up with `bsearch`, and the answer of a
/// binary search over a table depends on how the search probes it, so the
/// port probes the same way.  Returns the index of a matching element.
///
/// ```
/// use spimulator::opcodes::c_bsearch;
/// let table: [i32; 5] = [1, 3, 5, 7, 9];
/// assert_eq!(c_bsearch(&table, |x: &i32| 7.cmp(x)), Some(3));
/// assert_eq!(c_bsearch(&table, |x: &i32| 4.cmp(x)), None);
/// ```
pub fn c_bsearch<T>(table: &[T], compare_key_to: impl Fn(&T) -> Ordering) -> Option<usize> {
    let mut low: usize = 0;
    let mut high: usize = table.len();
    while low < high {
        let middle: usize = (low + high) / 2;
        match compare_key_to(&table[middle]) {
            Ordering::Less => high = middle,
            Ordering::Greater => low = middle + 1,
            Ordering::Equal => return Some(middle),
        }
    }
    None
}

/// The row for a mnemonic or directive name, as the scanner's
/// `check_keyword` finds it (a `strcmp` binary search of the table in
/// `opcodes.tsv` order).
///
/// ```
/// use spimulator::opcodes::{keyword_row, TOK_ADDU_OPCODE};
/// assert_eq!(keyword_row(b"addu").map(|r| r.token), Some(TOK_ADDU_OPCODE));
/// assert!(keyword_row(b"loop").is_none());
/// ```
pub fn keyword_row(name: &[u8]) -> Option<&'static OpcodeRow> {
    let index: Option<usize> = c_bsearch(&OPCODE_ROWS, |row: &OpcodeRow| {
        name.cmp(row.name.as_bytes())
    });
    index.map(|i: usize| &OPCODE_ROWS[i])
}

/// The row of a keyword token.
pub fn token_row(token: i32) -> Option<&'static OpcodeRow> {
    let index: i32 = token - FIRST_KEYWORD_TOKEN;
    usize::try_from(index)
        .ok()
        .and_then(|i: usize| OPCODE_ROWS.get(i))
}

/// The mnemonic of a token (`op_token_name`), or `"<unknown>"`.
///
/// ```
/// use spimulator::opcodes::{token_name, TOK_LI_PSEUDO_OP};
/// assert_eq!(token_name(TOK_LI_PSEUDO_OP), "li");
/// assert_eq!(token_name(7), "<unknown>");
/// ```
pub fn token_name(token: i32) -> &'static str {
    token_row(token).map_or("<unknown>", |row: &OpcodeRow| row.name)
}

/// The operand shape of a token (`find_op_type`), if it is a keyword.
pub fn token_type(token: i32) -> Option<OpType> {
    token_row(token).map(|row: &OpcodeRow| row.op_type)
}

/// The instruction token whose fixed bits are `a_opcode`, as the C
/// engine's decoder resolves it (see `DECODE_TABLE`).
pub fn decode_token(a_opcode: i32) -> Option<i32> {
    DECODE_TABLE
        .binary_search_by(|entry: &(i32, i32)| entry.0.cmp(&a_opcode))
        .ok()
        .map(|i: usize| DECODE_TABLE[i].1)
}
