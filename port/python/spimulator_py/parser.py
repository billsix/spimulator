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
from typing import BinaryIO, assert_never

from . import (
    ast_nodes,
    console,
    data,
    machine,
    opcodes,
    pseudo_op,
    scanner,
    symbol_table,
)
from . import (
    instruction as instruction_module,
)
from .ast_nodes import Kind, Node
from .inst import AddressExpression, ImmediateExpression
from .opcode_table import OpType, Tok
from .opcodes import (
    TOKEN_EOF,
    TOKEN_FLOATING_POINT,
    TOKEN_FLOATING_POINT_REGISTER,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_NEWLINE,
    TOKEN_REGISTER,
    TOKEN_STRING,
)

data_directive: bool = False
"""The current segment is a data segment."""
text_directive: bool = True
"""The current segment is a text segment."""

input_file_name: str | None = None
"""The name of the file being assembled, for messages."""

_null_term: bool = False
_auto_align: bool = True  # mirrors data.py's flag, as the C parser does

_current_file: Node | None = None
_current_pseudo: Node | None = None
_expression_unresolved_label: symbol_table.Label | None = None

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
_data_expressions: list[ImmediateExpression] = []
_data_doubles: list[float] = []
_data_directive_line: int = 0


def _append(node: Node) -> None:
    """Add node to the current pseudo-op's expansion, or else to the file."""
    if _current_pseudo is not None:
        _current_pseudo.children.append(node)
    elif _current_file is not None:
        _current_file.children.append(node)
    else:
        pass  # no tree is being built (parser_initialize has not run): the node is dropped


# --- the emit_* helpers the pseudo-op expansions call ---------------------------


