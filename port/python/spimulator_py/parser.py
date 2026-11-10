"""The recursive-descent parser and the AST emitter (the C engine's parser.c).

Assembling a file happens in two passes over the same tokens' results:

1. **Parse.**  Each line (an optional label, then a directive or an
   instruction) becomes AST nodes (ast_nodes.py).  A pseudo-instruction is
   expanded here, into a PSEUDO node holding the real instructions.
2. **Emit.**  :func:`emit_ast` walks the tree in order and stores every
   instruction and datum in memory, defining labels as it reaches them.

A few things happen during the parse, not the emit, because the C engine
does them then and a program can tell: labels are created (in the symbol
table) as they are mentioned, constants (``X = 5``) are substituted while
scanning, and an expression keeps only the first undefined symbol it sees.
"""

from collections.abc import Callable
from typing import BinaryIO

from . import (
    ast_nodes,
    console,
    data,
    instruction,
    machine,
    opcodes,
    pseudo_op,
    scanner,
    symbol_table,
)
from .ast_nodes import Kind, Node
from .inst import AddrExpr, ImmExpr
from .opcodes import (
    KIND,
    TOK_EOF,
    TOK_FP,
    TOK_FP_REG,
    TOK_ID,
    TOK_INT,
    TOK_NL,
    TOK_REG,
    TOK_STR,
)

TOK: dict[str, int] = opcodes.TOKEN

data_dir: bool = False
"""The current segment is a data segment."""
text_dir: bool = True
"""The current segment is a text segment."""

input_file_name: str | None = None
"""The name of the file being assembled, for messages."""

_null_term: bool = False
_auto_align: bool = True  # mirrors data.py's flag, as the C parser does

_current_file: Node | None = None
_current_pseudo: Node | None = None
_expr_unresolved_label: symbol_table.Label | None = None

# The command-line views of the tree (-print-ast, -show-expansion, ...).
print_ast: bool = False
show_expansion: bool = False
print_ast_json: bool = False
print_ast_only: bool = False
"""Skip the emit pass: the tree is only printed."""

# Labels defined on the current line; alignment moves them with the PC.
_this_line_labels: list[symbol_table.Label] = []

# The value list of the .word/.half/.byte (or .float/.double) being parsed.
_data_kind: Kind = Kind.DATA_WORD
_data_exprs: list[ImmExpr] = []
_data_doubles: list[float] = []
_data_directive_line: int = 0


def _append(node: Node) -> None:
    """Add node to the current pseudo-op's expansion, or else to the file."""
    if _current_pseudo is not None:
        _current_pseudo.children.append(node)
    elif _current_file is not None:
        _current_file.children.append(node)


# --- the emit_* helpers the pseudo-op expansions call ---------------------------


def emit_r(op: int, rd: int, rs: int, rt: int) -> None:
    """Append an R-type instruction node."""
    _append(ast_nodes.make(Kind.INST_R, op=op, rd=rd, rs=rs, rt=rt))


def emit_r_shift(op: int, rd: int, rt: int, shamt: int) -> None:
    """Append a shift node."""
    _append(ast_nodes.make(Kind.INST_R_SHIFT, op=op, rd=rd, rt=rt, shamt=shamt))


def emit_i(op: int, rt: int, rs: int, imm: ImmExpr) -> None:
    """Append an I-type node (it keeps its own copy of imm)."""
    _append(ast_nodes.make(Kind.INST_I, op=op, rt=rt, rs=rs, imm=imm.copy()))


def emit_j(op: int, target: ImmExpr) -> None:
    """Append a J-type node."""
    _append(ast_nodes.make(Kind.INST_J, op=op, imm=target.copy()))


def emit_fp_r(op: int, fd: int, fs: int, ft: int) -> None:
    """Append a coprocessor R-type node."""
    _append(ast_nodes.make(Kind.INST_FP_R, op=op, fd=fd, fs=fs, ft=ft))


def emit_fp_compare(op: int, fs: int, ft: int, cc: int) -> None:
    """Append an FP compare node."""
    _append(ast_nodes.make(Kind.INST_FP_COMPARE, op=op, fs=fs, ft=ft, cc=cc))


# --- data lists -------------------------------------------------------------------


def _data_int_begin(kind: Kind) -> None:
    """Start a .byte/.half/.word value list."""
    global _data_kind, _data_directive_line
    _data_kind = kind
    _data_exprs.clear()
    _data_directive_line = scanner.line_no


def _data_int_push(value: int) -> None:
    """Add one value, with the first undefined symbol it used, if any."""
    global _expr_unresolved_label
    name: str | None = (
        _expr_unresolved_label.name if _expr_unresolved_label is not None else None
    )
    _data_exprs.append(instruction.make_imm_expr(value, name, False))
    _expr_unresolved_label = None


def _data_int_finalize() -> None:
    """Turn the collected values into one node."""
    if not _data_exprs:
        return
    node: Node = ast_nodes.make(_data_kind, exprs=list(_data_exprs))
    node.source_line = _data_directive_line
    _append(node)
    _data_exprs.clear()


def _data_fp_begin(kind: Kind) -> None:
    """Start a .float/.double value list."""
    global _data_kind, _data_directive_line
    _data_kind = kind
    _data_doubles.clear()
    _data_directive_line = scanner.line_no


def _data_fp_finalize() -> None:
    """Turn the collected floating-point values into one node."""
    if not _data_doubles:
        return
    kind: Kind = Kind.DATA_FLOAT if _data_kind == Kind.DATA_FLOAT else Kind.DATA_DOUBLE
    node: Node = ast_nodes.make(kind, doubles=list(_data_doubles))
    node.source_line = _data_directive_line
    _append(node)
    _data_doubles.clear()


def _emit_dir_seg(kind: Kind, has_addr: bool, addr: int) -> None:
    """Record a segment directive; the parser's own segment flags change now."""
    global data_dir, text_dir, _auto_align
    if kind in (Kind.DIR_TEXT, Kind.DIR_KTEXT):
        data_dir = False
        text_dir = True
    else:
        data_dir = True
        text_dir = False
        _auto_align = True
    _append(ast_nodes.make(kind, has_start_addr=has_addr, start_addr=addr))


# --- labels on the current line ---------------------------------------------------


def _clear_labels() -> None:
    """Resolve the uses of the labels of the line just finished."""
    while _this_line_labels:
        symbol_table.resolve_label_uses(_this_line_labels.pop(0))


def fix_current_label_address(new_addr: int) -> None:
    """Move the current line's labels to new_addr (alignment moved the PC)."""
    for label in _this_line_labels:
        label.addr = new_addr


# --- errors -------------------------------------------------------------------------


def _parse_error_at(message: str) -> None:
    """Report a syntax error on the current line."""
    pseudo_op.parse_error_occurred = True
    pseudo_op.parse_errors_seen += 1
    name: str = input_file_name if input_file_name is not None else "(stdin)"
    console.error(
        f"spim: (parser) {message} on line {scanner.line_no} of file {name}\n"
    )


