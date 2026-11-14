"""Pseudo-instruction expansions and parse-error reporting (pseudo-op.c).

A pseudo-instruction is assembler shorthand for one or more real
instructions; for example ``sle rd, rs, rt`` becomes::

    bne  rs, rt, +3     # if rs != rt, skip to the slt
    ori  rd, $0, 1      # rs == rt: rd = 1
    beq  $0, $0, +3     # and skip the slt
    nop
    slt  rd, rs, rt

Each helper below appends the real instructions to the AST through the
parser's ``emit_*`` functions.  ``branch_offset(n)`` is a branch n
instructions ahead.
"""

from . import console, machine, parser, scanner
from . import instruction as instruction_module
from .inst import ImmediateExpression
from .opcode_table import Tok

parse_error_occurred: bool = False
"""A parse error happened on the current line."""

parse_errors_seen: int = 0
"""How many parse errors the current file has had."""


def parse_warn(message: str) -> None:
    """Print a parser message with the offending line."""
    name: str = (
        parser.input_file_name if parser.input_file_name is not None else "(input)"
    )
    console.error(
        f"spim: (parser) {message} on line {scanner.line_no} of file {name}\n"
        f"{scanner.erroneous_line()}"
    )


def parse_error(message: str) -> None:
    """Report a parse error (counted: the program will not run)."""
    global parse_error_occurred, parse_errors_seen
    parse_error_occurred = True
    parse_errors_seen += 1
    parse_warn(message)


_IMMEDIATE_TO_OPCODE: dict[int, int] = {
    Tok.ADDI_OPCODE: Tok.ADD_OPCODE,
    Tok.ADDIU_OPCODE: Tok.ADDU_OPCODE,
    Tok.ANDI_OPCODE: Tok.AND_OPCODE,
    Tok.ORI_OPCODE: Tok.OR_OPCODE,
    Tok.XORI_OPCODE: Tok.XOR_OPCODE,
    Tok.SLTI_OPCODE: Tok.SLT_OPCODE,
    Tok.SLTIU_OPCODE: Tok.SLTU_OPCODE,
    Tok.J_OPCODE: Tok.JR_OPCODE,
    Tok.LUI_OPCODE: Tok.ADDU_OPCODE,
    Tok.SLL_OPCODE: Tok.SLLV_OPCODE,
    Tok.SRA_OPCODE: Tok.SRAV_OPCODE,
    Tok.SRL_OPCODE: Tok.SRLV_OPCODE,
}

_OPCODE_TO_IMMEDIATE: dict[int, int] = {
    Tok.ADD_OPCODE: Tok.ADDI_OPCODE,
    Tok.ADDU_OPCODE: Tok.ADDIU_OPCODE,
    Tok.AND_OPCODE: Tok.ANDI_OPCODE,
    Tok.OR_OPCODE: Tok.ORI_OPCODE,
    Tok.XOR_OPCODE: Tok.XORI_OPCODE,
    Tok.SLT_OPCODE: Tok.SLTI_OPCODE,
    Tok.SLTU_OPCODE: Tok.SLTIU_OPCODE,
    Tok.SLLV_OPCODE: Tok.SLL_OPCODE,
    Tok.SRAV_OPCODE: Tok.SRA_OPCODE,
    Tok.SRLV_OPCODE: Tok.SRL_OPCODE,
}


def immediate_opcode_to_opcode(opcode: int) -> int:
    """The register form of an immediate instruction (``addi`` -> ``add``).

    Raises:
        SystemExit: there is none (the C engine's fatal error).
    """
    if opcode not in _IMMEDIATE_TO_OPCODE:
        raise console.fatal_error("Can't convert immediate op to op\n")
    return _IMMEDIATE_TO_OPCODE[opcode]


def opcode_to_immediate_opcode(opcode: int) -> int:
    """The immediate form of a register instruction (``add`` -> ``addi``).

    Raises:
        SystemExit: there is none.
    """
    if opcode not in _OPCODE_TO_IMMEDIATE:
        raise console.fatal_error("Can't convert op to immediate op\n")
    return _OPCODE_TO_IMMEDIATE[opcode]


def nop_instruction() -> None:
    """Emit ``nop`` (``sll $0, $0, 0``)."""
    parser.emit_r(Tok.SLL_OPCODE, 0, 0, 0)


def trap_instruction() -> None:
    """Emit ``break 0``."""
    parser.emit_r(Tok.BREAK_OPCODE, 0, 0, 0)


def branch_offset(instruction_count: int) -> ImmediateExpression:
    """A branch displacement of instruction_count instructions."""
    return instruction_module.constant_immediate_expression(instruction_count << 2)


def divide_instruction(
    opcode: int,
    destination_register: int,
    source_register: int,
    target_register: int,
    constant_divisor: bool,
) -> None:
    """Expand 3-operand div/divu/rem/remu, trapping on a zero register divisor."""
    if destination_register != 0 and not constant_divisor:
        parser.emit_i(Tok.BNE_OPCODE, 0, target_register, branch_offset(3))
        nop_instruction()
        trap_instruction()
    signed: bool = opcode in (Tok.DIV_OPCODE, Tok.REM_PSEUDO_OP)
    parser.emit_r(
        Tok.DIV_OPCODE if signed else Tok.DIVU_OPCODE,
        0,
        source_register,
        target_register,
    )
    if destination_register != 0:
        quotient: bool = opcode in (Tok.DIV_OPCODE, Tok.DIVU_OPCODE)
        parser.emit_r(
            Tok.MFLO_OPCODE if quotient else Tok.MFHI_OPCODE, destination_register, 0, 0
        )


