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
use crate::instruction::{bprint, ImmExpr};
use crate::opcodes::token_name;
use crate::symbol_table::SymbolTable;

/// What a node is, with its payload.
#[derive(Clone, Debug, PartialEq)]
pub enum AstKind {
    /// `op rd, rs, rt`.
    InstR { op: i32, rd: i32, rs: i32, rt: i32 },
    /// `op rd, rt, shamt`.
    InstRShift {
        op: i32,
        rd: i32,
        rt: i32,
        shamt: i32,
    },
    /// `op rt, rs, imm`.
    InstI {
        op: i32,
        rt: i32,
        rs: i32,
        imm: ImmExpr,
    },
    /// `op target`.
    InstJ { op: i32, target: ImmExpr },
    /// `op fd, fs, ft`.
    InstFpR { op: i32, fd: i32, fs: i32, ft: i32 },
    /// `op fs, ft, cc`.
    InstFpCompare { op: i32, fs: i32, ft: i32, cc: i32 },
    /// A pseudo-instruction, with the real instructions it became.
    Pseudo {
        mnemonic: String,
        children: Vec<AstNode>,
    },
    /// `.byte` values.
    DataByte(Vec<ImmExpr>),
    /// `.half` values.
    DataHalf(Vec<ImmExpr>),
    /// `.word` values.
    DataWord(Vec<ImmExpr>),
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
    DirText(Option<u32>),
    /// `.data [ADDR]`.
    DirData(Option<u32>),
    /// `.ktext [ADDR]`.
    DirKtext(Option<u32>),
    /// `.kdata [ADDR]`.
    DirKdata(Option<u32>),
    /// `.align N`.
    DirAlign(i32),
    /// `.space N`.
    DirSpace(i32),
    /// `.globl NAME`.
    DirGlobl(String),
    /// `.extern NAME SIZE`.
    DirExtern { name: String, size: i32 },
    /// `.comm NAME SIZE`.
    DirComm { name: String, size: i32 },
    /// `NAME:`.
    LabelNormal(String),
    /// `NAME = VALUE`.
    LabelConst(String, i32),
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
    pub src_text: Option<Vec<u8>>,
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

