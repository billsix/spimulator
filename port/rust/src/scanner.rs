//! The lexical scanner: assembly source text to tokens (`scanner.c`).
//!
//! The scanner reads its input a line at a time (as `fgets` does, at most
//! 4095 bytes at once) and cuts it into tokens: numbers, identifiers,
//! registers, strings, punctuation, and `TOK_NL` at the end of each
//! statement (a newline or a `;`).  A comma is skipped like a space, so
//! `add $t0 $t1 $t2` assembles.
//!
//! The parser looks at most two tokens ahead (`peek`, `peek2`), so the
//! scanner keeps a small buffer of tokens already cut.  That buffer
//! matters to the output: the line number the scanner reports moves to
//! the next line as soon as the newline has been *looked at*, and the
//! listings and error messages of the C engine show exactly that, so the
//! port keeps the same buffer.

use crate::arith::{c_atof, c_atoi};
use crate::machine::{error_bytes, fatal_error};
use crate::opcode_types::OpType;
use crate::opcodes::{c_bsearch, keyword_row, TOK_EOF, TOK_FP, TOK_FP_REG, TOK_ID, TOK_INT};
use crate::opcodes::{TOK_NL, TOK_REG, TOK_STR};
use crate::symbol_table::{Label, SymbolTable};

/// The size of the line buffer (`LINE_BUF_MAX`).
const LINE_BUF_MAX: usize = 4096;

/// The register names and numbers, sorted for a binary search
/// (`register_tbl`).
const REGISTER_NAMES: [(&str, i32); 35] = [
    ("a0", 4),
    ("a1", 5),
    ("a2", 6),
    ("a3", 7),
    ("at", 1),
    ("fp", 30),
    ("gp", 28),
    ("k0", 26),
    ("k1", 27),
    ("kt0", 26),
    ("kt1", 27),
    ("ra", 31),
    ("s0", 16),
    ("s1", 17),
    ("s2", 18),
    ("s3", 19),
    ("s4", 20),
    ("s5", 21),
    ("s6", 22),
    ("s7", 23),
    ("s8", 30),
    ("sp", 29),
    ("t0", 8),
    ("t1", 9),
    ("t2", 10),
    ("t3", 11),
    ("t4", 12),
    ("t5", 13),
    ("t6", 14),
    ("t7", 15),
    ("t8", 24),
    ("t9", 25),
    ("v0", 2),
    ("v1", 3),
    ("zero", 0),
];

/// The register number a name after `$` stands for, or -1
/// (`register_name_to_number`): `"8"` and `"t0"` are both 8, `"f2"` is
/// floating-point register 2.
///
/// ```
/// use spimulator::scanner::register_name_to_number;
/// assert_eq!(register_name_to_number(b"t0"), 8);
/// assert_eq!(register_name_to_number(b"31"), 31);
/// assert_eq!(register_name_to_number(b"f12"), 12);
/// assert_eq!(register_name_to_number(b"fp"), 30);
/// assert_eq!(register_name_to_number(b"pc"), -1);
/// ```
pub fn register_name_to_number(name: &[u8]) -> i32 {
    let c1: u8 = name.first().copied().unwrap_or(0);
    let c2: u8 = name.get(1).copied().unwrap_or(0);
    let c3: u8 = name.get(2).copied().unwrap_or(0);
    if c1.is_ascii_digit() && (c2 == 0 || (c2.is_ascii_digit() && c3 == 0)) {
        c_atoi(name)
    } else if c1 == b'f' && c2.is_ascii_digit() {
        // $f0..$f31 only; the digits are read without overflow (C: strtol,
        // then the range test), so a long number is not a register.
        let fp_reg: u64 = name[1..]
            .iter()
            .take_while(|c: &&u8| c.is_ascii_digit())
            .fold(0, |acc: u64, c: &u8| {
                acc.saturating_mul(10).saturating_add(u64::from(c - b'0'))
            });
        i32::try_from(fp_reg)
            .ok()
            .filter(|n: &i32| *n <= 31)
            .unwrap_or(-1)
    } else {
        let index: Option<usize> = c_bsearch(&REGISTER_NAMES, |entry: &(&str, i32)| {
            name.cmp(entry.0.as_bytes())
        });
        index.map_or(-1, |i: usize| REGISTER_NAMES[i].1)
    }
}

