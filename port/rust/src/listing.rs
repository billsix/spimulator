//! The `-listing FILE` trace: one line per thing the assembler does
//! (`assembler-event.c` and the listing observer in `spim.c`).
//!
//! `-listing trace.txt` writes a line each time the assembler defines a
//! label, stores an instruction or a datum, or records and later patches a
//! forward reference, each with the source line it came from.

use std::io::Write;

use crate::cformat::format_g;
use crate::machine::Machine;

impl Machine {
    /// Write one line of the trace, if a trace was asked for.
    fn listing_write(&mut self, text: &[u8]) {
        if let Some(listing) = self.listing.as_mut() {
            // Like the C engine, ignore a failure to write the trace.
            let _ = listing.write_all(text);
        }
    }

    /// A label was defined at `addr`.
    pub fn listing_label_def(&mut self, addr: u32, name: &str, global: bool) {
        if self.listing.is_none() {
            return;
        }
        let line: i32 = self.scanner.line_no();
        let text: String = format!(
            "line {line:4}  label  {name:<24} defined at 0x{addr:08x}{}\n",
            if global { "  (global)" } else { "" }
        );
        self.listing_write(text.as_bytes());
    }

    /// An instruction was stored at `addr`; the line shows its listing.
    pub fn listing_text_inst(&mut self, addr: u32, encoding: u32) {
        if self.listing.is_none() {
            return;
        }
        let line: i32 = self.scanner.line_no();
        let disassembly: Vec<u8> = self.inst_to_string(addr);
        let mut text: Vec<u8> = format!("line {line:4}  text   0x{addr:08x}  ").into_bytes();
        text.extend_from_slice(&disassembly);
        // `%-32s`: pad to 32 bytes.
        text.resize(text.len() + 32usize.saturating_sub(disassembly.len()), b' ');
        text.extend_from_slice(format!("  0x{encoding:08x}\n").as_bytes());
        self.listing_write(&text);
    }

    /// A `.byte` value was stored.
    pub fn listing_data_byte(&mut self, addr: u32, value: i32) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  data   0x{addr:08x}  byte    0x{:02x}\n",
                value & 0xff
            );
            self.listing_write(text.as_bytes());
        }
    }

    /// A `.half` value was stored.
    pub fn listing_data_half(&mut self, addr: u32, value: i32) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  data   0x{addr:08x}  half    0x{:04x}\n",
                value & 0xffff
            );
            self.listing_write(text.as_bytes());
        }
    }

    /// A `.word` value was stored.
    pub fn listing_data_word(&mut self, addr: u32, value: i32) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  data   0x{addr:08x}  word    0x{:08x}\n",
                value as u32
            );
            self.listing_write(text.as_bytes());
        }
    }

    /// A `.double` value was stored.
    pub fn listing_data_double(&mut self, addr: u32, value: f64) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  data   0x{addr:08x}  double  {}\n",
                format_g(false, 18, value)
            );
            self.listing_write(text.as_bytes());
        }
    }

    /// A string was stored.
    pub fn listing_data_string(&mut self, addr: u32, length: usize, null_terminate: bool) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  data   0x{addr:08x}  string  len={length}{}\n",
                if null_terminate { " (+\\0)" } else { "" }
            );
            self.listing_write(text.as_bytes());
        }
    }

    /// The datum or instruction at `from` uses the undefined `symbol`.
    pub fn listing_forward_ref(&mut self, from: u32, symbol: &str) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String =
                format!("line {line:4}  fref   0x{from:08x}  uses unresolved symbol '{symbol}'\n");
            self.listing_write(text.as_bytes());
        }
    }

    /// The forward reference at `at` was patched with `value`.
    pub fn listing_forward_resolved(&mut self, at: u32, symbol: &str, value: i32) {
        if self.listing.is_some() {
            let line: i32 = self.scanner.line_no();
            let text: String = format!(
                "line {line:4}  fres   0x{at:08x}  patched '{symbol}' = 0x{:08x}\n",
                value as u32
            );
            self.listing_write(text.as_bytes());
        }
    }
}