def emit_r(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Append an R-type instruction node."""
    _append(
        ast_nodes.make(
            Kind.INSTRUCTION_R,
            opcode=opcode,
            destination_register=destination_register,
            source_register=source_register,
            target_register=target_register,
        )
    )


def emit_r_shift(
    opcode: int, destination_register: int, target_register: int, shift_amount: int
) -> None:
    """Append a shift node."""
    _append(
        ast_nodes.make(
            Kind.INSTRUCTION_R_SHIFT,
            opcode=opcode,
            destination_register=destination_register,
            target_register=target_register,
            shift_amount=shift_amount,
        )
    )


def emit_i(
    opcode: int,
    target_register: int,
    source_register: int,
    immediate: ImmediateExpression,
) -> None:
    """Append an I-type node (it keeps its own copy of imm)."""
    _append(
        ast_nodes.make(
            Kind.INSTRUCTION_I,
            opcode=opcode,
            target_register=target_register,
            source_register=source_register,
            immediate=immediate.copy(),
        )
    )


def emit_j(opcode: int, target: ImmediateExpression) -> None:
    """Append a J-type node."""
    _append(ast_nodes.make(Kind.INSTRUCTION_J, opcode=opcode, immediate=target.copy()))


def emit_floating_point_r(
    opcode: int,
    floating_point_destination: int,
    floating_point_source: int,
    floating_point_target: int,
) -> None:
    """Append a coprocessor R-type node."""
    _append(
        ast_nodes.make(
            Kind.INSTRUCTION_FLOATING_POINT_R,
            opcode=opcode,
            floating_point_destination=floating_point_destination,
            floating_point_source=floating_point_source,
            floating_point_target=floating_point_target,
        )
    )


def emit_floating_point_compare(
    opcode: int,
    floating_point_source: int,
    floating_point_target: int,
    condition_code: int,
) -> None:
    """Append an FP compare node."""
    _append(
        ast_nodes.make(
            Kind.INSTRUCTION_FLOATING_POINT_COMPARE,
            opcode=opcode,
            floating_point_source=floating_point_source,
            floating_point_target=floating_point_target,
            condition_code=condition_code,
        )
    )


# --- data lists -------------------------------------------------------------------


def _data_integer_begin(kind: Kind) -> None:
    """Start a .byte/.half/.word value list."""
    global _data_kind, _data_directive_line
    _data_kind = kind
    _data_expressions.clear()
    _data_directive_line = scanner.line_no


def _data_integer_push(value: int) -> None:
    """Add one value, with the first undefined symbol it used, if any."""
    global _expression_unresolved_label
    name: str | None = (
        _expression_unresolved_label.name
        if _expression_unresolved_label is not None
        else None
    )
    _data_expressions.append(
        instruction_module.make_immediate_expression(value, name, False)
    )
    _expression_unresolved_label = None


def _data_integer_finalize() -> None:
    """Turn the collected values into one node."""
    if not _data_expressions:
        return
    node: Node = ast_nodes.make(_data_kind, expressions=list(_data_expressions))
    node.source_line = _data_directive_line
    _append(node)
    _data_expressions.clear()


def _data_floating_point_begin(kind: Kind) -> None:
    """Start a .float/.double value list."""
    global _data_kind, _data_directive_line
    _data_kind = kind
    _data_doubles.clear()
    _data_directive_line = scanner.line_no


def _data_floating_point_finalize() -> None:
    """Turn the collected floating-point values into one node."""
    if not _data_doubles:
        return
    node: Node = ast_nodes.make(
        Kind.DATA_FLOAT if _data_kind == Kind.DATA_FLOAT else Kind.DATA_DOUBLE,
        doubles=list(_data_doubles),
    )
    node.source_line = _data_directive_line
    _append(node)
    _data_doubles.clear()


def _emit_directive_segment(kind: Kind, has_address: bool, address: int) -> None:
    """Record a segment directive; the parser's own segment flags change now."""
    global data_directive, text_directive, _auto_align
    if kind in (Kind.DIRECTIVE_TEXT, Kind.DIRECTIVE_KTEXT):
        data_directive = False
        text_directive = True
    else:
        data_directive = True
        text_directive = False
        _auto_align = True
    _append(ast_nodes.make(kind, has_start_address=has_address, start_address=address))


# --- labels on the current line ---------------------------------------------------


def _clear_labels() -> None:
    """Resolve the uses of the labels of the line just finished."""
    while _this_line_labels:
        symbol_table.resolve_label_uses(_this_line_labels.pop(0))


def fix_current_label_address(new_address: int) -> None:
    """Move the current line's labels to new_address (alignment moved the PC)."""
    for label in _this_line_labels:
        label.address = new_address


# --- errors -------------------------------------------------------------------------


def _parse_error_at(message: str, line: int | None = None) -> None:
    """Report a syntax error on line (default: the current line)."""
    pseudo_op.parse_error_occurred = True
    pseudo_op.parse_errors_seen += 1
    name: str = input_file_name if input_file_name is not None else "(stdin)"
    at: int = scanner.line_no if line is None else line
    console.error(f"spim: (parser) {message} on line {at} of file {name}\n")


def _sync_to_newline() -> None:
    """Skip tokens up to, not past, the end of the line."""
    while scanner.peek() not in (TOKEN_NEWLINE, TOKEN_EOF):
        scanner.advance()


def _expect(expected: int, what: str) -> bool:
    """Consume the token expected, or report what was expected."""
    if scanner.peek() == expected:
        scanner.advance()
        return True
    _parse_error_at(what)
    return False


def _integer_value() -> int:
    """The scanner's last integer value."""
    value: int | float | str = scanner.scan_value
    if not isinstance(value, int):
        raise TypeError(f"_int_value: expected int, got {type(value).__name__}")
    return value


def _string_value() -> str:
    """The scanner's last identifier or string value."""
    value: int | float | str = scanner.scan_value
    if not isinstance(value, str):
        raise TypeError(f"_str_value: expected str, got {type(value).__name__}")
    return value


# --- operands -------------------------------------------------------------------


def _parse_register() -> int:
    """A ``$`` general register."""
    if scanner.peek() == TOKEN_REGISTER:
        scanner.advance()
        return _integer_value()
    _parse_error_at("Expected register")
    return 0


def _parse_floating_point_register() -> int:
    """A ``$f`` register."""
    if scanner.peek() == TOKEN_FLOATING_POINT_REGISTER:
        scanner.advance()
        return _integer_value()
    _parse_error_at("Expected FP register")
    return 0


def _parse_absolute_address() -> int:
    """``INT``, ``INT + INT``, or ``INT INT`` (a negative literal subtracts)."""
    if scanner.peek() != TOKEN_INTEGER:
        _parse_error_at("Expected integer")
        return 0
    scanner.advance()
    value: int = _integer_value()
    if scanner.peek() == ord("+"):
        scanner.advance()
        if scanner.peek() != TOKEN_INTEGER:
            _parse_error_at("Expected integer after '+'")
            return value
        scanner.advance()
        return machine.s32(value + _integer_value())
    elif scanner.peek() == TOKEN_INTEGER:
        scanner.advance()
        if _integer_value() >= 0:
            _parse_error_at("Syntax error")
        return machine.s32(value + _integer_value())
    else:
        return value


def _parse_factor() -> int:
    """``INT``, ``( EXPR )``, or a name (its address, or 0 if undefined yet)."""
    global _expression_unresolved_label
    token: int = scanner.peek()
    if token == TOKEN_INTEGER:
        scanner.advance()
        return _integer_value()
    elif token == ord("("):
        scanner.advance()
        value: int = _parse_sum_of_terms()
        _expect(ord(")"), "Expected ')'")
        return value
    elif token == TOKEN_IDENTIFIER:
        scanner.advance()
        label: symbol_table.Label = symbol_table.lookup_label(_string_value())
        if label.address == 0:
            # A forward reference: emit_ast records the use at the right PC.
            if _expression_unresolved_label is None:
                _expression_unresolved_label = label
            return 0
        return machine.s32(label.address)
    else:
        _parse_error_at("Expected expression")
        return 0


def _parse_term() -> int:
    """``FACTOR (('*' | '/') FACTOR)*``; division by zero gives 0."""
    value: int = _parse_factor()
    while scanner.peek() in (ord("*"), ord("/")):
        opcode: int = scanner.advance()
        right: int = _parse_factor()
        if opcode == ord("*"):
            value = machine.s32(value * right)
        else:
            value = 0 if right == 0 else machine.s32(machine.c_divide(value, right))
    return value


def _parse_sum_of_terms() -> int:
    """``TERM (('+' | '-') TERM)*``."""
    value: int = _parse_term()
    while scanner.peek() in (ord("+"), ord("-")):
        opcode: int = scanner.advance()
        right: int = _parse_term()
        value = machine.s32(value + right if opcode == ord("+") else value - right)
    return value


def _parse_expression() -> int:
    """An expression whose first name is never taken as a keyword."""
    global _expression_unresolved_label
    scanner.force_identifier()
    _expression_unresolved_label = None
    return _parse_sum_of_terms()


def _parse_immediate_32() -> ImmediateExpression:
    """A 32-bit immediate: ``ABS``, ``(ABS) >> INT``, ``ID``, ``ID +/- ABS``."""
    scanner.force_identifier()
    if scanner.peek() == TOKEN_IDENTIFIER:
        scanner.advance()
        name: str = _string_value()
        if scanner.peek() == ord("+"):
            scanner.advance()
            return instruction_module.make_immediate_expression(
                _parse_absolute_address(), name, False
            )
        elif scanner.peek() == ord("-"):
            scanner.advance()
            return instruction_module.make_immediate_expression(
                -_parse_absolute_address(), name, False
            )
        else:
            return instruction_module.make_immediate_expression(0, name, False)
    elif scanner.peek() == ord("("):
        scanner.advance()
        value: int = _parse_absolute_address()
        _expect(ord(")"), "Expected ')'")
        _expect(ord(">"), "Expected '>'")
        _expect(ord(">"), "Expected '>'")
        if scanner.peek() != TOKEN_INTEGER:
            _parse_error_at("Expected integer after '>>'")
            return instruction_module.make_immediate_expression(0, None, False)
        scanner.advance()
        shift: int = _integer_value()
        return instruction_module.make_immediate_expression(
            value >> (shift & 31), None, False
        )
    else:
        return instruction_module.make_immediate_expression(
            _parse_absolute_address(), None, False
        )


def _parse_immediate_16() -> ImmediateExpression:
    """A signed 16-bit immediate (a warning if it does not fit)."""
    # The instruction's line, before parsing the immediate scans the newline.
    line: int = scanner.line_no
    expression: ImmediateExpression = _parse_immediate_32()
    pseudo_op.check_immediate_range(
        expression,
        instruction_module.IMMEDIATE_MIN,
        instruction_module.IMMEDIATE_MAX,
        line,
    )
    return expression


def _parse_unsigned_immediate_16() -> ImmediateExpression:
    """An unsigned 16-bit immediate."""
    line: int = scanner.line_no  # as in _parse_immediate_16
    expression: ImmediateExpression = _parse_immediate_32()
    pseudo_op.check_unsigned_immediate_range(
        expression,
        instruction_module.UNSIGNED_IMMEDIATE_MIN,
        instruction_module.UNSIGNED_IMMEDIATE_MAX,
        line,
    )
    return expression


def _parse_label() -> ImmediateExpression:
    """A branch or jump target: a PC-relative reference to a name."""
    if scanner.peek() != TOKEN_IDENTIFIER:
        _parse_error_at("Expected label")
        return instruction_module.make_immediate_expression(0, None, True)
    scanner.advance()
    return instruction_module.make_immediate_expression(
        -instruction_module.current_text_location(), _string_value(), True
    )


def _parse_address() -> AddressExpression:
    """A load/store address: ``(REG)``, ``ABS [(REG)]``, ``ID [+-ABS] [(REG)]``."""
    scanner.force_identifier()
    token: int = scanner.peek()
    if token == ord("("):
        scanner.advance()
        register_number: int = _parse_register()
        _expect(ord(")"), "Expected ')' after register")
        return instruction_module.make_address_expression(0, None, register_number)
    elif token == TOKEN_INTEGER:
        offset: int = _parse_absolute_address()
        if scanner.peek() == ord("+") and scanner.peek2() == TOKEN_IDENTIFIER:
            scanner.advance()
            scanner.force_identifier()
            scanner.advance()
            return instruction_module.make_address_expression(
                offset, _string_value(), 0
            )
        elif scanner.peek() == ord("("):
            scanner.advance()
            register_number = _parse_register()
            _expect(ord(")"), "Expected ')'")
            return instruction_module.make_address_expression(
                offset, None, register_number
            )
        else:
            return instruction_module.make_address_expression(offset, None, 0)
    elif token == TOKEN_IDENTIFIER:
        scanner.advance()
        name: str = _string_value()
        offset = 0
        if scanner.peek() == ord("+"):
            scanner.advance()
            offset = _parse_absolute_address()
        elif scanner.peek() == ord("-"):
            scanner.advance()
            offset = -_parse_absolute_address()
        else:
            pass  # no offset: offset stays 0
        register_number = 0
        if scanner.peek() == ord("("):
            scanner.advance()
            register_number = _parse_register()
            _expect(ord(")"), "Expected ')'")
        return instruction_module.make_address_expression(offset, name, register_number)
    else:
        _parse_error_at("Expected address")
        return instruction_module.make_address_expression(0, None, 0)


# --- instructions -----------------------------------------------------------------

_HAS_IMMEDIATE_FORM: frozenset[int] = frozenset((
        Tok.ADD_OPCODE, Tok.ADDU_OPCODE, Tok.AND_OPCODE, Tok.OR_OPCODE, Tok.XOR_OPCODE,
        Tok.SLT_OPCODE, Tok.SLTU_OPCODE,
    ))  # fmt: skip


def _parse_r3(opcode: int) -> None:
    """``op rd, rs, rt``, and the immediate forms some of them accept."""
    destination_register: int = _parse_register()
    match opcode:
        case Tok.CLO_OPCODE | Tok.CLZ_OPCODE:
            emit_r(
                opcode, destination_register, _parse_register(), destination_register
            )  # rt must equal rd
            return
        case Tok.MUL_OPCODE:
            source_register: int = _parse_register()
            if scanner.peek() == TOKEN_REGISTER:
                emit_r(opcode, destination_register, source_register, _parse_register())
            else:
                emit_i(Tok.ORI_OPCODE, 1, 0, _parse_immediate_32())
                emit_r(opcode, destination_register, source_register, 1)
            return
        case Tok.SUB_OPCODE | Tok.SUBU_OPCODE:
            add: int = Tok.ADDI_OPCODE if opcode == Tok.SUB_OPCODE else Tok.ADDIU_OPCODE
            if scanner.peek() != TOKEN_REGISTER:
                negated: int = -instruction_module.evaluate_immediate_expression(
                    _parse_immediate_32()
                )
                emit_i(
                    add,
                    destination_register,
                    destination_register,
                    instruction_module.make_immediate_expression(negated, None, False),
                )
                return
            source_register = _parse_register()
            if scanner.peek() == TOKEN_REGISTER:
                emit_r(opcode, destination_register, source_register, _parse_register())
            else:
                negated = -instruction_module.evaluate_immediate_expression(
                    _parse_immediate_32()
                )
                emit_i(
                    add,
                    destination_register,
                    source_register,
                    instruction_module.make_immediate_expression(negated, None, False),
                )
            return
        case _ if opcode in _HAS_IMMEDIATE_FORM:
            if scanner.peek() != TOKEN_REGISTER:
                emit_i(
                    pseudo_op.opcode_to_immediate_opcode(opcode),
                    destination_register,
                    destination_register,
                    _parse_immediate_32(),
                )
                return
            source_register = _parse_register()
            if scanner.peek() == TOKEN_REGISTER:
                emit_r(opcode, destination_register, source_register, _parse_register())
            else:
                emit_i(
                    pseudo_op.opcode_to_immediate_opcode(opcode),
                    destination_register,
                    source_register,
                    _parse_immediate_32(),
                )
            return
        case _:
            source_register = _parse_register()
            emit_r(opcode, destination_register, source_register, _parse_register())


def _parse_two_registers_shift_amount(opcode: int) -> None:
    """``op rd, rt, shamt``; ``ssnop`` is ``sll $0, $0, 1``."""
    if opcode == Tok.SSNOP_OPCODE:
        emit_r_shift(Tok.SLL_OPCODE, 0, 0, 1)
        return
    destination_register: int = _parse_register()
    target_register: int = _parse_register()
    if scanner.peek() != TOKEN_INTEGER:
        _parse_error_at("Expected integer shift amount")
        return
    scanner.advance()
    emit_r_shift(opcode, destination_register, target_register, _integer_value())


def _parse_i2(opcode: int) -> None:
    """``op rt, rs, imm16``, or ``op rt, imm16`` with rs = rt."""
    target_register: int = _parse_register()
    source_register: int = (
        _parse_register() if scanner.peek() == TOKEN_REGISTER else target_register
    )
    unsigned: bool = opcode in (Tok.ANDI_OPCODE, Tok.ORI_OPCODE, Tok.XORI_OPCODE)
    emit_i(
        opcode,
        target_register,
        source_register,
        _parse_unsigned_immediate_16() if unsigned else _parse_immediate_16(),
    )


def _parse_b2(opcode: int) -> None:
    """``op rs, rt, label``, or ``op rs, imm, label`` through $at."""
    source_1: int = _parse_register()
    if scanner.peek() == TOKEN_REGISTER:
        source_2: int = _parse_register()
        emit_i(opcode, source_2, source_1, _parse_label())
        return
    immediate: ImmediateExpression = _parse_immediate_32()
    target: ImmediateExpression = _parse_label()
    if instruction_module.is_zero_immediate(immediate):
        emit_i(opcode, source_1, 0, target)
    else:
        emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
        emit_i(opcode, source_1, 1, target)


def _parse_j(opcode: int) -> None:
    """``j``/``jal``/``jr``/``jalr``/``jalr.hb`` with a label or register(s); ``cop2`` is an error.

    ``jalr.hb`` keeps its own opcode (``jalr`` with the hazard-barrier bit) and takes registers
    only. ``cop2`` shares ``J_TYPE_INSTRUCTION``, but spim has no coprocessor 2.
    """
    if opcode == Tok.COP2_OPCODE:
        _parse_error_at("cop2 is not supported: spim has no coprocessor 2")
        return
    elif scanner.peek() == TOKEN_REGISTER:
        r1: int = _parse_register()
        if scanner.peek() == TOKEN_REGISTER:
            # DEST SRC1 form
            r2: int = _parse_register()
            match opcode:
                case Tok.J_OPCODE | Tok.JR_OPCODE:
                    emit_r(Tok.JR_OPCODE, 0, r2, 0)
                case Tok.JAL_OPCODE | Tok.JALR_OPCODE:
                    emit_r(Tok.JALR_OPCODE, r1, r2, 0)
                case Tok.JALR_HB_OPCODE:
                    emit_r(Tok.JALR_HB_OPCODE, r1, r2, 0)
                case _:
                    # Never reached: only J_TYPE_INSTRUCTION and DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION opcodes come here.
                    raise console.fatal_error(
                        f"internal error: parse_j: unexpected opcode {opcode}\n"
                    )
        else:
            # SRC1-only form
            match opcode:
                case Tok.J_OPCODE | Tok.JR_OPCODE:
                    emit_r(Tok.JR_OPCODE, 0, r1, 0)
                case Tok.JAL_OPCODE | Tok.JALR_OPCODE:
                    emit_r(Tok.JALR_OPCODE, 31, r1, 0)
                case Tok.JALR_HB_OPCODE:
                    emit_r(Tok.JALR_HB_OPCODE, 31, r1, 0)
                case _:
                    # Never reached: as in the DEST SRC1 form above.
                    raise console.fatal_error(
                        f"internal error: parse_j: unexpected opcode {opcode}\n"
                    )
    elif opcode == Tok.JALR_HB_OPCODE:
        _parse_error_at("jalr.hb needs a register operand")
    else:
        target: ImmediateExpression = _parse_label()
        match opcode:
            case Tok.J_OPCODE | Tok.JR_OPCODE:
                emit_j(Tok.J_OPCODE, target)
            case Tok.JAL_OPCODE | Tok.JALR_OPCODE:
                emit_j(Tok.JAL_OPCODE, target)
            case _:
                # Never reached: jalr.hb and cop2 are handled above.
                raise console.fatal_error(
                    f"internal error: parse_j: unexpected opcode {opcode}\n"
                )


def _parse_no_argument(opcode: int) -> None:
    """``op`` alone, or ``op N`` (``break N``: N goes in the rd byte)."""
    if scanner.peek() == TOKEN_INTEGER:
        scanner.advance()
        number: int = _integer_value()
        if opcode == Tok.BREAK_OPCODE and number == 1:
            _parse_error_at("Breakpoint 1 is reserved for debugger")
        emit_r(opcode, number, 0, 0)
    else:
        emit_r(opcode, 0, 0, 0)


def _parse_source_register_target_register(opcode: int) -> None:
    """mult/multu, the trap-on-compare ops, and 2- or 3-operand div/divu."""
    r1: int = _parse_register()
    if scanner.peek() not in (TOKEN_REGISTER, TOKEN_INTEGER, TOKEN_IDENTIFIER):
        _parse_error_at("Expected operand for div/mult")
        return
    trap_opcodes: tuple[int, ...] = (Tok.TEQ_OPCODE, Tok.TGE_OPCODE, Tok.TGEU_OPCODE, Tok.TLT_OPCODE,
                  Tok.TLTU_OPCODE, Tok.TNE_OPCODE)  # fmt: skip
    if opcode in trap_opcodes:
        emit_r(opcode, 0, r1, _parse_register())
    elif opcode in (Tok.DIV_OPCODE, Tok.DIVU_OPCODE):
        r2: int = _parse_register()
        if scanner.peek() in (TOKEN_NEWLINE, TOKEN_EOF):
            emit_r(opcode, 0, r1, r2)
        elif scanner.peek() == TOKEN_REGISTER:
            pseudo_op.divide_instruction(opcode, r1, r2, _parse_register(), False)
        else:
            immediate: ImmediateExpression = _parse_immediate_32()
            if instruction_module.is_zero_immediate(immediate):
                _parse_error_at("Divide by zero")
            else:
                emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
                pseudo_op.divide_instruction(opcode, r1, r2, 1, True)
    else:
        emit_r(opcode, 0, r1, _parse_register())


def _parse_coprocessor_register_operand() -> int:
    """A coprocessor register, written ``$fN`` or ``$N``."""
    return (
        _parse_floating_point_register()
        if scanner.peek() == TOKEN_FLOATING_POINT_REGISTER
        else _parse_register()
    )


def _parse_assembly_code() -> None:
    """One instruction, dispatched on its operand shape."""
    opcode: int = scanner.advance()
    kind: int | None = instruction_module.kind_of(opcode)
    match kind:
        case OpType.NO_ARGUMENT_TYPE_INSTRUCTION:
            _parse_no_argument(opcode)
        case OpType.DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            _parse_r3(opcode)
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
            _parse_two_registers_shift_amount(opcode)
        case OpType.SOURCE_REGISTER_INSTRUCTION:
            emit_r(opcode, 0, _parse_register(), 0)
        case OpType.TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
            _parse_i2(opcode)
        case OpType.TARGET_REGISTER_IMMEDIATE_INSTRUCTION:
            target_register: int = _parse_register()
            emit_i(opcode, target_register, 0, _parse_unsigned_immediate_16())
        case (
            OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION
            | OpType.FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION
        ):
            register_number: int = (
                _parse_register()
                if kind == OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION
                else _parse_floating_point_register()
            )
            address: AddressExpression = _parse_address()
            emit_i(opcode, register_number, address.register_no, address.immediate)
        case OpType.BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION:
            _parse_b2(opcode)
        case OpType.BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION:
            source_register: int = _parse_register()
            emit_i(opcode, 0, source_register, _parse_label())
        case (
            OpType.J_TYPE_INSTRUCTION
            | OpType.DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION
        ):
            _parse_j(opcode)
        case opcodes.PSEUDO_OP:
            _parse_pseudo(opcode)
        case (
            OpType.TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
            | OpType.FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
        ):
            register_number = _parse_register()
            emit_floating_point_r(
                opcode, 0, _parse_coprocessor_register_operand(), register_number
            )
        case OpType.SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            _parse_source_register_target_register(opcode)
        case OpType.DESTINATION_REGISTER_INSTRUCTION:
            emit_r(opcode, _parse_register(), 0, 0)
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION:
            _parse_variable_shift(opcode)
        case OpType.SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
            source_register = _parse_register()
            emit_i(opcode, 0, source_register, _parse_immediate_16())
        case OpType.BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
            nullify_delay: int = (
                1 if instruction_module.opcode_is_nullified_branch(opcode) else 0
            )
            true_false: int = (
                1 if instruction_module.opcode_is_true_branch(opcode) else 0
            )
            condition_code: int = 0
            if scanner.peek() == TOKEN_INTEGER:
                scanner.advance()
                condition_code = _integer_value()
            target: ImmediateExpression = _parse_label()
            emit_i(
                opcode,
                (condition_code << 2) | (nullify_delay << 1) | true_false,
                0,
                target,
            )
        case OpType.MOVE_CONDITIONAL_TYPE_INSTRUCTION:
            destination_register: int = _parse_register()
            source_register = _parse_register()
            condition_code = 0
            if scanner.peek() == TOKEN_INTEGER:
                scanner.advance()
                condition_code = _integer_value()
            emit_r(
                opcode, destination_register, source_register, (condition_code & 7) << 2
            )
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
            floating_point_destination: int = _parse_floating_point_register()
            emit_floating_point_r(
                opcode, floating_point_destination, _parse_floating_point_register(), 0
            )
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION:
            floating_point_destination = _parse_floating_point_register()
            floating_point_source: int = _parse_floating_point_register()
            emit_floating_point_r(
                opcode,
                floating_point_destination,
                floating_point_source,
                _parse_floating_point_register(),
            )
        case OpType.FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
            condition_code = 0
            if scanner.peek() == TOKEN_INTEGER:
                scanner.advance()
                condition_code = _integer_value()
            floating_point_source = _parse_floating_point_register()
            emit_floating_point_compare(
                opcode,
                floating_point_source,
                _parse_floating_point_register(),
                condition_code,
            )
        case OpType.FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION:
            floating_point_destination = _parse_floating_point_register()
            floating_point_source = _parse_floating_point_register()
            if scanner.peek() == TOKEN_REGISTER:
                emit_floating_point_r(
                    opcode,
                    floating_point_destination,
                    floating_point_source,
                    _parse_register(),
                )
            elif scanner.peek() == TOKEN_INTEGER:
                scanner.advance()
                emit_floating_point_r(
                    opcode,
                    floating_point_destination,
                    floating_point_source,
                    (_integer_value() & 7) << 2,
                )
            else:
                emit_floating_point_r(
                    opcode, floating_point_destination, floating_point_source, 0
                )
        case _:
            _parse_error_at("Instruction not yet supported")
            _sync_to_newline()


def _parse_variable_shift(opcode: int) -> None:
    """``sllv rd, rt, rs`` (note the order), or ``sllv rd, rt, INT``."""
    destination_register: int = _parse_register()
    source_register: int = _parse_register()
    if scanner.peek() == TOKEN_REGISTER:
        emit_r(opcode, destination_register, _parse_register(), source_register)
    elif scanner.peek() == TOKEN_INTEGER:
        scanner.advance()
        emit_r_shift(
            pseudo_op.opcode_to_immediate_opcode(opcode),
            destination_register,
            source_register,
            _integer_value(),
        )
    else:
        _parse_error_at("Expected register or integer shift")


# --- pseudo-instructions ----------------------------------------------------------


def _parse_pseudo(opcode: int) -> None:
    """A pseudo-instruction, wrapped in a PSEUDO node holding its expansion."""
    global _current_pseudo
    node: Node = ast_nodes.make(Kind.PSEUDO, mnemonic=opcodes.name_of(opcode))
    saved: Node | None = _current_pseudo
    _current_pseudo = node
    _expand_pseudo(opcode)
    # The expansion is the pseudo-op's line, even when its last operand read
    # on to the newline before a child was built.
    for child in node.children:
        child.source_line = node.source_line
    _current_pseudo = saved
    _append(node)


def _expand_pseudo(opcode: int) -> None:
    """Parse a pseudo-instruction's operands and emit its expansion."""
    match opcode:
        case Tok.LI_PSEUDO_OP:
            target_register: int = _parse_register()
            emit_i(Tok.ORI_OPCODE, target_register, 0, _parse_immediate_32())
        case Tok.MOVE_PSEUDO_OP | Tok.NEG_PSEUDO_OP | Tok.NEGU_PSEUDO_OP:
            register_opcodes: dict[int, int] = {
                Tok.MOVE_PSEUDO_OP: Tok.ADDU_OPCODE,
                Tok.NEG_PSEUDO_OP: Tok.SUB_OPCODE,
                Tok.NEGU_PSEUDO_OP: Tok.SUBU_OPCODE,
            }
            real: int = register_opcodes[opcode]
            destination_register: int = _parse_register()
            emit_r(real, destination_register, 0, _parse_register())
        case Tok.NOT_PSEUDO_OP:
            destination_register = _parse_register()
            emit_r(Tok.NOR_OPCODE, destination_register, _parse_register(), 0)
        case Tok.ROR_PSEUDO_OP | Tok.ROL_PSEUDO_OP:
            _expand_rotate(opcode)
        case Tok.SLE_PSEUDO_OP | Tok.SLEU_PSEUDO_OP | Tok.SGT_PSEUDO_OP | Tok.SGTU_PSEUDO_OP | Tok.SGE_PSEUDO_OP | Tok.SGEU_PSEUDO_OP | Tok.SEQ_PSEUDO_OP | Tok.SNE_PSEUDO_OP:  # fmt: skip
            _expand_set(opcode)
        case Tok.ABS_PSEUDO_OP:
            destination_register = _parse_register()
            source_register: int = _parse_register()
            if destination_register != source_register:
                emit_r(Tok.ADDU_OPCODE, destination_register, 0, source_register)
            emit_i(Tok.BGEZ_OPCODE, 0, source_register, pseudo_op.branch_offset(3))
            pseudo_op.nop_instruction()
            emit_r(Tok.SUB_OPCODE, destination_register, 0, source_register)
        case Tok.NOP_PSEUDO_OP:
            pseudo_op.nop_instruction()
        case Tok.REM_PSEUDO_OP | Tok.REMU_PSEUDO_OP:
            destination_register = _parse_register()
            source_register = _parse_register()
            if scanner.peek() == TOKEN_REGISTER:
                pseudo_op.divide_instruction(
                    opcode,
                    destination_register,
                    source_register,
                    _parse_register(),
                    False,
                )
            else:
                immediate: ImmediateExpression = _parse_immediate_32()
                if instruction_module.is_zero_immediate(immediate):
                    _parse_error_at("Divide by zero")
                else:
                    emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
                    pseudo_op.divide_instruction(
                        opcode, destination_register, source_register, 1, True
                    )
        case Tok.MULO_PSEUDO_OP | Tok.MULOU_PSEUDO_OP:
            destination_register = _parse_register()
            source_register = _parse_register()
            if scanner.peek() == TOKEN_REGISTER:
                pseudo_op.multiply_instruction(
                    opcode, destination_register, source_register, _parse_register()
                )
            else:
                immediate = _parse_immediate_32()
                if instruction_module.is_zero_immediate(immediate):
                    emit_i(
                        Tok.ORI_OPCODE, destination_register, 0, immediate
                    )  # n * 0 is 0
                else:
                    emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
                    pseudo_op.multiply_instruction(
                        opcode, destination_register, source_register, 1
                    )
        case Tok.MFC1_D_PSEUDO_OP | Tok.MTC1_D_PSEUDO_OP:
            real = (
                Tok.MFC1_OPCODE if opcode == Tok.MFC1_D_PSEUDO_OP else Tok.MTC1_OPCODE
            )
            line: int = scanner.line_no  # the last operand can consume the newline
            register_number: int = _parse_register()
            coprocessor_register: int = _parse_coprocessor_register_operand()
            if 31 in (
                register_number,
                coprocessor_register,
            ):  # the pair's second register would be 32
                _parse_error_at("Register pair starts at the last register (31)", line)
                return
            emit_floating_point_r(real, 0, coprocessor_register, register_number)
            emit_floating_point_r(
                real, 0, coprocessor_register + 1, register_number + 1
            )
        case Tok.LI_D_PSEUDO_OP | Tok.LI_S_PSEUDO_OP:
            _expand_li_floating_point(opcode)
        case Tok.L_D_PSEUDO_OP | Tok.L_S_PSEUDO_OP | Tok.S_D_PSEUDO_OP | Tok.S_S_PSEUDO_OP:  # fmt: skip
            floating_point_memory_opcodes: dict[int, int] = {
                Tok.L_D_PSEUDO_OP: Tok.LDC1_OPCODE,
                Tok.L_S_PSEUDO_OP: Tok.LWC1_OPCODE,
                Tok.S_D_PSEUDO_OP: Tok.SDC1_OPCODE,
                Tok.S_S_PSEUDO_OP: Tok.SWC1_OPCODE,
            }
            real = floating_point_memory_opcodes[opcode]
            floating_point_addend: int = _parse_floating_point_register()
            address: AddressExpression = _parse_address()
            emit_i(real, floating_point_addend, address.register_no, address.immediate)
        case Tok.LD_PSEUDO_OP | Tok.SD_PSEUDO_OP:
            real = Tok.LW_OPCODE if opcode == Tok.LD_PSEUDO_OP else Tok.SW_OPCODE
            line = scanner.line_no  # the address can consume the newline
            target_register = _parse_register()
            address = _parse_address()
            if target_register == 31:  # the pair's second register would be 32
                _parse_error_at("Register pair starts at the last register (31)", line)
                return
            emit_i(real, target_register, address.register_no, address.immediate)
            emit_i(
                real,
                target_register + 1,
                address.register_no,
                instruction_module.increment_expression_offset(address.immediate, 4),
            )
        case Tok.ULW_PSEUDO_OP | Tok.USW_PSEUDO_OP:
            left: int = (
                Tok.LWL_OPCODE if opcode == Tok.ULW_PSEUDO_OP else Tok.SWL_OPCODE
            )
            right: int = (
                Tok.LWR_OPCODE if opcode == Tok.ULW_PSEUDO_OP else Tok.SWR_OPCODE
            )
            target_register = _parse_register()
            address = _parse_address()
            emit_i(
                left,
                target_register,
                address.register_no,
                instruction_module.increment_expression_offset(address.immediate, 3),
            )
            emit_i(right, target_register, address.register_no, address.immediate)
        case Tok.ULH_PSEUDO_OP | Tok.ULHU_PSEUDO_OP:
            high_load: int = (
                Tok.LB_OPCODE if opcode == Tok.ULH_PSEUDO_OP else Tok.LBU_OPCODE
            )
            target_register = _parse_register()
            address = _parse_address()
            emit_i(
                high_load,
                target_register,
                address.register_no,
                instruction_module.increment_expression_offset(address.immediate, 1),
            )
            emit_i(Tok.LBU_OPCODE, 1, address.register_no, address.immediate)
            emit_r_shift(Tok.SLL_OPCODE, target_register, target_register, 8)
            emit_r(Tok.OR_OPCODE, target_register, target_register, 1)
        case Tok.USH_PSEUDO_OP:
            target_register = _parse_register()
            address = _parse_address()
            emit_i(
                Tok.SB_OPCODE, target_register, address.register_no, address.immediate
            )
            emit_r_shift(Tok.SLL_OPCODE, 1, target_register, 24)
            emit_r_shift(Tok.SRL_OPCODE, target_register, target_register, 8)
            emit_r(Tok.OR_OPCODE, target_register, target_register, 1)
            emit_i(
                Tok.SB_OPCODE,
                target_register,
                address.register_no,
                instruction_module.increment_expression_offset(address.immediate, 1),
            )
            emit_r_shift(Tok.SRL_OPCODE, 1, target_register, 24)
            emit_r_shift(Tok.SLL_OPCODE, target_register, target_register, 8)
            emit_r(Tok.OR_OPCODE, target_register, target_register, 1)
        case Tok.LA_PSEUDO_OP:
            target_register = _parse_register()
            address = _parse_address()
            if address.register_no:
                emit_i(
                    Tok.ADDI_OPCODE,
                    target_register,
                    address.register_no,
                    address.immediate,
                )
            else:
                emit_i(Tok.ORI_OPCODE, target_register, 0, address.immediate)
        case Tok.BEQZ_PSEUDO_OP | Tok.BNEZ_PSEUDO_OP:
            source_register = _parse_register()
            real = Tok.BEQ_OPCODE if opcode == Tok.BEQZ_PSEUDO_OP else Tok.BNE_OPCODE
            emit_i(real, 0, source_register, _parse_label())
        case Tok.B_PSEUDO_OP | Tok.BAL_PSEUDO_OP:
            real = Tok.BGEZAL_OPCODE if opcode == Tok.BAL_PSEUDO_OP else Tok.BGEZ_OPCODE
            emit_i(real, 0, 0, _parse_label())
        case Tok.BGT_PSEUDO_OP | Tok.BGTU_PSEUDO_OP | Tok.BGE_PSEUDO_OP | Tok.BGEU_PSEUDO_OP | Tok.BLT_PSEUDO_OP | Tok.BLTU_PSEUDO_OP | Tok.BLE_PSEUDO_OP | Tok.BLEU_PSEUDO_OP:  # fmt: skip
            _expand_compare_branch(opcode)
        case _:
            _parse_error_at("Pseudo-op not yet supported")
            _sync_to_newline()


def _expand_rotate(opcode: int) -> None:
    """``ror``/``rol`` by a register or by a constant."""
    is_right: bool = opcode == Tok.ROR_PSEUDO_OP
    destination_register: int = _parse_register()
    source_register: int = _parse_register()
    if scanner.peek() == TOKEN_REGISTER:
        target_register: int = _parse_register()
        emit_r(Tok.SUBU_OPCODE, 1, 0, target_register)
        if is_right:
            emit_r(Tok.SLLV_OPCODE, 1, 1, source_register)
            emit_r(
                Tok.SRLV_OPCODE, destination_register, target_register, source_register
            )
        else:
            emit_r(Tok.SRLV_OPCODE, 1, 1, source_register)
            emit_r(
                Tok.SLLV_OPCODE, destination_register, target_register, source_register
            )
        emit_r(Tok.OR_OPCODE, destination_register, destination_register, 1)
        return
    line: int = scanner.line_no  # as in _parse_immediate_16
    immediate: ImmediateExpression = _parse_immediate_32()
    distance: int = instruction_module.evaluate_immediate_expression(immediate)
    pseudo_op.check_immediate_range(immediate, 0, 31, line)
    first: int = Tok.SLL_OPCODE if is_right else Tok.SRL_OPCODE
    second: int = Tok.SRL_OPCODE if is_right else Tok.SLL_OPCODE
    emit_r_shift(first, 1, source_register, -distance)
    emit_r_shift(second, destination_register, source_register, distance)
    emit_r(Tok.OR_OPCODE, destination_register, destination_register, 1)


def _expand_set(opcode: int) -> None:
    """``sle sleu sgt sgtu sge sgeu seq sne rd, rs, (rt | imm)``."""
    destination_register: int = _parse_register()
    source_register: int = _parse_register()
    target_register: int
    if scanner.peek() == TOKEN_REGISTER:
        target_register = _parse_register()
    else:
        immediate: ImmediateExpression = _parse_immediate_32()
        if instruction_module.is_zero_immediate(immediate):
            target_register = 0
        else:
            emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
            target_register = 1
    match opcode:
        case Tok.SLE_PSEUDO_OP | Tok.SLEU_PSEUDO_OP:
            pseudo_op.set_less_or_equal_instruction(
                opcode, destination_register, source_register, target_register
            )
        case Tok.SGT_PSEUDO_OP | Tok.SGTU_PSEUDO_OP:
            pseudo_op.set_greater_than_instruction(
                opcode, destination_register, source_register, target_register
            )
        case Tok.SGE_PSEUDO_OP | Tok.SGEU_PSEUDO_OP:
            pseudo_op.set_greater_or_equal_instruction(
                opcode, destination_register, source_register, target_register
            )
        case Tok.SEQ_PSEUDO_OP | Tok.SNE_PSEUDO_OP:
            pseudo_op.set_equal_instruction(
                opcode, destination_register, source_register, target_register
            )
        case _:
            raise AssertionError(
                f"_expand_set: op {opcode} is not a set pseudo-op; only they reach here"
            )


def _expand_li_floating_point(opcode: int) -> None:
    """``li.d``/``li.s fd, FLOAT``: build the bits in $at, move with mtc1."""
    floating_point_destination: int = _parse_floating_point_register()
    if scanner.peek() != TOKEN_FLOATING_POINT:
        _parse_error_at("Expected FP literal")
        return
    scanner.advance()
    value: int | float | str = scanner.scan_value
    if not isinstance(value, float):
        raise TypeError(f"_expand_li_fp: expected float, got {type(value).__name__}")
    if opcode == Tok.LI_D_PSEUDO_OP:
        low: int
        high: int
        low, high = machine.double_words(value)
        emit_i(
            Tok.ORI_OPCODE, 1, 0, instruction_module.constant_immediate_expression(low)
        )
        emit_floating_point_r(Tok.MTC1_OPCODE, 0, floating_point_destination, 1)
        emit_i(
            Tok.ORI_OPCODE, 1, 0, instruction_module.constant_immediate_expression(high)
        )
        emit_floating_point_r(Tok.MTC1_OPCODE, 0, floating_point_destination + 1, 1)
    else:
        emit_i(
            Tok.ORI_OPCODE,
            1,
            0,
            instruction_module.constant_immediate_expression(machine.float_bits(value)),
        )
        emit_floating_point_r(Tok.MTC1_OPCODE, 0, floating_point_destination, 1)


def _expand_compare_branch(opcode: int) -> None:
    """``bgt bgtu bge bgeu blt bltu ble bleu rs, (rt | imm), label``."""
    source_register: int = _parse_register()
    signed: bool = opcode in (
        Tok.BGT_PSEUDO_OP, Tok.BGE_PSEUDO_OP, Tok.BLT_PSEUDO_OP, Tok.BLE_PSEUDO_OP
    )  # fmt: skip
    slt: int = Tok.SLT_OPCODE if signed else Tok.SLTU_OPCODE
    slti: int = Tok.SLTI_OPCODE if signed else Tok.SLTIU_OPCODE
    greater: bool = opcode in (Tok.BGT_PSEUDO_OP, Tok.BGTU_PSEUDO_OP, Tok.BLE_PSEUDO_OP,
                           Tok.BLEU_PSEUDO_OP)  # fmt: skip
    # bgt/ble branch on $at != 0 / == 0 after slt $at, rt, rs; bge/blt after
    # slt $at, rs, rt.
    on_set: bool = opcode in (Tok.BGT_PSEUDO_OP, Tok.BGTU_PSEUDO_OP, Tok.BLT_PSEUDO_OP,
                          Tok.BLTU_PSEUDO_OP)  # fmt: skip
    branch: int = Tok.BNE_OPCODE if on_set else Tok.BEQ_OPCODE
    if scanner.peek() == TOKEN_REGISTER:
        target_register: int = _parse_register()
        target: ImmediateExpression = _parse_label()
        if greater:
            emit_r(slt, 1, target_register, source_register)
        else:
            emit_r(slt, 1, source_register, target_register)
        emit_i(branch, 0, 1, target)
        return
    immediate: ImmediateExpression = _parse_immediate_32()
    target = _parse_label()
    match opcode:
        case Tok.BGE_PSEUDO_OP | Tok.BGEU_PSEUDO_OP | Tok.BLT_PSEUDO_OP | Tok.BLTU_PSEUDO_OP:  # fmt: skip
            emit_i(slti, 1, source_register, immediate)
            emit_i(branch, 0, 1, target)
        case Tok.BGT_PSEUDO_OP | Tok.BLE_PSEUDO_OP:
            emit_i(
                Tok.SLTI_OPCODE,
                1,
                source_register,
                instruction_module.increment_expression_offset(immediate, 1),
            )
            emit_i(
                Tok.BEQ_OPCODE if opcode == Tok.BGT_PSEUDO_OP else Tok.BNE_OPCODE,
                0,
                1,
                target,
            )
        case Tok.BGTU_PSEUDO_OP:
            emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
            emit_i(Tok.BEQ_OPCODE, source_register, 1, pseudo_op.branch_offset(3))
            emit_r(Tok.SLTU_OPCODE, 1, source_register, 1)
            emit_i(Tok.BEQ_OPCODE, 0, 1, target)
        case Tok.BLEU_PSEUDO_OP:
            emit_i(Tok.ORI_OPCODE, 1, 0, immediate)
            emit_i(Tok.BEQ_OPCODE, source_register, 1, target)
            emit_r(Tok.SLTU_OPCODE, 1, source_register, 1)
            emit_i(Tok.BNE_OPCODE, 0, 1, target)
        case _:
            raise AssertionError(
                f"_expand_compare_branch: op {opcode} is not a compare-branch pseudo-op;"
                " only they reach here"
            )


# --- directives -------------------------------------------------------------------


def _parse_segment(kind: Kind) -> None:
    """``.text/.data/.ktext/.kdata [ADDR]``."""
    has_address: bool = False
    address: int = 0
    if scanner.peek() == TOKEN_INTEGER:
        scanner.advance()
        has_address = True
        address = machine.u32(_integer_value())
    _emit_directive_segment(kind, has_address, address)


def _parse_named_size(kind: Kind) -> None:
    """``.extern NAME SIZE`` / ``.comm NAME SIZE``."""
    scanner.force_identifier()
    if scanner.peek() != TOKEN_IDENTIFIER:
        _parse_error_at("Expected ID")
        return
    scanner.advance()
    name: str = _string_value()
    size: int = _parse_expression()
    _append(ast_nodes.make(kind, name=name, number=size))


def _parse_expression_list() -> None:
    """The comma- (really white-space-) separated values of .word etc."""
    while True:
        errors_before: int = pseudo_op.parse_errors_seen
        value: int = _parse_expression()
        if pseudo_op.parse_errors_seen != errors_before:
            # The bad token is still there: skip the line, don't loop on it.
            _sync_to_newline()
            break
        _data_integer_push(value)
        if scanner.peek() in (TOKEN_NEWLINE, TOKEN_EOF):
            break
    _data_integer_finalize()


def _align_labels_to(alignment: int) -> None:
    """Move the line's labels to where the alignment will put the data."""
    if not data_directive or not _auto_align or not _this_line_labels:
        return
    fix_current_label_address(
        (data.current_data_location() + (1 << alignment) - 1)
        & ((0xFFFFFFFF << alignment) & 0xFFFFFFFF)
    )


def _parse_floating_point_expression_list() -> None:
    """The floating-point literals of .float/.double."""
    while True:
        if scanner.peek() != TOKEN_FLOATING_POINT:
            _parse_error_at("Expected floating-point literal")
            return
        scanner.advance()
        value: int | float | str = scanner.scan_value
        if not isinstance(value, float):
            raise TypeError(
                f"_parse_fp_expr_list: expected float, got {type(value).__name__}"
            )
        _data_doubles.append(value)
        if scanner.peek() in (TOKEN_NEWLINE, TOKEN_EOF):
            break
    _data_floating_point_finalize()


def _parse_aligned_list(kind: Kind, alignment: int) -> None:
    """.word/.half/.float/.double: align now (as the C parser does), then parse."""
    _align_labels_to(alignment)
    if data_directive:
        data.set_data_alignment(alignment)
    if kind in (Kind.DATA_FLOAT, Kind.DATA_DOUBLE):
        _data_floating_point_begin(kind)
        _parse_floating_point_expression_list()
    else:
        _data_integer_begin(kind)
        _parse_expression_list()


def _parse_string_list() -> None:
    """The strings of .ascii/.asciiz."""
    while True:
        if scanner.peek() != TOKEN_STRING:
            _parse_error_at("Expected string literal")
            return
        scanner.advance()
        # A \000 escape ends the string: C measures it with strlen.
        text: bytes = console.to_bytes(_string_value()).split(b"\0", 1)[0]
        if text_directive:
            _parse_error_at("Can't put data in text segment")
        else:
            _append(
                ast_nodes.make(Kind.DATA_STRING, string=text, null_terminate=_null_term)
            )
        if scanner.peek() in (TOKEN_NEWLINE, TOKEN_EOF):
            break


_IGNORED_DIRECTIVES: frozenset[int] = frozenset((
        Tok.FILE_DIRECTIVE, Tok.LOC_DIRECTIVE, Tok.FRAME_DIRECTIVE, Tok.MASK_DIRECTIVE,
        Tok.FMASK_DIRECTIVE, Tok.ENT_DIRECTIVE, Tok.END_DIRECTIVE, Tok.LABEL_DIRECTIVE,
        Tok.LIVEREG_DIRECTIVE, Tok.OPTIONS_DIRECTIVE, Tok.BGNB_DIRECTIVE, Tok.ENDB_DIRECTIVE,
        Tok.ENDR_DIRECTIVE, Tok.ASM0_DIRECTIVE, Tok.ALIAS_DIRECTIVE, Tok.SET_DIRECTIVE,
    ))  # fmt: skip


def _parse_directive(token: int) -> None:
    """One assembler directive."""
    global _null_term, _auto_align
    match token:
        case Tok.DATA_DIRECTIVE:
            _parse_segment(Kind.DIRECTIVE_DATA)
        case Tok.K_DATA_DIRECTIVE:
            _parse_segment(Kind.DIRECTIVE_KDATA)
        case Tok.TEXT_DIRECTIVE:
            _parse_segment(Kind.DIRECTIVE_TEXT)
        case Tok.K_TEXT_DIRECTIVE:
            _parse_segment(Kind.DIRECTIVE_KTEXT)
        case Tok.GLOBAL_DIRECTIVE:
            scanner.force_identifier()
            if scanner.peek() != TOKEN_IDENTIFIER:
                _parse_error_at("Expected identifier after .globl")
                return
            scanner.advance()
            _append(ast_nodes.make(Kind.DIRECTIVE_GLOBL, name=_string_value()))
        case Tok.WORD_DIRECTIVE:
            _parse_aligned_list(Kind.DATA_WORD, 2)
        case Tok.HALF_DIRECTIVE:
            _parse_aligned_list(Kind.DATA_HALF, 1)
        case Tok.BYTE_DIRECTIVE:
            _data_integer_begin(Kind.DATA_BYTE)
            _parse_expression_list()
        case Tok.ASCII_DIRECTIVE | Tok.ASCIIZ_DIRECTIVE:
            _null_term = token == Tok.ASCIIZ_DIRECTIVE
            _parse_string_list()
        case Tok.FLOAT_DIRECTIVE:
            _parse_aligned_list(Kind.DATA_FLOAT, 2)
        case Tok.DOUBLE_DIRECTIVE:
            _parse_aligned_list(Kind.DATA_DOUBLE, 3)
        case Tok.SPACE_DIRECTIVE:
            size: int = _parse_expression()
            if text_directive:
                _parse_error_at("Can't put data in text segment")
                return
            _append(ast_nodes.make(Kind.DIRECTIVE_SPACE, number=size))
        case Tok.ALIGN_DIRECTIVE:
            line: int = scanner.line_no  # the expression can consume the newline
            power: int = _parse_expression()
            if not 0 <= power <= 31:  # C shifts a 32-bit mask by it
                _parse_error_at("Alignment out of range (0 .. 31)", line)
                return
            if power == 0:
                _auto_align = False
            _append(ast_nodes.make(Kind.DIRECTIVE_ALIGN, number=power))
        case Tok.COMM_DIRECTIVE:
            _parse_named_size(Kind.DIRECTIVE_COMM)
        case Tok.EXTERN_DIRECTIVE:
            _parse_named_size(Kind.DIRECTIVE_EXTERN)
        case Tok.ERR_DIRECTIVE:
            _parse_error_at(".err directive")
        case _ if token in _IGNORED_DIRECTIVES:
            _sync_to_newline()
        case _:
            _parse_error_at("Directive not yet supported")
            _sync_to_newline()


# --- lines and files ----------------------------------------------------------------


def _parse_optional_label() -> None:
    """``NAME:`` or ``NAME = EXPR`` (the caller saw the ':' or '=')."""
    scanner.advance()
    name: str = _string_value()
    separator: int = scanner.advance()
    if separator == ord(":"):
        _append(ast_nodes.make(Kind.LABEL_DEFINITION, name=name))
    else:
        value: int = _parse_expression()
        _append(
            ast_nodes.make(
                Kind.LABEL_DEFINITION, name=name, is_constant=True, number=value
            )
        )


def _parse_line() -> None:
    """One line: optional label, then a directive or an instruction."""
    if scanner.peek() == TOKEN_NEWLINE:
        scanner.advance()
        return
    elif scanner.peek() == TOKEN_EOF:
        return
    else:
        if scanner.peek() == TOKEN_IDENTIFIER and scanner.peek2() in (
            ord(":"),
            ord("="),
        ):
            _parse_optional_label()
        token: int = scanner.peek()
        if token == TOKEN_NEWLINE:
            scanner.advance()
            return
        elif token == TOKEN_EOF:
            return
        else:
            if instruction_module.kind_of(token) == opcodes.ASSEMBLER_DIRECTIVE:
                _parse_directive(scanner.advance())
            else:
                _parse_assembly_code()
            _clear_labels()
            if pseudo_op.parse_error_occurred:
                _sync_to_newline()
            elif scanner.peek() == TOKEN_NEWLINE:
                scanner.advance()
            elif scanner.peek() != TOKEN_EOF:
                _parse_error_at("Extra tokens after instruction")
                _sync_to_newline()
            else:
                pass  # at TOKEN_EOF: there is no newline to consume


def parser_initialize(source: BinaryIO | None, file_name: str | None) -> None:
    """Get ready to assemble source (named file_name in messages)."""
    global input_file_name, data_directive, text_directive, _current_file
    scanner.initialize(source)
    input_file_name = file_name
    pseudo_op.parse_errors_seen = 0
    pseudo_op.parse_error_occurred = False
    data_directive = False
    text_directive = True
    _current_file = ast_nodes.make(Kind.FILE, name=file_name)


def parse_file() -> int:
    """Parse to the end of the input, print the requested views, and emit.

    Returns:
        int: the number of parse errors.
    """
    global data_directive, text_directive
    # Emit only what this call parses, starting in the segment that was
    # current when it began.  The REPL's `.` command parses one line onto the
    # tree of the file loaded last, whose nodes were emitted already; and the
    # parse moves data_directive/text_directive as it meets .data and .text, so the emit
    # pass must replay them from the start.
    already_emitted: int = len(_current_file.children) if _current_file else 0
    start_data_directive: bool = data_directive
    start_text_directive: bool = text_directive
    while scanner.peek() != TOKEN_EOF:
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
            data_directive = start_data_directive
            text_directive = start_text_directive
            emit_ast(_current_file, already_emitted)
    return pseudo_op.parse_errors_seen


# --- the emit pass --------------------------------------------------------------------


def emit_ast(file: Node, skip: int = 0) -> None:
    """Store the file's nodes after the first skip into memory, in order."""
    for node in file.children[skip:]:
        _emit_one(node)
        if node.kind not in (Kind.LABEL_DEFINITION, Kind.DIRECTIVE_GLOBL):
            _clear_labels()
    _clear_labels()
    instruction_module.emit_source_clear()


def _emit_data_integers(node: Node, store: Callable[[int], None], width: int) -> None:
    """Store a .byte/.half/.word list, recording forward references."""
    for expression in node.expressions:
        if expression.symbol is not None and expression.symbol.address == 0:
            symbol_table.record_data_uses_symbol(
                data.current_data_location(),
                expression.symbol,
                expression.offset,
                width,
            )
            store(expression.offset)
        else:
            store(instruction_module.evaluate_immediate_expression(expression))


def _emit_one(node: Node) -> None:
    """Store one node: the action the C engine's emit_one takes for its kind."""
    global data_directive, text_directive, _auto_align
    scanner.line_no = node.source_line
    instruction_module.emit_source_set(node.source_text)
    match node.kind:
        case Kind.DIRECTIVE_TEXT | Kind.DIRECTIVE_KTEXT:
            instruction_module.user_kernel_text_segment(
                node.kind == Kind.DIRECTIVE_KTEXT
            )
            data_directive = False
            text_directive = True
            if node.has_start_address:
                instruction_module.set_text_location(node.start_address)
        case Kind.DIRECTIVE_DATA | Kind.DIRECTIVE_KDATA:
            data.user_kernel_data_segment(node.kind == Kind.DIRECTIVE_KDATA)
            data_directive = True
            text_directive = False
            data.enable_data_alignment()
            _auto_align = True
            if node.has_start_address:
                data.set_data_location(node.start_address)
        case Kind.DIRECTIVE_ALIGN:
            if node.number == 0:
                _auto_align = False
            if text_directive:
                instruction_module.align_text(node.number)
            else:
                data.align_data(node.number)
        case Kind.DIRECTIVE_SPACE:
            data.increment_data_location(node.number)
        case Kind.DIRECTIVE_GLOBL:
            symbol_table.make_label_global(node.name or "")
        case Kind.DIRECTIVE_EXTERN | Kind.DIRECTIVE_COMM:
            name: str = node.name or ""
            if node.kind == Kind.DIRECTIVE_EXTERN:
                symbol_table.make_label_global(name)
            else:
                data.align_data(2)
            if symbol_table.lookup_label(name).address == 0:
                symbol_table.record_label(name, data.current_data_location(), True)
            data.increment_data_location(node.number)
        case Kind.DATA_BYTE:
            _emit_data_integers(node, data.store_byte, 1)
        case Kind.DATA_HALF | Kind.DATA_WORD | Kind.DATA_FLOAT | Kind.DATA_DOUBLE:
            alignment: int = {
                Kind.DATA_HALF: 1,
                Kind.DATA_WORD: 2,
                Kind.DATA_FLOAT: 2,
                Kind.DATA_DOUBLE: 3,
            }[node.kind]
            _align_labels_to(alignment)
            if data_directive:
                data.set_data_alignment(alignment)
            if node.kind == Kind.DATA_HALF:
                _emit_data_integers(node, data.store_half, 2)
            elif node.kind == Kind.DATA_WORD:
                _emit_data_integers(node, data.store_word, 4)
            else:
                store_floating_point: Callable[[float], None] = (
                    data.store_float
                    if node.kind == Kind.DATA_FLOAT
                    else data.store_double
                )
                for value in node.doubles:
                    store_floating_point(value)
        case Kind.DATA_STRING:
            if not text_directive:
                data.store_string(node.string, node.null_terminate)
        case Kind.LABEL_DEFINITION:
            if node.is_constant:
                label: symbol_table.Label = symbol_table.record_label(
                    node.name or "", machine.u32(node.number), True
                )
                label.constant_flag = True
            else:
                address: int = (
                    instruction_module.current_text_location()
                    if text_directive
                    else data.current_data_location()
                )
                _this_line_labels.insert(
                    0, symbol_table.record_label(node.name or "", address, False)
                )
        case Kind.INSTRUCTION_R:
            instruction_module.r_type(
                node.opcode,
                node.destination_register,
                node.source_register,
                node.target_register,
            )
        case Kind.INSTRUCTION_R_SHIFT:
            instruction_module.r_shift_amount_type(
                node.opcode,
                node.destination_register,
                node.target_register,
                node.shift_amount,
            )
        case Kind.INSTRUCTION_I:
            if node.immediate is None:
                raise AssertionError("_emit_one: node.imm is None")
            instruction_module.i_type(
                node.opcode,
                node.target_register,
                node.source_register,
                node.immediate.copy(),
            )
        case Kind.INSTRUCTION_J:
            if node.immediate is None:
                raise AssertionError("_emit_one: node.imm is None")
            instruction_module.j_type(node.opcode, node.immediate.copy())
        case Kind.INSTRUCTION_FLOATING_POINT_R:
            instruction_module.r_coprocessor_type(
                node.opcode,
                node.floating_point_destination,
                node.floating_point_source,
                node.floating_point_target,
            )
        case Kind.INSTRUCTION_FLOATING_POINT_COMPARE:
            instruction_module.r_condition_type(
                node.opcode,
                node.floating_point_source,
                node.floating_point_target,
                node.condition_code,
            )
        case Kind.PSEUDO:
            for child in node.children:
                _emit_one(child)
                if child.kind not in (Kind.LABEL_DEFINITION, Kind.DIRECTIVE_GLOBL):
                    _clear_labels()
        case Kind.FILE:
            raise AssertionError(
                "_emit_one: a FILE node is never a child; emit_ast walks its children"
            )
        case _:
            assert_never(node.kind)