/// The value a token carries (`scan_value`).
#[derive(Clone, Debug, Default, PartialEq)]
pub enum ScanValue {
    /// No value.
    #[default]
    None,
    /// An integer or a register number.
    Int(i32),
    /// An identifier's name or a string literal's bytes.
    Text(Vec<u8>),
    /// A floating-point literal.
    Fp(f64),
}

impl ScanValue {
    /// The integer value (0 if the token had none).
    pub fn int(&self) -> i32 {
        match self {
            ScanValue::Int(value) => *value,
            _ => 0,
        }
    }

    /// The text value (empty if the token had none).
    pub fn text(&self) -> Vec<u8> {
        match self {
            ScanValue::Text(text) => text.clone(),
            _ => Vec::new(),
        }
    }

    /// The text value as a string (identifiers are always ASCII).
    pub fn name(&self) -> String {
        String::from_utf8_lossy(&self.text()).into_owned()
    }

    /// The floating-point value.
    pub fn fp(&self) -> f64 {
        match self {
            ScanValue::Fp(value) => *value,
            _ => 0.0,
        }
    }
}

/// One token: its kind (a `TOK_*` or a punctuation character) and value.
#[derive(Clone, Debug, PartialEq)]
struct Token {
    kind: i32,
    value: ScanValue,
}

impl Token {
    fn plain(kind: i32) -> Token {
        Token {
            kind,
            value: ScanValue::None,
        }
    }
}

/// The scanner's whole state, saved and restored as one piece by
/// `push_source`/`pop_source` (the REPL scans each command line as its own
/// input, then goes back).
#[derive(Clone, Debug)]
struct ScanState {
    /// The input, read whole, and how far `fgets` has read it.
    input: Vec<u8>,
    input_pos: usize,
    /// The line buffer, as `fgets` fills it: the bytes of the current line,
    /// a NUL, and whatever earlier lines left after it.
    line_buf: Vec<u8>,
    line_pos: usize,
    line_len: usize,
    at_eof: bool,
    line_no: i32,
    current_line_buf: Vec<u8>,
    current_line_no_saved: i32,
    current_line_saved: bool,
    force_id_next: bool,
    tokens: [Option<Token>; 3],
}

impl Default for ScanState {
    fn default() -> Self {
        ScanState {
            input: Vec::new(),
            input_pos: 0,
            line_buf: vec![0; LINE_BUF_MAX],
            line_pos: 0,
            line_len: 0,
            at_eof: false,
            line_no: 1,
            current_line_buf: Vec::new(),
            current_line_no_saved: 0,
            current_line_saved: false,
            force_id_next: false,
            tokens: [None, None, None],
        }
    }
}

/// The scanner.
#[derive(Clone, Debug, Default)]
pub struct Scanner {
    state: ScanState,
    saved: Vec<ScanState>,
    /// The value of the last token taken with `advance` that had one.
    pub scan_value: ScanValue,
}

/// What the scanner needs to know about the rest of the machine: which
/// labels are constants, and whether pseudo-instructions are keywords.
pub struct ScanContext<'a> {
    /// The symbol table, for `X = 5` constants.
    pub symbols: &'a SymbolTable,
    /// Pseudo-instructions are recognized as keywords.
    pub allow_pseudo_ops: bool,
}

