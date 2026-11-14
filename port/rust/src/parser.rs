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
use crate::instruction::{opcode_is_nullified_branch, opcode_is_true_branch, ImmediateExpression};
use crate::machine::{error_bytes, Machine};
use crate::opcode_types::OpType;
use crate::opcodes::*;
use crate::pseudo_op::{branch_offset, opcode_to_immediate_opcode};
use crate::registers::GLOBAL_POINTER_REGISTER;
use crate::scanner::ScanContext;
use crate::symbol_table::{Label, LabelId};

/// An address operand, `offset(register)` with an optional label
/// (`addr_expr`).
#[derive(Clone, Copy, Debug)]
pub struct AddressExpression {
    /// The base register (stored in 8 bits, as in C).
    pub register_number: i32,
    /// The offset expression.
    pub immediate: ImmediateExpression,
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
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_instructions,
        };
        self.scanner.peek(&context)
    }

    /// The kind of the token after the next.
    pub fn peek2(&mut self) -> i32 {
        let context: ScanContext<'_> = ScanContext {
            symbols: &self.symbols,
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_instructions,
        };
        self.scanner.peek2(&context)
    }

    /// Take the next token; its value goes to `scanner.scan_value`.
    pub fn advance(&mut self) -> i32 {
        let context: ScanContext<'_> = ScanContext {
            symbols: &self.symbols,
            allow_pseudo_ops: !self.options.bare_machine && self.options.accept_pseudo_instructions,
        };
        self.scanner.advance(&context)
    }

    /// The integer value of the last token taken.
    fn value_integer(&self) -> i32 {
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
            source_text: self.scanner.source_line(),
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
    pub fn emit_r(
        &mut self,
        op: i32,
        destination_register: i32,
        source_register: i32,
        target_register: i32,
    ) {
        let node: AstNode = self.node(AstKind::InstructionR {
            op,
            destination_register,
            source_register,
            target_register,
        });
        self.ast_append(node);
    }

    /// Append a constant-shift node (`emit_r_shift`).
    pub fn emit_r_shift(
        &mut self,
        op: i32,
        destination_register: i32,
        target_register: i32,
        shift_amount: i32,
    ) {
        let node: AstNode = self.node(AstKind::InstructionRShift {
            op,
            destination_register,
            target_register,
            shift_amount,
        });
        self.ast_append(node);
    }

    /// Append an I-type instruction node (`emit_i`).
    pub fn emit_i(
        &mut self,
        op: i32,
        target_register: i32,
        source_register: i32,
        immediate: &ImmediateExpression,
    ) {
        let node: AstNode = self.node(AstKind::InstructionI {
            op,
            target_register,
            source_register,
            immediate: *immediate,
        });
        self.ast_append(node);
    }

    /// Append a jump node (`emit_j`).
    fn emit_j(&mut self, op: i32, target: &ImmediateExpression) {
        let node: AstNode = self.node(AstKind::InstructionJ {
            op,
            target: *target,
        });
        self.ast_append(node);
    }

    /// Append a floating-point R-type node (`emit_floating_point_r`).
    fn emit_floating_point_r(
        &mut self,
        op: i32,
        floating_point_destination: i32,
        fs: i32,
        floating_point_target: i32,
    ) {
        let node: AstNode = self.node(AstKind::InstructionFloatingPointR {
            op,
            floating_point_destination,
            fs,
            floating_point_target,
        });
        self.ast_append(node);
    }

    /// Append a floating-point compare node (`emit_floating_point_compare`).
    fn emit_floating_point_compare(
        &mut self,
        op: i32,
        fs: i32,
        floating_point_target: i32,
        condition_code: i32,
    ) {
        let node: AstNode = self.node(AstKind::InstructionFloatingPointCompare {
            op,
            fs,
            floating_point_target,
            condition_code,
        });
        self.ast_append(node);
    }

    /// Append a directive or label node.
    fn emit_node(&mut self, kind: AstKind) {
        let node: AstNode = self.node(kind);
        self.ast_append(node);
    }

    /// An immediate expression on an optional label (`make_immediate_expression`).
    /// Naming a label creates it in the symbol table if it is new.
    pub fn make_immediate_expression(
        &mut self,
        offset: i32,
        symbol: Option<&str>,
        program_counter_relative: bool,
    ) -> ImmediateExpression {
        ImmediateExpression {
            offset,
            symbol: symbol.map(|name: &str| self.symbols.lookup(name)),
            bits: 0,
            program_counter_relative,
        }
    }

    /// An address expression (`make_address_expression`).  A label in the `$gp`
    /// small-data area is addressed off `$gp`.
    fn make_address_expression(
        &mut self,
        offset: i32,
        symbol: Option<&str>,
        register_number: i32,
    ) -> AddressExpression {
        if let (0, Some(name)) = (register_number, symbol) {
            let id: LabelId = self.symbols.lookup(name);
            let label: &Label = self.symbols.label(id);
            if label.gp {
                let value: i64 =
                    i64::from(offset) + label.address - i64::from(self.mem.global_pointer_midpoint);
                return AddressExpression {
                    register_number: GLOBAL_POINTER_REGISTER as i32,
                    immediate: ImmediateExpression::constant(value as i32),
                };
            }
        }
        AddressExpression {
            register_number: i32::from(register_number as u8),
            immediate: self.make_immediate_expression(offset, symbol, false),
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

    /// Skip to (not past) the end of the statement (`sync_to_newline`).
    fn sync_to_newline(&mut self) {
        while self.peek() != TOKEN_NEWLINE && self.peek() != TOKEN_EOF {
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
        if self.peek() == TOKEN_REGISTER {
            self.advance();
            return self.value_integer();
        }
        self.parse_error_at("Expected register");
        0
    }

    fn parse_floating_point_register(&mut self) -> i32 {
        if self.peek() == TOKEN_FLOATING_POINT_REGISTER {
            self.advance();
            return self.value_integer();
        }
        self.parse_error_at("Expected FP register");
        0
    }

    /// An absolute value: `INT`, `INT + INT`, or `INT INT` where the second
    /// is negative (`8 -4` is a subtraction) (`parse_absolute_address`).
    fn parse_absolute_address(&mut self) -> i32 {
        if self.peek() != TOKEN_INTEGER {
            self.parse_error_at("Expected integer");
            return 0;
        }
        self.advance();
        let value: i32 = self.value_integer();
        if self.peek() == i32::from(b'+') {
            self.advance();
            if self.peek() != TOKEN_INTEGER {
                self.parse_error_at("Expected integer after '+'");
                return value;
            }
            self.advance();
            return value.wrapping_add(self.value_integer());
        }
        if self.peek() == TOKEN_INTEGER {
            self.advance();
            let second: i32 = self.value_integer();
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
            TOKEN_INTEGER => {
                self.advance();
                self.value_integer()
            }
            t if t == i32::from(b'(') => {
                self.advance();
                let value: i32 = self.parse_sum_of_terms();
                self.expect(i32::from(b')'), "Expected ')'");
                value
            }
            TOKEN_IDENTIFIER => {
                self.advance();
                let name: String = self.value_name();
                let id: LabelId = self.symbols.lookup(&name);
                let address: i64 = self.symbols.label(id).address;
                if address == 0 {
                    if self.asm.expression_unresolved_label.is_none() {
                        self.asm.expression_unresolved_label = Some(id);
                    }
                    return 0;
                }
                address as i32
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
    fn parse_sum_of_terms(&mut self) -> i32 {
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
        self.asm.expression_unresolved_label = None;
        self.parse_sum_of_terms()
    }

    /// A 32-bit immediate: a value, `label`, `label±value`, or
    /// `(value) >> n` (`parse_immediate_32`).
    fn parse_immediate_32(&mut self) -> ImmediateExpression {
        self.scanner.force_identifier();
        if self.peek() == TOKEN_IDENTIFIER {
            self.advance();
            let symbol: String = self.value_name();
            if self.peek() == i32::from(b'+') {
                self.advance();
                let offset: i32 = self.parse_absolute_address();
                return self.make_immediate_expression(offset, Some(&symbol), false);
            }
            if self.peek() == i32::from(b'-') {
                self.advance();
                let offset: i32 = self.parse_absolute_address();
                return self.make_immediate_expression(offset.wrapping_neg(), Some(&symbol), false);
            }
            return self.make_immediate_expression(0, Some(&symbol), false);
        }
        if self.peek() == i32::from(b'(') {
            self.advance();
            let value: i32 = self.parse_absolute_address();
            self.expect(i32::from(b')'), "Expected ')'");
            self.expect(i32::from(b'>'), "Expected '>'");
            self.expect(i32::from(b'>'), "Expected '>'");
            if self.peek() != TOKEN_INTEGER {
                self.parse_error_at("Expected integer after '>>'");
                return ImmediateExpression::constant(0);
            }
            self.advance();
            let shift: i32 = self.value_integer();
            return ImmediateExpression::constant(value.wrapping_shr(shift as u32));
        }
        let value: i32 = self.parse_absolute_address();
        ImmediateExpression::constant(value)
    }

    /// A signed 16-bit immediate (warns if it does not fit).
    fn parse_immediate_16(&mut self) -> ImmediateExpression {
        // The instruction's line, before parsing the immediate scans the newline.
        let line: i32 = self.scanner.line_no();
        let expr: ImmediateExpression = self.parse_immediate_32();
        self.check_immediate_range(&expr, -0x8000, 0x7fff, line);
        expr
    }

    /// An unsigned 16-bit immediate (warns if it does not fit).
    fn parse_unsigned_immediate_16(&mut self) -> ImmediateExpression {
        let line: i32 = self.scanner.line_no(); // as in parse_immediate_16
        let expr: ImmediateExpression = self.parse_immediate_32();
        self.check_unsigned_immediate_range(&expr, 0, 0xffff, line);
        expr
    }

    /// A branch target: a label, relative to the branch (`parse_label`).
    fn parse_label(&mut self) -> ImmediateExpression {
        if self.peek() != TOKEN_IDENTIFIER {
            self.parse_error_at("Expected label");
            return ImmediateExpression {
                program_counter_relative: true,
                ..ImmediateExpression::default()
            };
        }
        self.advance();
        let symbol: String = self.value_name();
        let offset: i32 = (self.current_text_location() as i32).wrapping_neg();
        self.make_immediate_expression(offset, Some(&symbol), true)
    }

    /// An address operand: `(reg)`, `value`, `value(reg)`, `value+label`,
    /// `label`, `label±value`, `label±value(reg)` (`parse_address`).
    fn parse_address(&mut self) -> AddressExpression {
        self.scanner.force_identifier();
        if self.peek() == i32::from(b'(') {
            self.advance();
            let register_number: i32 = self.parse_register();
            self.expect(i32::from(b')'), "Expected ')' after register");
            return self.make_address_expression(0, None, register_number);
        }
        if self.peek() == TOKEN_INTEGER {
            let immediate: i32 = self.parse_absolute_address();
            if self.peek() == i32::from(b'+') && self.peek2() == TOKEN_IDENTIFIER {
                self.advance();
                self.scanner.force_identifier();
                self.advance();
                let symbol: String = self.value_name();
                return self.make_address_expression(immediate, Some(&symbol), 0);
            }
            if self.peek() == i32::from(b'(') {
                self.advance();
                let register_number: i32 = self.parse_register();
                self.expect(i32::from(b')'), "Expected ')'");
                return self.make_address_expression(immediate, None, register_number);
            }
            return self.make_address_expression(immediate, None, 0);
        }
        if self.peek() == TOKEN_IDENTIFIER {
            self.advance();
            let symbol: String = self.value_name();
            let mut offset: i32 = 0;
            if self.peek() == i32::from(b'+') {
                self.advance();
                offset = self.parse_absolute_address();
            } else if self.peek() == i32::from(b'-') {
                self.advance();
                offset = self.parse_absolute_address().wrapping_neg();
            }
            let mut register_number: i32 = 0;
            if self.peek() == i32::from(b'(') {
                self.advance();
                register_number = self.parse_register();
                self.expect(i32::from(b')'), "Expected ')'");
            }
            return self.make_address_expression(offset, Some(&symbol), register_number);
        }
        self.parse_error_at("Expected address");
        self.make_address_expression(0, None, 0)
    }

    // ----- Instructions -----

    /// `op rd, rs, rt`, with the immediate and two-operand forms of the
    /// arithmetic instructions (`parse_r3`).
    fn parse_r3(&mut self, op: i32) {
        let destination_register: i32 = self.parse_register();
        if op == TOKEN_CLO_OPCODE || op == TOKEN_CLZ_OPCODE {
            // The hardware wants rt equal to rd.
            let source_register: i32 = self.parse_register();
            self.emit_r(
                op,
                destination_register,
                source_register,
                destination_register,
            );
            return;
        }
        if op == TOKEN_MUL_OPCODE {
            let source_register: i32 = self.parse_register();
            if self.peek() == TOKEN_REGISTER {
                let target_register: i32 = self.parse_register();
                self.emit_r(op, destination_register, source_register, target_register);
            } else {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                self.emit_r(op, destination_register, source_register, 1);
            }
            return;
        }
        if op == TOKEN_SUB_OPCODE || op == TOKEN_SUBU_OPCODE {
            // `sub rd, rs, imm` becomes `addi rd, rs, -imm`.
            let add_opcode: i32 = if op == TOKEN_SUB_OPCODE {
                TOKEN_ADDI_OPCODE
            } else {
                TOKEN_ADDIU_OPCODE
            };
            if self.peek() != TOKEN_REGISTER {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                let value: i32 = self.evaluate_immediate_expression(&immediate);
                self.emit_i(
                    add_opcode,
                    destination_register,
                    destination_register,
                    &ImmediateExpression::constant(value.wrapping_neg()),
                );
                return;
            }
            let source_register: i32 = self.parse_register();
            if self.peek() == TOKEN_REGISTER {
                let target_register: i32 = self.parse_register();
                self.emit_r(op, destination_register, source_register, target_register);
            } else {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                let value: i32 = self.evaluate_immediate_expression(&immediate);
                self.emit_i(
                    add_opcode,
                    destination_register,
                    source_register,
                    &ImmediateExpression::constant(value.wrapping_neg()),
                );
            }
            return;
        }
        if matches!(
            op,
            TOKEN_ADD_OPCODE
                | TOKEN_ADDU_OPCODE
                | TOKEN_AND_OPCODE
                | TOKEN_OR_OPCODE
                | TOKEN_XOR_OPCODE
                | TOKEN_SLT_OPCODE
                | TOKEN_SLTU_OPCODE
        ) {
            if self.peek() != TOKEN_REGISTER {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                self.emit_i(
                    opcode_to_immediate_opcode(op),
                    destination_register,
                    destination_register,
                    &immediate,
                );
                return;
            }
            let source_register: i32 = self.parse_register();
            if self.peek() == TOKEN_REGISTER {
                let target_register: i32 = self.parse_register();
                self.emit_r(op, destination_register, source_register, target_register);
            } else {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                self.emit_i(
                    opcode_to_immediate_opcode(op),
                    destination_register,
                    source_register,
                    &immediate,
                );
            }
            return;
        }
        let source_register: i32 = self.parse_register();
        let target_register: i32 = self.parse_register();
        self.emit_r(op, destination_register, source_register, target_register);
    }

    /// `op rd, rt, shamt`; `ssnop` is `sll $0, $0, 1` (`parse_two_registers_shift_amount`).
    fn parse_two_registers_shift_amount(&mut self, op: i32) {
        if op == TOKEN_SSNOP_OPCODE {
            self.emit_r_shift(TOKEN_SLL_OPCODE, 0, 0, 1);
            return;
        }
        let destination_register: i32 = self.parse_register();
        let target_register: i32 = self.parse_register();
        if self.peek() != TOKEN_INTEGER {
            self.parse_error_at("Expected integer shift amount");
            return;
        }
        self.advance();
        let shift_amount: i32 = self.value_integer();
        self.emit_r_shift(op, destination_register, target_register, shift_amount);
    }

    /// `op rt, rs, imm16`, or `op rt, imm16` with rs = rt (`parse_i2`).
    fn parse_i2(&mut self, op: i32) {
        let target_register: i32 = self.parse_register();
        let source_register: i32 = if self.peek() == TOKEN_REGISTER {
            self.parse_register()
        } else {
            target_register
        };
        let immediate: ImmediateExpression =
            if matches!(op, TOKEN_ANDI_OPCODE | TOKEN_ORI_OPCODE | TOKEN_XORI_OPCODE) {
                self.parse_unsigned_immediate_16()
            } else {
                self.parse_immediate_16()
            };
        self.emit_i(op, target_register, source_register, &immediate);
    }

    /// `op rt, address` (loads and stores).
    fn parse_i2a(&mut self, op: i32) {
        let target_register: i32 = self.parse_register();
        let address: AddressExpression = self.parse_address();
        self.emit_i(
            op,
            target_register,
            address.register_number,
            &address.immediate,
        );
    }

    /// `beq rs, rt, label`, or `beq rs, imm, label` through `$at`.
    fn parse_b2(&mut self, op: i32) {
        let source_1: i32 = self.parse_register();
        if self.peek() == TOKEN_REGISTER {
            let source_2: i32 = self.parse_register();
            let target: ImmediateExpression = self.parse_label();
            self.emit_i(op, source_2, source_1, &target);
        } else {
            let immediate: ImmediateExpression = self.parse_immediate_32();
            let target: ImmediateExpression = self.parse_label();
            if immediate.is_zero() {
                self.emit_i(op, source_1, 0, &target);
            } else {
                self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                self.emit_i(op, source_1, 1, &target);
            }
        }
    }

    /// `j label`, `j rs`, `jal label`, `jalr rs`, `jalr rd, rs`, the same two register forms of
    /// `jalr.hb`; `cop2` is an error (`parse_j`).
    fn parse_j(&mut self, op: i32) {
        // jalr.hb keeps its own opcode (jalr with the hazard-barrier bit) and takes
        // registers only. cop2 shares J_TYPE_INST, but spim has no coprocessor 2.
        if op == TOKEN_COP2_OPCODE {
            self.parse_error_at("cop2 is not supported: spim has no coprocessor 2");
        } else if self.peek() == TOKEN_REGISTER {
            let r1: i32 = self.parse_register();
            if self.peek() == TOKEN_REGISTER {
                // DEST SRC1 form
                let r2: i32 = self.parse_register();
                match op {
                    TOKEN_J_OPCODE | TOKEN_JR_OPCODE => self.emit_r(TOKEN_JR_OPCODE, 0, r2, 0),
                    TOKEN_JAL_OPCODE | TOKEN_JALR_OPCODE => {
                        self.emit_r(TOKEN_JALR_OPCODE, r1, r2, 0)
                    }
                    TOKEN_JALR_HB_OPCODE => self.emit_r(TOKEN_JALR_HB_OPCODE, r1, r2, 0),
                    // Never reached: only J_TYPE_INST and RD_RS_INST opcodes come here.
                    _ => crate::machine::fatal_error(&format!(
                        "internal error: parse_j: unexpected opcode {op}\n"
                    )),
                }
            } else {
                // SRC1-only form
                match op {
                    TOKEN_J_OPCODE | TOKEN_JR_OPCODE => self.emit_r(TOKEN_JR_OPCODE, 0, r1, 0),
                    TOKEN_JAL_OPCODE | TOKEN_JALR_OPCODE => {
                        self.emit_r(TOKEN_JALR_OPCODE, 31, r1, 0)
                    }
                    TOKEN_JALR_HB_OPCODE => self.emit_r(TOKEN_JALR_HB_OPCODE, 31, r1, 0),
                    // Never reached: as in the DEST SRC1 form above.
                    _ => crate::machine::fatal_error(&format!(
                        "internal error: parse_j: unexpected opcode {op}\n"
                    )),
                }
            }
        } else if op == TOKEN_JALR_HB_OPCODE {
            self.parse_error_at("jalr.hb needs a register operand");
        } else {
            let target: ImmediateExpression = self.parse_label();
            match op {
                TOKEN_J_OPCODE | TOKEN_JR_OPCODE => self.emit_j(TOKEN_J_OPCODE, &target),
                TOKEN_JAL_OPCODE | TOKEN_JALR_OPCODE => self.emit_j(TOKEN_JAL_OPCODE, &target),
                // Never reached: jalr.hb and cop2 are handled above.
                _ => crate::machine::fatal_error(&format!(
                    "internal error: parse_j: unexpected opcode {op}\n"
                )),
            }
        }
    }

    /// `syscall`, or `break n` (`parse_no_argument`).
    fn parse_no_argument(&mut self, op: i32) {
        if self.peek() == TOKEN_INTEGER {
            self.advance();
            let n: i32 = self.value_integer();
            if op == TOKEN_BREAK_OPCODE && n == 1 {
                self.parse_error_at("Breakpoint 1 is reserved for debugger");
            }
            self.emit_r(op, n, 0, 0);
        } else {
            self.emit_r(op, 0, 0, 0);
        }
    }

    /// A coprocessor register operand: written `$n` or `$fn`.
    fn parse_coprocessor_register(&mut self) -> i32 {
        if self.peek() == TOKEN_FLOATING_POINT_REGISTER {
            self.parse_floating_point_register()
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
        let mut pseudo: AstNode = self
            .asm
            .pseudo_stack
            .pop()
            .expect("parse_pseudo pushed this node");
        // The expansion is the pseudo-op's line, even when its last operand
        // read on to the newline before a child was built.
        if let AstKind::Pseudo { children, .. } = &mut pseudo.kind {
            for child in children.iter_mut() {
                child.source_line = saved_line;
            }
        }
        self.ast_append(pseudo);
    }

    /// The expansion of each pseudo-instruction (`do_parse_pseudo`).
    fn do_parse_pseudo(&mut self, op: i32) {
        match op {
            TOKEN_LI_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let immediate: ImmediateExpression = self.parse_immediate_32();
                self.emit_i(TOKEN_ORI_OPCODE, target_register, 0, &immediate);
            }
            TOKEN_MOVE_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                self.emit_r(TOKEN_ADDU_OPCODE, destination_register, 0, source_register);
            }
            TOKEN_NEG_PSEUDO_OP | TOKEN_NEGU_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                let subtract: i32 = if op == TOKEN_NEG_PSEUDO_OP {
                    TOKEN_SUB_OPCODE
                } else {
                    TOKEN_SUBU_OPCODE
                };
                self.emit_r(subtract, destination_register, 0, source_register);
            }
            TOKEN_NOT_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                self.emit_r(TOKEN_NOR_OPCODE, destination_register, source_register, 0);
            }
            TOKEN_ROR_PSEUDO_OP | TOKEN_ROL_PSEUDO_OP => self.parse_rotate(op),
            TOKEN_SLE_PSEUDO_OP | TOKEN_SLEU_PSEUDO_OP | TOKEN_SGT_PSEUDO_OP
            | TOKEN_SGTU_PSEUDO_OP | TOKEN_SGE_PSEUDO_OP | TOKEN_SGEU_PSEUDO_OP
            | TOKEN_SEQ_PSEUDO_OP | TOKEN_SNE_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                let target_register: i32 = if self.peek() == TOKEN_REGISTER {
                    self.parse_register()
                } else {
                    let immediate: ImmediateExpression = self.parse_immediate_32();
                    if immediate.is_zero() {
                        0
                    } else {
                        self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                        1
                    }
                };
                match op {
                    TOKEN_SLE_PSEUDO_OP | TOKEN_SLEU_PSEUDO_OP => self
                        .set_less_or_equal_instruction(
                            op,
                            destination_register,
                            source_register,
                            target_register,
                        ),
                    TOKEN_SGT_PSEUDO_OP | TOKEN_SGTU_PSEUDO_OP => self
                        .set_greater_than_instruction(
                            op,
                            destination_register,
                            source_register,
                            target_register,
                        ),
                    TOKEN_SGE_PSEUDO_OP | TOKEN_SGEU_PSEUDO_OP => self
                        .set_greater_or_equal_instruction(
                            op,
                            destination_register,
                            source_register,
                            target_register,
                        ),
                    _ => self.set_equal_instruction(
                        op,
                        destination_register,
                        source_register,
                        target_register,
                    ),
                }
            }
            TOKEN_ABS_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                if destination_register != source_register {
                    self.emit_r(TOKEN_ADDU_OPCODE, destination_register, 0, source_register);
                }
                self.emit_i(TOKEN_BGEZ_OPCODE, 0, source_register, &branch_offset(3));
                self.nop_instruction();
                self.emit_r(TOKEN_SUB_OPCODE, destination_register, 0, source_register);
            }
            TOKEN_NOP_PSEUDO_OP => self.nop_instruction(),
            TOKEN_REM_PSEUDO_OP | TOKEN_REMU_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                if self.peek() == TOKEN_REGISTER {
                    let target_register: i32 = self.parse_register();
                    self.divide_instruction(
                        op,
                        destination_register,
                        source_register,
                        target_register,
                        false,
                    );
                } else {
                    let immediate: ImmediateExpression = self.parse_immediate_32();
                    if immediate.is_zero() {
                        self.parse_error_at("Divide by zero");
                    } else {
                        self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                        self.divide_instruction(op, destination_register, source_register, 1, true);
                    }
                }
            }
            TOKEN_MULO_PSEUDO_OP | TOKEN_MULOU_PSEUDO_OP => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                if self.peek() == TOKEN_REGISTER {
                    let target_register: i32 = self.parse_register();
                    self.multiply_instruction(
                        op,
                        destination_register,
                        source_register,
                        target_register,
                    );
                } else {
                    let immediate: ImmediateExpression = self.parse_immediate_32();
                    if immediate.is_zero() {
                        // n * 0 is 0.
                        self.emit_i(TOKEN_ORI_OPCODE, destination_register, 0, &immediate);
                    } else {
                        self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                        self.multiply_instruction(op, destination_register, source_register, 1);
                    }
                }
            }
            TOKEN_MFC1_D_PSEUDO_OP | TOKEN_MTC1_D_PSEUDO_OP => {
                let line: i32 = self.scanner.line_no(); // the last operand can consume the newline
                let register_number: i32 = self.parse_register();
                let coprocessor_register: i32 = self.parse_coprocessor_register();
                if register_number == 31 || coprocessor_register == 31 {
                    // The pair's second register would be 32.
                    self.parse_error_on_line(
                        "Register pair starts at the last register (31)",
                        line,
                    );
                    return;
                }
                let real: i32 = if op == TOKEN_MFC1_D_PSEUDO_OP {
                    TOKEN_MFC1_OPCODE
                } else {
                    TOKEN_MTC1_OPCODE
                };
                self.emit_floating_point_r(real, 0, coprocessor_register, register_number);
                self.emit_floating_point_r(
                    real,
                    0,
                    coprocessor_register.wrapping_add(1),
                    register_number.wrapping_add(1),
                );
            }
            TOKEN_LI_D_PSEUDO_OP => {
                let floating_point_destination: i32 = self.parse_floating_point_register();
                if self.peek() != TOKEN_FLOATING_POINT {
                    self.parse_error_at("Expected FP literal");
                    return;
                }
                self.advance();
                let bits: u64 = self.scanner.scan_value.fp().to_bits();
                // The double's two words, low word first (little-endian).
                self.emit_i(
                    TOKEN_ORI_OPCODE,
                    1,
                    0,
                    &ImmediateExpression::constant(bits as i32),
                );
                self.emit_floating_point_r(TOKEN_MTC1_OPCODE, 0, floating_point_destination, 1);
                self.emit_i(
                    TOKEN_ORI_OPCODE,
                    1,
                    0,
                    &ImmediateExpression::constant((bits >> 32) as i32),
                );
                self.emit_floating_point_r(
                    TOKEN_MTC1_OPCODE,
                    0,
                    floating_point_destination.wrapping_add(1),
                    1,
                );
            }
            TOKEN_LI_S_PSEUDO_OP => {
                let floating_point_destination: i32 = self.parse_floating_point_register();
                if self.peek() != TOKEN_FLOATING_POINT {
                    self.parse_error_at("Expected FP literal");
                    return;
                }
                self.advance();
                let single: f32 = self.scanner.scan_value.fp() as f32;
                self.emit_i(
                    TOKEN_ORI_OPCODE,
                    1,
                    0,
                    &ImmediateExpression::constant(single.to_bits() as i32),
                );
                self.emit_floating_point_r(TOKEN_MTC1_OPCODE, 0, floating_point_destination, 1);
            }
            TOKEN_L_D_PSEUDO_OP | TOKEN_L_S_PSEUDO_OP | TOKEN_S_D_PSEUDO_OP
            | TOKEN_S_S_PSEUDO_OP => {
                let real: i32 = match op {
                    TOKEN_L_D_PSEUDO_OP => TOKEN_LDC1_OPCODE,
                    TOKEN_L_S_PSEUDO_OP => TOKEN_LWC1_OPCODE,
                    TOKEN_S_D_PSEUDO_OP => TOKEN_SDC1_OPCODE,
                    _ => TOKEN_SWC1_OPCODE,
                };
                let floating_point_addend: i32 = self.parse_floating_point_register();
                let address: AddressExpression = self.parse_address();
                self.emit_i(
                    real,
                    floating_point_addend,
                    address.register_number,
                    &address.immediate,
                );
            }
            TOKEN_LD_PSEUDO_OP | TOKEN_SD_PSEUDO_OP => {
                let real: i32 = if op == TOKEN_LD_PSEUDO_OP {
                    TOKEN_LW_OPCODE
                } else {
                    TOKEN_SW_OPCODE
                };
                let line: i32 = self.scanner.line_no(); // the address can consume the newline
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                if target_register == 31 {
                    // The pair's second register would be 32.
                    self.parse_error_on_line(
                        "Register pair starts at the last register (31)",
                        line,
                    );
                    return;
                }
                self.emit_i(
                    real,
                    target_register,
                    address.register_number,
                    &address.immediate,
                );
                self.emit_i(
                    real,
                    target_register.wrapping_add(1),
                    address.register_number,
                    &address.immediate.plus(4),
                );
            }
            TOKEN_ULW_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                self.emit_i(
                    TOKEN_LWL_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate.plus(3),
                );
                self.emit_i(
                    TOKEN_LWR_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate,
                );
            }
            TOKEN_ULH_PSEUDO_OP | TOKEN_ULHU_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                let load_high: i32 = if op == TOKEN_ULH_PSEUDO_OP {
                    TOKEN_LB_OPCODE
                } else {
                    TOKEN_LBU_OPCODE
                };
                self.emit_i(
                    load_high,
                    target_register,
                    address.register_number,
                    &address.immediate.plus(1),
                );
                self.emit_i(
                    TOKEN_LBU_OPCODE,
                    1,
                    address.register_number,
                    &address.immediate,
                );
                self.emit_r_shift(TOKEN_SLL_OPCODE, target_register, target_register, 8);
                self.emit_r(TOKEN_OR_OPCODE, target_register, target_register, 1);
            }
            TOKEN_USW_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                self.emit_i(
                    TOKEN_SWL_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate.plus(3),
                );
                self.emit_i(
                    TOKEN_SWR_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate,
                );
            }
            TOKEN_USH_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                self.emit_i(
                    TOKEN_SB_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate,
                );
                self.emit_r_shift(TOKEN_SLL_OPCODE, 1, target_register, 24);
                self.emit_r_shift(TOKEN_SRL_OPCODE, target_register, target_register, 8);
                self.emit_r(TOKEN_OR_OPCODE, target_register, target_register, 1);
                self.emit_i(
                    TOKEN_SB_OPCODE,
                    target_register,
                    address.register_number,
                    &address.immediate.plus(1),
                );
                self.emit_r_shift(TOKEN_SRL_OPCODE, 1, target_register, 24);
                self.emit_r_shift(TOKEN_SLL_OPCODE, target_register, target_register, 8);
                self.emit_r(TOKEN_OR_OPCODE, target_register, target_register, 1);
            }
            TOKEN_LA_PSEUDO_OP => {
                let target_register: i32 = self.parse_register();
                let address: AddressExpression = self.parse_address();
                if address.register_number != 0 {
                    self.emit_i(
                        TOKEN_ADDI_OPCODE,
                        target_register,
                        address.register_number,
                        &address.immediate,
                    );
                } else {
                    self.emit_i(TOKEN_ORI_OPCODE, target_register, 0, &address.immediate);
                }
            }
            TOKEN_BEQZ_PSEUDO_OP | TOKEN_BNEZ_PSEUDO_OP => {
                let source_register: i32 = self.parse_register();
                let target: ImmediateExpression = self.parse_label();
                let real: i32 = if op == TOKEN_BEQZ_PSEUDO_OP {
                    TOKEN_BEQ_OPCODE
                } else {
                    TOKEN_BNE_OPCODE
                };
                self.emit_i(real, 0, source_register, &target);
            }
            TOKEN_B_PSEUDO_OP | TOKEN_BAL_PSEUDO_OP => {
                let target: ImmediateExpression = self.parse_label();
                let real: i32 = if op == TOKEN_BAL_PSEUDO_OP {
                    TOKEN_BGEZAL_OPCODE
                } else {
                    TOKEN_BGEZ_OPCODE
                };
                self.emit_i(real, 0, 0, &target);
            }
            TOKEN_BGT_PSEUDO_OP | TOKEN_BGTU_PSEUDO_OP | TOKEN_BLE_PSEUDO_OP
            | TOKEN_BLEU_PSEUDO_OP => self.parse_branch_greater_than_less_or_equal(op),
            TOKEN_BGE_PSEUDO_OP | TOKEN_BGEU_PSEUDO_OP | TOKEN_BLT_PSEUDO_OP
            | TOKEN_BLTU_PSEUDO_OP => self.parse_branch_greater_or_equal_less_than(op),
            _ => {
                self.parse_error_at("Pseudo-op not yet supported");
                self.sync_to_newline();
            }
        }
    }

    /// `ror`/`rol` by a register or a constant.
    fn parse_rotate(&mut self, op: i32) {
        let destination_register: i32 = self.parse_register();
        let source_register: i32 = self.parse_register();
        let right: bool = op == TOKEN_ROR_PSEUDO_OP;
        if self.peek() == TOKEN_REGISTER {
            let target_register: i32 = self.parse_register();
            self.emit_r(TOKEN_SUBU_OPCODE, 1, 0, target_register);
            if right {
                self.emit_r(TOKEN_SLLV_OPCODE, 1, 1, source_register);
                self.emit_r(
                    TOKEN_SRLV_OPCODE,
                    destination_register,
                    target_register,
                    source_register,
                );
            } else {
                self.emit_r(TOKEN_SRLV_OPCODE, 1, 1, source_register);
                self.emit_r(
                    TOKEN_SLLV_OPCODE,
                    destination_register,
                    target_register,
                    source_register,
                );
            }
            self.emit_r(
                TOKEN_OR_OPCODE,
                destination_register,
                destination_register,
                1,
            );
        } else {
            let line: i32 = self.scanner.line_no(); // as in parse_immediate_16
            let immediate: ImmediateExpression = self.parse_immediate_32();
            let distance: i32 = self.evaluate_immediate_expression(&immediate);
            self.check_immediate_range(&immediate, 0, 31, line);
            if right {
                self.emit_r_shift(
                    TOKEN_SLL_OPCODE,
                    1,
                    source_register,
                    distance.wrapping_neg(),
                );
                self.emit_r_shift(
                    TOKEN_SRL_OPCODE,
                    destination_register,
                    source_register,
                    distance,
                );
            } else {
                self.emit_r_shift(
                    TOKEN_SRL_OPCODE,
                    1,
                    source_register,
                    distance.wrapping_neg(),
                );
                self.emit_r_shift(
                    TOKEN_SLL_OPCODE,
                    destination_register,
                    source_register,
                    distance,
                );
            }
            self.emit_r(
                TOKEN_OR_OPCODE,
                destination_register,
                destination_register,
                1,
            );
        }
    }

    /// `bgt`, `bgtu`, `ble`, `bleu`: compare with `slt` into `$at`, branch.
    fn parse_branch_greater_than_less_or_equal(&mut self, op: i32) {
        let greater: bool = op == TOKEN_BGT_PSEUDO_OP || op == TOKEN_BGTU_PSEUDO_OP;
        let signed: bool = op == TOKEN_BGT_PSEUDO_OP || op == TOKEN_BLE_PSEUDO_OP;
        // bgt branches when the comparison is true (bne), ble when false.
        let branch: i32 = if greater {
            TOKEN_BNE_OPCODE
        } else {
            TOKEN_BEQ_OPCODE
        };
        let source_register: i32 = self.parse_register();
        if self.peek() == TOKEN_REGISTER {
            let target_register: i32 = self.parse_register();
            let target: ImmediateExpression = self.parse_label();
            let slt: i32 = if signed {
                TOKEN_SLT_OPCODE
            } else {
                TOKEN_SLTU_OPCODE
            };
            self.emit_r(slt, 1, target_register, source_register);
            self.emit_i(branch, 0, 1, &target);
            return;
        }
        let immediate: ImmediateExpression = self.parse_immediate_32();
        let target: ImmediateExpression = self.parse_label();
        if signed {
            self.emit_i(TOKEN_SLTI_OPCODE, 1, source_register, &immediate.plus(1));
            // bgt: rs >= imm+1 means rs > imm; branch when slti is false.
            let after_slti: i32 = if greater {
                TOKEN_BEQ_OPCODE
            } else {
                TOKEN_BNE_OPCODE
            };
            self.emit_i(after_slti, 0, 1, &target);
        } else if greater {
            self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
            self.emit_i(TOKEN_BEQ_OPCODE, source_register, 1, &branch_offset(3));
            self.emit_r(TOKEN_SLTU_OPCODE, 1, source_register, 1);
            self.emit_i(TOKEN_BEQ_OPCODE, 0, 1, &target);
        } else {
            self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
            self.emit_i(TOKEN_BEQ_OPCODE, source_register, 1, &target);
            self.emit_r(TOKEN_SLTU_OPCODE, 1, source_register, 1);
            self.emit_i(TOKEN_BNE_OPCODE, 0, 1, &target);
        }
    }

    /// `bge`, `bgeu`, `blt`, `bltu`: `slt` into `$at`, branch.
    fn parse_branch_greater_or_equal_less_than(&mut self, op: i32) {
        let signed: bool = op == TOKEN_BGE_PSEUDO_OP || op == TOKEN_BLT_PSEUDO_OP;
        // bge branches when rs < rt is false (beq $at, $0), blt when true.
        let branch: i32 = if op == TOKEN_BGE_PSEUDO_OP || op == TOKEN_BGEU_PSEUDO_OP {
            TOKEN_BEQ_OPCODE
        } else {
            TOKEN_BNE_OPCODE
        };
        let source_register: i32 = self.parse_register();
        if self.peek() == TOKEN_REGISTER {
            let target_register: i32 = self.parse_register();
            let target: ImmediateExpression = self.parse_label();
            let slt: i32 = if signed {
                TOKEN_SLT_OPCODE
            } else {
                TOKEN_SLTU_OPCODE
            };
            self.emit_r(slt, 1, source_register, target_register);
            self.emit_i(branch, 0, 1, &target);
        } else {
            let immediate: ImmediateExpression = self.parse_immediate_32();
            let target: ImmediateExpression = self.parse_label();
            let slti: i32 = if signed {
                TOKEN_SLTI_OPCODE
            } else {
                TOKEN_SLTIU_OPCODE
            };
            self.emit_i(slti, 1, source_register, &immediate);
            self.emit_i(branch, 0, 1, &target);
        }
    }

    /// Parse one instruction, by the shape of its opcode (`parse_assembly_code`).
    fn parse_assembly_code(&mut self) {
        let op: i32 = self.advance();
        match token_type(op) {
            Some(OpType::NoArgumentTypeInstruction) => self.parse_no_argument(op),
            Some(OpType::DestinationRegisterSourceRegisterTargetRegisterInstruction) => {
                self.parse_r3(op)
            }
            Some(OpType::DestinationRegisterTargetRegisterShiftAmountInstruction) => {
                self.parse_two_registers_shift_amount(op)
            }
            Some(OpType::SourceRegisterInstruction) => {
                let source_register: i32 = self.parse_register();
                self.emit_r(op, 0, source_register, 0);
            }
            Some(OpType::TargetRegisterSourceRegisterImmediateInstruction) => self.parse_i2(op),
            Some(OpType::TargetRegisterImmediateInstruction) => {
                let target_register: i32 = self.parse_register();
                let immediate: ImmediateExpression = self.parse_unsigned_immediate_16();
                self.emit_i(op, target_register, 0, &immediate);
            }
            Some(OpType::TargetRegisterAddressInstruction) => self.parse_i2a(op),
            Some(OpType::BranchSourceRegisterTargetRegisterLabelInstruction) => self.parse_b2(op),
            Some(OpType::BranchSourceRegisterLabelInstruction) => {
                let source_register: i32 = self.parse_register();
                let target: ImmediateExpression = self.parse_label();
                self.emit_i(op, 0, source_register, &target);
            }
            Some(OpType::JTypeInstruction)
            | Some(OpType::DestinationRegisterSourceRegisterInstruction) => self.parse_j(op),
            Some(OpType::PseudoOp) => self.parse_pseudo(op),
            Some(OpType::TargetRegisterCoprocessorRegisterInstruction)
            | Some(OpType::FloatingPointTargetRegisterCoprocessorRegisterInstruction) => {
                let register_number: i32 = self.parse_register();
                let coprocessor_register: i32 = self.parse_coprocessor_register();
                self.emit_floating_point_r(op, 0, coprocessor_register, register_number);
            }
            Some(OpType::SourceRegisterTargetRegisterInstruction) => {
                self.parse_source_register_target_register(op)
            }
            Some(OpType::DestinationRegisterInstruction) => {
                let destination_register: i32 = self.parse_register();
                self.emit_r(op, destination_register, 0, 0);
            }
            Some(OpType::DestinationRegisterTargetRegisterSourceRegisterInstruction) => {
                // sllv rd, rt, rs: the register operands are swapped in
                // the encoding; a constant third operand means sll.
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                if self.peek() == TOKEN_REGISTER {
                    let target_register: i32 = self.parse_register();
                    self.emit_r(op, destination_register, target_register, source_register);
                } else {
                    if self.peek() != TOKEN_INTEGER {
                        self.parse_error_at("Expected register or integer shift");
                        return;
                    }
                    self.advance();
                    let shift_amount: i32 = self.value_integer();
                    self.emit_r_shift(
                        opcode_to_immediate_opcode(op),
                        destination_register,
                        source_register,
                        shift_amount,
                    );
                }
            }
            Some(OpType::SourceRegisterImmediateInstruction) => {
                let source_register: i32 = self.parse_register();
                let immediate: ImmediateExpression = self.parse_immediate_16();
                self.emit_i(op, 0, source_register, &immediate);
            }
            Some(OpType::BranchCoprocessorConditionInstruction) => {
                let nd: i32 = i32::from(opcode_is_nullified_branch(op));
                let tf: i32 = i32::from(opcode_is_true_branch(op));
                let mut condition_code: i32 = 0;
                if self.peek() == TOKEN_INTEGER {
                    self.advance();
                    condition_code = self.value_integer();
                }
                let target: ImmediateExpression = self.parse_label();
                self.emit_i(op, (condition_code << 2) | (nd << 1) | tf, 0, &target);
            }
            Some(OpType::MoveConditionalTypeInstruction) => {
                let destination_register: i32 = self.parse_register();
                let source_register: i32 = self.parse_register();
                let mut condition_code: i32 = 0;
                if self.peek() == TOKEN_INTEGER {
                    self.advance();
                    condition_code = self.value_integer();
                }
                self.emit_r(
                    op,
                    destination_register,
                    source_register,
                    (condition_code & 0x7) << 2,
                );
            }
            Some(OpType::FloatingPointDestinationSourceInstruction) => {
                let floating_point_destination: i32 = self.parse_floating_point_register();
                let fs: i32 = self.parse_floating_point_register();
                self.emit_floating_point_r(op, floating_point_destination, fs, 0);
            }
            Some(OpType::FloatingPointDestinationSourceTargetInstruction) => {
                let floating_point_destination: i32 = self.parse_floating_point_register();
                let fs: i32 = self.parse_floating_point_register();
                let floating_point_target: i32 = self.parse_floating_point_register();
                self.emit_floating_point_r(
                    op,
                    floating_point_destination,
                    fs,
                    floating_point_target,
                );
            }
            Some(OpType::FloatingPointCompareSourceTargetInstruction) => {
                let mut condition_code: i32 = 0;
                if self.peek() == TOKEN_INTEGER {
                    self.advance();
                    condition_code = self.value_integer();
                }
                let fs: i32 = self.parse_floating_point_register();
                let floating_point_target: i32 = self.parse_floating_point_register();
                self.emit_floating_point_compare(op, fs, floating_point_target, condition_code);
            }
            Some(OpType::FloatingPointMoveConditionalTypeInstruction) => {
                let floating_point_destination: i32 = self.parse_floating_point_register();
                let fs: i32 = self.parse_floating_point_register();
                if self.peek() == TOKEN_REGISTER {
                    let target_register: i32 = self.parse_register();
                    self.emit_floating_point_r(op, floating_point_destination, fs, target_register);
                } else if self.peek() == TOKEN_INTEGER {
                    self.advance();
                    let condition_code: i32 = self.value_integer();
                    self.emit_floating_point_r(
                        op,
                        floating_point_destination,
                        fs,
                        (condition_code & 0x7) << 2,
                    );
                } else {
                    self.emit_floating_point_r(op, floating_point_destination, fs, 0);
                }
            }
            Some(OpType::FloatingPointTargetAddressInstruction) => {
                let floating_point_addend: i32 = self.parse_floating_point_register();
                let address: AddressExpression = self.parse_address();
                self.emit_i(
                    op,
                    floating_point_addend,
                    address.register_number,
                    &address.immediate,
                );
            }
            Some(OpType::AssemblerDirective)
            | Some(OpType::FloatingPointDestinationAddendSourceTargetInstruction)
            | None => {
                self.parse_error_at("Instruction not yet supported");
                self.sync_to_newline();
            }
        }
    }

    /// `mult rs, rt`, the traps `teq rs, rt`, and `div`/`divu` with two
    /// operands (the instruction) or three (the pseudo-instruction).
    fn parse_source_register_target_register(&mut self, op: i32) {
        let r1: i32 = self.parse_register();
        let next: i32 = self.peek();
        if next != TOKEN_REGISTER && next != TOKEN_INTEGER && next != TOKEN_IDENTIFIER {
            self.parse_error_at("Expected operand for div/mult");
            return;
        }
        if matches!(
            op,
            TOKEN_TEQ_OPCODE
                | TOKEN_TGE_OPCODE
                | TOKEN_TGEU_OPCODE
                | TOKEN_TLT_OPCODE
                | TOKEN_TLTU_OPCODE
                | TOKEN_TNE_OPCODE
        ) {
            let r2: i32 = self.parse_register();
            self.emit_r(op, 0, r1, r2);
        } else if op == TOKEN_DIV_OPCODE || op == TOKEN_DIVU_OPCODE {
            let r2: i32 = self.parse_register();
            if self.peek() == TOKEN_NEWLINE || self.peek() == TOKEN_EOF {
                self.emit_r(op, 0, r1, r2);
            } else if self.peek() == TOKEN_REGISTER {
                let r3: i32 = self.parse_register();
                self.divide_instruction(op, r1, r2, r3, false);
            } else {
                let immediate: ImmediateExpression = self.parse_immediate_32();
                if immediate.is_zero() {
                    self.parse_error_at("Divide by zero");
                } else {
                    self.emit_i(TOKEN_ORI_OPCODE, 1, 0, &immediate);
                    self.divide_instruction(op, r1, r2, 1, true);
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
    fn parse_directive_segment(&mut self, directive: i32) {
        let mut start: Option<u32> = None;
        if self.peek() == TOKEN_INTEGER {
            self.advance();
            start = Some(self.value_integer() as u32);
        }
        let kind: AstKind = match directive {
            TOKEN_TEXT_DIRECTIVE => AstKind::DirectiveText(start),
            TOKEN_K_TEXT_DIRECTIVE => AstKind::DirectiveKtext(start),
            TOKEN_K_DATA_DIRECTIVE => AstKind::DirectiveKdata(start),
            _ => AstKind::DirectiveData(start),
        };
        if matches!(kind, AstKind::DirectiveText(_) | AstKind::DirectiveKtext(_)) {
            self.asm.data_dir = false;
            self.asm.text_directive = true;
        } else {
            self.asm.data_dir = true;
            self.asm.text_directive = false;
            self.asm.auto_align = true;
        }
        self.emit_node(kind);
    }

    /// The `NAME` of `.globl`, `.extern`, `.comm`.
    fn parse_directive_name(&mut self, what: &str) -> Option<String> {
        self.scanner.force_identifier();
        if self.peek() != TOKEN_IDENTIFIER {
            self.parse_error_at(what);
            return None;
        }
        self.advance();
        Some(self.value_name())
    }

    /// The values of `.byte`, `.half`, `.word` (`parse_expression_list`).
    fn parse_expression_list(&mut self, kind: DataKind) {
        self.asm.data_integer_buffer.clear();
        self.asm.data_directive_line = self.scanner.line_no();
        loop {
            let errors_before: i32 = self.asm.parse_errors_seen;
            let value: i32 = self.parse_expression();
            if self.asm.parse_errors_seen != errors_before {
                // The bad token was not taken; skip the rest of the line
                // rather than ask for an expression again forever.
                self.sync_to_newline();
                break;
            }
            let symbol: Option<LabelId> = self.asm.expression_unresolved_label.take();
            self.asm.data_integer_buffer.push(ImmediateExpression {
                offset: value,
                symbol,
                ..ImmediateExpression::default()
            });
            if self.peek() == TOKEN_NEWLINE || self.peek() == TOKEN_EOF {
                break;
            }
        }
        if self.asm.data_integer_buffer.is_empty() {
            return;
        }
        let values: Vec<ImmediateExpression> = std::mem::take(&mut self.asm.data_integer_buffer);
        let ast_kind: AstKind = match kind {
            DataKind::Byte => AstKind::DataByte(values),
            DataKind::Half => AstKind::DataHalf(values),
            _ => AstKind::DataWord(values),
        };
        let mut node: AstNode = self.node(ast_kind);
        node.source_line = self.asm.data_directive_line;
        self.ast_append(node);
    }

    /// The literals of `.float` and `.double` (`parse_floating_point_expression_list`).
    fn parse_floating_point_expression_list(&mut self, kind: DataKind) {
        self.asm.data_floating_point_buffer.clear();
        self.asm.data_directive_line = self.scanner.line_no();
        loop {
            if self.peek() != TOKEN_FLOATING_POINT {
                self.parse_error_at("Expected floating-point literal");
                return;
            }
            self.advance();
            let value: f64 = self.scanner.scan_value.fp();
            self.asm.data_floating_point_buffer.push(value);
            if self.peek() == TOKEN_NEWLINE || self.peek() == TOKEN_EOF {
                break;
            }
        }
        let values: Vec<f64> = std::mem::take(&mut self.asm.data_floating_point_buffer);
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
        let current: u32 = self.current_data_location();
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
            if self.peek() != TOKEN_STRING {
                self.parse_error_at("Expected string literal");
                return;
            }
            self.advance();
            let mut bytes: Vec<u8> = self.scanner.scan_value.text();
            // The C engine measures the decoded string with strlen.
            if let Some(nul) = bytes.iter().position(|b: &u8| *b == 0) {
                bytes.truncate(nul);
            }
            if self.asm.text_directive {
                self.parse_error_at("Can't put data in text segment");
            } else {
                let null_terminate: bool = self.asm.null_term;
                self.emit_node(AstKind::DataString {
                    bytes,
                    null_terminate,
                });
            }
            if self.peek() == TOKEN_NEWLINE || self.peek() == TOKEN_EOF {
                break;
            }
        }
    }

    /// Parse one directive (`parse_directive`).
    fn parse_directive(&mut self, directive: i32) {
        match directive {
            TOKEN_DATA_DIRECTIVE
            | TOKEN_K_DATA_DIRECTIVE
            | TOKEN_TEXT_DIRECTIVE
            | TOKEN_K_TEXT_DIRECTIVE => self.parse_directive_segment(directive),
            TOKEN_GLOBAL_DIRECTIVE => {
                if let Some(name) = self.parse_directive_name("Expected identifier after .globl") {
                    self.emit_node(AstKind::DirectiveGlobl(name));
                }
            }
            TOKEN_WORD_DIRECTIVE => {
                self.align_for_data(2);
                self.parse_expression_list(DataKind::Word);
            }
            TOKEN_HALF_DIRECTIVE => {
                self.align_for_data(1);
                self.parse_expression_list(DataKind::Half);
            }
            TOKEN_BYTE_DIRECTIVE => self.parse_expression_list(DataKind::Byte),
            TOKEN_ASCII_DIRECTIVE | TOKEN_ASCIIZ_DIRECTIVE => {
                self.asm.null_term = directive == TOKEN_ASCIIZ_DIRECTIVE;
                self.parse_string_list();
            }
            TOKEN_FLOAT_DIRECTIVE => {
                self.align_for_data(2);
                self.parse_floating_point_expression_list(DataKind::Float);
            }
            TOKEN_DOUBLE_DIRECTIVE => {
                self.align_for_data(3);
                self.parse_floating_point_expression_list(DataKind::Double);
            }
            TOKEN_SPACE_DIRECTIVE => {
                let size: i32 = self.parse_expression();
                if self.asm.text_directive {
                    self.parse_error_at("Can't put data in text segment");
                    return;
                }
                self.emit_node(AstKind::DirectiveSpace(size));
            }
            TOKEN_ALIGN_DIRECTIVE => {
                let line: i32 = self.scanner.line_no(); // the expression can consume the newline
                let n: i32 = self.parse_expression();
                if !(0..=31).contains(&n) {
                    // C shifts a 32-bit mask by it.
                    self.parse_error_on_line("Alignment out of range (0 .. 31)", line);
                } else {
                    if n == 0 {
                        self.asm.auto_align = false;
                    }
                    self.emit_node(AstKind::DirectiveAlign(n));
                }
            }
            TOKEN_COMM_DIRECTIVE | TOKEN_EXTERN_DIRECTIVE => {
                if let Some(name) = self.parse_directive_name("Expected ID") {
                    let size: i32 = self.parse_expression();
                    let kind: AstKind = if directive == TOKEN_COMM_DIRECTIVE {
                        AstKind::DirectiveComm { name, size }
                    } else {
                        AstKind::DirectiveExtern { name, size }
                    };
                    self.emit_node(kind);
                }
            }
            TOKEN_ERR_DIRECTIVE => self.parse_error_at(".err directive"),
            TOKEN_FILE_DIRECTIVE
            | TOKEN_LOC_DIRECTIVE
            | TOKEN_FRAME_DIRECTIVE
            | TOKEN_MASK_DIRECTIVE
            | TOKEN_FMASK_DIRECTIVE
            | TOKEN_ENT_DIRECTIVE
            | TOKEN_END_DIRECTIVE
            | TOKEN_LABEL_DIRECTIVE
            | TOKEN_LIVEREG_DIRECTIVE
            | TOKEN_OPTIONS_DIRECTIVE
            | TOKEN_BGNB_DIRECTIVE
            | TOKEN_ENDB_DIRECTIVE
            | TOKEN_ENDR_DIRECTIVE
            | TOKEN_ASM0_DIRECTIVE
            | TOKEN_ALIAS_DIRECTIVE
            | TOKEN_SET_DIRECTIVE => {
                // Metadata for other assemblers: ignore the rest of the line.
                self.sync_to_newline();
            }
            _ => {
                self.parse_error_at("Directive not yet supported");
                self.sync_to_newline();
            }
        }
    }

    /// `NAME:` or `NAME = EXPR` (`parse_optional_label`).
    fn parse_optional_label(&mut self) {
        self.advance();
        let name: String = self.value_name();
        let separator: i32 = self.advance();
        if separator == i32::from(b':') {
            self.emit_node(AstKind::LabelNormal(name));
        } else {
            let value: i32 = self.parse_expression();
            self.emit_node(AstKind::LabelConstant(name, value));
        }
    }

    /// Parse one statement (`parse_line`).
    fn parse_line(&mut self) {
        if self.peek() == TOKEN_NEWLINE {
            self.advance();
            return;
        }
        if self.peek() == TOKEN_EOF {
            return;
        }
        if self.peek() == TOKEN_IDENTIFIER
            && (self.peek2() == i32::from(b':') || self.peek2() == i32::from(b'='))
        {
            self.parse_optional_label();
        }
        let t: i32 = self.peek();
        if t == TOKEN_NEWLINE {
            self.advance();
            return;
        }
        if t == TOKEN_EOF {
            return;
        }
        if token_type(t) == Some(OpType::AssemblerDirective) {
            let directive: i32 = self.advance();
            self.parse_directive(directive);
        } else {
            self.parse_assembly_code();
        }
        self.clear_labels();
        if self.asm.parse_error_occurred {
            self.sync_to_newline();
        } else if self.peek() == TOKEN_NEWLINE {
            self.advance();
        } else if self.peek() != TOKEN_EOF {
            self.parse_error_at("Extra tokens after instruction");
            self.sync_to_newline();
        }
    }

    /// Start parsing `input`, named `file_name` in messages (`parser_initialize`).
    pub fn parser_initialize(&mut self, input: Vec<u8>, file_name: &[u8]) {
        self.scanner.initialize(input);
        self.asm.input_file_name = Some(file_name.to_vec());
        self.asm.parse_errors_seen = 0;
        self.asm.parse_error_occurred = false;
        self.asm.data_dir = false;
        self.asm.text_directive = true;
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
        // and the parse moves `data_dir`/`text_directive` as it meets `.data` and
        // `.text`, which the emit pass must replay from the start.
        let already_emitted: usize = match self.asm.file.as_ref().map(|f: &AstNode| &f.kind) {
            Some(AstKind::File { children, .. }) => children.len(),
            _ => 0,
        };
        let (start_data_dir, start_text_dir): (bool, bool) =
            (self.asm.data_dir, self.asm.text_directive);
        while self.peek() != TOKEN_EOF {
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
                self.asm.text_directive = start_text_dir;
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

    /// Move this line's labels to `new_address`, after an alignment
    /// (`fix_current_label_address`).
    pub fn fix_current_label_address(&mut self, new_address: u32) {
        for index in 0..self.asm.this_line_labels.len() {
            let id: LabelId = self.asm.this_line_labels[index];
            self.symbols.label_mut(id).address = i64::from(new_address);
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
                AstKind::LabelNormal(_) | AstKind::LabelConstant(..) | AstKind::DirectiveGlobl(_)
            ) {
                self.clear_labels();
            }
        }
    }

    /// Store each value of a `.byte`/`.half`/`.word`; a value on an
    /// undefined label is patched when the label is defined
    /// (`emit_data_integer_node`).
    fn emit_data_integer_node(&mut self, values: &[ImmediateExpression], width: i32) {
        for expr in values {
            let value: i32 = match expr.symbol {
                Some(id) if !self.symbols.label(id).is_defined() => {
                    let program_counter: u32 = self.current_data_location();
                    self.record_data_uses_symbol(program_counter, id, expr.offset, width);
                    expr.offset
                }
                _ => self.evaluate_immediate_expression(expr),
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
        self.asm.emit_source_text = node.source_text.as_deref().map(Rc::from);
        match &node.kind {
            AstKind::File { .. } => {}
            AstKind::DirectiveText(start) | AstKind::DirectiveKtext(start) => {
                self.user_kernel_text_segment(matches!(node.kind, AstKind::DirectiveKtext(_)));
                self.asm.data_dir = false;
                self.asm.text_directive = true;
                if let Some(address) = start {
                    self.set_text_location(*address);
                }
            }
            AstKind::DirectiveData(start) | AstKind::DirectiveKdata(start) => {
                self.user_kernel_data_segment(matches!(node.kind, AstKind::DirectiveKdata(_)));
                self.asm.data_dir = true;
                self.asm.text_directive = false;
                self.enable_data_alignment();
                self.asm.auto_align = true;
                if let Some(address) = start {
                    self.set_data_location(*address);
                }
            }
            AstKind::DirectiveAlign(n) => {
                if *n == 0 {
                    self.asm.auto_align = false;
                }
                if self.asm.text_directive {
                    self.align_text(*n);
                } else {
                    self.align_data(*n);
                }
            }
            AstKind::DirectiveSpace(size) => self.increment_data_location(*size),
            AstKind::DirectiveGlobl(name) => {
                self.make_label_global(name);
            }
            AstKind::DirectiveExtern { name, size } => {
                self.make_label_global(name);
                let id: LabelId = self.symbols.lookup(name);
                if self.symbols.label(id).address == 0 {
                    let program_counter: u32 = self.current_data_location();
                    self.record_label(name, program_counter, true);
                }
                self.increment_data_location(*size);
            }
            AstKind::DirectiveComm { name, size } => {
                self.align_data(2);
                let id: LabelId = self.symbols.lookup(name);
                if self.symbols.label(id).address == 0 {
                    let program_counter: u32 = self.current_data_location();
                    self.record_label(name, program_counter, true);
                }
                self.increment_data_location(*size);
            }
            AstKind::DataByte(values) => self.emit_data_integer_node(values, 1),
            AstKind::DataHalf(values) => {
                self.align_for_data(1);
                self.emit_data_integer_node(values, 2);
            }
            AstKind::DataWord(values) => {
                self.align_for_data(2);
                self.emit_data_integer_node(values, 4);
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
                if !self.asm.text_directive {
                    self.store_string(bytes, *null_terminate);
                }
            }
            AstKind::LabelNormal(name) => {
                let address: u32 = if self.asm.text_directive {
                    self.current_text_location()
                } else {
                    self.current_data_location()
                };
                let id: LabelId = self.record_label(name, address, false);
                self.asm.this_line_labels.insert(0, id);
            }
            AstKind::LabelConstant(name, value) => {
                let id: LabelId = self.record_label(name, *value as u32, true);
                self.symbols.label_mut(id).constant = true;
            }
            AstKind::InstructionR {
                op,
                destination_register,
                source_register,
                target_register,
            } => self.r_type(
                *op,
                *destination_register,
                *source_register,
                *target_register,
            ),
            AstKind::InstructionRShift {
                op,
                destination_register,
                target_register,
                shift_amount,
            } => self.r_shift_amount_type(
                *op,
                *destination_register,
                *target_register,
                *shift_amount,
            ),
            AstKind::InstructionI {
                op,
                target_register,
                source_register,
                immediate,
            } => self.i_type(*op, *target_register, *source_register, immediate),
            AstKind::InstructionJ { op, target } => self.j_type(*op, target),
            AstKind::InstructionFloatingPointR {
                op,
                floating_point_destination,
                fs,
                floating_point_target,
            } => self.r_coprocessor_type(
                *op,
                *floating_point_destination,
                *fs,
                *floating_point_target,
            ),
            AstKind::InstructionFloatingPointCompare {
                op,
                fs,
                floating_point_target,
                condition_code,
            } => self.r_condition_type(*op, *fs, *floating_point_target, *condition_code),
            AstKind::Pseudo { children, .. } => self.emit_chain(children),
        }
    }
}