def _sync_to_nl() -> None:
    """Skip tokens up to, not past, the end of the line."""
    while scanner.peek() not in (TOK_NL, TOK_EOF):
        scanner.advance()


def _expect(expected: int, what: str) -> bool:
    """Consume the token expected, or report what was expected."""
    if scanner.peek() == expected:
        scanner.advance()
        return True
    _parse_error_at(what)
    return False


def _int_value() -> int:
    """The scanner's last integer value."""
    value: int | float | str = scanner.scan_value
    assert isinstance(value, int)
    return value


def _str_value() -> str:
    """The scanner's last identifier or string value."""
    value: int | float | str = scanner.scan_value
    assert isinstance(value, str)
    return value


# --- operands -------------------------------------------------------------------


def _parse_register() -> int:
    """A ``$`` general register."""
    if scanner.peek() == TOK_REG:
        scanner.advance()
        return _int_value()
    _parse_error_at("Expected register")
    return 0


def _parse_fp_register() -> int:
    """A ``$f`` register."""
    if scanner.peek() == TOK_FP_REG:
        scanner.advance()
        return _int_value()
    _parse_error_at("Expected FP register")
    return 0


def _parse_abs_addr() -> int:
    """``INT``, ``INT + INT``, or ``INT INT`` (a negative literal subtracts)."""
    if scanner.peek() != TOK_INT:
        _parse_error_at("Expected integer")
        return 0
    scanner.advance()
    value: int = _int_value()
    if scanner.peek() == ord("+"):
        scanner.advance()
        if scanner.peek() != TOK_INT:
            _parse_error_at("Expected integer after '+'")
            return value
        scanner.advance()
        return machine.s32(value + _int_value())
    if scanner.peek() == TOK_INT:
        scanner.advance()
        if _int_value() >= 0:
            _parse_error_at("Syntax error")
        return machine.s32(value + _int_value())
    return value


def _parse_factor() -> int:
    """``INT``, ``( EXPR )``, or a name (its address, or 0 if undefined yet)."""
    global _expr_unresolved_label
    if scanner.peek() == TOK_INT:
        scanner.advance()
        return _int_value()
    if scanner.peek() == ord("("):
        scanner.advance()
        value: int = _parse_expr()
        _expect(ord(")"), "Expected ')'")
        return value
    if scanner.peek() == TOK_ID:
        scanner.advance()
        label: symbol_table.Label = symbol_table.lookup_label(_str_value())
        if label.addr == 0:
            # A forward reference: emit_ast records the use at the right PC.
            if _expr_unresolved_label is None:
                _expr_unresolved_label = label
            return 0
        return machine.s32(label.addr)
    _parse_error_at("Expected expression")
    return 0


def _parse_term() -> int:
    """``FACTOR (('*' | '/') FACTOR)*``; division by zero gives 0."""
    value: int = _parse_factor()
    while scanner.peek() in (ord("*"), ord("/")):
        op: int = scanner.advance()
        right: int = _parse_factor()
        if op == ord("*"):
            value = machine.s32(value * right)
        else:
            value = 0 if right == 0 else machine.s32(machine.c_div(value, right))
    return value


def _parse_expr() -> int:
    """``TERM (('+' | '-') TERM)*``."""
    value: int = _parse_term()
    while scanner.peek() in (ord("+"), ord("-")):
        op: int = scanner.advance()
        right: int = _parse_term()
        value = machine.s32(value + right if op == ord("+") else value - right)
    return value


def _parse_expression() -> int:
    """An expression whose first name is never taken as a keyword."""
    global _expr_unresolved_label
    scanner.force_identifier()
    _expr_unresolved_label = None
    return _parse_expr()


def _parse_imm32() -> ImmExpr:
    """A 32-bit immediate: ``ABS``, ``(ABS) >> INT``, ``ID``, ``ID +/- ABS``."""
    scanner.force_identifier()
    if scanner.peek() == TOK_ID:
        scanner.advance()
        name: str = _str_value()
        if scanner.peek() == ord("+"):
            scanner.advance()
            return instruction.make_imm_expr(_parse_abs_addr(), name, False)
        if scanner.peek() == ord("-"):
            scanner.advance()
            return instruction.make_imm_expr(-_parse_abs_addr(), name, False)
        return instruction.make_imm_expr(0, name, False)
    if scanner.peek() == ord("("):
        scanner.advance()
        value: int = _parse_abs_addr()
        _expect(ord(")"), "Expected ')'")
        _expect(ord(">"), "Expected '>'")
        _expect(ord(">"), "Expected '>'")
        if scanner.peek() != TOK_INT:
            _parse_error_at("Expected integer after '>>'")
            return instruction.make_imm_expr(0, None, False)
        scanner.advance()
        shift: int = _int_value()
        return instruction.make_imm_expr(value >> (shift & 31), None, False)
    return instruction.make_imm_expr(_parse_abs_addr(), None, False)


def _parse_imm16() -> ImmExpr:
    """A signed 16-bit immediate (a warning if it does not fit)."""
    expr: ImmExpr = _parse_imm32()
    pseudo_op.check_imm_range(expr, instruction.IMM_MIN, instruction.IMM_MAX)
    return expr


def _parse_uimm16() -> ImmExpr:
    """An unsigned 16-bit immediate."""
    expr: ImmExpr = _parse_imm32()
    pseudo_op.check_uimm_range(expr, instruction.UIMM_MIN, instruction.UIMM_MAX)
    return expr


def _parse_label() -> ImmExpr:
    """A branch or jump target: a PC-relative reference to a name."""
    if scanner.peek() != TOK_ID:
        _parse_error_at("Expected label")
        return instruction.make_imm_expr(0, None, True)
    scanner.advance()
    return instruction.make_imm_expr(-instruction.current_text_pc(), _str_value(), True)


def _parse_address() -> AddrExpr:
    """A load/store address: ``(REG)``, ``ABS [(REG)]``, ``ID [+-ABS] [(REG)]``."""
    scanner.force_identifier()
    if scanner.peek() == ord("("):
        scanner.advance()
        reg: int = _parse_register()
        _expect(ord(")"), "Expected ')' after register")
        return instruction.make_addr_expr(0, None, reg)
    if scanner.peek() == TOK_INT:
        offset: int = _parse_abs_addr()
        if scanner.peek() == ord("+") and scanner.peek2() == TOK_ID:
            scanner.advance()
            scanner.force_identifier()
            scanner.advance()
            return instruction.make_addr_expr(offset, _str_value(), 0)
        if scanner.peek() == ord("("):
            scanner.advance()
            reg = _parse_register()
            _expect(ord(")"), "Expected ')'")
            return instruction.make_addr_expr(offset, None, reg)
        return instruction.make_addr_expr(offset, None, 0)
    if scanner.peek() == TOK_ID:
        scanner.advance()
        name: str = _str_value()
        offset = 0
        if scanner.peek() == ord("+"):
            scanner.advance()
            offset = _parse_abs_addr()
        elif scanner.peek() == ord("-"):
            scanner.advance()
            offset = -_parse_abs_addr()
        reg = 0
        if scanner.peek() == ord("("):
            scanner.advance()
            reg = _parse_register()
            _expect(ord(")"), "Expected ')'")
        return instruction.make_addr_expr(offset, name, reg)
    _parse_error_at("Expected address")
    return instruction.make_addr_expr(0, None, 0)