impl Scanner {
    /// Start scanning `input` (`scanner_init`).  The text of the last
    /// line seen is kept: error messages may still print it.
    pub fn init(&mut self, input: Vec<u8>) {
        let state: &mut ScanState = &mut self.state;
        state.input = input;
        state.input_pos = 0;
        state.line_pos = 0;
        state.line_len = 0;
        state.at_eof = false;
        state.line_no = 1;
        state.current_line_saved = false;
        state.force_id_next = false;
        state.tokens = [None, None, None];
    }

    /// The line number of the next token (`line_no`).
    pub fn line_no(&self) -> i32 {
        self.state.line_no
    }

    /// Set the line number (`emit_ast` replays each node's line).
    pub fn set_line_no(&mut self, line_no: i32) {
        self.state.line_no = line_no;
    }

    /// A new statement starts (`scanner_start_line`).
    pub fn start_line(&mut self) {
        self.state.current_line_saved = false;
    }

    /// Scan the next identifier as an identifier even if it is a keyword
    /// (`scanner_force_identifier`), so `j b` can jump to a label `b`.
    pub fn force_identifier(&mut self) {
        self.state.force_id_next = true;
    }

    /// Save the scanner and start scanning `input` (`scanner_push_source`).
    pub fn push_source(&mut self, input: Vec<u8>) {
        if self.saved.len() >= 8 {
            fatal_error("scanner_push_source: nested scanner stack overflow\n");
        }
        self.saved.push(self.state.clone());
        self.init(input);
    }

    /// Go back to the scanner saved by the last `push_source`
    /// (`scanner_pop_source`); nothing if none is saved.
    pub fn pop_source(&mut self) {
        if let Some(state) = self.saved.pop() {
            self.state = state;
        }
    }

    /// The kind of the next token (`scanner_peek`).
    pub fn peek(&mut self, context: &ScanContext<'_>) -> i32 {
        self.ensure_filled(1, context);
        self.state.tokens[0]
            .as_ref()
            .map_or(TOK_EOF, |t: &Token| t.kind)
    }

    /// The kind of the token after the next (`scanner_peek2`).
    pub fn peek2(&mut self, context: &ScanContext<'_>) -> i32 {
        self.ensure_filled(2, context);
        self.state.tokens[1]
            .as_ref()
            .map_or(TOK_EOF, |t: &Token| t.kind)
    }

    /// Take the next token and return its kind (`scanner_advance`).  If it
    /// carries a value, the value goes to `scan_value`.
    pub fn advance(&mut self, context: &ScanContext<'_>) -> i32 {
        self.ensure_filled(1, context);
        let token: Token = self.state.tokens[0]
            .take()
            .expect("ensure_filled filled the first slot");
        if matches!(
            token.kind,
            TOK_INT | TOK_ID | TOK_REG | TOK_FP_REG | TOK_STR | TOK_FP
        ) {
            self.scan_value = token.value;
        }
        self.state.tokens.rotate_left(1);
        token.kind
    }

    /// The current line for an error message, as `"\t  TEXT\n"`
    /// (`erroneous_line`).
    pub fn erroneous_line(&self) -> Vec<u8> {
        let mut text: Vec<u8> = b"\t  ".to_vec();
        text.extend_from_slice(&self.state.current_line_buf);
        text.push(b'\n');
        text
    }

    /// The current line as `"12: TEXT"`, for an instruction's listing
    /// annotation, or `None` if no token of the line has been scanned
    /// (`source_line`).
    pub fn source_line(&self) -> Option<Vec<u8>> {
        if !self.state.current_line_saved {
            return None;
        }
        let mut text: Vec<u8> = format!("{}: ", self.state.current_line_no_saved).into_bytes();
        text.extend_from_slice(&self.state.current_line_buf);
        Some(text)
    }

    fn ensure_filled(&mut self, n: usize, context: &ScanContext<'_>) {
        for i in 0..n {
            if self.state.tokens[i].is_none() {
                let token: Token = self.scan_one_token(context);
                self.state.tokens[i] = Some(token);
            }
        }
    }

    // ----- Characters -----

