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

from . import console, instruction, machine, parser, scanner
from .inst import ImmExpr
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


_IMM_TO_OP: dict[int, int] = {
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

_OP_TO_IMM: dict[int, int] = {
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


def imm_op_to_op(opcode: int) -> int:
    """The register form of an immediate instruction (``addi`` -> ``add``).

    Raises:
        SystemExit: there is none (the C engine's fatal error).
    """
    if opcode not in _IMM_TO_OP:
        raise console.fatal_error("Can't convert immediate op to op\n")
    return _IMM_TO_OP[opcode]


def op_to_imm_op(opcode: int) -> int:
    """The immediate form of a register instruction (``add`` -> ``addi``).

    Raises:
        SystemExit: there is none.
    """
    if opcode not in _OP_TO_IMM:
        raise console.fatal_error("Can't convert op to immediate op\n")
    return _OP_TO_IMM[opcode]


def nop_inst() -> None:
    """Emit ``nop`` (``sll $0, $0, 0``)."""
    parser.emit_r(Tok.SLL_OPCODE, 0, 0, 0)


def trap_inst() -> None:
    """Emit ``break 0``."""
    parser.emit_r(Tok.BREAK_OPCODE, 0, 0, 0)


def branch_offset(n_inst: int) -> ImmExpr:
    """A branch displacement of n_inst instructions."""
    return instruction.const_imm_expr(n_inst << 2)


def div_inst(op: int, rd: int, rs: int, rt: int, const_divisor: bool) -> None:
    """Expand 3-operand div/divu/rem/remu, trapping on a zero register divisor."""
    if rd != 0 and not const_divisor:
        parser.emit_i(Tok.BNE_OPCODE, 0, rt, branch_offset(3))
        nop_inst()
        trap_inst()
    signed: bool = op in (Tok.DIV_OPCODE, Tok.REM_PSEUDO_OP)
    parser.emit_r(Tok.DIV_OPCODE if signed else Tok.DIVU_OPCODE, 0, rs, rt)
    if rd != 0:
        quotient: bool = op in (Tok.DIV_OPCODE, Tok.DIVU_OPCODE)
        parser.emit_r(Tok.MFLO_OPCODE if quotient else Tok.MFHI_OPCODE, rd, 0, 0)


def mult_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand mulo/mulou: multiply, and trap if the product overflows 32 bits."""
    unsigned: bool = op == Tok.MULOU_PSEUDO_OP
    parser.emit_r(Tok.MULTU_OPCODE if unsigned else Tok.MULT_OPCODE, 0, rs, rt)
    if unsigned and rd != 0:
        parser.emit_r(Tok.MFHI_OPCODE, 1, 0, 0)
        parser.emit_i(Tok.BEQ_OPCODE, 0, 1, branch_offset(3))
        nop_inst()
        trap_inst()
    elif op == Tok.MULO_PSEUDO_OP and rd != 0:
        parser.emit_r(Tok.MFHI_OPCODE, 1, 0, 0)
        parser.emit_r(Tok.MFLO_OPCODE, rd, 0, 0)
        parser.emit_r_shift(Tok.SRA_OPCODE, rd, rd, 31)
        parser.emit_i(Tok.BEQ_OPCODE, rd, 1, branch_offset(3))
        nop_inst()
        trap_inst()
    else:
        pass  # any other op, or a result into $zero: no overflow check
    if rd != 0:
        parser.emit_r(Tok.MFLO_OPCODE, rd, 0, 0)


def _set_compare(rd: int, rs: int, rt: int, slt: int, a: int, b: int) -> None:
    """The sle/sge pattern: rd = 1 if rs == rt, else ``slt rd, a, b``."""
    parser.emit_i(Tok.BNE_OPCODE, rs, rt, branch_offset(3))
    parser.emit_i(Tok.ORI_OPCODE, rd, 0, instruction.const_imm_expr(1))
    parser.emit_i(Tok.BEQ_OPCODE, 0, 0, branch_offset(3))
    nop_inst()
    parser.emit_r(slt, rd, a, b)


def set_le_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sle/sleu."""
    slt: int = Tok.SLT_OPCODE if op == Tok.SLE_PSEUDO_OP else Tok.SLTU_OPCODE
    _set_compare(rd, rs, rt, slt, rs, rt)


def set_gt_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sgt/sgtu (slt with the operands swapped)."""
    slt: int = Tok.SLT_OPCODE if op == Tok.SGT_PSEUDO_OP else Tok.SLTU_OPCODE
    parser.emit_r(slt, rd, rt, rs)


def set_ge_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sge/sgeu."""
    slt: int = Tok.SLT_OPCODE if op == Tok.SGE_PSEUDO_OP else Tok.SLTU_OPCODE
    _set_compare(rd, rs, rt, slt, rt, rs)


def set_eq_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand seq/sne."""
    equal: int = 1 if op == Tok.SEQ_PSEUDO_OP else 0
    parser.emit_i(Tok.BEQ_OPCODE, rs, rt, branch_offset(3))
    parser.emit_i(Tok.ORI_OPCODE, rd, 0, instruction.const_imm_expr(1 - equal))
    parser.emit_i(Tok.BEQ_OPCODE, 0, 0, branch_offset(3))
    nop_inst()
    parser.emit_i(Tok.ORI_OPCODE, rd, 0, instruction.const_imm_expr(equal))


def check_imm_range(expr: ImmExpr, low: int, high: int, line: int) -> None:
    """Warn (not an error: the code is widened) if expr is outside low..high.

    ``line`` is the instruction's line: the scanner's line can already be the
    next one, as reading past the immediate scans the newline.
    """
    if expr.symbol is None or expr.symbol.addr != 0:
        value: int = instruction.eval_imm_expr(expr)
        if value < low or high < value:
            _range_warning(value, low, high, line)


def check_uimm_range(expr: ImmExpr, low: int, high: int, line: int) -> None:
    """Warn if expr's known value, read unsigned, is outside low..high (on line)."""
    if expr.symbol is None or expr.symbol.addr != 0:
        value: int = machine.u32(instruction.eval_imm_expr(expr))
        if value < low or high < value:
            _range_warning(machine.s32(value), low, high, line)


def _range_warning(value: int, low: int, high: int, line: int) -> None:
    """The out-of-range immediate warning."""
    console.error(
        f"spim: (parser) immediate value ({value}) out of range"
        f" ({low} .. {high}) on line {line}\n"
    )