# --- instructions -----------------------------------------------------------------

_HAS_IMMEDIATE_FORM: frozenset[int] = frozenset(
    TOK[name]
    for name in (
        "ADD_OPCODE", "ADDU_OPCODE", "AND_OPCODE", "OR_OPCODE", "XOR_OPCODE",
        "SLT_OPCODE", "SLTU_OPCODE",
    )
)  # fmt: skip


def _parse_r3(op: int) -> None:
    """``op rd, rs, rt``, and the immediate forms some of them accept."""
    rd: int = _parse_register()
    if op in (TOK["CLO_OPCODE"], TOK["CLZ_OPCODE"]):
        emit_r(op, rd, _parse_register(), rd)  # rt must equal rd
        return
    if op == TOK["MUL_OPCODE"]:
        rs: int = _parse_register()
        if scanner.peek() == TOK_REG:
            emit_r(op, rd, rs, _parse_register())
        else:
            emit_i(TOK["ORI_OPCODE"], 1, 0, _parse_imm32())
            emit_r(op, rd, rs, 1)
        return
    if op in (TOK["SUB_OPCODE"], TOK["SUBU_OPCODE"]):
        add: int = (
            TOK["ADDI_OPCODE"] if op == TOK["SUB_OPCODE"] else TOK["ADDIU_OPCODE"]
        )
        if scanner.peek() != TOK_REG:
            negated: int = -instruction.eval_imm_expr(_parse_imm32())
            emit_i(add, rd, rd, instruction.make_imm_expr(negated, None, False))
            return
        rs = _parse_register()
        if scanner.peek() == TOK_REG:
            emit_r(op, rd, rs, _parse_register())
        else:
            negated = -instruction.eval_imm_expr(_parse_imm32())
            emit_i(add, rd, rs, instruction.make_imm_expr(negated, None, False))
        return
    if op in _HAS_IMMEDIATE_FORM:
        if scanner.peek() != TOK_REG:
            emit_i(pseudo_op.op_to_imm_op(op), rd, rd, _parse_imm32())
            return
        rs = _parse_register()
        if scanner.peek() == TOK_REG:
            emit_r(op, rd, rs, _parse_register())
        else:
            emit_i(pseudo_op.op_to_imm_op(op), rd, rs, _parse_imm32())
        return
    rs = _parse_register()
    emit_r(op, rd, rs, _parse_register())


def _parse_r2sh(op: int) -> None:
    """``op rd, rt, shamt``; ``ssnop`` is ``sll $0, $0, 1``."""
    if op == TOK["SSNOP_OPCODE"]:
        emit_r_shift(TOK["SLL_OPCODE"], 0, 0, 1)
        return
    rd: int = _parse_register()
    rt: int = _parse_register()
    if scanner.peek() != TOK_INT:
        _parse_error_at("Expected integer shift amount")
        return
    scanner.advance()
    emit_r_shift(op, rd, rt, _int_value())


def _parse_i2(op: int) -> None:
    """``op rt, rs, imm16``, or ``op rt, imm16`` with rs = rt."""
    rt: int = _parse_register()
    rs: int = _parse_register() if scanner.peek() == TOK_REG else rt
    unsigned: bool = op in (TOK["ANDI_OPCODE"], TOK["ORI_OPCODE"], TOK["XORI_OPCODE"])
    emit_i(op, rt, rs, _parse_uimm16() if unsigned else _parse_imm16())


def _parse_b2(op: int) -> None:
    """``op rs, rt, label``, or ``op rs, imm, label`` through $at."""
    src1: int = _parse_register()
    if scanner.peek() == TOK_REG:
        src2: int = _parse_register()
        emit_i(op, src2, src1, _parse_label())
        return
    imm: ImmExpr = _parse_imm32()
    target: ImmExpr = _parse_label()
    if instruction.is_zero_imm(imm):
        emit_i(op, src1, 0, target)
    else:
        emit_i(TOK["ORI_OPCODE"], 1, 0, imm)
        emit_i(op, src1, 1, target)


def _parse_j(op: int) -> None:
    """``j``/``jal``/``jr``/``jalr`` with a label or register(s)."""
    is_plain_jump: bool = op in (TOK["J_OPCODE"], TOK["JR_OPCODE"])
    is_link: bool = op in (TOK["JAL_OPCODE"], TOK["JALR_OPCODE"])
    if scanner.peek() == TOK_REG:
        r1: int = _parse_register()
        if scanner.peek() == TOK_REG:
            r2: int = _parse_register()
            if is_plain_jump:
                emit_r(TOK["JR_OPCODE"], 0, r2, 0)
            elif is_link:
                emit_r(TOK["JALR_OPCODE"], r1, r2, 0)
            return
        if is_plain_jump:
            emit_r(TOK["JR_OPCODE"], 0, r1, 0)
        elif is_link:
            emit_r(TOK["JALR_OPCODE"], 31, r1, 0)
        return
    target: ImmExpr = _parse_label()
    if is_plain_jump:
        emit_j(TOK["J_OPCODE"], target)
    elif is_link:
        emit_j(TOK["JAL_OPCODE"], target)


def _parse_noarg(op: int) -> None:
    """``op`` alone, or ``op N`` (``break N``: N goes in the rd byte)."""
    if scanner.peek() == TOK_INT:
        scanner.advance()
        number: int = _int_value()
        if op == TOK["BREAK_OPCODE"] and number == 1:
            _parse_error_at("Breakpoint 1 is reserved for debugger")
        emit_r(op, number, 0, 0)
    else:
        emit_r(op, 0, 0, 0)


def _parse_rs_rt(op: int) -> None:
    """mult/multu, the trap-on-compare ops, and 2- or 3-operand div/divu."""
    r1: int = _parse_register()
    if scanner.peek() not in (TOK_REG, TOK_INT, TOK_ID):
        _parse_error_at("Expected operand for div/mult")
        return
    trap_ops: tuple[int, ...] = tuple(
        TOK[n]
        for n in ("TEQ_OPCODE", "TGE_OPCODE", "TGEU_OPCODE", "TLT_OPCODE",
                  "TLTU_OPCODE", "TNE_OPCODE")
    )  # fmt: skip
    if op in trap_ops:
        emit_r(op, 0, r1, _parse_register())
    elif op in (TOK["DIV_OPCODE"], TOK["DIVU_OPCODE"]):
        r2: int = _parse_register()
        if scanner.peek() in (TOK_NL, TOK_EOF):
            emit_r(op, 0, r1, r2)
        elif scanner.peek() == TOK_REG:
            pseudo_op.div_inst(op, r1, r2, _parse_register(), False)
        else:
            imm: ImmExpr = _parse_imm32()
            if instruction.is_zero_imm(imm):
                _parse_error_at("Divide by zero")
            else:
                emit_i(TOK["ORI_OPCODE"], 1, 0, imm)
                pseudo_op.div_inst(op, r1, r2, 1, True)
    else:
        emit_r(op, 0, r1, _parse_register())