    /// Read the next line into the line buffer, as `fgets` does.
    fn fill_line_buf(&mut self) {
        let state: &mut ScanState = &mut self.state;
        if state.at_eof {
            return;
        }
        if state.input_pos >= state.input.len() {
            state.at_eof = true;
            state.line_len = 0;
            state.line_pos = 0;
            return;
        }
        let rest: &[u8] = &state.input[state.input_pos..];
        let mut count: usize = 0;
        while count < LINE_BUF_MAX - 1 && count < rest.len() {
            count += 1;
            if rest[count - 1] == b'\n' {
                break;
            }
        }
        state.line_buf[..count].copy_from_slice(&rest[..count]);
        state.line_buf[count] = 0;
        state.input_pos += count;
        // The C engine measures the line with strlen: a NUL byte ends it.
        state.line_len = state.line_buf[..count]
            .iter()
            .position(|b: &u8| *b == 0)
            .unwrap_or(count);
        state.line_pos = 0;
    }

    fn peek_char(&self) -> u8 {
        if self.state.line_pos >= self.state.line_len {
            0
        } else {
            self.state.line_buf[self.state.line_pos]
        }
    }

    fn peek_char2(&self) -> u8 {
        if self.state.line_pos + 1 >= self.state.line_len {
            0
        } else {
            self.state.line_buf[self.state.line_pos + 1]
        }
    }

    fn next_char(&mut self) -> u8 {
        let c: u8 = self.peek_char();
        if self.state.line_pos < self.state.line_len {
            self.state.line_pos += 1;
        }
        c
    }

    /// The byte at `index` of the line buffer, as C's pointer would read
    /// it (past the line, the NUL and then older bytes).
    fn buf_at(&self, index: usize) -> u8 {
        self.state.line_buf.get(index).copied().unwrap_or(0)
    }

    /// Remember the current line the first time a token of it is scanned.
    fn maybe_snapshot_line(&mut self) {
        let state: &mut ScanState = &mut self.state;
        if !state.current_line_saved {
            let mut len: usize = state.line_len;
            if len > 0 && state.line_buf[len - 1] == b'\n' {
                len -= 1;
            }
            state.current_line_buf = state.line_buf[..len].to_vec();
            state.current_line_no_saved = state.line_no;
            state.current_line_saved = true;
        }
    }

    // ----- Tokens -----

    /// If a floating-point literal (`digits.digits[e[+-]digits]`) starts
    /// here, the position after it (`scan_fp_check`).
    fn scan_fp_check(&self) -> Option<usize> {
        let buf: &[u8] = &self.state.line_buf;
        let len: usize = self.state.line_len;
        let mut p: usize = self.state.line_pos;
        if p >= len || !buf[p].is_ascii_digit() {
            return None;
        }
        while p < len && buf[p].is_ascii_digit() {
            p += 1;
        }
        // The decimal point; `,` separates operands, so `.word 1,2` is two.
        if p >= len || buf[p] != b'.' {
            return None;
        }
        p += 1;
        if p >= len || !buf[p].is_ascii_digit() {
            return None;
        }
        while p < len && buf[p].is_ascii_digit() {
            p += 1;
        }
        if p < len && (buf[p] == b'e' || buf[p] == b'E') {
            p += 1;
            if p < len && (buf[p] == b'+' || buf[p] == b'-') {
                p += 1;
            }
            while p < len && buf[p].is_ascii_digit() {
                p += 1;
            }
        }
        Some(p)
    }

    fn scan_fp(&mut self, sign: f64, end: usize) -> Token {
        let start: usize = self.state.line_pos;
        let length: usize = (end - start).min(63);
        let value: f64 = sign * c_atof(&self.state.line_buf[start..start + length]);
        self.state.line_pos = end;
        Token {
            kind: TOK_FP,
            value: ScanValue::Fp(value),
        }
    }

