//! The recursive-descent parser, and `emit_ast`, which assembles the tree
//! it builds (`parser.c`).
//!
//! Each line is `[label:] [instruction-or-directive] [# comment]`.  The
//! parser reads the operands an instruction's shape calls for (the
//! opcode table says which), and appends a node to the tree; a
//! pseudo-instruction appends a `PSEUDO` node with the real instructions
//! it expands to.  After the whole file is parsed, [`Machine::emit_ast`]
//! walks the tree and stores each instruction and datum in memory.
//!
//! Two passes have a visible consequence the port keeps: labels get their
//! addresses only during the second pass, so while parsing, every label
//! of the current file still looks undefined.

use std::rc::Rc;

use crate::ast::{AstKind, AstNode};
use crate::instruction::{opcode_is_nullified_branch, opcode_is_true_branch, ImmExpr};
use crate::machine::{error_bytes, Machine};
use crate::opcode_types::OpType;
use crate::opcodes::*;
use crate::pseudo_op::{branch_offset, op_to_imm_op};
use crate::registers::REG_GP;
use crate::scanner::ScanContext;
use crate::symbol_table::{Label, LabelId};

/// An address operand, `offset(register)` with an optional label
/// (`addr_expr`).
#[derive(Clone, Copy, Debug)]
pub struct AddrExpr {
    /// The base register (stored in 8 bits, as in C).
    pub reg: i32,
    /// The offset expression.
    pub imm: ImmExpr,
}

/// Which data directive the value buffer belongs to.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum DataKind {
    Byte,
    Half,
    Word,
    Float,
    Double,
}

impl Machine {
    // ----- Talking to the scanner -----