def _parse_copreg_operand() -> int:
    """A coprocessor register, written ``$fN`` or ``$N``."""
    return _parse_fp_register() if scanner.peek() == TOK_FP_REG else _parse_register()


def _parse_asm_code() -> None:
    """One instruction, dispatched on its operand shape."""
    op: int = scanner.advance()
    kind: int | None = instruction.kind_of(op)
    if kind == KIND["NOARG_TYPE_INST"]:
        _parse_noarg(op)
    elif kind == KIND["RD_RS_RT_INST"]:
        _parse_r3(op)
    elif kind == KIND["RD_RT_SHAMT_INST"]:
        _parse_r2sh(op)
    elif kind == KIND["RS_INST"]:
        emit_r(op, 0, _parse_register(), 0)
    elif kind == KIND["RT_RS_IMM_INST"]:
        _parse_i2(op)
    elif kind == KIND["RT_IMM_INST"]:
        rt: int = _parse_register()
        emit_i(op, rt, 0, _parse_uimm16())
    elif kind in (KIND["RT_ADDR_INST"], KIND["FP_FT_ADDR_INST"]):
        reg: int = (
            _parse_register() if kind == KIND["RT_ADDR_INST"] else _parse_fp_register()
        )
        address: AddrExpr = _parse_address()
        emit_i(op, reg, address.reg_no, address.imm)
    elif kind == KIND["BRANCH_RS_RT_LABEL_INST"]:
        _parse_b2(op)
    elif kind == KIND["BRANCH_RS_LABEL_INST"]:
        rs: int = _parse_register()
        emit_i(op, 0, rs, _parse_label())
    elif kind in (KIND["J_TYPE_INST"], KIND["RD_RS_INST"]):
        _parse_j(op)
    elif kind == opcodes.PSEUDO_OP:
        _parse_pseudo(op)
    elif kind in (KIND["RT_COPREG_INST"], KIND["FP_RT_COPREG_INST"]):
        reg = _parse_register()
        emit_fp_r(op, 0, _parse_copreg_operand(), reg)
    elif kind == KIND["RS_RT_INST"]:
        _parse_rs_rt(op)
    elif kind == KIND["RD_INST"]:
        emit_r(op, _parse_register(), 0, 0)
    elif kind == KIND["RD_RT_RS_INST"]:
        _parse_variable_shift(op)
    elif kind == KIND["RS_IMM_INST"]:
        rs = _parse_register()
        emit_i(op, 0, rs, _parse_imm16())
    elif kind == KIND["BRANCH_COPROC_COND_INST"]:
        nd: int = 1 if instruction.opcode_is_nullified_branch(op) else 0
        tf: int = 1 if instruction.opcode_is_true_branch(op) else 0
        cc: int = 0
        if scanner.peek() == TOK_INT:
            scanner.advance()
            cc = _int_value()
        target: ImmExpr = _parse_label()
        emit_i(op, (cc << 2) | (nd << 1) | tf, 0, target)
    elif kind == KIND["MOVC_TYPE_INST"]:
        rd: int = _parse_register()
        rs = _parse_register()
        cc = 0
        if scanner.peek() == TOK_INT:
            scanner.advance()
            cc = _int_value()
        emit_r(op, rd, rs, (cc & 7) << 2)
    elif kind == KIND["FP_FD_FS_INST"]:
        fd: int = _parse_fp_register()
        emit_fp_r(op, fd, _parse_fp_register(), 0)
    elif kind == KIND["FP_FD_FS_FT_INST"]:
        fd = _parse_fp_register()
        fs: int = _parse_fp_register()
        emit_fp_r(op, fd, fs, _parse_fp_register())
    elif kind == KIND["FP_CMP_FS_FT_INST"]:
        cc = 0
        if scanner.peek() == TOK_INT:
            scanner.advance()
            cc = _int_value()
        fs = _parse_fp_register()
        emit_fp_compare(op, fs, _parse_fp_register(), cc)
    elif kind == KIND["FP_MOVC_TYPE_INST"]:
        fd = _parse_fp_register()
        fs = _parse_fp_register()
        if scanner.peek() == TOK_REG:
            emit_fp_r(op, fd, fs, _parse_register())
        elif scanner.peek() == TOK_INT:
            scanner.advance()
            emit_fp_r(op, fd, fs, (_int_value() & 7) << 2)
        else:
            emit_fp_r(op, fd, fs, 0)
    else:
        _parse_error_at("Instruction not yet supported")
        _sync_to_nl()


def _parse_variable_shift(op: int) -> None:
    """``sllv rd, rt, rs`` (note the order), or ``sllv rd, rt, INT``."""
    rd: int = _parse_register()
    rs: int = _parse_register()
    if scanner.peek() == TOK_REG:
        emit_r(op, rd, _parse_register(), rs)
    elif scanner.peek() == TOK_INT:
        scanner.advance()
        emit_r_shift(pseudo_op.op_to_imm_op(op), rd, rs, _int_value())
    else:
        _parse_error_at("Expected register or integer shift")


# --- pseudo-instructions ----------------------------------------------------------


def _parse_pseudo(op: int) -> None:
    """A pseudo-instruction, wrapped in a PSEUDO node holding its expansion."""
    global _current_pseudo
    node: Node = ast_nodes.make(Kind.PSEUDO, mnemonic=opcodes.name_of(op))
    saved: Node | None = _current_pseudo
    _current_pseudo = node
    _expand_pseudo(op)
    _current_pseudo = saved
    _append(node)