    /// An integer, decimal or `0x` hexadecimal; it wraps silently past 32
    /// bits (`scan_int`).
    fn scan_int(&mut self, sign: u32) -> Token {
        let mut value: u32 = 0;
        if self.peek_char() == b'0' && (self.peek_char2() == b'x' || self.peek_char2() == b'X') {
            self.next_char();
            self.next_char();
            while self.peek_char().is_ascii_hexdigit() {
                let digit: u32 = char::from(self.next_char()).to_digit(16).unwrap_or(0);
                value = (value << 4) | digit;
            }
        } else {
            while self.peek_char().is_ascii_digit() {
                let digit: u32 = u32::from(self.next_char() - b'0');
                value = value.wrapping_mul(10).wrapping_add(digit);
            }
        }
        Token {
            kind: TOK_INT,
            value: ScanValue::Int(sign.wrapping_mul(value) as i32),
        }
    }

    /// The character after a backslash in a character literal; `p` is the
    /// index after the backslash and moves past the escape.
    fn scan_escape_char(&self, p: &mut usize) -> i32 {
        let first: u8 = self.buf_at(*p);
        *p += 1;
        match first {
            b'a' => 0x07,
            b'b' => 0x08,
            b'f' => 0x0c,
            b'n' => i32::from(b'\n'),
            b'r' => i32::from(b'\r'),
            b't' => i32::from(b'\t'),
            b'\\' => i32::from(b'\\'),
            b'"' => i32::from(b'"'),
            b'\'' => i32::from(b'\''),
            b'x' | b'X' => {
                let high: u8 = self.buf_at(*p);
                let low: u8 = self.buf_at(*p + 1);
                let mut value: i32 = 0;
                if high.is_ascii_hexdigit() {
                    value = hex_digit(high) << 4;
                } else {
                    error_bytes(b"Bad character in \\X construct in char literal\n");
                }
                if low.is_ascii_hexdigit() {
                    value |= hex_digit(low);
                } else {
                    error_bytes(b"Bad character in \\X construct in char literal\n");
                }
                *p += 2;
                value
            }
            _ => {
                let mut message: Vec<u8> = b"Bad escape character \\".to_vec();
                message.push(first);
                message.extend_from_slice(b" in char literal\n");
                error_bytes(&message);
                0
            }
        }
    }

    /// Scan an identifier or a keyword (`scan_identifier`).  A defined
    /// constant (`X = 5`) scans as its value.
    fn scan_identifier(&mut self, force_id: bool, context: &ScanContext<'_>) -> Token {
        let start: usize = self.state.line_pos;
        while self.peek_char() == b'_'
            || self.peek_char() == b'.'
            || self.peek_char().is_ascii_alphanumeric()
        {
            self.next_char();
        }
        let end: usize = self.state.line_pos;
        let id: Vec<u8> = self.state.line_buf[start..start + (end - start).min(255)].to_vec();
        if !force_id {
            if let Some(row) = keyword_row(&id) {
                if context.allow_pseudo_ops || row.op_type != OpType::PseudoOp {
                    return Token {
                        kind: row.token,
                        value: ScanValue::Int(row.token),
                    };
                }
            }
        }
        self.identifier_or_constant(id, context)
    }

    /// A defined constant scans as its value, anything else as an
    /// identifier.
    fn identifier_or_constant(&self, id: Vec<u8>, context: &ScanContext<'_>) -> Token {
        let name: String = String::from_utf8_lossy(&id).into_owned();
        if let Some(label) = context.symbols.find(&name) {
            let label: &Label = context.symbols.label(label);
            if label.constant {
                return Token {
                    kind: TOK_INT,
                    value: ScanValue::Int(label.addr as i32),
                };
            }
        }
        Token {
            kind: TOK_ID,
            value: ScanValue::Text(id),
        }
    }

