//! The abstract syntax tree the parser builds, and its two printers
//! (`ast.c`, `include/ast.h`).
//!
//! The parser does not assemble as it reads.  It builds a tree with one
//! node per statement; then `emit_ast` walks the tree in source order and
//! assembles each node.  `-print-ast`, `-print-ast-json` and
//! `-show-expansion` print the tree instead of assembling it, so a student
//! can see what the parser understood, and what each pseudo-instruction
//! (`li`, `la`, `bge`, ...) turns into.

use crate::cformat::format_g;
use crate::instruction::{bprint, ImmediateExpression};
use crate::opcodes::token_name;
use crate::symbol_table::SymbolTable;

/// What a node is, with its payload.
#[derive(Clone, Debug, PartialEq)]
pub enum AstKind {
    /// `op rd, rs, rt`.
    InstructionR {
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    },
    /// `op rd, rt, shamt`.
    InstructionRShift {
        op: i32,
        destination_register: i32,
        target_register: i32,
        shift_amount: i32,
    },
    /// `op rt, rs, imm`.
    InstructionI {
        op: i32,
        target_register: i32,
        source_register: i32,
        immediate: ImmediateExpression,
    },
    /// `op target`.
    InstructionJ {
        op: i32,
        target: ImmediateExpression,
    },
    /// `op fd, fs, ft`.
    InstructionFloatingPointR {
        op: i32,
        floating_point_destination: i32,
        fs: i32,
        floating_point_target: i32,
    },
    /// `op fs, ft, cc`.
    InstructionFloatingPointCompare {
        op: i32,
        fs: i32,
        floating_point_target: i32,
        condition_code: i32,
    },
    /// A pseudo-instruction, with the real instructions it became.
    Pseudo {
        mnemonic: String,
        children: Vec<AstNode>,
    },
    /// `.byte` values.
    DataByte(Vec<ImmediateExpression>),
    /// `.half` values.
    DataHalf(Vec<ImmediateExpression>),
    /// `.word` values.
    DataWord(Vec<ImmediateExpression>),
    /// `.float` values.
    DataFloat(Vec<f64>),
    /// `.double` values.
    DataDouble(Vec<f64>),
    /// `.ascii` / `.asciiz`.
    DataString {
        bytes: Vec<u8>,
        null_terminate: bool,
    },
    /// `.text [ADDR]`.
    DirectiveText(Option<u32>),
    /// `.data [ADDR]`.
    DirectiveData(Option<u32>),
    /// `.ktext [ADDR]`.
    DirectiveKtext(Option<u32>),
    /// `.kdata [ADDR]`.
    DirectiveKdata(Option<u32>),
    /// `.align N`.
    DirectiveAlign(i32),
    /// `.space N`.
    DirectiveSpace(i32),
    /// `.globl NAME`.
    DirectiveGlobl(String),
    /// `.extern NAME SIZE`.
    DirectiveExtern { name: String, size: i32 },
    /// `.comm NAME SIZE`.
    DirectiveComm { name: String, size: i32 },
    /// `NAME:`.
    LabelNormal(String),
    /// `NAME = VALUE`.
    LabelConstant(String, i32),
    /// The root: one per file.
    File {
        source_file: Option<Vec<u8>>,
        children: Vec<AstNode>,
    },
}

/// One node: what it is, and where in the source it came from.
#[derive(Clone, Debug, PartialEq)]
pub struct AstNode {
    /// The node's kind and payload.
    pub kind: AstKind,
    /// The line number the scanner was at when the node was built.
    pub source_line: i32,
    /// The source line as `"12: text"`, for the listing annotation of the
    /// instructions this node assembles into.
    pub source_text: Option<Vec<u8>>,
}

/// Append a JSON string literal (RFC 8259 escapes).
fn json_string(out: &mut Vec<u8>, bytes: &[u8]) {
    out.push(b'"');
    for &c in bytes {
        match c {
            b'"' => out.extend_from_slice(b"\\\""),
            b'\\' => out.extend_from_slice(b"\\\\"),
            b'\n' => out.extend_from_slice(b"\\n"),
            b'\r' => out.extend_from_slice(b"\\r"),
            b'\t' => out.extend_from_slice(b"\\t"),
            0x08 => out.extend_from_slice(b"\\b"),
            0x0c => out.extend_from_slice(b"\\f"),
            c if c < 0x20 => bprint!(out, "\\u{:04x}", c),
            c => out.push(c),
        }
    }
    out.push(b'"');
}

impl AstNode {
    /// The `-print-ast` dump of this node and its children (`ast_print`).
    pub fn print(&self, symbols: &SymbolTable, out: &mut Vec<u8>) {
        self.print_at(symbols, out, 0);
    }