def _expand_pseudo(op: int) -> None:  # noqa: C901 - one case per pseudo-op, as in C
    """Parse a pseudo-instruction's operands and emit its expansion."""
    t: dict[str, int] = TOK
    if op == t["LI_PSEUDO_OP"]:
        rt: int = _parse_register()
        emit_i(t["ORI_OPCODE"], rt, 0, _parse_imm32())
    elif op in (t["MOVE_PSEUDO_OP"], t["NEG_PSEUDO_OP"], t["NEGU_PSEUDO_OP"]):
        real: int = {
            t["MOVE_PSEUDO_OP"]: t["ADDU_OPCODE"],
            t["NEG_PSEUDO_OP"]: t["SUB_OPCODE"],
            t["NEGU_PSEUDO_OP"]: t["SUBU_OPCODE"],
        }[op]
        rd: int = _parse_register()
        emit_r(real, rd, 0, _parse_register())
    elif op == t["NOT_PSEUDO_OP"]:
        rd = _parse_register()
        emit_r(t["NOR_OPCODE"], rd, _parse_register(), 0)
    elif op in (t["ROR_PSEUDO_OP"], t["ROL_PSEUDO_OP"]):
        _expand_rotate(op)
    elif op in (
        t["SLE_PSEUDO_OP"], t["SLEU_PSEUDO_OP"], t["SGT_PSEUDO_OP"],
        t["SGTU_PSEUDO_OP"], t["SGE_PSEUDO_OP"], t["SGEU_PSEUDO_OP"],
        t["SEQ_PSEUDO_OP"], t["SNE_PSEUDO_OP"],
    ):  # fmt: skip
        _expand_set(op)
    elif op == t["ABS_PSEUDO_OP"]:
        rd = _parse_register()
        rs: int = _parse_register()
        if rd != rs:
            emit_r(t["ADDU_OPCODE"], rd, 0, rs)
        emit_i(t["BGEZ_OPCODE"], 0, rs, pseudo_op.branch_offset(3))
        pseudo_op.nop_inst()
        emit_r(t["SUB_OPCODE"], rd, 0, rs)
    elif op == t["NOP_PSEUDO_OP"]:
        pseudo_op.nop_inst()
    elif op in (t["REM_PSEUDO_OP"], t["REMU_PSEUDO_OP"]):
        rd = _parse_register()
        rs = _parse_register()
        if scanner.peek() == TOK_REG:
            pseudo_op.div_inst(op, rd, rs, _parse_register(), False)
        else:
            imm: ImmExpr = _parse_imm32()
            if instruction.is_zero_imm(imm):
                _parse_error_at("Divide by zero")
            else:
                emit_i(t["ORI_OPCODE"], 1, 0, imm)
                pseudo_op.div_inst(op, rd, rs, 1, True)
    elif op in (t["MULO_PSEUDO_OP"], t["MULOU_PSEUDO_OP"]):
        rd = _parse_register()
        rs = _parse_register()
        if scanner.peek() == TOK_REG:
            pseudo_op.mult_inst(op, rd, rs, _parse_register())
        else:
            imm = _parse_imm32()
            if instruction.is_zero_imm(imm):
                emit_i(t["ORI_OPCODE"], rd, 0, imm)  # n * 0 is 0
            else:
                emit_i(t["ORI_OPCODE"], 1, 0, imm)
                pseudo_op.mult_inst(op, rd, rs, 1)
    elif op in (t["MFC1_D_PSEUDO_OP"], t["MTC1_D_PSEUDO_OP"]):
        real = t["MFC1_OPCODE"] if op == t["MFC1_D_PSEUDO_OP"] else t["MTC1_OPCODE"]
        reg: int = _parse_register()
        copreg: int = _parse_copreg_operand()
        emit_fp_r(real, 0, copreg, reg)
        emit_fp_r(real, 0, copreg + 1, reg + 1)
    elif op in (t["LI_D_PSEUDO_OP"], t["LI_S_PSEUDO_OP"]):
        _expand_li_fp(op)
    elif op in (t["L_D_PSEUDO_OP"], t["L_S_PSEUDO_OP"], t["S_D_PSEUDO_OP"],
                t["S_S_PSEUDO_OP"]):  # fmt: skip
        real = {
            t["L_D_PSEUDO_OP"]: t["LDC1_OPCODE"],
            t["L_S_PSEUDO_OP"]: t["LWC1_OPCODE"],
            t["S_D_PSEUDO_OP"]: t["SDC1_OPCODE"],
            t["S_S_PSEUDO_OP"]: t["SWC1_OPCODE"],
        }[op]
        fr: int = _parse_fp_register()
        address: AddrExpr = _parse_address()
        emit_i(real, fr, address.reg_no, address.imm)
    elif op in (t["LD_PSEUDO_OP"], t["SD_PSEUDO_OP"]):
        real = t["LW_OPCODE"] if op == t["LD_PSEUDO_OP"] else t["SW_OPCODE"]
        rt = _parse_register()
        address = _parse_address()
        emit_i(real, rt, address.reg_no, address.imm)
        emit_i(
            real, rt + 1, address.reg_no, instruction.incr_expr_offset(address.imm, 4)
        )
    elif op in (t["ULW_PSEUDO_OP"], t["USW_PSEUDO_OP"]):
        left: int = t["LWL_OPCODE"] if op == t["ULW_PSEUDO_OP"] else t["SWL_OPCODE"]
        right: int = t["LWR_OPCODE"] if op == t["ULW_PSEUDO_OP"] else t["SWR_OPCODE"]
        rt = _parse_register()
        address = _parse_address()
        emit_i(left, rt, address.reg_no, instruction.incr_expr_offset(address.imm, 3))
        emit_i(right, rt, address.reg_no, address.imm)
    elif op in (t["ULH_PSEUDO_OP"], t["ULHU_PSEUDO_OP"]):
        high_load: int = t["LB_OPCODE"] if op == t["ULH_PSEUDO_OP"] else t["LBU_OPCODE"]
        rt = _parse_register()
        address = _parse_address()
        emit_i(
            high_load, rt, address.reg_no, instruction.incr_expr_offset(address.imm, 1)
        )
        emit_i(t["LBU_OPCODE"], 1, address.reg_no, address.imm)
        emit_r_shift(t["SLL_OPCODE"], rt, rt, 8)
        emit_r(t["OR_OPCODE"], rt, rt, 1)
    elif op == t["USH_PSEUDO_OP"]:
        rt = _parse_register()
        address = _parse_address()
        emit_i(t["SB_OPCODE"], rt, address.reg_no, address.imm)
        emit_r_shift(t["SLL_OPCODE"], 1, rt, 24)
        emit_r_shift(t["SRL_OPCODE"], rt, rt, 8)
        emit_r(t["OR_OPCODE"], rt, rt, 1)
        emit_i(
            t["SB_OPCODE"],
            rt,
            address.reg_no,
            instruction.incr_expr_offset(address.imm, 1),
        )
        emit_r_shift(t["SRL_OPCODE"], 1, rt, 24)
        emit_r_shift(t["SLL_OPCODE"], rt, rt, 8)
        emit_r(t["OR_OPCODE"], rt, rt, 1)
    elif op == t["LA_PSEUDO_OP"]:
        rt = _parse_register()
        address = _parse_address()
        if address.reg_no:
            emit_i(t["ADDI_OPCODE"], rt, address.reg_no, address.imm)
        else:
            emit_i(t["ORI_OPCODE"], rt, 0, address.imm)
    elif op in (t["BEQZ_PSEUDO_OP"], t["BNEZ_PSEUDO_OP"]):
        rs = _parse_register()
        real = t["BEQ_OPCODE"] if op == t["BEQZ_PSEUDO_OP"] else t["BNE_OPCODE"]
        emit_i(real, 0, rs, _parse_label())
    elif op in (t["B_PSEUDO_OP"], t["BAL_PSEUDO_OP"]):
        real = t["BGEZAL_OPCODE"] if op == t["BAL_PSEUDO_OP"] else t["BGEZ_OPCODE"]
        emit_i(real, 0, 0, _parse_label())
    elif op in (t["BGT_PSEUDO_OP"], t["BGTU_PSEUDO_OP"], t["BGE_PSEUDO_OP"],
                t["BGEU_PSEUDO_OP"], t["BLT_PSEUDO_OP"], t["BLTU_PSEUDO_OP"],
                t["BLE_PSEUDO_OP"], t["BLEU_PSEUDO_OP"]):  # fmt: skip
        _expand_compare_branch(op)
    else:
        _parse_error_at("Pseudo-op not yet supported")
        _sync_to_nl()


