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

from . import console, instruction, machine, opcodes, parser, scanner
from .inst import ImmExpr

TOK: dict[str, int] = opcodes.TOKEN

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


_IMM_TO_OP: dict[str, str] = {
    "ADDI_OPCODE": "ADD_OPCODE",
    "ADDIU_OPCODE": "ADDU_OPCODE",
    "ANDI_OPCODE": "AND_OPCODE",
    "ORI_OPCODE": "OR_OPCODE",
    "XORI_OPCODE": "XOR_OPCODE",
    "SLTI_OPCODE": "SLT_OPCODE",
    "SLTIU_OPCODE": "SLTU_OPCODE",
    "J_OPCODE": "JR_OPCODE",
    "LUI_OPCODE": "ADDU_OPCODE",
    "SLL_OPCODE": "SLLV_OPCODE",
    "SRA_OPCODE": "SRAV_OPCODE",
    "SRL_OPCODE": "SRLV_OPCODE",
}
_IMM_TO_OP_TOKENS: dict[int, int] = {TOK[a]: TOK[b] for a, b in _IMM_TO_OP.items()}

_OP_TO_IMM: dict[str, str] = {
    "ADD_OPCODE": "ADDI_OPCODE",
    "ADDU_OPCODE": "ADDIU_OPCODE",
    "AND_OPCODE": "ANDI_OPCODE",
    "OR_OPCODE": "ORI_OPCODE",
    "XOR_OPCODE": "XORI_OPCODE",
    "SLT_OPCODE": "SLTI_OPCODE",
    "SLTU_OPCODE": "SLTIU_OPCODE",
    "SLLV_OPCODE": "SLL_OPCODE",
    "SRAV_OPCODE": "SRA_OPCODE",
    "SRLV_OPCODE": "SRL_OPCODE",
}
_OP_TO_IMM_TOKENS: dict[int, int] = {TOK[a]: TOK[b] for a, b in _OP_TO_IMM.items()}


def imm_op_to_op(opcode: int) -> int:
    """The register form of an immediate instruction (``addi`` -> ``add``).

    Raises:
        SystemExit: there is none (the C engine's fatal error).
    """
    if opcode not in _IMM_TO_OP_TOKENS:
        raise console.fatal_error("Can't convert immediate op to op\n")
    return _IMM_TO_OP_TOKENS[opcode]


def op_to_imm_op(opcode: int) -> int:
    """The immediate form of a register instruction (``add`` -> ``addi``).

    Raises:
        SystemExit: there is none.
    """
    if opcode not in _OP_TO_IMM_TOKENS:
        raise console.fatal_error("Can't convert op to immediate op\n")
    return _OP_TO_IMM_TOKENS[opcode]


def nop_inst() -> None:
    """Emit ``nop`` (``sll $0, $0, 0``)."""
    parser.emit_r(TOK["SLL_OPCODE"], 0, 0, 0)


def trap_inst() -> None:
    """Emit ``break 0``."""
    parser.emit_r(TOK["BREAK_OPCODE"], 0, 0, 0)


def branch_offset(n_inst: int) -> ImmExpr:
    """A branch displacement of n_inst instructions."""
    return instruction.const_imm_expr(n_inst << 2)


def div_inst(op: int, rd: int, rs: int, rt: int, const_divisor: bool) -> None:
    """Expand 3-operand div/divu/rem/remu, trapping on a zero register divisor."""
    if rd != 0 and not const_divisor:
        parser.emit_i(TOK["BNE_OPCODE"], 0, rt, branch_offset(3))
        nop_inst()
        trap_inst()
    signed: bool = op in (TOK["DIV_OPCODE"], TOK["REM_PSEUDO_OP"])
    parser.emit_r(TOK["DIV_OPCODE"] if signed else TOK["DIVU_OPCODE"], 0, rs, rt)
    if rd != 0:
        quotient: bool = op in (TOK["DIV_OPCODE"], TOK["DIVU_OPCODE"])
        parser.emit_r(TOK["MFLO_OPCODE"] if quotient else TOK["MFHI_OPCODE"], rd, 0, 0)