    /// Append an immediate the way `-print-ast` shows it (`print_immediate`).
    fn print_immediate(expr: &ImmediateExpression, symbols: &SymbolTable, out: &mut Vec<u8>) {
        match expr.symbol {
            Some(id) => {
                out.extend_from_slice(symbols.label(id).name.as_bytes());
                if expr.offset > 0 {
                    bprint!(out, "+{}", expr.offset);
                }
                if expr.offset < 0 {
                    bprint!(out, "{}", expr.offset);
                }
                if expr.program_counter_relative {
                    out.extend_from_slice(b" (pc-rel)");
                }
            }
            None => bprint!(out, "{}", expr.offset),
        }
    }

    fn print_at(&self, symbols: &SymbolTable, out: &mut Vec<u8>, depth: usize) {
        for _ in 0..depth {
            out.extend_from_slice(b"  ");
        }
        bprint!(out, "[line {}] ", self.source_line);
        match &self.kind {
            AstKind::File {
                source_file,
                children,
            } => {
                out.extend_from_slice(b"FILE source=");
                out.extend_from_slice(source_file.as_deref().unwrap_or(b"(stdin)"));
                out.push(b'\n');
                for child in children {
                    child.print_at(symbols, out, depth + 1);
                }
            }
            AstKind::InstructionR {
                op,
                destination_register,
                source_register,
                target_register,
            } => {
                bprint!(
                    out,
                    "INST_R op={} rd={destination_register} rs={source_register} rt={target_register}\n",
                    token_name(*op)
                )
            }
            AstKind::InstructionRShift {
                op,
                destination_register,
                target_register,
                shift_amount,
            } => {
                bprint!(
                    out,
                    "INST_R_SHIFT op={} rd={destination_register} rt={target_register} shamt={shift_amount}\n",
                    token_name(*op)
                )
            }
            AstKind::InstructionI {
                op,
                target_register,
                source_register,
                immediate,
            } => {
                bprint!(
                    out,
                    "INST_I op={} rt={target_register} rs={source_register} imm=",
                    token_name(*op)
                );
                Self::print_immediate(immediate, symbols, out);
                out.push(b'\n');
            }
            AstKind::InstructionJ { op, target } => {
                bprint!(out, "INST_J op={} target=", token_name(*op));
                Self::print_immediate(target, symbols, out);
                out.push(b'\n');
            }
            AstKind::InstructionFloatingPointR {
                op,
                floating_point_destination,
                fs,
                floating_point_target,
            } => {
                bprint!(
                    out,
                    "INST_FP_R op={} fd={floating_point_destination} fs={fs} ft={floating_point_target}\n",
                    token_name(*op)
                )
            }
            AstKind::InstructionFloatingPointCompare {
                op,
                fs,
                floating_point_target,
                condition_code,
            } => {
                bprint!(
                    out,
                    "INST_FP_COMPARE op={} fs={fs} ft={floating_point_target} cc={condition_code}\n",
                    token_name(*op)
                )
            }
            AstKind::Pseudo { mnemonic, children } => {
                bprint!(out, "PSEUDO mnemonic={mnemonic}\n");
                for child in children {
                    child.print_at(symbols, out, depth + 1);
                }
            }
            AstKind::DataByte(values) | AstKind::DataHalf(values) | AstKind::DataWord(values) => {
                let tag: &str = match &self.kind {
                    AstKind::DataByte(_) => "DATA_BYTE",
                    AstKind::DataHalf(_) => "DATA_HALF",
                    _ => "DATA_WORD",
                };
                bprint!(out, "{tag} count={} values=[", values.len());
                for (i, value) in values.iter().enumerate() {
                    if i > 0 {
                        out.extend_from_slice(b", ");
                    }
                    Self::print_immediate(value, symbols, out);
                }
                out.extend_from_slice(b"]\n");
            }
            AstKind::DataFloat(values) | AstKind::DataDouble(values) => {
                let tag: &str = if matches!(self.kind, AstKind::DataFloat(_)) {
                    "DATA_FLOAT"
                } else {
                    "DATA_DOUBLE"
                };
                bprint!(out, "{tag} count={} values=[", values.len());
                for (i, value) in values.iter().enumerate() {
                    if i > 0 {
                        out.extend_from_slice(b", ");
                    }
                    out.extend_from_slice(format_g(false, 6, *value).as_bytes());
                }
                out.extend_from_slice(b"]\n");
            }
            AstKind::DataString {
                bytes,
                null_terminate,
            } => {
                bprint!(
                    out,
                    "DATA_STRING len={} null_term={} value=\"",
                    bytes.len(),
                    if *null_terminate { "yes" } else { "no" }
                );
                for &c in bytes {
                    match c {
                        b'\n' => out.extend_from_slice(b"\\n"),
                        b'\t' => out.extend_from_slice(b"\\t"),
                        b'\\' | b'"' => {
                            out.push(b'\\');
                            out.push(c);
                        }
                        0x20..=0x7e => out.push(c),
                        _ => bprint!(out, "\\x{c:02x}"),
                    }
                }
                out.extend_from_slice(b"\"\n");
            }
            AstKind::DirectiveText(start)
            | AstKind::DirectiveData(start)
            | AstKind::DirectiveKtext(start)
            | AstKind::DirectiveKdata(start) => {
                let tag: &str = match &self.kind {
                    AstKind::DirectiveText(_) => "DIR_TEXT",
                    AstKind::DirectiveData(_) => "DIR_DATA",
                    AstKind::DirectiveKtext(_) => "DIR_KTEXT",
                    _ => "DIR_KDATA",
                };
                match start {
                    Some(address) => bprint!(out, "{tag} start=0x{address:08x}\n"),
                    None => bprint!(out, "{tag}\n"),
                }
            }
            AstKind::DirectiveAlign(n) => bprint!(out, "DIR_ALIGN n={n}\n"),
            AstKind::DirectiveSpace(size) => bprint!(out, "DIR_SPACE size={size}\n"),
            AstKind::DirectiveGlobl(name) => bprint!(out, "DIR_GLOBL name={name}\n"),
            AstKind::DirectiveExtern { name, size } => {
                bprint!(out, "DIR_EXTERN name={name} size={size}\n")
            }
            AstKind::DirectiveComm { name, size } => {
                bprint!(out, "DIR_COMM name={name} size={size}\n")
            }
            AstKind::LabelNormal(name) => bprint!(out, "LABEL_DEF name={name} (placement)\n"),
            AstKind::LabelConstant(name, value) => {
                bprint!(out, "LABEL_DEF name={name} = {value} (constant)\n")
            }
        }
    }