def _expand_rotate(op: int) -> None:
    """``ror``/``rol`` by a register or by a constant."""
    t: dict[str, int] = TOK
    is_right: bool = op == t["ROR_PSEUDO_OP"]
    rd: int = _parse_register()
    rs: int = _parse_register()
    if scanner.peek() == TOK_REG:
        rt: int = _parse_register()
        emit_r(t["SUBU_OPCODE"], 1, 0, rt)
        if is_right:
            emit_r(t["SLLV_OPCODE"], 1, 1, rs)
            emit_r(t["SRLV_OPCODE"], rd, rt, rs)
        else:
            emit_r(t["SRLV_OPCODE"], 1, 1, rs)
            emit_r(t["SLLV_OPCODE"], rd, rt, rs)
        emit_r(t["OR_OPCODE"], rd, rd, 1)
        return
    imm: ImmExpr = _parse_imm32()
    distance: int = instruction.eval_imm_expr(imm)
    pseudo_op.check_imm_range(imm, 0, 31)
    first: int = t["SLL_OPCODE"] if is_right else t["SRL_OPCODE"]
    second: int = t["SRL_OPCODE"] if is_right else t["SLL_OPCODE"]
    emit_r_shift(first, 1, rs, -distance)
    emit_r_shift(second, rd, rs, distance)
    emit_r(t["OR_OPCODE"], rd, rd, 1)


def _expand_set(op: int) -> None:
    """``sle sleu sgt sgtu sge sgeu seq sne rd, rs, (rt | imm)``."""
    t: dict[str, int] = TOK
    rd: int = _parse_register()
    rs: int = _parse_register()
    rt: int
    if scanner.peek() == TOK_REG:
        rt = _parse_register()
    else:
        imm: ImmExpr = _parse_imm32()
        if instruction.is_zero_imm(imm):
            rt = 0
        else:
            emit_i(t["ORI_OPCODE"], 1, 0, imm)
            rt = 1
    if op in (t["SLE_PSEUDO_OP"], t["SLEU_PSEUDO_OP"]):
        pseudo_op.set_le_inst(op, rd, rs, rt)
    elif op in (t["SGT_PSEUDO_OP"], t["SGTU_PSEUDO_OP"]):
        pseudo_op.set_gt_inst(op, rd, rs, rt)
    elif op in (t["SGE_PSEUDO_OP"], t["SGEU_PSEUDO_OP"]):
        pseudo_op.set_ge_inst(op, rd, rs, rt)
    else:
        pseudo_op.set_eq_inst(op, rd, rs, rt)


def _expand_li_fp(op: int) -> None:
    """``li.d``/``li.s fd, FLOAT``: build the bits in $at, move with mtc1."""
    t: dict[str, int] = TOK
    fd: int = _parse_fp_register()
    if scanner.peek() != TOK_FP:
        _parse_error_at("Expected FP literal")
        return
    scanner.advance()
    value: int | float | str = scanner.scan_value
    assert isinstance(value, float)
    if op == t["LI_D_PSEUDO_OP"]:
        low: int
        high: int
        low, high = machine.double_words(value)
        emit_i(t["ORI_OPCODE"], 1, 0, instruction.const_imm_expr(low))
        emit_fp_r(t["MTC1_OPCODE"], 0, fd, 1)
        emit_i(t["ORI_OPCODE"], 1, 0, instruction.const_imm_expr(high))
        emit_fp_r(t["MTC1_OPCODE"], 0, fd + 1, 1)
    else:
        bits: int = machine.float_bits(value)
        emit_i(t["ORI_OPCODE"], 1, 0, instruction.const_imm_expr(bits))
        emit_fp_r(t["MTC1_OPCODE"], 0, fd, 1)


def _expand_compare_branch(op: int) -> None:
    """``bgt bgtu bge bgeu blt bltu ble bleu rs, (rt | imm), label``."""
    t: dict[str, int] = TOK
    rs: int = _parse_register()
    signed: bool = op in (
        t["BGT_PSEUDO_OP"], t["BGE_PSEUDO_OP"], t["BLT_PSEUDO_OP"], t["BLE_PSEUDO_OP"]
    )  # fmt: skip
    slt: int = t["SLT_OPCODE"] if signed else t["SLTU_OPCODE"]
    slti: int = t["SLTI_OPCODE"] if signed else t["SLTIU_OPCODE"]
    greater: bool = op in (t["BGT_PSEUDO_OP"], t["BGTU_PSEUDO_OP"], t["BLE_PSEUDO_OP"],
                           t["BLEU_PSEUDO_OP"])  # fmt: skip
    # bgt/ble branch on $at != 0 / == 0 after slt $at, rt, rs; bge/blt after
    # slt $at, rs, rt.
    on_set: bool = op in (t["BGT_PSEUDO_OP"], t["BGTU_PSEUDO_OP"], t["BLT_PSEUDO_OP"],
                          t["BLTU_PSEUDO_OP"])  # fmt: skip
    branch: int = t["BNE_OPCODE"] if on_set else t["BEQ_OPCODE"]
    if scanner.peek() == TOK_REG:
        rt: int = _parse_register()
        target: ImmExpr = _parse_label()
        if greater:
            emit_r(slt, 1, rt, rs)
        else:
            emit_r(slt, 1, rs, rt)
        emit_i(branch, 0, 1, target)
        return
    imm: ImmExpr = _parse_imm32()
    target = _parse_label()
    if op in (t["BGE_PSEUDO_OP"], t["BGEU_PSEUDO_OP"], t["BLT_PSEUDO_OP"],
              t["BLTU_PSEUDO_OP"]):  # fmt: skip
        emit_i(slti, 1, rs, imm)
        emit_i(branch, 0, 1, target)
    elif op in (t["BGT_PSEUDO_OP"], t["BLE_PSEUDO_OP"]):
        emit_i(t["SLTI_OPCODE"], 1, rs, instruction.incr_expr_offset(imm, 1))
        emit_i(
            t["BEQ_OPCODE"] if op == t["BGT_PSEUDO_OP"] else t["BNE_OPCODE"],
            0,
            1,
            target,
        )
    elif op == t["BGTU_PSEUDO_OP"]:
        emit_i(t["ORI_OPCODE"], 1, 0, imm)
        emit_i(t["BEQ_OPCODE"], rs, 1, pseudo_op.branch_offset(3))
        emit_r(t["SLTU_OPCODE"], 1, rs, 1)
        emit_i(t["BEQ_OPCODE"], 0, 1, target)
    else:  # bleu
        emit_i(t["ORI_OPCODE"], 1, 0, imm)
        emit_i(t["BEQ_OPCODE"], rs, 1, target)
        emit_r(t["SLTU_OPCODE"], 1, rs, 1)
        emit_i(t["BNE_OPCODE"], 0, 1, target)


# --- directives -------------------------------------------------------------------


def _parse_segment(kind: Kind) -> None:
    """``.text/.data/.ktext/.kdata [ADDR]``."""
    has_addr: bool = False
    addr: int = 0
    if scanner.peek() == TOK_INT:
        scanner.advance()
        has_addr = True
        addr = machine.u32(_int_value())
    _emit_dir_seg(kind, has_addr, addr)