    /// Scan `$name` (`scan_register`): a general register, a
    /// floating-point register, or else an identifier.
    fn scan_register(&mut self, context: &ScanContext<'_>) -> Token {
        self.next_char();
        let start: usize = self.state.line_pos;
        while matches!(self.peek_char(), b'_' | b'.' | b'$')
            || self.peek_char().is_ascii_alphanumeric()
        {
            self.next_char();
        }
        let end: usize = self.state.line_pos;
        let name: Vec<u8> = self.state.line_buf[start..start + (end - start).min(63)].to_vec();
        let number: i32 = register_name_to_number(&name);
        let first: u8 = name.first().copied().unwrap_or(0);
        let second: u8 = name.get(1).copied().unwrap_or(0);
        if number != -1 && first == b'f' && second != b'p' {
            return Token {
                kind: TOK_FP_REG,
                value: ScanValue::Int(number),
            };
        }
        if (0..32).contains(&number) {
            return Token {
                kind: TOK_REG,
                value: ScanValue::Int(number),
            };
        }
        self.identifier_or_constant(name, context)
    }

    /// Scan a string literal; its escapes are decoded (`scan_string`).
    fn scan_string(&mut self) -> Token {
        self.next_char();
        let start: usize = self.state.line_pos;
        while self.peek_char() != 0 && self.peek_char() != b'"' {
            if self.peek_char() == b'\\' && self.peek_char2() != 0 {
                self.next_char();
            }
            self.next_char();
        }
        let end: usize = self.state.line_pos;
        if self.peek_char() == b'"' {
            self.next_char();
        }
        let decoded: Vec<u8> = decode_string(&self.state.line_buf[start..end]);
        Token {
            kind: TOK_STR,
            value: ScanValue::Text(decoded),
        }
    }

    /// Scan a character literal `'c'` as an integer (`scan_char`).
    fn scan_char(&mut self) -> Token {
        self.next_char();
        let value: i32 = if self.peek_char() == b'\\' {
            self.next_char();
            let mut p: usize = self.state.line_pos;
            let value: i32 = self.scan_escape_char(&mut p);
            self.state.line_pos = p;
            value
        } else {
            i32::from(self.next_char())
        };
        if self.peek_char() == b'\'' {
            self.next_char();
        }
        Token {
            kind: TOK_INT,
            value: ScanValue::Int(value),
        }
    }

    /// Scan one token, skipping white space and comments (`scan_one_token`).
    fn scan_one_token(&mut self, context: &ScanContext<'_>) -> Token {
        let force_id: bool = self.state.force_id_next;
        self.state.force_id_next = false;
        loop {
            while self.state.line_pos < self.state.line_len {
                match self.peek_char() {
                    b' ' | b'\t' | b'\r' | b',' => {
                        self.next_char();
                    }
                    b'#' => {
                        while self.peek_char() != 0 && self.peek_char() != b'\n' {
                            self.next_char();
                        }
                    }
                    _ => return self.scan_token_at_char(force_id, context),
                }
            }
            if self.state.at_eof {
                return Token::plain(TOK_EOF);
            }
            self.fill_line_buf();
            if self.state.at_eof && self.state.line_len == 0 {
                return Token::plain(TOK_EOF);
            }
            self.state.current_line_saved = false;
        }
    }