    /// The `-print-ast-json` dump of this node (`ast_print_json`), without
    /// the final newline.
    pub fn print_json(&self, symbols: &SymbolTable, out: &mut Vec<u8>) {
        let kind_name: &str = match &self.kind {
            AstKind::InstructionR { .. } => "INST_R",
            AstKind::InstructionRShift { .. } => "INST_R_SHIFT",
            AstKind::InstructionI { .. } => "INST_I",
            AstKind::InstructionJ { .. } => "INST_J",
            AstKind::InstructionFloatingPointR { .. } => "INST_FP_R",
            AstKind::InstructionFloatingPointCompare { .. } => "INST_FP_COMPARE",
            AstKind::Pseudo { .. } => "PSEUDO",
            AstKind::DataByte(_) => "DATA_BYTE",
            AstKind::DataHalf(_) => "DATA_HALF",
            AstKind::DataWord(_) => "DATA_WORD",
            AstKind::DataFloat(_) => "DATA_FLOAT",
            AstKind::DataDouble(_) => "DATA_DOUBLE",
            AstKind::DataString { .. } => "DATA_STRING",
            AstKind::DirectiveText(_) => "DIR_TEXT",
            AstKind::DirectiveData(_) => "DIR_DATA",
            AstKind::DirectiveKtext(_) => "DIR_KTEXT",
            AstKind::DirectiveKdata(_) => "DIR_KDATA",
            AstKind::DirectiveAlign(_) => "DIR_ALIGN",
            AstKind::DirectiveSpace(_) => "DIR_SPACE",
            AstKind::DirectiveGlobl(_) => "DIR_GLOBL",
            AstKind::DirectiveExtern { .. } => "DIR_EXTERN",
            AstKind::DirectiveComm { .. } => "DIR_COMM",
            AstKind::LabelNormal(_) | AstKind::LabelConstant(..) => "LABEL_DEF",
            AstKind::File { .. } => "FILE",
        };
        bprint!(
            out,
            "{{\"kind\":\"{kind_name}\",\"source_line\":{}",
            self.source_line
        );
        let mnemonic = |out: &mut Vec<u8>, op: i32| -> () {
            bprint!(out, ",\"op\":{op},\"mnemonic\":");
            json_string(out, token_name(op).as_bytes());
        };
        match &self.kind {
            AstKind::File {
                source_file,
                children,
            } => {
                if let Some(name) = source_file {
                    out.extend_from_slice(b",\"source_file\":");
                    json_string(out, name);
                }
                out.extend_from_slice(b",\"children\":");
                Self::json_chain(children, symbols, out);
            }
            AstKind::InstructionR {
                op,
                destination_register,
                source_register,
                target_register,
            } => {
                mnemonic(out, *op);
                bprint!(out, ",\"rd\":{destination_register},\"rs\":{source_register},\"rt\":{target_register}");
            }
            AstKind::InstructionRShift {
                op,
                destination_register,
                target_register,
                shift_amount,
            } => {
                mnemonic(out, *op);
                bprint!(out, ",\"rd\":{destination_register},\"rt\":{target_register},\"shamt\":{shift_amount}");
            }
            AstKind::InstructionI {
                op,
                target_register,
                source_register,
                immediate,
            } => {
                mnemonic(out, *op);
                bprint!(
                    out,
                    ",\"rt\":{target_register},\"rs\":{source_register},\"imm\":"
                );
                Self::json_immediate(immediate, symbols, out);
            }
            AstKind::InstructionJ { op, target } => {
                mnemonic(out, *op);
                out.extend_from_slice(b",\"target\":");
                Self::json_immediate(target, symbols, out);
            }
            AstKind::InstructionFloatingPointR {
                op,
                floating_point_destination,
                fs,
                floating_point_target,
            } => {
                mnemonic(out, *op);
                bprint!(out, ",\"fd\":{floating_point_destination},\"fs\":{fs},\"ft\":{floating_point_target}");
            }
            AstKind::InstructionFloatingPointCompare {
                op,
                fs,
                floating_point_target,
                condition_code,
            } => {
                mnemonic(out, *op);
                bprint!(
                    out,
                    ",\"fs\":{fs},\"ft\":{floating_point_target},\"cc\":{condition_code}"
                );
            }
            AstKind::Pseudo { mnemonic, children } => {
                out.extend_from_slice(b",\"mnemonic\":");
                json_string(out, mnemonic.as_bytes());
                out.extend_from_slice(b",\"children\":");
                Self::json_chain(children, symbols, out);
            }
            AstKind::DataByte(values) | AstKind::DataHalf(values) | AstKind::DataWord(values) => {
                bprint!(out, ",\"count\":{},\"values\":[", values.len());
                for (i, value) in values.iter().enumerate() {
                    if i > 0 {
                        out.push(b',');
                    }
                    Self::json_immediate(value, symbols, out);
                }
                out.push(b']');
            }
            AstKind::DataFloat(values) | AstKind::DataDouble(values) => {
                bprint!(out, ",\"count\":{},\"values\":[", values.len());
                for (i, value) in values.iter().enumerate() {
                    if i > 0 {
                        out.push(b',');
                    }
                    out.extend_from_slice(format_g(false, 17, *value).as_bytes());
                }
                out.push(b']');
            }
            AstKind::DataString {
                bytes,
                null_terminate,
            } => {
                bprint!(
                    out,
                    ",\"length\":{},\"null_terminate\":{},\"value\":",
                    bytes.len(),
                    null_terminate
                );
                json_string(out, bytes);
            }
            AstKind::DirectiveText(start)
            | AstKind::DirectiveData(start)
            | AstKind::DirectiveKtext(start)
            | AstKind::DirectiveKdata(start) => {
                if let Some(address) = start {
                    bprint!(out, ",\"start_addr\":{address}");
                }
            }
            AstKind::DirectiveAlign(n) => bprint!(out, ",\"n\":{n}"),
            AstKind::DirectiveSpace(size) => bprint!(out, ",\"size\":{size}"),
            AstKind::DirectiveGlobl(name) => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
            }
            AstKind::DirectiveExtern { name, size } | AstKind::DirectiveComm { name, size } => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
                bprint!(out, ",\"size\":{size}");
            }
            AstKind::LabelNormal(name) => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
                out.extend_from_slice(b",\"label_kind\":\"placement\"");
            }
            AstKind::LabelConstant(name, value) => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
                bprint!(out, ",\"label_kind\":\"constant\",\"value\":{value}");
            }
        }
        out.push(b'}');
    }

    fn json_chain(children: &[AstNode], symbols: &SymbolTable, out: &mut Vec<u8>) {
        out.push(b'[');
        for (i, child) in children.iter().enumerate() {
            if i > 0 {
                out.push(b',');
            }
            child.print_json(symbols, out);
        }
        out.push(b']');
    }

    fn json_immediate(expr: &ImmediateExpression, symbols: &SymbolTable, out: &mut Vec<u8>) {
        bprint!(out, "{{\"offset\":{},\"symbol\":", expr.offset);
        match expr.symbol {
            Some(id) => json_string(out, symbols.label(id).name.as_bytes()),
            None => out.extend_from_slice(b"null"),
        }
        bprint!(out, ",\"pc_relative\":{}}}", expr.program_counter_relative);
    }
}