    /// The kind of the next token.
    pub fn peek(&mut self) -> i32 {
        let context: ScanContext<'_> = ScanContext {
            symbols: &self.symbols,
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_insts,
        };
        self.scanner.peek(&context)
    }

    /// The kind of the token after the next.
    pub fn peek2(&mut self) -> i32 {
        let context: ScanContext<'_> = ScanContext {
            symbols: &self.symbols,
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_insts,
        };
        self.scanner.peek2(&context)
    }

    /// Take the next token; its value goes to `scanner.scan_value`.
    pub fn advance(&mut self) -> i32 {
        let context: ScanContext<'_> = ScanContext {
            symbols: &self.symbols,
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_insts,
        };
        self.scanner.advance(&context)
    }

    /// The integer value of the last token taken.
    fn value_int(&self) -> i32 {
        self.scanner.scan_value.int()
    }

    /// The name carried by the last token taken.
    fn value_name(&self) -> String {
        self.scanner.scan_value.name()
    }

    // ----- Building the tree -----

    /// A new node, stamped with the scanner's current line (`new_node`).
    fn node(&self, kind: AstKind) -> AstNode {
        AstNode {
            kind,
            source_line: self.scanner.line_no(),
            src_text: self.scanner.source_line(),
        }
    }

    /// Append a node to the pseudo-instruction being expanded, or else to
    /// the file (`ast_append`).
    fn ast_append(&mut self, node: AstNode) {
        if let Some(pseudo) = self.asm.pseudo_stack.last_mut() {
            if let AstKind::Pseudo { children, .. } = &mut pseudo.kind {
                children.push(node);
            }
        } else if let Some(file) = self.asm.file.as_mut() {
            if let AstKind::File { children, .. } = &mut file.kind {
                children.push(node);
            }
        }
    }

    /// Append an R-type instruction node (`emit_r`).
    pub fn emit_r(&mut self, op: i32, rd: i32, rs: i32, rt: i32) {
        let node: AstNode = self.node(AstKind::InstR { op, rd, rs, rt });
        self.ast_append(node);
    }

    /// Append a constant-shift node (`emit_r_shift`).
    pub fn emit_r_shift(&mut self, op: i32, rd: i32, rt: i32, shamt: i32) {
        let node: AstNode = self.node(AstKind::InstRShift { op, rd, rt, shamt });
        self.ast_append(node);
    }

    /// Append an I-type instruction node (`emit_i`).
    pub fn emit_i(&mut self, op: i32, rt: i32, rs: i32, imm: &ImmExpr) {
        let node: AstNode = self.node(AstKind::InstI {
            op,
            rt,
            rs,
            imm: *imm,
        });
        self.ast_append(node);
    }

    /// Append a jump node (`emit_j`).
    fn emit_j(&mut self, op: i32, target: &ImmExpr) {
        let node: AstNode = self.node(AstKind::InstJ {
            op,
            target: *target,
        });
        self.ast_append(node);
    }

    /// Append a floating-point R-type node (`emit_fp_r`).
    fn emit_fp_r(&mut self, op: i32, fd: i32, fs: i32, ft: i32) {
        let node: AstNode = self.node(AstKind::InstFpR { op, fd, fs, ft });
        self.ast_append(node);
    }

    /// Append a floating-point compare node (`emit_fp_compare`).
    fn emit_fp_compare(&mut self, op: i32, fs: i32, ft: i32, cc: i32) {
        let node: AstNode = self.node(AstKind::InstFpCompare { op, fs, ft, cc });
        self.ast_append(node);
    }

    /// Append a directive or label node.
    fn emit_node(&mut self, kind: AstKind) {
        let node: AstNode = self.node(kind);
        self.ast_append(node);
    }

    /// An immediate expression on an optional label (`make_imm_expr`).
    /// Naming a label creates it in the symbol table if it is new.
    pub fn make_imm_expr(
        &mut self,
        offset: i32,
        symbol: Option<&str>,
        pc_relative: bool,
    ) -> ImmExpr {
        ImmExpr {
            offset,
            symbol: symbol.map(|name: &str| self.symbols.lookup(name)),
            bits: 0,
            pc_relative,
        }
    }

    /// An address expression (`make_addr_expr`).  A label in the `$gp`
    /// small-data area is addressed off `$gp`.
    fn make_addr_expr(&mut self, offset: i32, symbol: Option<&str>, reg: i32) -> AddrExpr {
        if let (0, Some(name)) = (reg, symbol) {
            let id: LabelId = self.symbols.lookup(name);
            let label: &Label = self.symbols.label(id);
            if label.gp {
                let value: i64 = i64::from(offset) + label.addr - i64::from(self.mem.gp_midpoint);
                return AddrExpr {
                    reg: REG_GP as i32,
                    imm: ImmExpr::constant(value as i32),
                };
            }
        }
        AddrExpr {
            reg: i32::from(reg as u8),
            imm: self.make_imm_expr(offset, symbol, false),
        }
    }

    // ----- Errors -----

    /// Report a syntax error at the current line (`parse_error_at`).
    fn parse_error_at(&mut self, message: &str) {
        let line: i32 = self.scanner.line_no();
        self.parse_error_on_line(message, line);
    }

    /// Report a syntax error on a given line (`parse_error_on_line`).
    fn parse_error_on_line(&mut self, message: &str, line: i32) {
        self.asm.parse_error_occurred = true;
        self.asm.parse_errors_seen += 1;
        let mut text: Vec<u8> =
            format!("spim: (parser) {message} on line {line} of file ").into_bytes();
        text.extend_from_slice(self.asm.input_file_name.as_deref().unwrap_or(b"(stdin)"));
        text.push(b'\n');
        error_bytes(&text);
    }

    /// Skip to (not past) the end of the statement (`sync_to_nl`).
    fn sync_to_nl(&mut self) {
        while self.peek() != TOK_NL && self.peek() != TOK_EOF {
            self.advance();
        }
    }

    /// Take a token of kind `expected`, or report `what` (`expect`).
    fn expect(&mut self, expected: i32, what: &str) -> bool {
        if self.peek() == expected {
            self.advance();
            true
        } else {
            self.parse_error_at(what);
            false
        }
    }

    // ----- Operands -----

    fn parse_register(&mut self) -> i32 {
        if self.peek() == TOK_REG {
            self.advance();
            return self.value_int();
        }
        self.parse_error_at("Expected register");
        0
    }

    fn parse_fp_register(&mut self) -> i32 {
        if self.peek() == TOK_FP_REG {
            self.advance();
            return self.value_int();
        }
        self.parse_error_at("Expected FP register");
        0
    }

    /// An absolute value: `INT`, `INT + INT`, or `INT INT` where the second
    /// is negative (`8 -4` is a subtraction) (`parse_abs_addr`).
    fn parse_abs_addr(&mut self) -> i32 {
        if self.peek() != TOK_INT {
            self.parse_error_at("Expected integer");
            return 0;
        }
        self.advance();
        let value: i32 = self.value_int();
        if self.peek() == i32::from(b'+') {
            self.advance();
            if self.peek() != TOK_INT {
                self.parse_error_at("Expected integer after '+'");
                return value;
            }
            self.advance();
            return value.wrapping_add(self.value_int());
        }
        if self.peek() == TOK_INT {
            self.advance();
            let second: i32 = self.value_int();
            if second >= 0 {
                self.parse_error_at("Syntax error");
            }
            return value.wrapping_sub(second.wrapping_neg());
        }
        value
    }

    /// `INT`, `( EXPR )`, or a label (`parse_factor`).  An undefined
    /// label counts as 0; the first one is remembered so the datum can
    /// be patched when the label is defined.
    fn parse_factor(&mut self) -> i32 {
        match self.peek() {
            TOK_INT => {
                self.advance();
                self.value_int()
            }
            t if t == i32::from(b'(') => {
                self.advance();
                let value: i32 = self.parse_expr();
                self.expect(i32::from(b')'), "Expected ')'");
                value
            }
            TOK_ID => {
                self.advance();
                let name: String = self.value_name();
                let id: LabelId = self.symbols.lookup(&name);
                let addr: i64 = self.symbols.label(id).addr;
                if addr == 0 {
                    if self.asm.expr_unresolved_label.is_none() {
                        self.asm.expr_unresolved_label = Some(id);
                    }
                    return 0;
                }
                addr as i32
            }
            _ => {
                self.parse_error_at("Expected expression");
                0
            }
        }
    }

    /// `FACTOR (('*' | '/') FACTOR)*`.  The constant arithmetic wraps at
    /// 32 bits, as the machine's does (`i32::MIN / -1` is `i32::MIN`), and
    /// dividing by 0 gives 0.
    fn parse_term(&mut self) -> i32 {
        let mut value: i32 = self.parse_factor();
        while self.peek() == i32::from(b'*') || self.peek() == i32::from(b'/') {
            let op: i32 = self.advance();
            let right: i32 = self.parse_factor();
            value = if op == i32::from(b'*') {
                value.wrapping_mul(right)
            } else if right == 0 {
                0
            } else {
                value.wrapping_div(right)
            };
        }
        value
    }

    /// `TERM (('+' | '-') TERM)*`.
    fn parse_expr(&mut self) -> i32 {
        let mut value: i32 = self.parse_term();
        while self.peek() == i32::from(b'+') || self.peek() == i32::from(b'-') {
            let op: i32 = self.advance();
            let right: i32 = self.parse_term();
            value = if op == i32::from(b'+') {
                value.wrapping_add(right)
            } else {
                value.wrapping_sub(right)
            };
        }
        value
    }

    /// An expression whose first identifier is a label even if it spells
    /// a keyword (`parse_expression`).
    fn parse_expression(&mut self) -> i32 {
        self.scanner.force_identifier();
        self.asm.expr_unresolved_label = None;
        self.parse_expr()
    }

    /// A 32-bit immediate: a value, `label`, `label±value`, or
    /// `(value) >> n` (`parse_imm32`).
    fn parse_imm32(&mut self) -> ImmExpr {
        self.scanner.force_identifier();
        if self.peek() == TOK_ID {
            self.advance();
            let symbol: String = self.value_name();
            if self.peek() == i32::from(b'+') {
                self.advance();
                let offset: i32 = self.parse_abs_addr();
                return self.make_imm_expr(offset, Some(&symbol), false);
            }
            if self.peek() == i32::from(b'-') {
                self.advance();
                let offset: i32 = self.parse_abs_addr();
                return self.make_imm_expr(offset.wrapping_neg(), Some(&symbol), false);
            }
            return self.make_imm_expr(0, Some(&symbol), false);
        }
        if self.peek() == i32::from(b'(') {
            self.advance();
            let value: i32 = self.parse_abs_addr();
            self.expect(i32::from(b')'), "Expected ')'");
            self.expect(i32::from(b'>'), "Expected '>'");
            self.expect(i32::from(b'>'), "Expected '>'");
            if self.peek() != TOK_INT {
                self.parse_error_at("Expected integer after '>>'");
                return ImmExpr::constant(0);
            }
            self.advance();
            let shift: i32 = self.value_int();
            return ImmExpr::constant(value.wrapping_shr(shift as u32));
        }
        let value: i32 = self.parse_abs_addr();
        ImmExpr::constant(value)
    }

    /// A signed 16-bit immediate (warns if it does not fit).
    fn parse_imm16(&mut self) -> ImmExpr {
        // The instruction's line, before parsing the immediate scans the newline.
        let line: i32 = self.scanner.line_no();
        let expr: ImmExpr = self.parse_imm32();
        self.check_imm_range(&expr, -0x8000, 0x7fff, line);
        expr
    }

    /// An unsigned 16-bit immediate (warns if it does not fit).
    fn parse_uimm16(&mut self) -> ImmExpr {
        let line: i32 = self.scanner.line_no(); // as in parse_imm16
        let expr: ImmExpr = self.parse_imm32();
        self.check_uimm_range(&expr, 0, 0xffff, line);
        expr
    }

    /// A branch target: a label, relative to the branch (`parse_label`).
    fn parse_label(&mut self) -> ImmExpr {
        if self.peek() != TOK_ID {
            self.parse_error_at("Expected label");
            return ImmExpr {
                pc_relative: true,
                ..ImmExpr::default()
            };
        }
        self.advance();
        let symbol: String = self.value_name();
        let offset: i32 = (self.current_text_pc() as i32).wrapping_neg();
        self.make_imm_expr(offset, Some(&symbol), true)
    }

    /// An address operand: `(reg)`, `value`, `value(reg)`, `value+label`,
    /// `label`, `label±value`, `label±value(reg)` (`parse_address`).
    fn parse_address(&mut self) -> AddrExpr {
        self.scanner.force_identifier();
        if self.peek() == i32::from(b'(') {
            self.advance();
            let reg: i32 = self.parse_register();
            self.expect(i32::from(b')'), "Expected ')' after register");
            return self.make_addr_expr(0, None, reg);
        }
        if self.peek() == TOK_INT {
            let imm: i32 = self.parse_abs_addr();
            if self.peek() == i32::from(b'+') && self.peek2() == TOK_ID {
                self.advance();
                self.scanner.force_identifier();
                self.advance();
                let symbol: String = self.value_name();
                return self.make_addr_expr(imm, Some(&symbol), 0);
            }
            if self.peek() == i32::from(b'(') {
                self.advance();
                let reg: i32 = self.parse_register();
                self.expect(i32::from(b')'), "Expected ')'");
                return self.make_addr_expr(imm, None, reg);
            }
            return self.make_addr_expr(imm, None, 0);
        }
        if self.peek() == TOK_ID {
            self.advance();
            let symbol: String = self.value_name();
            let mut offset: i32 = 0;
            if self.peek() == i32::from(b'+') {
                self.advance();
                offset = self.parse_abs_addr();
            } else if self.peek() == i32::from(b'-') {
                self.advance();
                offset = self.parse_abs_addr().wrapping_neg();
            }
            let mut reg: i32 = 0;
            if self.peek() == i32::from(b'(') {
                self.advance();
                reg = self.parse_register();
                self.expect(i32::from(b')'), "Expected ')'");
            }
            return self.make_addr_expr(offset, Some(&symbol), reg);
        }
        self.parse_error_at("Expected address");
        self.make_addr_expr(0, None, 0)
    }

    // ----- Instructions -----

    /// `op rd, rs, rt`, with the immediate and two-operand forms of the
    /// arithmetic instructions (`parse_r3`).
    fn parse_r3(&mut self, op: i32) {
        let rd: i32 = self.parse_register();
        if op == TOK_CLO_OPCODE || op == TOK_CLZ_OPCODE {
            // The hardware wants rt equal to rd.
            let rs: i32 = self.parse_register();
            self.emit_r(op, rd, rs, rd);
            return;
        }
        if op == TOK_MUL_OPCODE {
            let rs: i32 = self.parse_register();
            if self.peek() == TOK_REG {
                let rt: i32 = self.parse_register();
                self.emit_r(op, rd, rs, rt);
            } else {
                let imm: ImmExpr = self.parse_imm32();
                self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                self.emit_r(op, rd, rs, 1);
            }
            return;
        }
        if op == TOK_SUB_OPCODE || op == TOK_SUBU_OPCODE {
            // `sub rd, rs, imm` becomes `addi rd, rs, -imm`.
            let add_op: i32 = if op == TOK_SUB_OPCODE {
                TOK_ADDI_OPCODE
            } else {
                TOK_ADDIU_OPCODE
            };
            if self.peek() != TOK_REG {
                let imm: ImmExpr = self.parse_imm32();
                let value: i32 = self.eval_imm_expr(&imm);
                self.emit_i(add_op, rd, rd, &ImmExpr::constant(value.wrapping_neg()));
                return;
            }
            let rs: i32 = self.parse_register();
            if self.peek() == TOK_REG {
                let rt: i32 = self.parse_register();
                self.emit_r(op, rd, rs, rt);
            } else {
                let imm: ImmExpr = self.parse_imm32();
                let value: i32 = self.eval_imm_expr(&imm);
                self.emit_i(add_op, rd, rs, &ImmExpr::constant(value.wrapping_neg()));
            }
            return;
        }
        if matches!(
            op,
            TOK_ADD_OPCODE
                | TOK_ADDU_OPCODE
                | TOK_AND_OPCODE
                | TOK_OR_OPCODE
                | TOK_XOR_OPCODE
                | TOK_SLT_OPCODE
                | TOK_SLTU_OPCODE
        ) {
            if self.peek() != TOK_REG {
                let imm: ImmExpr = self.parse_imm32();
                self.emit_i(op_to_imm_op(op), rd, rd, &imm);
                return;
            }
            let rs: i32 = self.parse_register();
            if self.peek() == TOK_REG {
                let rt: i32 = self.parse_register();
                self.emit_r(op, rd, rs, rt);
            } else {
                let imm: ImmExpr = self.parse_imm32();
                self.emit_i(op_to_imm_op(op), rd, rs, &imm);
            }
            return;
        }
        let rs: i32 = self.parse_register();
        let rt: i32 = self.parse_register();
        self.emit_r(op, rd, rs, rt);
    }

    /// `op rd, rt, shamt`; `ssnop` is `sll $0, $0, 1` (`parse_r2sh`).
    fn parse_r2sh(&mut self, op: i32) {
        if op == TOK_SSNOP_OPCODE {
            self.emit_r_shift(TOK_SLL_OPCODE, 0, 0, 1);
            return;
        }
        let rd: i32 = self.parse_register();
        let rt: i32 = self.parse_register();
        if self.peek() != TOK_INT {
            self.parse_error_at("Expected integer shift amount");
            return;
        }
        self.advance();
        let shamt: i32 = self.value_int();
        self.emit_r_shift(op, rd, rt, shamt);
    }

    /// `op rt, rs, imm16`, or `op rt, imm16` with rs = rt (`parse_i2`).
    fn parse_i2(&mut self, op: i32) {
        let rt: i32 = self.parse_register();
        let rs: i32 = if self.peek() == TOK_REG {
            self.parse_register()
        } else {
            rt
        };
        let imm: ImmExpr = if matches!(op, TOK_ANDI_OPCODE | TOK_ORI_OPCODE | TOK_XORI_OPCODE) {
            self.parse_uimm16()
        } else {
            self.parse_imm16()
        };
        self.emit_i(op, rt, rs, &imm);
    }

    /// `op rt, address` (loads and stores).
    fn parse_i2a(&mut self, op: i32) {
        let rt: i32 = self.parse_register();
        let addr: AddrExpr = self.parse_address();
        self.emit_i(op, rt, addr.reg, &addr.imm);
    }

    /// `beq rs, rt, label`, or `beq rs, imm, label` through `$at`.
    fn parse_b2(&mut self, op: i32) {
        let src1: i32 = self.parse_register();
        if self.peek() == TOK_REG {
            let src2: i32 = self.parse_register();
            let target: ImmExpr = self.parse_label();
            self.emit_i(op, src2, src1, &target);
        } else {
            let imm: ImmExpr = self.parse_imm32();
            let target: ImmExpr = self.parse_label();
            if imm.is_zero() {
                self.emit_i(op, src1, 0, &target);
            } else {
                self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                self.emit_i(op, src1, 1, &target);
            }
        }
    }

    /// `j label`, `j rs`, `jal label`, `jalr rs`, `jalr rd, rs`, the same two register forms of
    /// `jalr.hb`; `cop2` is an error (`parse_j`).
    fn parse_j(&mut self, op: i32) {
        // jalr.hb keeps its own opcode (jalr with the hazard-barrier bit) and takes
        // registers only. cop2 shares J_TYPE_INST, but spim has no coprocessor 2.
        if op == TOK_COP2_OPCODE {
            self.parse_error_at("cop2 is not supported: spim has no coprocessor 2");
        } else if self.peek() == TOK_REG {
            let r1: i32 = self.parse_register();
            if self.peek() == TOK_REG {
                // DEST SRC1 form
                let r2: i32 = self.parse_register();
                match op {
                    TOK_J_OPCODE | TOK_JR_OPCODE => self.emit_r(TOK_JR_OPCODE, 0, r2, 0),
                    TOK_JAL_OPCODE | TOK_JALR_OPCODE => self.emit_r(TOK_JALR_OPCODE, r1, r2, 0),
                    TOK_JALR_HB_OPCODE => self.emit_r(TOK_JALR_HB_OPCODE, r1, r2, 0),
                    // Never reached: only J_TYPE_INST and RD_RS_INST opcodes come here.
                    _ => crate::machine::fatal_error(&format!(
                        "internal error: parse_j: unexpected opcode {op}\n"
                    )),
                }
            } else {
                // SRC1-only form
                match op {
                    TOK_J_OPCODE | TOK_JR_OPCODE => self.emit_r(TOK_JR_OPCODE, 0, r1, 0),
                    TOK_JAL_OPCODE | TOK_JALR_OPCODE => self.emit_r(TOK_JALR_OPCODE, 31, r1, 0),
                    TOK_JALR_HB_OPCODE => self.emit_r(TOK_JALR_HB_OPCODE, 31, r1, 0),
                    // Never reached: as in the DEST SRC1 form above.
                    _ => crate::machine::fatal_error(&format!(
                        "internal error: parse_j: unexpected opcode {op}\n"
                    )),
                }
            }
        } else if op == TOK_JALR_HB_OPCODE {
            self.parse_error_at("jalr.hb needs a register operand");
        } else {
            let target: ImmExpr = self.parse_label();
            match op {
                TOK_J_OPCODE | TOK_JR_OPCODE => self.emit_j(TOK_J_OPCODE, &target),
                TOK_JAL_OPCODE | TOK_JALR_OPCODE => self.emit_j(TOK_JAL_OPCODE, &target),
                // Never reached: jalr.hb and cop2 are handled above.
                _ => crate::machine::fatal_error(&format!(
                    "internal error: parse_j: unexpected opcode {op}\n"
                )),
            }
        }
    }

    /// `syscall`, or `break n` (`parse_noarg`).
    fn parse_noarg(&mut self, op: i32) {
        if self.peek() == TOK_INT {
            self.advance();
            let n: i32 = self.value_int();
            if op == TOK_BREAK_OPCODE && n == 1 {
                self.parse_error_at("Breakpoint 1 is reserved for debugger");
            }
            self.emit_r(op, n, 0, 0);
        } else {
            self.emit_r(op, 0, 0, 0);
        }
    }

    /// A coprocessor register operand: written `$n` or `$fn`.
    fn parse_copreg(&mut self) -> i32 {
        if self.peek() == TOK_FP_REG {
            self.parse_fp_register()
        } else {
            self.parse_register()
        }
    }

    /// Parse a pseudo-instruction under a `PSEUDO` node (`parse_pseudo`).
    fn parse_pseudo(&mut self, op: i32) {
        let saved_line: i32 = self.scanner.line_no();
        let mut pseudo: AstNode = self.node(AstKind::Pseudo {
            mnemonic: token_name(op).to_string(),
            children: Vec::new(),
        });
        pseudo.source_line = saved_line;
        self.asm.pseudo_stack.push(pseudo);
        self.do_parse_pseudo(op);
        let pseudo: AstNode = self
            .asm
            .pseudo_stack
            .pop()
            .expect("parse_pseudo pushed this node");
        self.ast_append(pseudo);
    }

    /// The expansion of each pseudo-instruction (`do_parse_pseudo`).
    fn do_parse_pseudo(&mut self, op: i32) {
        match op {
            TOK_LI_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let imm: ImmExpr = self.parse_imm32();
                self.emit_i(TOK_ORI_OPCODE, rt, 0, &imm);
            }
            TOK_MOVE_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                self.emit_r(TOK_ADDU_OPCODE, rd, 0, rs);
            }
            TOK_NEG_PSEUDO_OP | TOK_NEGU_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                let sub: i32 = if op == TOK_NEG_PSEUDO_OP {
                    TOK_SUB_OPCODE
                } else {
                    TOK_SUBU_OPCODE
                };
                self.emit_r(sub, rd, 0, rs);
            }
            TOK_NOT_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                self.emit_r(TOK_NOR_OPCODE, rd, rs, 0);
            }
            TOK_ROR_PSEUDO_OP | TOK_ROL_PSEUDO_OP => self.parse_rotate(op),
            TOK_SLE_PSEUDO_OP | TOK_SLEU_PSEUDO_OP | TOK_SGT_PSEUDO_OP | TOK_SGTU_PSEUDO_OP
            | TOK_SGE_PSEUDO_OP | TOK_SGEU_PSEUDO_OP | TOK_SEQ_PSEUDO_OP | TOK_SNE_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                let rt: i32 = if self.peek() == TOK_REG {
                    self.parse_register()
                } else {
                    let imm: ImmExpr = self.parse_imm32();
                    if imm.is_zero() {
                        0
                    } else {
                        self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                        1
                    }
                };
                match op {
                    TOK_SLE_PSEUDO_OP | TOK_SLEU_PSEUDO_OP => self.set_le_inst(op, rd, rs, rt),
                    TOK_SGT_PSEUDO_OP | TOK_SGTU_PSEUDO_OP => self.set_gt_inst(op, rd, rs, rt),
                    TOK_SGE_PSEUDO_OP | TOK_SGEU_PSEUDO_OP => self.set_ge_inst(op, rd, rs, rt),
                    _ => self.set_eq_inst(op, rd, rs, rt),
                }
            }
            TOK_ABS_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                if rd != rs {
                    self.emit_r(TOK_ADDU_OPCODE, rd, 0, rs);
                }
                self.emit_i(TOK_BGEZ_OPCODE, 0, rs, &branch_offset(3));
                self.nop_inst();
                self.emit_r(TOK_SUB_OPCODE, rd, 0, rs);
            }
            TOK_NOP_PSEUDO_OP => self.nop_inst(),
            TOK_REM_PSEUDO_OP | TOK_REMU_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                if self.peek() == TOK_REG {
                    let rt: i32 = self.parse_register();
                    self.div_inst(op, rd, rs, rt, false);
                } else {
                    let imm: ImmExpr = self.parse_imm32();
                    if imm.is_zero() {
                        self.parse_error_at("Divide by zero");
                    } else {
                        self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                        self.div_inst(op, rd, rs, 1, true);
                    }
                }
            }
            TOK_MULO_PSEUDO_OP | TOK_MULOU_PSEUDO_OP => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                if self.peek() == TOK_REG {
                    let rt: i32 = self.parse_register();
                    self.mult_inst(op, rd, rs, rt);
                } else {
                    let imm: ImmExpr = self.parse_imm32();
                    if imm.is_zero() {
                        // n * 0 is 0.
                        self.emit_i(TOK_ORI_OPCODE, rd, 0, &imm);
                    } else {
                        self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                        self.mult_inst(op, rd, rs, 1);
                    }
                }
            }
            TOK_MFC1_D_PSEUDO_OP | TOK_MTC1_D_PSEUDO_OP => {
                let line: i32 = self.scanner.line_no(); // the last operand can consume the newline
                let reg: i32 = self.parse_register();
                let copreg: i32 = self.parse_copreg();
                if reg == 31 || copreg == 31 {
                    // The pair's second register would be 32.
                    self.parse_error_on_line(
                        "Register pair starts at the last register (31)",
                        line,
                    );
                    return;
                }
                let real: i32 = if op == TOK_MFC1_D_PSEUDO_OP {
                    TOK_MFC1_OPCODE
                } else {
                    TOK_MTC1_OPCODE
                };
                self.emit_fp_r(real, 0, copreg, reg);
                self.emit_fp_r(real, 0, copreg.wrapping_add(1), reg.wrapping_add(1));
            }
            TOK_LI_D_PSEUDO_OP => {
                let fd: i32 = self.parse_fp_register();
                if self.peek() != TOK_FP {
                    self.parse_error_at("Expected FP literal");
                    return;
                }
                self.advance();
                let bits: u64 = self.scanner.scan_value.fp().to_bits();
                // The double's two words, low word first (little-endian).
                self.emit_i(TOK_ORI_OPCODE, 1, 0, &ImmExpr::constant(bits as i32));
                self.emit_fp_r(TOK_MTC1_OPCODE, 0, fd, 1);
                self.emit_i(
                    TOK_ORI_OPCODE,
                    1,
                    0,
                    &ImmExpr::constant((bits >> 32) as i32),
                );
                self.emit_fp_r(TOK_MTC1_OPCODE, 0, fd.wrapping_add(1), 1);
            }
            TOK_LI_S_PSEUDO_OP => {
                let fd: i32 = self.parse_fp_register();
                if self.peek() != TOK_FP {
                    self.parse_error_at("Expected FP literal");
                    return;
                }
                self.advance();
                let single: f32 = self.scanner.scan_value.fp() as f32;
                self.emit_i(
                    TOK_ORI_OPCODE,
                    1,
                    0,
                    &ImmExpr::constant(single.to_bits() as i32),
                );
                self.emit_fp_r(TOK_MTC1_OPCODE, 0, fd, 1);
            }
            TOK_L_D_PSEUDO_OP | TOK_L_S_PSEUDO_OP | TOK_S_D_PSEUDO_OP | TOK_S_S_PSEUDO_OP => {
                let real: i32 = match op {
                    TOK_L_D_PSEUDO_OP => TOK_LDC1_OPCODE,
                    TOK_L_S_PSEUDO_OP => TOK_LWC1_OPCODE,
                    TOK_S_D_PSEUDO_OP => TOK_SDC1_OPCODE,
                    _ => TOK_SWC1_OPCODE,
                };
                let fr: i32 = self.parse_fp_register();
                let addr: AddrExpr = self.parse_address();
                self.emit_i(real, fr, addr.reg, &addr.imm);
            }
            TOK_LD_PSEUDO_OP | TOK_SD_PSEUDO_OP => {
                let real: i32 = if op == TOK_LD_PSEUDO_OP {
                    TOK_LW_OPCODE
                } else {
                    TOK_SW_OPCODE
                };
                let line: i32 = self.scanner.line_no(); // the address can consume the newline
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                if rt == 31 {
                    // The pair's second register would be 32.
                    self.parse_error_on_line(
                        "Register pair starts at the last register (31)",
                        line,
                    );
                    return;
                }
                self.emit_i(real, rt, addr.reg, &addr.imm);
                self.emit_i(real, rt.wrapping_add(1), addr.reg, &addr.imm.plus(4));
            }
            TOK_ULW_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                self.emit_i(TOK_LWL_OPCODE, rt, addr.reg, &addr.imm.plus(3));
                self.emit_i(TOK_LWR_OPCODE, rt, addr.reg, &addr.imm);
            }
            TOK_ULH_PSEUDO_OP | TOK_ULHU_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                let load_high: i32 = if op == TOK_ULH_PSEUDO_OP {
                    TOK_LB_OPCODE
                } else {
                    TOK_LBU_OPCODE
                };
                self.emit_i(load_high, rt, addr.reg, &addr.imm.plus(1));
                self.emit_i(TOK_LBU_OPCODE, 1, addr.reg, &addr.imm);
                self.emit_r_shift(TOK_SLL_OPCODE, rt, rt, 8);
                self.emit_r(TOK_OR_OPCODE, rt, rt, 1);
            }
            TOK_USW_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                self.emit_i(TOK_SWL_OPCODE, rt, addr.reg, &addr.imm.plus(3));
                self.emit_i(TOK_SWR_OPCODE, rt, addr.reg, &addr.imm);
            }
            TOK_USH_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                self.emit_i(TOK_SB_OPCODE, rt, addr.reg, &addr.imm);
                self.emit_r_shift(TOK_SLL_OPCODE, 1, rt, 24);
                self.emit_r_shift(TOK_SRL_OPCODE, rt, rt, 8);
                self.emit_r(TOK_OR_OPCODE, rt, rt, 1);
                self.emit_i(TOK_SB_OPCODE, rt, addr.reg, &addr.imm.plus(1));
                self.emit_r_shift(TOK_SRL_OPCODE, 1, rt, 24);
                self.emit_r_shift(TOK_SLL_OPCODE, rt, rt, 8);
                self.emit_r(TOK_OR_OPCODE, rt, rt, 1);
            }
            TOK_LA_PSEUDO_OP => {
                let rt: i32 = self.parse_register();
                let addr: AddrExpr = self.parse_address();
                if addr.reg != 0 {
                    self.emit_i(TOK_ADDI_OPCODE, rt, addr.reg, &addr.imm);
                } else {
                    self.emit_i(TOK_ORI_OPCODE, rt, 0, &addr.imm);
                }
            }
            TOK_BEQZ_PSEUDO_OP | TOK_BNEZ_PSEUDO_OP => {
                let rs: i32 = self.parse_register();
                let target: ImmExpr = self.parse_label();
                let real: i32 = if op == TOK_BEQZ_PSEUDO_OP {
                    TOK_BEQ_OPCODE
                } else {
                    TOK_BNE_OPCODE
                };
                self.emit_i(real, 0, rs, &target);
            }
            TOK_B_PSEUDO_OP | TOK_BAL_PSEUDO_OP => {
                let target: ImmExpr = self.parse_label();
                let real: i32 = if op == TOK_BAL_PSEUDO_OP {
                    TOK_BGEZAL_OPCODE
                } else {
                    TOK_BGEZ_OPCODE
                };
                self.emit_i(real, 0, 0, &target);
            }
            TOK_BGT_PSEUDO_OP | TOK_BGTU_PSEUDO_OP | TOK_BLE_PSEUDO_OP | TOK_BLEU_PSEUDO_OP => {
                self.parse_branch_gt_le(op)
            }
            TOK_BGE_PSEUDO_OP | TOK_BGEU_PSEUDO_OP | TOK_BLT_PSEUDO_OP | TOK_BLTU_PSEUDO_OP => {
                self.parse_branch_ge_lt(op)
            }
            _ => {
                self.parse_error_at("Pseudo-op not yet supported");
                self.sync_to_nl();
            }
        }
    }

    /// `ror`/`rol` by a register or a constant.
    fn parse_rotate(&mut self, op: i32) {
        let rd: i32 = self.parse_register();
        let rs: i32 = self.parse_register();
        let right: bool = op == TOK_ROR_PSEUDO_OP;
        if self.peek() == TOK_REG {
            let rt: i32 = self.parse_register();
            self.emit_r(TOK_SUBU_OPCODE, 1, 0, rt);
            if right {
                self.emit_r(TOK_SLLV_OPCODE, 1, 1, rs);
                self.emit_r(TOK_SRLV_OPCODE, rd, rt, rs);
            } else {
                self.emit_r(TOK_SRLV_OPCODE, 1, 1, rs);
                self.emit_r(TOK_SLLV_OPCODE, rd, rt, rs);
            }
            self.emit_r(TOK_OR_OPCODE, rd, rd, 1);
        } else {
            let line: i32 = self.scanner.line_no(); // as in parse_imm16
            let imm: ImmExpr = self.parse_imm32();
            let distance: i32 = self.eval_imm_expr(&imm);
            self.check_imm_range(&imm, 0, 31, line);
            if right {
                self.emit_r_shift(TOK_SLL_OPCODE, 1, rs, distance.wrapping_neg());
                self.emit_r_shift(TOK_SRL_OPCODE, rd, rs, distance);
            } else {
                self.emit_r_shift(TOK_SRL_OPCODE, 1, rs, distance.wrapping_neg());
                self.emit_r_shift(TOK_SLL_OPCODE, rd, rs, distance);
            }
            self.emit_r(TOK_OR_OPCODE, rd, rd, 1);
        }
    }

    /// `bgt`, `bgtu`, `ble`, `bleu`: compare with `slt` into `$at`, branch.
    fn parse_branch_gt_le(&mut self, op: i32) {
        let greater: bool = op == TOK_BGT_PSEUDO_OP || op == TOK_BGTU_PSEUDO_OP;
        let signed: bool = op == TOK_BGT_PSEUDO_OP || op == TOK_BLE_PSEUDO_OP;
        // bgt branches when the comparison is true (bne), ble when false.
        let branch: i32 = if greater {
            TOK_BNE_OPCODE
        } else {
            TOK_BEQ_OPCODE
        };
        let rs: i32 = self.parse_register();
        if self.peek() == TOK_REG {
            let rt: i32 = self.parse_register();
            let target: ImmExpr = self.parse_label();
            let slt: i32 = if signed {
                TOK_SLT_OPCODE
            } else {
                TOK_SLTU_OPCODE
            };
            self.emit_r(slt, 1, rt, rs);
            self.emit_i(branch, 0, 1, &target);
            return;
        }
        let imm: ImmExpr = self.parse_imm32();
        let target: ImmExpr = self.parse_label();
        if signed {
            self.emit_i(TOK_SLTI_OPCODE, 1, rs, &imm.plus(1));
            // bgt: rs >= imm+1 means rs > imm; branch when slti is false.
            let after_slti: i32 = if greater {
                TOK_BEQ_OPCODE
            } else {
                TOK_BNE_OPCODE
            };
            self.emit_i(after_slti, 0, 1, &target);
        } else if greater {
            self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
            self.emit_i(TOK_BEQ_OPCODE, rs, 1, &branch_offset(3));
            self.emit_r(TOK_SLTU_OPCODE, 1, rs, 1);
            self.emit_i(TOK_BEQ_OPCODE, 0, 1, &target);
        } else {
            self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
            self.emit_i(TOK_BEQ_OPCODE, rs, 1, &target);
            self.emit_r(TOK_SLTU_OPCODE, 1, rs, 1);
            self.emit_i(TOK_BNE_OPCODE, 0, 1, &target);
        }
    }

    /// `bge`, `bgeu`, `blt`, `bltu`: `slt` into `$at`, branch.
    fn parse_branch_ge_lt(&mut self, op: i32) {
        let signed: bool = op == TOK_BGE_PSEUDO_OP || op == TOK_BLT_PSEUDO_OP;
        // bge branches when rs < rt is false (beq $at, $0), blt when true.
        let branch: i32 = if op == TOK_BGE_PSEUDO_OP || op == TOK_BGEU_PSEUDO_OP {
            TOK_BEQ_OPCODE
        } else {
            TOK_BNE_OPCODE
        };
        let rs: i32 = self.parse_register();
        if self.peek() == TOK_REG {
            let rt: i32 = self.parse_register();
            let target: ImmExpr = self.parse_label();
            let slt: i32 = if signed {
                TOK_SLT_OPCODE
            } else {
                TOK_SLTU_OPCODE
            };
            self.emit_r(slt, 1, rs, rt);
            self.emit_i(branch, 0, 1, &target);
        } else {
            let imm: ImmExpr = self.parse_imm32();
            let target: ImmExpr = self.parse_label();
            let slti: i32 = if signed {
                TOK_SLTI_OPCODE
            } else {
                TOK_SLTIU_OPCODE
            };
            self.emit_i(slti, 1, rs, &imm);
            self.emit_i(branch, 0, 1, &target);
        }
    }

    /// Parse one instruction, by the shape of its opcode (`parse_asm_code`).
    fn parse_asm_code(&mut self) {
        let op: i32 = self.advance();
        match token_type(op) {
            Some(OpType::NoargTypeInst) => self.parse_noarg(op),
            Some(OpType::RdRsRtInst) => self.parse_r3(op),
            Some(OpType::RdRtShamtInst) => self.parse_r2sh(op),
            Some(OpType::RsInst) => {
                let rs: i32 = self.parse_register();
                self.emit_r(op, 0, rs, 0);
            }
            Some(OpType::RtRsImmInst) => self.parse_i2(op),
            Some(OpType::RtImmInst) => {
                let rt: i32 = self.parse_register();
                let imm: ImmExpr = self.parse_uimm16();
                self.emit_i(op, rt, 0, &imm);
            }
            Some(OpType::RtAddrInst) => self.parse_i2a(op),
            Some(OpType::BranchRsRtLabelInst) => self.parse_b2(op),
            Some(OpType::BranchRsLabelInst) => {
                let rs: i32 = self.parse_register();
                let target: ImmExpr = self.parse_label();
                self.emit_i(op, 0, rs, &target);
            }
            Some(OpType::JTypeInst) | Some(OpType::RdRsInst) => self.parse_j(op),
            Some(OpType::PseudoOp) => self.parse_pseudo(op),
            Some(OpType::RtCopregInst) | Some(OpType::FpRtCopregInst) => {
                let reg: i32 = self.parse_register();
                let copreg: i32 = self.parse_copreg();
                self.emit_fp_r(op, 0, copreg, reg);
            }
            Some(OpType::RsRtInst) => self.parse_rs_rt(op),
            Some(OpType::RdInst) => {
                let rd: i32 = self.parse_register();
                self.emit_r(op, rd, 0, 0);
            }
            Some(OpType::RdRtRsInst) => {
                // sllv rd, rt, rs: the register operands are swapped in
                // the encoding; a constant third operand means sll.
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                if self.peek() == TOK_REG {
                    let rt: i32 = self.parse_register();
                    self.emit_r(op, rd, rt, rs);
                } else {
                    if self.peek() != TOK_INT {
                        self.parse_error_at("Expected register or integer shift");
                        return;
                    }
                    self.advance();
                    let shamt: i32 = self.value_int();
                    self.emit_r_shift(op_to_imm_op(op), rd, rs, shamt);
                }
            }
            Some(OpType::RsImmInst) => {
                let rs: i32 = self.parse_register();
                let imm: ImmExpr = self.parse_imm16();
                self.emit_i(op, 0, rs, &imm);
            }
            Some(OpType::BranchCoprocCondInst) => {
                let nd: i32 = i32::from(opcode_is_nullified_branch(op));
                let tf: i32 = i32::from(opcode_is_true_branch(op));
                let mut cc: i32 = 0;
                if self.peek() == TOK_INT {
                    self.advance();
                    cc = self.value_int();
                }
                let target: ImmExpr = self.parse_label();
                self.emit_i(op, (cc << 2) | (nd << 1) | tf, 0, &target);
            }
            Some(OpType::MovcTypeInst) => {
                let rd: i32 = self.parse_register();
                let rs: i32 = self.parse_register();
                let mut cc: i32 = 0;
                if self.peek() == TOK_INT {
                    self.advance();
                    cc = self.value_int();
                }
                self.emit_r(op, rd, rs, (cc & 0x7) << 2);
            }
            Some(OpType::FpFdFsInst) => {
                let fd: i32 = self.parse_fp_register();
                let fs: i32 = self.parse_fp_register();
                self.emit_fp_r(op, fd, fs, 0);
            }
            Some(OpType::FpFdFsFtInst) => {
                let fd: i32 = self.parse_fp_register();
                let fs: i32 = self.parse_fp_register();
                let ft: i32 = self.parse_fp_register();
                self.emit_fp_r(op, fd, fs, ft);
            }
            Some(OpType::FpCmpFsFtInst) => {
                let mut cc: i32 = 0;
                if self.peek() == TOK_INT {
                    self.advance();
                    cc = self.value_int();
                }
                let fs: i32 = self.parse_fp_register();
                let ft: i32 = self.parse_fp_register();
                self.emit_fp_compare(op, fs, ft, cc);
            }
            Some(OpType::FpMovcTypeInst) => {
                let fd: i32 = self.parse_fp_register();
                let fs: i32 = self.parse_fp_register();
                if self.peek() == TOK_REG {
                    let rt: i32 = self.parse_register();
                    self.emit_fp_r(op, fd, fs, rt);
                } else if self.peek() == TOK_INT {
                    self.advance();
                    let cc: i32 = self.value_int();
                    self.emit_fp_r(op, fd, fs, (cc & 0x7) << 2);
                } else {
                    self.emit_fp_r(op, fd, fs, 0);
                }
            }
            Some(OpType::FpFtAddrInst) => {
                let fr: i32 = self.parse_fp_register();
                let addr: AddrExpr = self.parse_address();
                self.emit_i(op, fr, addr.reg, &addr.imm);
            }
            Some(OpType::AsmDir) | Some(OpType::FpFdFrFsFtInst) | None => {
                self.parse_error_at("Instruction not yet supported");
                self.sync_to_nl();
            }
        }
    }

    /// `mult rs, rt`, the traps `teq rs, rt`, and `div`/`divu` with two
    /// operands (the instruction) or three (the pseudo-instruction).
    fn parse_rs_rt(&mut self, op: i32) {
        let r1: i32 = self.parse_register();
        let next: i32 = self.peek();
        if next != TOK_REG && next != TOK_INT && next != TOK_ID {
            self.parse_error_at("Expected operand for div/mult");
            return;
        }
        if matches!(
            op,
            TOK_TEQ_OPCODE
                | TOK_TGE_OPCODE
                | TOK_TGEU_OPCODE
                | TOK_TLT_OPCODE
                | TOK_TLTU_OPCODE
                | TOK_TNE_OPCODE
        ) {
            let r2: i32 = self.parse_register();
            self.emit_r(op, 0, r1, r2);
        } else if op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE {
            let r2: i32 = self.parse_register();
            if self.peek() == TOK_NL || self.peek() == TOK_EOF {
                self.emit_r(op, 0, r1, r2);
            } else if self.peek() == TOK_REG {
                let r3: i32 = self.parse_register();
                self.div_inst(op, r1, r2, r3, false);
            } else {
                let imm: ImmExpr = self.parse_imm32();
                if imm.is_zero() {
                    self.parse_error_at("Divide by zero");
                } else {
                    self.emit_i(TOK_ORI_OPCODE, 1, 0, &imm);
                    self.div_inst(op, r1, r2, 1, true);
                }
            }
        } else {
            let r2: i32 = self.parse_register();
            self.emit_r(op, 0, r1, r2);
        }
    }

    // ----- Directives -----

    /// A segment directive (`.data`, `.text`, ...), optionally with a
    /// start address (`emit_dir_seg`).  The parser follows the segment
    /// at once, so it knows whether data is allowed.
    fn parse_dir_segment(&mut self, directive: i32) {
        let mut start: Option<u32> = None;
        if self.peek() == TOK_INT {
            self.advance();
            start = Some(self.value_int() as u32);
        }
        let kind: AstKind = match directive {
            TOK_TEXT_DIRECTIVE => AstKind::DirText(start),
            TOK_K_TEXT_DIRECTIVE => AstKind::DirKtext(start),
            TOK_K_DATA_DIRECTIVE => AstKind::DirKdata(start),
            _ => AstKind::DirData(start),
        };
        if matches!(kind, AstKind::DirText(_) | AstKind::DirKtext(_)) {
            self.asm.data_dir = false;
            self.asm.text_dir = true;
        } else {
            self.asm.data_dir = true;
            self.asm.text_dir = false;
            self.asm.auto_align = true;
        }
        self.emit_node(kind);
    }

    /// The `NAME` of `.globl`, `.extern`, `.comm`.
    fn parse_directive_name(&mut self, what: &str) -> Option<String> {
        self.scanner.force_identifier();
        if self.peek() != TOK_ID {
            self.parse_error_at(what);
            return None;
        }
        self.advance();
        Some(self.value_name())
    }

    /// The values of `.byte`, `.half`, `.word` (`parse_expr_list`).
    fn parse_expr_list(&mut self, kind: DataKind) {
        self.asm.data_int_buf.clear();
        self.asm.data_directive_line = self.scanner.line_no();
        loop {
            let errors_before: i32 = self.asm.parse_errors_seen;
            let value: i32 = self.parse_expression();
            if self.asm.parse_errors_seen != errors_before {
                // The bad token was not taken; skip the rest of the line
                // rather than ask for an expression again forever.
                self.sync_to_nl();
                break;
            }
            let symbol: Option<LabelId> = self.asm.expr_unresolved_label.take();
            self.asm.data_int_buf.push(ImmExpr {
                offset: value,
                symbol,
                ..ImmExpr::default()
            });
            if self.peek() == TOK_NL || self.peek() == TOK_EOF {
                break;
            }
        }
        if self.asm.data_int_buf.is_empty() {
            return;
        }
        let values: Vec<ImmExpr> = std::mem::take(&mut self.asm.data_int_buf);
        let ast_kind: AstKind = match kind {
            DataKind::Byte => AstKind::DataByte(values),
            DataKind::Half => AstKind::DataHalf(values),
            _ => AstKind::DataWord(values),
        };
        let mut node: AstNode = self.node(ast_kind);
        node.source_line = self.asm.data_directive_line;
        self.ast_append(node);
    }

    /// The literals of `.float` and `.double` (`parse_fp_expr_list`).
    fn parse_fp_expr_list(&mut self, kind: DataKind) {
        self.asm.data_fp_buf.clear();
        self.asm.data_directive_line = self.scanner.line_no();
        loop {
            if self.peek() != TOK_FP {
                self.parse_error_at("Expected floating-point literal");
                return;
            }
            self.advance();
            let value: f64 = self.scanner.scan_value.fp();
            self.asm.data_fp_buf.push(value);
            if self.peek() == TOK_NL || self.peek() == TOK_EOF {
                break;
            }
        }
        let values: Vec<f64> = std::mem::take(&mut self.asm.data_fp_buf);
        let ast_kind: AstKind = if kind == DataKind::Float {
            AstKind::DataFloat(values)
        } else {
            AstKind::DataDouble(values)
        };
        let mut node: AstNode = self.node(ast_kind);
        node.source_line = self.asm.data_directive_line;
        self.ast_append(node);
    }

    /// Move this line's labels to the address an alignment will give the
    /// next datum (`align_labels_to`).
    fn align_labels_to(&mut self, alignment: u32) {
        if !self.asm.data_dir || !self.asm.auto_align || self.asm.this_line_labels.is_empty() {
            return;
        }
        let current: u32 = self.current_data_pc();
        let aligned: u32 = current.wrapping_add((1u32 << alignment) - 1) & (!0u32 << alignment);
        self.fix_current_label_address(aligned);
    }

    /// A data directive aligns its data to its size, at parse time and
    /// again when it is emitted.
    fn align_for_data(&mut self, alignment: u32) {
        self.align_labels_to(alignment);
        if self.asm.data_dir {
            self.set_data_alignment(alignment as i32);
        }
    }

    /// The strings of `.ascii`/`.asciiz` (`parse_string_list`).
    fn parse_string_list(&mut self) {
        loop {
            if self.peek() != TOK_STR {
                self.parse_error_at("Expected string literal");
                return;
            }
            self.advance();
            let mut bytes: Vec<u8> = self.scanner.scan_value.text();
            // The C engine measures the decoded string with strlen.
            if let Some(nul) = bytes.iter().position(|b: &u8| *b == 0) {
                bytes.truncate(nul);
            }
            if self.asm.text_dir {
                self.parse_error_at("Can't put data in text segment");
            } else {
                let null_terminate: bool = self.asm.null_term;
                self.emit_node(AstKind::DataString {
                    bytes,
                    null_terminate,
                });
            }
            if self.peek() == TOK_NL || self.peek() == TOK_EOF {
                break;
            }
        }
    }

    /// Parse one directive (`parse_directive`).
    fn parse_directive(&mut self, directive: i32) {
        match directive {
            TOK_DATA_DIRECTIVE | TOK_K_DATA_DIRECTIVE | TOK_TEXT_DIRECTIVE
            | TOK_K_TEXT_DIRECTIVE => self.parse_dir_segment(directive),
            TOK_GLOBAL_DIRECTIVE => {
                if let Some(name) = self.parse_directive_name("Expected identifier after .globl") {
                    self.emit_node(AstKind::DirGlobl(name));
                }
            }
            TOK_WORD_DIRECTIVE => {
                self.align_for_data(2);
                self.parse_expr_list(DataKind::Word);
            }
            TOK_HALF_DIRECTIVE => {
                self.align_for_data(1);
                self.parse_expr_list(DataKind::Half);
            }
            TOK_BYTE_DIRECTIVE => self.parse_expr_list(DataKind::Byte),
            TOK_ASCII_DIRECTIVE | TOK_ASCIIZ_DIRECTIVE => {
                self.asm.null_term = directive == TOK_ASCIIZ_DIRECTIVE;
                self.parse_string_list();
            }
            TOK_FLOAT_DIRECTIVE => {
                self.align_for_data(2);
                self.parse_fp_expr_list(DataKind::Float);
            }
            TOK_DOUBLE_DIRECTIVE => {
                self.align_for_data(3);
                self.parse_fp_expr_list(DataKind::Double);
            }
            TOK_SPACE_DIRECTIVE => {
                let size: i32 = self.parse_expression();
                if self.asm.text_dir {
                    self.parse_error_at("Can't put data in text segment");
                    return;
                }
                self.emit_node(AstKind::DirSpace(size));
            }
            TOK_ALIGN_DIRECTIVE => {
                let line: i32 = self.scanner.line_no(); // the expression can consume the newline
                let n: i32 = self.parse_expression();
                if !(0..=31).contains(&n) {
                    // C shifts a 32-bit mask by it.
                    self.parse_error_on_line("Alignment out of range (0 .. 31)", line);
                } else {
                    if n == 0 {
                        self.asm.auto_align = false;
                    }
                    self.emit_node(AstKind::DirAlign(n));
                }
            }
            TOK_COMM_DIRECTIVE | TOK_EXTERN_DIRECTIVE => {
                if let Some(name) = self.parse_directive_name("Expected ID") {
                    let size: i32 = self.parse_expression();
                    let kind: AstKind = if directive == TOK_COMM_DIRECTIVE {
                        AstKind::DirComm { name, size }
                    } else {
                        AstKind::DirExtern { name, size }
                    };
                    self.emit_node(kind);
                }
            }
            TOK_ERR_DIRECTIVE => self.parse_error_at(".err directive"),
            TOK_FILE_DIRECTIVE
            | TOK_LOC_DIRECTIVE
            | TOK_FRAME_DIRECTIVE
            | TOK_MASK_DIRECTIVE
            | TOK_FMASK_DIRECTIVE
            | TOK_ENT_DIRECTIVE
            | TOK_END_DIRECTIVE
            | TOK_LABEL_DIRECTIVE
            | TOK_LIVEREG_DIRECTIVE
            | TOK_OPTIONS_DIRECTIVE
            | TOK_BGNB_DIRECTIVE
            | TOK_ENDB_DIRECTIVE
            | TOK_ENDR_DIRECTIVE
            | TOK_ASM0_DIRECTIVE
            | TOK_ALIAS_DIRECTIVE
            | TOK_SET_DIRECTIVE => {
                // Metadata for other assemblers: ignore the rest of the line.
                self.sync_to_nl();
            }
            _ => {
                self.parse_error_at("Directive not yet supported");
                self.sync_to_nl();
            }
        }
    }

    /// `NAME:` or `NAME = EXPR` (`parse_opt_label`).
    fn parse_opt_label(&mut self) {
        self.advance();
        let name: String = self.value_name();
        let separator: i32 = self.advance();
        if separator == i32::from(b':') {
            self.emit_node(AstKind::LabelNormal(name));
        } else {
            let value: i32 = self.parse_expression();
            self.emit_node(AstKind::LabelConst(name, value));
        }
    }

    /// Parse one statement (`parse_line`).
    fn parse_line(&mut self) {
        if self.peek() == TOK_NL {
            self.advance();
            return;
        }
        if self.peek() == TOK_EOF {
            return;
        }
        if self.peek() == TOK_ID
            && (self.peek2() == i32::from(b':') || self.peek2() == i32::from(b'='))
        {
            self.parse_opt_label();
        }
        let t: i32 = self.peek();
        if t == TOK_NL {
            self.advance();
            return;
        }
        if t == TOK_EOF {
            return;
        }
        if token_type(t) == Some(OpType::AsmDir) {
            let directive: i32 = self.advance();
            self.parse_directive(directive);
        } else {
            self.parse_asm_code();
        }
        self.clear_labels();
        if self.asm.parse_error_occurred {
            self.sync_to_nl();
        } else if self.peek() == TOK_NL {
            self.advance();
        } else if self.peek() != TOK_EOF {
            self.parse_error_at("Extra tokens after instruction");
            self.sync_to_nl();
        }
    }

    /// Start parsing `input`, named `file_name` in messages (`parser_init`).
    pub fn parser_init(&mut self, input: Vec<u8>, file_name: &[u8]) {
        self.scanner.init(input);
        self.asm.input_file_name = Some(file_name.to_vec());
        self.asm.parse_errors_seen = 0;
        self.asm.parse_error_occurred = false;
        self.asm.data_dir = false;
        self.asm.text_dir = true;
        let file: AstNode = self.node(AstKind::File {
            source_file: Some(file_name.to_vec()),
            children: Vec::new(),
        });
        self.asm.file = Some(file);
    }

    /// Parse to the end of the input, print the tree if asked, and emit it
    /// unless only printing was asked (`parse_file`).  Returns the number
    /// of errors.
    pub fn parse_file(&mut self) -> i32 {
        // Emit only what this call parses, starting in the segment that was
        // current when it began: the REPL's `.` command parses one line onto
        // the tree of the file loaded last, whose nodes were emitted already,
        // and the parse moves `data_dir`/`text_dir` as it meets `.data` and
        // `.text`, which the emit pass must replay from the start.
        let already_emitted: usize = match self.asm.file.as_ref().map(|f: &AstNode| &f.kind) {
            Some(AstKind::File { children, .. }) => children.len(),
            _ => 0,
        };
        let (start_data_dir, start_text_dir): (bool, bool) = (self.asm.data_dir, self.asm.text_dir);
        while self.peek() != TOK_EOF {
            self.asm.parse_error_occurred = false;
            self.scanner.start_line();
            self.parse_line();
        }
        self.clear_labels();
        if let Some(file) = self.asm.file.take() {
            if self.asm.print_ast {
                let mut text: Vec<u8> = Vec::new();
                file.print(&self.symbols, &mut text);
                error_bytes(&text);
            }
            if self.asm.show_expansion {
                let mut text: Vec<u8> = Vec::new();
                if let AstKind::File { children, .. } = &file.kind {
                    for child in children {
                        if matches!(child.kind, AstKind::Pseudo { .. }) {
                            child.print(&self.symbols, &mut text);
                        }
                    }
                }
                error_bytes(&text);
            }
            if self.asm.print_ast_json {
                let mut text: Vec<u8> = Vec::new();
                file.print_json(&self.symbols, &mut text);
                text.push(b'\n');
                crate::machine::write_stdout(&text);
            }
            if !self.asm.print_ast_only {
                self.asm.data_dir = start_data_dir;
                self.asm.text_dir = start_text_dir;
                self.emit_ast(&file, already_emitted);
            }
            self.asm.file = Some(file);
        }
        self.asm.parse_errors_seen
    }

    // ----- Emitting the tree -----

    /// Resolve the forward references of this line's labels and forget
    /// them (`clear_labels`).
    pub fn clear_labels(&mut self) {
        let labels: Vec<LabelId> = std::mem::take(&mut self.asm.this_line_labels);
        for id in labels {
            self.resolve_label_uses(id);
        }
    }

    /// Move this line's labels to `new_addr`, after an alignment
    /// (`fix_current_label_address`).
    pub fn fix_current_label_address(&mut self, new_addr: u32) {
        for index in 0..self.asm.this_line_labels.len() {
            let id: LabelId = self.asm.this_line_labels[index];
            self.symbols.label_mut(id).addr = i64::from(new_addr);
        }
    }

    /// Assemble the tree of a file into memory, in source order
    /// (`emit_ast`).
    /// Only the top-level nodes after the first `skip` are emitted (the
    /// ones before were emitted by an earlier call).
    pub fn emit_ast(&mut self, file: &AstNode, skip: usize) {
        let AstKind::File { children, .. } = &file.kind else {
            return;
        };
        self.emit_chain(children.get(skip..).unwrap_or(&[]));
        self.clear_labels();
        self.asm.emit_source_active = false;
        self.asm.emit_source_text = None;
    }

    /// Emit a list of nodes; a label stays "on the line" until the next
    /// node that is not a label or a `.globl`.
    fn emit_chain(&mut self, nodes: &[AstNode]) {
        for node in nodes {
            self.emit_one(node);
            if !matches!(
                node.kind,
                AstKind::LabelNormal(_) | AstKind::LabelConst(..) | AstKind::DirGlobl(_)
            ) {
                self.clear_labels();
            }
        }
    }

    /// Store each value of a `.byte`/`.half`/`.word`; a value on an
    /// undefined label is patched when the label is defined
    /// (`emit_data_int_node`).
    fn emit_data_int_node(&mut self, values: &[ImmExpr], width: i32) {
        for expr in values {
            let value: i32 = match expr.symbol {
                Some(id) if !self.symbols.label(id).is_defined() => {
                    let pc: u32 = self.current_data_pc();
                    self.record_data_uses_symbol(pc, id, expr.offset, width);
                    expr.offset
                }
                _ => self.eval_imm_expr(expr),
            };
            match width {
                1 => self.store_byte(value),
                2 => self.store_half(value),
                _ => self.store_word(value),
            }
        }
    }

    /// Emit one node (`emit_one`).
    fn emit_one(&mut self, node: &AstNode) {
        // Replay the node's line, for messages and listings.
        self.scanner.set_line_no(node.source_line);
        self.asm.emit_source_active = true;
        self.asm.emit_source_text = node.src_text.as_deref().map(Rc::from);
        match &node.kind {
            AstKind::File { .. } => {}
            AstKind::DirText(start) | AstKind::DirKtext(start) => {
                self.user_kernel_text_segment(matches!(node.kind, AstKind::DirKtext(_)));
                self.asm.data_dir = false;
                self.asm.text_dir = true;
                if let Some(addr) = start {
                    self.set_text_pc(*addr);
                }
            }
            AstKind::DirData(start) | AstKind::DirKdata(start) => {
                self.user_kernel_data_segment(matches!(node.kind, AstKind::DirKdata(_)));
                self.asm.data_dir = true;
                self.asm.text_dir = false;
                self.enable_data_alignment();
                self.asm.auto_align = true;
                if let Some(addr) = start {
                    self.set_data_pc(*addr);
                }
            }
            AstKind::DirAlign(n) => {
                if *n == 0 {
                    self.asm.auto_align = false;
                }
                if self.asm.text_dir {
                    self.align_text(*n);
                } else {
                    self.align_data(*n);
                }
            }
            AstKind::DirSpace(size) => self.increment_data_pc(*size),
            AstKind::DirGlobl(name) => {
                self.make_label_global(name);
            }
            AstKind::DirExtern { name, size } => {
                self.make_label_global(name);
                let id: LabelId = self.symbols.lookup(name);
                if self.symbols.label(id).addr == 0 {
                    let pc: u32 = self.current_data_pc();
                    self.record_label(name, pc, true);
                }
                self.increment_data_pc(*size);
            }
            AstKind::DirComm { name, size } => {
                self.align_data(2);
                let id: LabelId = self.symbols.lookup(name);
                if self.symbols.label(id).addr == 0 {
                    let pc: u32 = self.current_data_pc();
                    self.record_label(name, pc, true);
                }
                self.increment_data_pc(*size);
            }
            AstKind::DataByte(values) => self.emit_data_int_node(values, 1),
            AstKind::DataHalf(values) => {
                self.align_for_data(1);
                self.emit_data_int_node(values, 2);
            }
            AstKind::DataWord(values) => {
                self.align_for_data(2);
                self.emit_data_int_node(values, 4);
            }
            AstKind::DataFloat(values) => {
                self.align_for_data(2);
                for value in values {
                    self.store_float(*value);
                }
            }
            AstKind::DataDouble(values) => {
                self.align_for_data(3);
                for value in values {
                    self.store_double(*value);
                }
            }
            AstKind::DataString {
                bytes,
                null_terminate,
            } => {
                if !self.asm.text_dir {
                    self.store_string(bytes, *null_terminate);
                }
            }
            AstKind::LabelNormal(name) => {
                let addr: u32 = if self.asm.text_dir {
                    self.current_text_pc()
                } else {
                    self.current_data_pc()
                };
                let id: LabelId = self.record_label(name, addr, false);
                self.asm.this_line_labels.insert(0, id);
            }
            AstKind::LabelConst(name, value) => {
                let id: LabelId = self.record_label(name, *value as u32, true);
                self.symbols.label_mut(id).constant = true;
            }
            AstKind::InstR { op, rd, rs, rt } => self.r_type_inst(*op, *rd, *rs, *rt),
            AstKind::InstRShift { op, rd, rt, shamt } => self.r_sh_type_inst(*op, *rd, *rt, *shamt),
            AstKind::InstI { op, rt, rs, imm } => self.i_type_inst(*op, *rt, *rs, imm),
            AstKind::InstJ { op, target } => self.j_type_inst(*op, target),
            AstKind::InstFpR { op, fd, fs, ft } => self.r_co_type_inst(*op, *fd, *fs, *ft),
            AstKind::InstFpCompare { op, fs, ft, cc } => self.r_cond_type_inst(*op, *fs, *ft, *cc),
            AstKind::Pseudo { children, .. } => self.emit_chain(children),
        }
    }
}