    /// Scan the token that starts at the current character.
    fn scan_token_at_char(&mut self, force_id: bool, context: &ScanContext<'_>) -> Token {
        let c: u8 = self.peek_char();
        if c == b'\n' {
            // The line ends; the next token is on the next line.
            self.next_char();
            self.state.line_no += 1;
            return Token::plain(TOK_NL);
        }
        if c == b';' {
            self.next_char();
            return Token::plain(TOK_NL);
        }
        self.maybe_snapshot_line();
        if c == b'-' && self.peek_char2().is_ascii_digit() {
            // A negative literal: `8 -4` is two integers, not a subtraction.
            self.next_char();
            return match self.scan_fp_check() {
                Some(end) => self.scan_fp(-1.0, end),
                None => self.scan_int(u32::MAX),
            };
        }
        if c.is_ascii_digit() {
            return match self.scan_fp_check() {
                Some(end) => self.scan_fp(1.0, end),
                None => self.scan_int(1),
            };
        }
        if c == b'_' || c == b'.' || c.is_ascii_alphabetic() {
            return self.scan_identifier(force_id, context);
        }
        match c {
            b'$' => self.scan_register(context),
            b'"' => self.scan_string(),
            b'\'' => self.scan_char(),
            b'?' => {
                self.next_char();
                Token {
                    kind: TOK_ID,
                    value: ScanValue::Text(b"?".to_vec()),
                }
            }
            b'*' | b'/' | b':' | b'(' | b')' | b'+' | b'-' | b'>' | b'=' => {
                self.next_char();
                Token {
                    kind: i32::from(c),
                    value: ScanValue::Int(i32::from(c)),
                }
            }
            _ => {
                self.next_char();
                let mut message: Vec<u8> = b"Unknown character in source: '".to_vec();
                message.push(c);
                message.extend_from_slice(format!("' (0x{c:02x})\n").as_bytes());
                error_bytes(&message);
                Token::plain(TOK_NL)
            }
        }
    }
}

/// The value of a hexadecimal digit (`hex_digit_value`).
fn hex_digit(c: u8) -> i32 {
    char::from(c).to_digit(16).map_or(0, |d: u32| d as i32)
}

/// Decode the escapes of a string literal (`decode_string`): `\n`, `\t`,
/// `\"`, three-digit octal `\NNN`, and `\XHH`.  Any other backslash is
/// kept, so `\\` stays two backslashes.
///
/// ```
/// use spimulator::scanner::decode_string;
/// assert_eq!(decode_string(br"a\nb"), b"a\nb");
/// assert_eq!(decode_string(br"\101\X42"), b"AB");
/// assert_eq!(decode_string(br"x\\y"), br"x\\y");
/// ```
pub fn decode_string(source: &[u8]) -> Vec<u8> {
    let mut out: Vec<u8> = Vec::with_capacity(source.len());
    let end: usize = source.len();
    let mut s: usize = 0;
    while s < end {
        if source[s] != b'\\' || s + 1 >= end {
            out.push(source[s]);
            s += 1;
            continue;
        }
        match source[s + 1] {
            b'n' => {
                out.push(b'\n');
                s += 2;
            }
            b't' => {
                out.push(b'\t');
                s += 2;
            }
            b'"' => {
                out.push(b'"');
                s += 2;
            }
            first @ b'0'..=b'3' => {
                if s + 3 >= end {
                    out.push(source[s]);
                    s += 1;
                    continue;
                }
                let (c2, c3): (u8, u8) = (source[s + 2], source[s + 3]);
                let mut byte: i32 = i32::from(first - b'0') << 6;
                if (b'0'..=b'7').contains(&c2) {
                    byte += i32::from(c2 - b'0') << 3;
                } else {
                    error_bytes(b"Bad character in \\ooo construct in string\n");
                }
                if (b'0'..=b'7').contains(&c3) {
                    byte += i32::from(c3 - b'0');
                } else {
                    error_bytes(b"Bad character in \\ooo construct in string\n");
                }
                out.push(byte as u8);
                s += 4;
            }
            b'X' => {
                if s + 3 >= end {
                    out.push(source[s]);
                    s += 1;
                    continue;
                }
                let (c2, c3): (u8, u8) = (source[s + 2], source[s + 3]);
                let mut byte: i32 = 0;
                if c2.is_ascii_hexdigit() {
                    byte = hex_digit(c2);
                } else {
                    error_bytes(b"Bad character in \\X construct in string\n");
                }
                byte <<= 4;
                if c3.is_ascii_hexdigit() {
                    byte |= hex_digit(c3);
                } else {
                    error_bytes(b"Bad character in \\X construct in string\n");
                }
                out.push(byte as u8);
                s += 4;
            }
            _ => {
                out.push(source[s]);
                s += 1;
            }
        }
    }
    out
}