def mult_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand mulo/mulou: multiply, and trap if the product overflows 32 bits."""
    unsigned: bool = op == TOK["MULOU_PSEUDO_OP"]
    parser.emit_r(TOK["MULTU_OPCODE"] if unsigned else TOK["MULT_OPCODE"], 0, rs, rt)
    if unsigned and rd != 0:
        parser.emit_r(TOK["MFHI_OPCODE"], 1, 0, 0)
        parser.emit_i(TOK["BEQ_OPCODE"], 0, 1, branch_offset(3))
        nop_inst()
        trap_inst()
    elif op == TOK["MULO_PSEUDO_OP"] and rd != 0:
        parser.emit_r(TOK["MFHI_OPCODE"], 1, 0, 0)
        parser.emit_r(TOK["MFLO_OPCODE"], rd, 0, 0)
        parser.emit_r_shift(TOK["SRA_OPCODE"], rd, rd, 31)
        parser.emit_i(TOK["BEQ_OPCODE"], rd, 1, branch_offset(3))
        nop_inst()
        trap_inst()
    if rd != 0:
        parser.emit_r(TOK["MFLO_OPCODE"], rd, 0, 0)


def _set_compare(rd: int, rs: int, rt: int, slt: int, a: int, b: int) -> None:
    """The sle/sge pattern: rd = 1 if rs == rt, else ``slt rd, a, b``."""
    parser.emit_i(TOK["BNE_OPCODE"], rs, rt, branch_offset(3))
    parser.emit_i(TOK["ORI_OPCODE"], rd, 0, instruction.const_imm_expr(1))
    parser.emit_i(TOK["BEQ_OPCODE"], 0, 0, branch_offset(3))
    nop_inst()
    parser.emit_r(slt, rd, a, b)


def set_le_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sle/sleu."""
    slt: int = TOK["SLT_OPCODE"] if op == TOK["SLE_PSEUDO_OP"] else TOK["SLTU_OPCODE"]
    _set_compare(rd, rs, rt, slt, rs, rt)


def set_gt_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sgt/sgtu (slt with the operands swapped)."""
    slt: int = TOK["SLT_OPCODE"] if op == TOK["SGT_PSEUDO_OP"] else TOK["SLTU_OPCODE"]
    parser.emit_r(slt, rd, rt, rs)


def set_ge_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand sge/sgeu."""
    slt: int = TOK["SLT_OPCODE"] if op == TOK["SGE_PSEUDO_OP"] else TOK["SLTU_OPCODE"]
    _set_compare(rd, rs, rt, slt, rt, rs)


def set_eq_inst(op: int, rd: int, rs: int, rt: int) -> None:
    """Expand seq/sne."""
    equal: int = 1 if op == TOK["SEQ_PSEUDO_OP"] else 0
    parser.emit_i(TOK["BEQ_OPCODE"], rs, rt, branch_offset(3))
    parser.emit_i(TOK["ORI_OPCODE"], rd, 0, instruction.const_imm_expr(1 - equal))
    parser.emit_i(TOK["BEQ_OPCODE"], 0, 0, branch_offset(3))
    nop_inst()
    parser.emit_i(TOK["ORI_OPCODE"], rd, 0, instruction.const_imm_expr(equal))


def check_imm_range(expr: ImmExpr, low: int, high: int) -> None:
    """Warn (not an error: the code is widened) if expr is outside low..high."""
    if expr.symbol is None or expr.symbol.addr != 0:
        value: int = instruction.eval_imm_expr(expr)
        if value < low or high < value:
            _range_warning(value, low, high)


def check_uimm_range(expr: ImmExpr, low: int, high: int) -> None:
    """Warn if expr's known value, read unsigned, is outside low..high."""
    if expr.symbol is None or expr.symbol.addr != 0:
        value: int = machine.u32(instruction.eval_imm_expr(expr))
        if value < low or high < value:
            _range_warning(machine.s32(value), low, high)


def _range_warning(value: int, low: int, high: int) -> None:
    """The out-of-range immediate warning."""
    console.error(
        f"spim: (parser) immediate value ({value}) out of range"
        f" ({low} .. {high}) on line {scanner.line_no}\n"
    )