def _parse_named_size(kind: Kind) -> None:
    """``.extern NAME SIZE`` / ``.comm NAME SIZE``."""
    scanner.force_identifier()
    if scanner.peek() != TOK_ID:
        _parse_error_at("Expected ID")
        return
    scanner.advance()
    name: str = _str_value()
    size: int = _parse_expression()
    _append(ast_nodes.make(kind, name=name, number=size))


def _parse_expr_list() -> None:
    """The comma- (really white-space-) separated values of .word etc."""
    while True:
        errors_before: int = pseudo_op.parse_errors_seen
        value: int = _parse_expression()
        if pseudo_op.parse_errors_seen != errors_before:
            # The bad token is still there: skip the line, don't loop on it.
            _sync_to_nl()
            break
        _data_int_push(value)
        if scanner.peek() in (TOK_NL, TOK_EOF):
            break
    _data_int_finalize()


def _align_labels_to(alignment: int) -> None:
    """Move the line's labels to where the alignment will put the data."""
    if not data_dir or not _auto_align or not _this_line_labels:
        return
    current: int = data.current_data_pc()
    fix_current_label_address(
        (current + (1 << alignment) - 1) & ((0xFFFFFFFF << alignment) & 0xFFFFFFFF)
    )


def _parse_fp_expr_list() -> None:
    """The floating-point literals of .float/.double."""
    while True:
        if scanner.peek() != TOK_FP:
            _parse_error_at("Expected floating-point literal")
            return
        scanner.advance()
        value: int | float | str = scanner.scan_value
        assert isinstance(value, float)
        _data_doubles.append(value)
        if scanner.peek() in (TOK_NL, TOK_EOF):
            break
    _data_fp_finalize()


def _parse_aligned_list(kind: Kind, alignment: int) -> None:
    """.word/.half/.float/.double: align now (as the C parser does), then parse."""
    _align_labels_to(alignment)
    if data_dir:
        data.set_data_alignment(alignment)
    if kind in (Kind.DATA_FLOAT, Kind.DATA_DOUBLE):
        _data_fp_begin(kind)
        _parse_fp_expr_list()
    else:
        _data_int_begin(kind)
        _parse_expr_list()


def _parse_string_list() -> None:
    """The strings of .ascii/.asciiz."""
    while True:
        if scanner.peek() != TOK_STR:
            _parse_error_at("Expected string literal")
            return
        scanner.advance()
        # A \000 escape ends the string: C measures it with strlen.
        text: bytes = console.to_bytes(_str_value()).split(b"\0", 1)[0]
        if text_dir:
            _parse_error_at("Can't put data in text segment")
        else:
            _append(
                ast_nodes.make(Kind.DATA_STRING, string=text, null_terminate=_null_term)
            )
        if scanner.peek() in (TOK_NL, TOK_EOF):
            break


_IGNORED_DIRECTIVES: frozenset[int] = frozenset(
    TOK[name]
    for name in (
        "FILE_DIRECTIVE", "LOC_DIRECTIVE", "FRAME_DIRECTIVE", "MASK_DIRECTIVE",
        "FMASK_DIRECTIVE", "ENT_DIRECTIVE", "END_DIRECTIVE", "LABEL_DIRECTIVE",
        "LIVEREG_DIRECTIVE", "OPTIONS_DIRECTIVE", "BGNB_DIRECTIVE", "ENDB_DIRECTIVE",
        "ENDR_DIRECTIVE", "ASM0_DIRECTIVE", "ALIAS_DIRECTIVE", "SET_DIRECTIVE",
    )
)  # fmt: skip


def _parse_directive(token: int) -> None:
    """One assembler directive."""
    global _null_term, _auto_align
    t: dict[str, int] = TOK
    if token == t["DATA_DIRECTIVE"]:
        _parse_segment(Kind.DIR_DATA)
    elif token == t["K_DATA_DIRECTIVE"]:
        _parse_segment(Kind.DIR_KDATA)
    elif token == t["TEXT_DIRECTIVE"]:
        _parse_segment(Kind.DIR_TEXT)
    elif token == t["K_TEXT_DIRECTIVE"]:
        _parse_segment(Kind.DIR_KTEXT)
    elif token == t["GLOBAL_DIRECTIVE"]:
        scanner.force_identifier()
        if scanner.peek() != TOK_ID:
            _parse_error_at("Expected identifier after .globl")
            return
        scanner.advance()
        _append(ast_nodes.make(Kind.DIR_GLOBL, name=_str_value()))
    elif token == t["WORD_DIRECTIVE"]:
        _parse_aligned_list(Kind.DATA_WORD, 2)
    elif token == t["HALF_DIRECTIVE"]:
        _parse_aligned_list(Kind.DATA_HALF, 1)
    elif token == t["BYTE_DIRECTIVE"]:
        _data_int_begin(Kind.DATA_BYTE)
        _parse_expr_list()
    elif token in (t["ASCII_DIRECTIVE"], t["ASCIIZ_DIRECTIVE"]):
        _null_term = token == t["ASCIIZ_DIRECTIVE"]
        _parse_string_list()
    elif token == t["FLOAT_DIRECTIVE"]:
        _parse_aligned_list(Kind.DATA_FLOAT, 2)
    elif token == t["DOUBLE_DIRECTIVE"]:
        _parse_aligned_list(Kind.DATA_DOUBLE, 3)
    elif token == t["SPACE_DIRECTIVE"]:
        size: int = _parse_expression()
        if text_dir:
            _parse_error_at("Can't put data in text segment")
            return
        _append(ast_nodes.make(Kind.DIR_SPACE, number=size))
    elif token == t["ALIGN_DIRECTIVE"]:
        power: int = _parse_expression()
        if power == 0:
            _auto_align = False
        _append(ast_nodes.make(Kind.DIR_ALIGN, number=power))
    elif token == t["COMM_DIRECTIVE"]:
        _parse_named_size(Kind.DIR_COMM)
    elif token == t["EXTERN_DIRECTIVE"]:
        _parse_named_size(Kind.DIR_EXTERN)
    elif token == t["ERR_DIRECTIVE"]:
        _parse_error_at(".err directive")
    elif token in _IGNORED_DIRECTIVES:
        _sync_to_nl()
    else:
        _parse_error_at("Directive not yet supported")
        _sync_to_nl()


# --- lines and files ----------------------------------------------------------------


def _parse_opt_label() -> None:
    """``NAME:`` or ``NAME = EXPR`` (the caller saw the ':' or '=')."""
    scanner.advance()
    name: str = _str_value()
    separator: int = scanner.advance()
    if separator == ord(":"):
        _append(ast_nodes.make(Kind.LABEL_DEF, name=name))
    else:
        value: int = _parse_expression()
        _append(
            ast_nodes.make(Kind.LABEL_DEF, name=name, is_constant=True, number=value)
        )