def multiply_instruction(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Expand mulo/mulou: multiply, and trap if the product overflows 32 bits."""
    unsigned: bool = opcode == Tok.MULOU_PSEUDO_OP
    parser.emit_r(
        Tok.MULTU_OPCODE if unsigned else Tok.MULT_OPCODE,
        0,
        source_register,
        target_register,
    )
    if unsigned and destination_register != 0:
        parser.emit_r(Tok.MFHI_OPCODE, 1, 0, 0)
        parser.emit_i(Tok.BEQ_OPCODE, 0, 1, branch_offset(3))
        nop_instruction()
        trap_instruction()
    elif opcode == Tok.MULO_PSEUDO_OP and destination_register != 0:
        parser.emit_r(Tok.MFHI_OPCODE, 1, 0, 0)
        parser.emit_r(Tok.MFLO_OPCODE, destination_register, 0, 0)
        parser.emit_r_shift(
            Tok.SRA_OPCODE, destination_register, destination_register, 31
        )
        parser.emit_i(Tok.BEQ_OPCODE, destination_register, 1, branch_offset(3))
        nop_instruction()
        trap_instruction()
    else:
        pass  # any other op, or a result into $zero: no overflow check
    if destination_register != 0:
        parser.emit_r(Tok.MFLO_OPCODE, destination_register, 0, 0)


def _set_compare(
    destination_register: int,
    source_register: int,
    target_register: int,
    slt: int,
    a: int,
    b: int,
) -> None:
    """The sle/sge pattern: rd = 1 if rs == rt, else ``slt rd, a, b``."""
    parser.emit_i(Tok.BNE_OPCODE, source_register, target_register, branch_offset(3))
    parser.emit_i(
        Tok.ORI_OPCODE,
        destination_register,
        0,
        instruction_module.constant_immediate_expression(1),
    )
    parser.emit_i(Tok.BEQ_OPCODE, 0, 0, branch_offset(3))
    nop_instruction()
    parser.emit_r(slt, destination_register, a, b)


def set_less_or_equal_instruction(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Expand sle/sleu."""
    slt: int = Tok.SLT_OPCODE if opcode == Tok.SLE_PSEUDO_OP else Tok.SLTU_OPCODE
    _set_compare(
        destination_register,
        source_register,
        target_register,
        slt,
        source_register,
        target_register,
    )


def set_greater_than_instruction(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Expand sgt/sgtu (slt with the operands swapped)."""
    slt: int = Tok.SLT_OPCODE if opcode == Tok.SGT_PSEUDO_OP else Tok.SLTU_OPCODE
    parser.emit_r(slt, destination_register, target_register, source_register)


def set_greater_or_equal_instruction(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Expand sge/sgeu."""
    slt: int = Tok.SLT_OPCODE if opcode == Tok.SGE_PSEUDO_OP else Tok.SLTU_OPCODE
    _set_compare(
        destination_register,
        source_register,
        target_register,
        slt,
        target_register,
        source_register,
    )


def set_equal_instruction(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Expand seq/sne."""
    equal: int = 1 if opcode == Tok.SEQ_PSEUDO_OP else 0
    parser.emit_i(Tok.BEQ_OPCODE, source_register, target_register, branch_offset(3))
    parser.emit_i(
        Tok.ORI_OPCODE,
        destination_register,
        0,
        instruction_module.constant_immediate_expression(1 - equal),
    )
    parser.emit_i(Tok.BEQ_OPCODE, 0, 0, branch_offset(3))
    nop_instruction()
    parser.emit_i(
        Tok.ORI_OPCODE,
        destination_register,
        0,
        instruction_module.constant_immediate_expression(equal),
    )


def check_immediate_range(
    expression: ImmediateExpression, low: int, high: int, line: int
) -> None:
    """Warn (not an error: the code is widened) if expr is outside low..high.

    ``line`` is the instruction's line: the scanner's line can already be the
    next one, as reading past the immediate scans the newline.
    """
    if expression.symbol is None or expression.symbol.address != 0:
        value: int = instruction_module.evaluate_immediate_expression(expression)
        if value < low or high < value:
            _range_warning(value, low, high, line)


def check_unsigned_immediate_range(
    expression: ImmediateExpression, low: int, high: int, line: int
) -> None:
    """Warn if expr's known value, read unsigned, is outside low..high (on line)."""
    if expression.symbol is None or expression.symbol.address != 0:
        value: int = machine.u32(
            instruction_module.evaluate_immediate_expression(expression)
        )
        if value < low or high < value:
            _range_warning(machine.s32(value), low, high, line)


def _range_warning(value: int, low: int, high: int, line: int) -> None:
    """The out-of-range immediate warning."""
    console.error(
        f"spim: (parser) immediate value ({value}) out of range"
        f" ({low} .. {high}) on line {line}\n"
    )