    /// Append an immediate the way `-print-ast` shows it (`print_imm`).
    fn print_imm(expr: &ImmExpr, symbols: &SymbolTable, out: &mut Vec<u8>) {
        match expr.symbol {
            Some(id) => {
                out.extend_from_slice(symbols.label(id).name.as_bytes());
                if expr.offset > 0 {
                    bprint!(out, "+{}", expr.offset);
                }
                if expr.offset < 0 {
                    bprint!(out, "{}", expr.offset);
                }
                if expr.pc_relative {
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
            AstKind::InstR { op, rd, rs, rt } => {
                bprint!(
                    out,
                    "INST_R op={} rd={rd} rs={rs} rt={rt}\n",
                    token_name(*op)
                )
            }
            AstKind::InstRShift { op, rd, rt, shamt } => {
                bprint!(
                    out,
                    "INST_R_SHIFT op={} rd={rd} rt={rt} shamt={shamt}\n",
                    token_name(*op)
                )
            }
            AstKind::InstI { op, rt, rs, imm } => {
                bprint!(out, "INST_I op={} rt={rt} rs={rs} imm=", token_name(*op));
                Self::print_imm(imm, symbols, out);
                out.push(b'\n');
            }
            AstKind::InstJ { op, target } => {
                bprint!(out, "INST_J op={} target=", token_name(*op));
                Self::print_imm(target, symbols, out);
                out.push(b'\n');
            }
            AstKind::InstFpR { op, fd, fs, ft } => {
                bprint!(
                    out,
                    "INST_FP_R op={} fd={fd} fs={fs} ft={ft}\n",
                    token_name(*op)
                )
            }
            AstKind::InstFpCompare { op, fs, ft, cc } => {
                bprint!(
                    out,
                    "INST_FP_COMPARE op={} fs={fs} ft={ft} cc={cc}\n",
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
                    Self::print_imm(value, symbols, out);
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
            AstKind::DirText(start)
            | AstKind::DirData(start)
            | AstKind::DirKtext(start)
            | AstKind::DirKdata(start) => {
                let tag: &str = match &self.kind {
                    AstKind::DirText(_) => "DIR_TEXT",
                    AstKind::DirData(_) => "DIR_DATA",
                    AstKind::DirKtext(_) => "DIR_KTEXT",
                    _ => "DIR_KDATA",
                };
                match start {
                    Some(addr) => bprint!(out, "{tag} start=0x{addr:08x}\n"),
                    None => bprint!(out, "{tag}\n"),
                }
            }
            AstKind::DirAlign(n) => bprint!(out, "DIR_ALIGN n={n}\n"),
            AstKind::DirSpace(size) => bprint!(out, "DIR_SPACE size={size}\n"),
            AstKind::DirGlobl(name) => bprint!(out, "DIR_GLOBL name={name}\n"),
            AstKind::DirExtern { name, size } => {
                bprint!(out, "DIR_EXTERN name={name} size={size}\n")
            }
            AstKind::DirComm { name, size } => bprint!(out, "DIR_COMM name={name} size={size}\n"),
            AstKind::LabelNormal(name) => bprint!(out, "LABEL_DEF name={name} (placement)\n"),
            AstKind::LabelConst(name, value) => {
                bprint!(out, "LABEL_DEF name={name} = {value} (constant)\n")
            }
        }
    }

    /// The `-print-ast-json` dump of this node (`ast_print_json`), without
    /// the final newline.
    pub fn print_json(&self, symbols: &SymbolTable, out: &mut Vec<u8>) {
        let kind_name: &str = match &self.kind {
            AstKind::InstR { .. } => "INST_R",
            AstKind::InstRShift { .. } => "INST_R_SHIFT",
            AstKind::InstI { .. } => "INST_I",
            AstKind::InstJ { .. } => "INST_J",
            AstKind::InstFpR { .. } => "INST_FP_R",
            AstKind::InstFpCompare { .. } => "INST_FP_COMPARE",
            AstKind::Pseudo { .. } => "PSEUDO",
            AstKind::DataByte(_) => "DATA_BYTE",
            AstKind::DataHalf(_) => "DATA_HALF",
            AstKind::DataWord(_) => "DATA_WORD",
            AstKind::DataFloat(_) => "DATA_FLOAT",
            AstKind::DataDouble(_) => "DATA_DOUBLE",
            AstKind::DataString { .. } => "DATA_STRING",
            AstKind::DirText(_) => "DIR_TEXT",
            AstKind::DirData(_) => "DIR_DATA",
            AstKind::DirKtext(_) => "DIR_KTEXT",
            AstKind::DirKdata(_) => "DIR_KDATA",
            AstKind::DirAlign(_) => "DIR_ALIGN",
            AstKind::DirSpace(_) => "DIR_SPACE",
            AstKind::DirGlobl(_) => "DIR_GLOBL",
            AstKind::DirExtern { .. } => "DIR_EXTERN",
            AstKind::DirComm { .. } => "DIR_COMM",
            AstKind::LabelNormal(_) | AstKind::LabelConst(..) => "LABEL_DEF",
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
            AstKind::InstR { op, rd, rs, rt } => {
                mnemonic(out, *op);
                bprint!(out, ",\"rd\":{rd},\"rs\":{rs},\"rt\":{rt}");
            }
            AstKind::InstRShift { op, rd, rt, shamt } => {
                mnemonic(out, *op);
                bprint!(out, ",\"rd\":{rd},\"rt\":{rt},\"shamt\":{shamt}");
            }
            AstKind::InstI { op, rt, rs, imm } => {
                mnemonic(out, *op);
                bprint!(out, ",\"rt\":{rt},\"rs\":{rs},\"imm\":");
                Self::json_imm(imm, symbols, out);
            }
            AstKind::InstJ { op, target } => {
                mnemonic(out, *op);
                out.extend_from_slice(b",\"target\":");
                Self::json_imm(target, symbols, out);
            }
            AstKind::InstFpR { op, fd, fs, ft } => {
                mnemonic(out, *op);
                bprint!(out, ",\"fd\":{fd},\"fs\":{fs},\"ft\":{ft}");
            }
            AstKind::InstFpCompare { op, fs, ft, cc } => {
                mnemonic(out, *op);
                bprint!(out, ",\"fs\":{fs},\"ft\":{ft},\"cc\":{cc}");
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
                    Self::json_imm(value, symbols, out);
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
            AstKind::DirText(start)
            | AstKind::DirData(start)
            | AstKind::DirKtext(start)
            | AstKind::DirKdata(start) => {
                if let Some(addr) = start {
                    bprint!(out, ",\"start_addr\":{addr}");
                }
            }
            AstKind::DirAlign(n) => bprint!(out, ",\"n\":{n}"),
            AstKind::DirSpace(size) => bprint!(out, ",\"size\":{size}"),
            AstKind::DirGlobl(name) => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
            }
            AstKind::DirExtern { name, size } | AstKind::DirComm { name, size } => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
                bprint!(out, ",\"size\":{size}");
            }
            AstKind::LabelNormal(name) => {
                out.extend_from_slice(b",\"name\":");
                json_string(out, name.as_bytes());
                out.extend_from_slice(b",\"label_kind\":\"placement\"");
            }
            AstKind::LabelConst(name, value) => {
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

    fn json_imm(expr: &ImmExpr, symbols: &SymbolTable, out: &mut Vec<u8>) {
        bprint!(out, "{{\"offset\":{},\"symbol\":", expr.offset);
        match expr.symbol {
            Some(id) => json_string(out, symbols.label(id).name.as_bytes()),
            None => out.extend_from_slice(b"null"),
        }
        bprint!(out, ",\"pc_relative\":{}}}", expr.pc_relative);
    }
}