def _parse_line() -> None:
    """One line: optional label, then a directive or an instruction."""
    if scanner.peek() == TOK_NL:
        scanner.advance()
        return
    if scanner.peek() == TOK_EOF:
        return
    if scanner.peek() == TOK_ID and scanner.peek2() in (ord(":"), ord("=")):
        _parse_opt_label()
    token: int = scanner.peek()
    if token == TOK_NL:
        scanner.advance()
        return
    if token == TOK_EOF:
        return
    if instruction.kind_of(token) == opcodes.ASM_DIR:
        _parse_directive(scanner.advance())
    else:
        _parse_asm_code()
    _clear_labels()
    if pseudo_op.parse_error_occurred:
        _sync_to_nl()
    elif scanner.peek() == TOK_NL:
        scanner.advance()
    elif scanner.peek() != TOK_EOF:
        _parse_error_at("Extra tokens after instruction")
        _sync_to_nl()


def parser_init(source: BinaryIO | None, file_name: str | None) -> None:
    """Get ready to assemble source (named file_name in messages)."""
    global input_file_name, data_dir, text_dir, _current_file
    scanner.init(source)
    input_file_name = file_name
    pseudo_op.parse_errors_seen = 0
    pseudo_op.parse_error_occurred = False
    data_dir = False
    text_dir = True
    _current_file = ast_nodes.make(Kind.FILE, name=file_name)


def parse_file() -> int:
    """Parse to the end of the input, print the requested views, and emit.

    Returns:
        int: the number of parse errors.
    """
    while scanner.peek() != TOK_EOF:
        pseudo_op.parse_error_occurred = False
        scanner.start_line()
        _parse_line()
    _clear_labels()
    if _current_file is not None:
        if print_ast:
            console.error(ast_nodes.format_tree(_current_file))
        if show_expansion:
            console.error(
                "".join(
                    ast_nodes.format_tree(child)
                    for child in _current_file.children
                    if child.kind == Kind.PSEUDO
                )
            )
        if print_ast_json:
            console.output(ast_nodes.format_json(_current_file))
        if not print_ast_only:
            emit_ast(_current_file)
    return pseudo_op.parse_errors_seen


# --- the emit pass --------------------------------------------------------------------


def emit_ast(file: Node) -> None:
    """Store every node of the file into memory, in source order."""
    for node in file.children:
        _emit_one(node)
        if node.kind not in (Kind.LABEL_DEF, Kind.DIR_GLOBL):
            _clear_labels()
    _clear_labels()
    instruction.emit_source_clear()


def _emit_data_ints(node: Node, store: Callable[[int], None], width: int) -> None:
    """Store a .byte/.half/.word list, recording forward references."""
    for expr in node.exprs:
        if expr.symbol is not None and expr.symbol.addr == 0:
            symbol_table.record_data_uses_symbol(
                data.current_data_pc(), expr.symbol, expr.offset, width
            )
            store(expr.offset)
        else:
            store(instruction.eval_imm_expr(expr))


def _emit_one(node: Node) -> None:  # noqa: C901 - one case per node kind
    """Store one node: the action the C engine's emit_one takes for its kind."""
    global data_dir, text_dir, _auto_align
    scanner.line_no = node.source_line
    instruction.emit_source_set(node.src_text)
    match node.kind:
        case Kind.DIR_TEXT | Kind.DIR_KTEXT:
            instruction.user_kernel_text_segment(node.kind == Kind.DIR_KTEXT)
            data_dir = False
            text_dir = True
            if node.has_start_addr:
                instruction.set_text_pc(node.start_addr)
        case Kind.DIR_DATA | Kind.DIR_KDATA:
            data.user_kernel_data_segment(node.kind == Kind.DIR_KDATA)
            data_dir = True
            text_dir = False
            data.enable_data_alignment()
            _auto_align = True
            if node.has_start_addr:
                data.set_data_pc(node.start_addr)
        case Kind.DIR_ALIGN:
            if node.number == 0:
                _auto_align = False
            if text_dir:
                instruction.align_text(node.number)
            else:
                data.align_data(node.number)
        case Kind.DIR_SPACE:
            data.increment_data_pc(node.number)
        case Kind.DIR_GLOBL:
            symbol_table.make_label_global(node.name or "")
        case Kind.DIR_EXTERN | Kind.DIR_COMM:
            name: str = node.name or ""
            if node.kind == Kind.DIR_EXTERN:
                symbol_table.make_label_global(name)
            else:
                data.align_data(2)
            if symbol_table.lookup_label(name).addr == 0:
                symbol_table.record_label(name, data.current_data_pc(), True)
            data.increment_data_pc(node.number)
        case Kind.DATA_BYTE:
            _emit_data_ints(node, data.store_byte, 1)
        case Kind.DATA_HALF | Kind.DATA_WORD | Kind.DATA_FLOAT | Kind.DATA_DOUBLE:
            alignment: int = {
                Kind.DATA_HALF: 1,
                Kind.DATA_WORD: 2,
                Kind.DATA_FLOAT: 2,
                Kind.DATA_DOUBLE: 3,
            }[node.kind]
            _align_labels_to(alignment)
            if data_dir:
                data.set_data_alignment(alignment)
            if node.kind == Kind.DATA_HALF:
                _emit_data_ints(node, data.store_half, 2)
            elif node.kind == Kind.DATA_WORD:
                _emit_data_ints(node, data.store_word, 4)
            else:
                store_fp: Callable[[float], None] = (
                    data.store_float
                    if node.kind == Kind.DATA_FLOAT
                    else data.store_double
                )
                for value in node.doubles:
                    store_fp(value)
        case Kind.DATA_STRING:
            if not text_dir:
                data.store_string(node.string, node.null_terminate)
        case Kind.LABEL_DEF:
            if node.is_constant:
                label: symbol_table.Label = symbol_table.record_label(
                    node.name or "", machine.u32(node.number), True
                )
                label.const_flag = True
            else:
                addr: int = (
                    instruction.current_text_pc()
                    if text_dir
                    else data.current_data_pc()
                )
                _this_line_labels.insert(
                    0, symbol_table.record_label(node.name or "", addr, False)
                )
        case Kind.INST_R:
            instruction.r_type_inst(node.op, node.rd, node.rs, node.rt)
        case Kind.INST_R_SHIFT:
            instruction.r_sh_type_inst(node.op, node.rd, node.rt, node.shamt)
        case Kind.INST_I:
            assert node.imm is not None
            instruction.i_type_inst(node.op, node.rt, node.rs, node.imm.copy())
        case Kind.INST_J:
            assert node.imm is not None
            instruction.j_type_inst(node.op, node.imm.copy())
        case Kind.INST_FP_R:
            instruction.r_co_type_inst(node.op, node.fd, node.fs, node.ft)
        case Kind.INST_FP_COMPARE:
            instruction.r_cond_type_inst(node.op, node.fs, node.ft, node.cc)
        case Kind.PSEUDO:
            for child in node.children:
                _emit_one(child)
                if child.kind not in (Kind.LABEL_DEF, Kind.DIR_GLOBL):
                    _clear_labels()
        case Kind.FILE | Kind.ERROR:
            pass  # never a direct child of the file
