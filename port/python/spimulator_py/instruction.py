"""Building, encoding, decoding and printing instructions (instruction.c).

Three jobs, as in the C file:

* the assembler's back end: ``r_type_inst``, ``i_type_inst``, ... build an
  :class:`Instruction` and store it at the next text address.  An immediate
  that does not fit in 16 bits is synthesized with ``lui $at`` + ``ori``,
  exactly as the C engine does (so the addresses of everything after it
  match);
* ``inst_encode`` / ``inst_decode`` convert between an Instruction and its
  32-bit machine word;
* ``format_an_inst`` prints one line of disassembly.
"""

from . import (
    assembler_event,
    console,
    data,
    machine,
    memory,
    opcodes,
    options,
    parser,
    pseudo_op,
    scanner,
    spim_utils,
    symbol_table,
    traps,
)
from . import registers as regs
from .inst import AddrExpr, ImmExpr, Instruction
from .opcodes import KIND

TOK: dict[str, int] = opcodes.TOKEN

# Operand-shape tags (opcode-types.h), named once for the switches below.
BRANCH_COPROC_COND_INST: int = KIND["BRANCH_COPROC_COND_INST"]
BRANCH_RS_LABEL_INST: int = KIND["BRANCH_RS_LABEL_INST"]
RS_IMM_INST: int = KIND["RS_IMM_INST"]
RT_IMM_INST: int = KIND["RT_IMM_INST"]
RT_RS_IMM_INST: int = KIND["RT_RS_IMM_INST"]
BRANCH_RS_RT_LABEL_INST: int = KIND["BRANCH_RS_RT_LABEL_INST"]
RT_ADDR_INST: int = KIND["RT_ADDR_INST"]
RS_INST: int = KIND["RS_INST"]
RD_INST: int = KIND["RD_INST"]
RS_RT_INST: int = KIND["RS_RT_INST"]
RD_RS_INST: int = KIND["RD_RS_INST"]
RT_COPREG_INST: int = KIND["RT_COPREG_INST"]
RD_RT_SHAMT_INST: int = KIND["RD_RT_SHAMT_INST"]
RD_RS_RT_INST: int = KIND["RD_RS_RT_INST"]
RD_RT_RS_INST: int = KIND["RD_RT_RS_INST"]
FP_FT_ADDR_INST: int = KIND["FP_FT_ADDR_INST"]
FP_FD_FS_INST: int = KIND["FP_FD_FS_INST"]
FP_RT_COPREG_INST: int = KIND["FP_RT_COPREG_INST"]
FP_CMP_FS_FT_INST: int = KIND["FP_CMP_FS_FT_INST"]
FP_FD_FS_FT_INST: int = KIND["FP_FD_FS_FT_INST"]
FP_FD_FR_FS_FT_INST: int = KIND["FP_FD_FR_FS_FT_INST"]
FP_MOVC_TYPE_INST: int = KIND["FP_MOVC_TYPE_INST"]
MOVC_TYPE_INST: int = KIND["MOVC_TYPE_INST"]
J_TYPE_INST: int = KIND["J_TYPE_INST"]
NOARG_TYPE_INST: int = KIND["NOARG_TYPE_INST"]

IMM_MIN: int = -0x8000
IMM_MAX: int = 0x7FFF
UIMM_MIN: int = 0
UIMM_MAX: int = 0xFFFF

COND_UN: int = 0x1
COND_EQ: int = 0x2
COND_LT: int = 0x4
COND_IN: int = 0x8


def _tokens(*names: str) -> frozenset[int]:
    """The set of tokens with these C names (without ``TOK_``)."""
    return frozenset(TOK[name] for name in names)


BRANCH_OPCODES: frozenset[int] = _tokens(
    "BC1F_OPCODE", "BC1FL_OPCODE", "BC1T_OPCODE", "BC1TL_OPCODE",
    "BC2F_OPCODE", "BC2FL_OPCODE", "BC2T_OPCODE", "BC2TL_OPCODE",
    "BEQ_OPCODE", "BEQL_OPCODE", "BEQZ_PSEUDO_OP", "BGE_PSEUDO_OP",
    "BGEU_PSEUDO_OP", "BGEZ_OPCODE", "BGEZAL_OPCODE", "BGEZALL_OPCODE",
    "BGEZL_OPCODE", "BGT_PSEUDO_OP", "BGTU_PSEUDO_OP", "BGTZ_OPCODE",
    "BGTZL_OPCODE", "BLE_PSEUDO_OP", "BLEU_PSEUDO_OP", "BLEZ_OPCODE",
    "BLEZL_OPCODE", "BLT_PSEUDO_OP", "BLTU_PSEUDO_OP", "BLTZ_OPCODE",
    "BLTZAL_OPCODE", "BLTZALL_OPCODE", "BLTZL_OPCODE", "BNE_OPCODE",
    "BNEL_OPCODE", "BNEZ_PSEUDO_OP",
)  # fmt: skip

NULLIFIED_BRANCH_OPCODES: frozenset[int] = _tokens(
    "BC1FL_OPCODE", "BC1TL_OPCODE", "BC2FL_OPCODE", "BC2TL_OPCODE",
    "BEQL_OPCODE", "BGEZALL_OPCODE", "BGEZL_OPCODE", "BGTZL_OPCODE",
    "BLEZL_OPCODE", "BLTZALL_OPCODE", "BLTZL_OPCODE", "BNEL_OPCODE",
)  # fmt: skip

TRUE_BRANCH_OPCODES: frozenset[int] = _tokens(
    "BC1T_OPCODE", "BC1TL_OPCODE", "BC2T_OPCODE", "BC2TL_OPCODE"
)

JUMP_OPCODES: frozenset[int] = _tokens("J_OPCODE", "JAL_OPCODE")

LOAD_STORE_OPCODES: frozenset[int] = _tokens(
    "LB_OPCODE", "LBU_OPCODE", "LH_OPCODE", "LHU_OPCODE", "LL_OPCODE",
    "LDC1_OPCODE", "LDC2_OPCODE", "LW_OPCODE", "LWC1_OPCODE", "LWC2_OPCODE",
    "LWL_OPCODE", "LWR_OPCODE", "SB_OPCODE", "SC_OPCODE", "SH_OPCODE",
    "SDC1_OPCODE", "SDC2_OPCODE", "SW_OPCODE", "SWC1_OPCODE", "SWC2_OPCODE",
    "SWL_OPCODE", "SWR_OPCODE",
)  # fmt: skip

# Immediates these instructions sign-extend; every other I-type zero-extends.
_SIGNED_IMMEDIATE_OPCODES: frozenset[int] = _tokens(
    "ADDI_OPCODE", "ADDIU_OPCODE", "SLTI_OPCODE", "SLTIU_OPCODE",
    "TEQI_OPCODE", "TGEI_OPCODE", "TGEIU_OPCODE", "TLTI_OPCODE",
    "TLTIU_OPCODE", "TNEI_OPCODE",
)  # fmt: skip

# The condition bits each FP compare stores (r_cond_type_inst).
_COMPARE_CONDITIONS: dict[int, int] = {}
for _suffix, _cond in (
    ("EQ", COND_EQ),
    ("LE", COND_IN | COND_LT | COND_EQ),
    ("LT", COND_IN | COND_LT),
    ("NGE", COND_IN | COND_LT | COND_UN),
    ("NGLE", COND_IN | COND_UN),
    ("NGL", COND_IN | COND_EQ | COND_UN),
    ("NGT", COND_IN | COND_LT | COND_EQ | COND_UN),
    ("OLT", COND_LT),
    ("OLE", COND_LT | COND_EQ),
    ("SEQ", COND_IN | COND_EQ),
    ("SF", COND_IN),
    ("F", 0),
    ("UEQ", COND_EQ | COND_UN),
    ("ULT", COND_LT | COND_UN),
    ("ULE", COND_LT | COND_EQ | COND_UN),
    ("UN", COND_UN),
):
    for _fmt in ("D", "S"):
        _COMPARE_CONDITIONS[TOK[f"C_{_suffix}_{_fmt}_OPCODE"]] = _cond

_FP_MOVE_ON_GPR: frozenset[int] = _tokens(
    "MOVN_S_OPCODE", "MOVN_D_OPCODE", "MOVZ_S_OPCODE", "MOVZ_D_OPCODE"
)


def opcode_is_branch(opcode: int) -> bool:
    """True for a conditional branch (including the branch pseudo-ops)."""
    return opcode in BRANCH_OPCODES


def opcode_is_nullified_branch(opcode: int) -> bool:
    """True for a branch-likely (``beql``, ...), which nullifies its slot."""
    return opcode in NULLIFIED_BRANCH_OPCODES


def opcode_is_true_branch(opcode: int) -> bool:
    """True for a coprocessor branch on true (``bc1t``, ...)."""
    return opcode in TRUE_BRANCH_OPCODES


def opcode_is_jump(opcode: int) -> bool:
    """True for ``j`` and ``jal``."""
    return opcode in JUMP_OPCODES


def opcode_is_load_store(opcode: int) -> bool:
    """True for a load or a store."""
    return opcode in LOAD_STORE_OPCODES


def kind_of(opcode: int) -> int | None:
    """The operand-shape tag of an opcode token, or None if unknown."""
    row: opcodes.OpcodeRow | None = opcodes.ROW_BY_TOKEN.get(opcode)
    return row.kind if row is not None else None


# --- where the next instruction goes -----------------------------------------

_in_kernel: bool = False
_next_text_pc: int = 0
_next_k_text_pc: int = 0


def text_begins_at_point(addr: int) -> None:
    """Start the user text segment at addr."""
    global _next_text_pc, _in_kernel
    _next_text_pc = addr
    _in_kernel = False


def k_text_begins_at_point(addr: int) -> None:
    """Start the kernel text segment at addr."""
    global _next_k_text_pc, _in_kernel
    _next_k_text_pc = addr
    _in_kernel = False


def align_text(alignment: int) -> None:
    """Round the text PC up to a 2**alignment boundary (``.align`` in text)."""
    global _next_text_pc, _next_k_text_pc
    if alignment == 0:
        return  # disables auto-alignment, which text never does anyway
    mask: int = (0xFFFFFFFF << alignment) & 0xFFFFFFFF
    if _in_kernel:
        _next_k_text_pc = ((_next_k_text_pc + (1 << alignment) - 1) & mask) & 0xFFFFFFFF
        parser.fix_current_label_address(_next_k_text_pc)
    else:
        _next_text_pc = ((_next_text_pc + (1 << alignment) - 1) & mask) & 0xFFFFFFFF
        parser.fix_current_label_address(_next_text_pc)


def set_text_pc(addr: int) -> None:
    """Put the next instruction at addr (``.text ADDR``)."""
    global _next_text_pc, _next_k_text_pc
    if _in_kernel:
        _next_k_text_pc = addr
    else:
        _next_text_pc = addr


def current_text_pc() -> int:
    """The address the next instruction will be stored at."""
    return _next_k_text_pc if _in_kernel else _next_text_pc


def _increment_text_pc(delta: int) -> None:
    """Advance the text PC, which must stay inside its segment."""
    global _next_text_pc, _next_k_text_pc
    if _in_kernel:
        _next_k_text_pc = (_next_k_text_pc + delta) & 0xFFFFFFFF
        if memory.k_text_top <= _next_k_text_pc:
            raise console.run_error("Can't expand kernel text segment\n")
    else:
        _next_text_pc = (_next_text_pc + delta) & 0xFFFFFFFF
        if memory.text_top <= _next_text_pc:
            raise console.run_error("Can't expand text segment\n")


def user_kernel_text_segment(to_kernel: bool) -> None:
    """Send the next instructions to the kernel (True) or user text segment."""
    global _in_kernel
    _in_kernel = to_kernel


# The source text the next stored instruction records.  emit_ast sets it per
# AST node, because by emit time the scanner has moved past the line.
_emit_source_active: bool = False
_emit_source_text: str | None = None


def emit_source_set(text: str | None) -> None:
    """Make the next stored instructions record text as their source line."""
    global _emit_source_active, _emit_source_text
    _emit_source_active = True
    _emit_source_text = text


def emit_source_clear() -> None:
    """Go back to recording the scanner's current line."""
    global _emit_source_active, _emit_source_text
    _emit_source_active = False
    _emit_source_text = None


def _store_instruction(inst: Instruction) -> None:
    """Store inst at the next text address (or as a word in a data segment)."""
    if parser.data_dir:
        data.store_word(inst_encode(inst))
    elif parser.text_dir:
        at: int = current_text_pc()
        traps.exception_occurred = False
        memory.write_inst(at, inst)
        if traps.exception_occurred:
            console.error(f"Invalid address (0x{at:08x}) for instruction\n")
        else:
            _increment_text_pc(4)
        inst.source = (
            _emit_source_text if _emit_source_active else scanner.source_line()
        )
        if inst.encoding == 0:
            inst.set_encoding(inst_encode(inst))
        assembler_event.text_inst(at, machine.u32(inst.encoding))


# --- the assembler's instruction builders ------------------------------------


def make_imm_expr(offset: int, symbol: str | None, pc_relative: bool) -> ImmExpr:
    """``symbol + offset``; naming a symbol creates its label if it is new."""
    label: symbol_table.Label | None = (
        symbol_table.lookup_label(symbol) if symbol is not None else None
    )
    return ImmExpr(machine.s32(offset), label, 0, pc_relative)


def const_imm_expr(value: int) -> ImmExpr:
    """A constant immediate."""
    return make_imm_expr(value, None, False)


def incr_expr_offset(expr: ImmExpr, value: int) -> ImmExpr:
    """A copy of expr with value added to its offset."""
    copy: ImmExpr = expr.copy()
    copy.offset = machine.s32(copy.offset + value)
    return copy


def _upper_bits_of_expr(expr: ImmExpr) -> ImmExpr:
    """A copy of expr that yields its upper 16 bits."""
    copy: ImmExpr = expr.copy()
    copy.bits = 1
    return copy


def _lower_bits_of_expr(expr: ImmExpr) -> ImmExpr:
    """A copy of expr that yields its lower 16 bits."""
    copy: ImmExpr = expr.copy()
    copy.bits = -1
    return copy


def is_zero_imm(expr: ImmExpr) -> bool:
    """True if expr is the constant 0."""
    return expr.offset == 0 and expr.symbol is None


def eval_imm_expr(expr: ImmExpr) -> int:
    """The value of expr (or its upper/lower 16 bits)."""
    value: int
    if expr.symbol is None:
        value = expr.offset
    elif expr.symbol.addr != 0:
        value = machine.s32(expr.offset + expr.symbol.addr)
    else:
        console.error(f"Evaluated undefined symbol: {expr.symbol.name}\n")
        value = 0
    if expr.bits > 0:
        return (value >> 16) & 0xFFFF
    if expr.bits < 0:
        return value & 0xFFFF
    return value


def make_addr_expr(offset: int, symbol: str | None, reg_no: int) -> AddrExpr:
    """``symbol + offset (reg_no)``; a $gp-addressed symbol uses $gp."""
    if reg_no == 0 and symbol is not None:
        label: symbol_table.Label = symbol_table.lookup_label(symbol)
        if label.gp_flag:
            return AddrExpr(
                regs.REG_GP,
                make_imm_expr(offset + label.addr - memory.gp_midpoint, None, False),
            )
    return AddrExpr(reg_no & 0xFF, make_imm_expr(offset, symbol, False))


def _needs_full_word(opcode: int, value: int, expr: ImmExpr) -> bool:
    """True if value does not fit the instruction's 16-bit immediate."""
    if opcode in _SIGNED_IMMEDIATE_OPCODES or (
        opcode_is_load_store(opcode) and expr.bits == 0
    ):
        high: int = machine.u32(value) & 0xFFFF8000
        return high not in (0, 0xFFFF8000)
    return machine.u32(value) & 0xFFFF0000 != 0


def i_type_inst(opcode: int, rt: int, rs: int, expr: ImmExpr) -> None:
    """Store an I-type instruction; a large immediate takes more than one.

    On the extended machine an immediate that does not fit 16 bits (or a
    still-undefined symbol whose full value is needed) is built in $at
    first: see :func:`_i_type_inst_full_word`.
    """
    inst: Instruction = Instruction(opcode)
    inst.set_rs(rs)
    inst.set_rt(rt)
    inst.expr = expr.copy()
    if expr.symbol is None or expr.symbol.addr != 0:
        value: int = eval_imm_expr(expr)
        if not options.bare_machine and _needs_full_word(opcode, value, expr):
            _i_type_inst_full_word(opcode, rt, rs, expr, True, value)
            return
        symbol_table.resolve_a_label(expr.symbol, inst)
    elif options.bare_machine or expr.bits != 0:
        # Only 16 bits of the value are needed: patch them when it is known.
        symbol_table.record_inst_uses_symbol(inst, expr.symbol)
    else:
        # The whole value is needed and unknown: assume it is large.
        _i_type_inst_full_word(opcode, rt, rs, expr, False, 0)
        return
    _store_instruction(inst)


def _gp_offset(expr: ImmExpr, rs: int) -> int | None:
    """The $gp-relative offset of a $gp-addressed symbol, if it fits."""
    if expr.symbol is not None and expr.symbol.gp_flag and rs == 0:
        offset: int = machine.s32(expr.symbol.addr + expr.offset)
        if IMM_MIN <= offset <= IMM_MAX:
            return offset
    return None


def _i_type_inst_full_word(
    opcode: int, rt: int, rs: int, expr: ImmExpr, value_known: bool, value: int
) -> None:
    """Build an immediate that may not fit 16 bits from separate instructions."""
    gp_offset: int | None = _gp_offset(expr, rs)
    if opcode_is_load_store(opcode):
        if gp_offset is not None:
            i_type_inst(opcode, rt, regs.REG_GP, make_imm_expr(gp_offset, None, False))
        elif value_known:
            high: int = (value >> 16) & 0xFFFF
            low: int = value & 0xFFFF
            if not (high == 0 and not low & 0x8000) and not (
                high == 0xFFFF and low & 0x8000
            ):
                if low & 0x8000:
                    high += 1  # the load sign-extends the low half
                i_type_inst(TOK["LUI_OPCODE"], 1, 0, const_imm_expr(high))
                if rs != 0:
                    r_type_inst(TOK["ADDU_OPCODE"], 1, 1, rs)
                i_type_inst(opcode, rt, 1, _lower_bits_of_expr(const_imm_expr(low)))
            else:
                # Sign-extending the low half already gives the high half.
                i_type_inst(opcode, rt, rs, const_imm_expr(low))
        else:
            i_type_inst(TOK["LUI_OPCODE"], 1, 0, _upper_bits_of_expr(expr))
            if rs != 0:
                r_type_inst(TOK["ADDU_OPCODE"], 1, 1, rs)
            i_type_inst(opcode, rt, 1, _lower_bits_of_expr(expr))
    elif opcode_is_branch(opcode):
        # This only allows branches +/- 32K, which is not correct!
        i_type_inst(opcode, rt, rs, _lower_bits_of_expr(expr))
    elif gp_offset is not None:
        computed: int = TOK["ADDIU_OPCODE"] if opcode == TOK["LUI_OPCODE"] else opcode
        i_type_inst(computed, rt, regs.REG_GP, make_imm_expr(gp_offset, None, False))
    elif rs == 0 and opcode in _tokens(
        "ORI_OPCODE", "ADDI_OPCODE", "ADDIU_OPCODE", "LUI_OPCODE"
    ):
        _produce_immediate(expr, rt, value_known, value)
    else:
        _produce_immediate(expr, 1, value_known, value)
        r_type_inst(pseudo_op.imm_op_to_op(opcode), rt, rs, 1)


def _produce_immediate(expr: ImmExpr, rt: int, value_known: bool, value: int) -> None:
    """Load expr's 32-bit value into rt with lui and/or ori."""
    if value_known and value & 0xFFFF == 0:
        i_type_inst(TOK["LUI_OPCODE"], rt, 0, _upper_bits_of_expr(expr))
    elif value_known and machine.u32(value) & 0xFFFF0000 == 0:
        i_type_inst(TOK["ORI_OPCODE"], rt, 0, _lower_bits_of_expr(expr))
    else:
        i_type_inst(TOK["LUI_OPCODE"], 1, 0, _upper_bits_of_expr(expr))
        i_type_inst(TOK["ORI_OPCODE"], rt, 1, _lower_bits_of_expr(expr))


def j_type_inst(opcode: int, target: ImmExpr) -> None:
    """Store a J-type instruction (the target is not PC-relative)."""
    inst: Instruction = Instruction(opcode)
    target.offset = 0
    target.pc_relative = False
    inst.expr = target.copy()
    if target.symbol is None or target.symbol.addr != 0:
        symbol_table.resolve_a_label(target.symbol, inst)
    else:
        symbol_table.record_inst_uses_symbol(inst, target.symbol)
    _store_instruction(inst)


def _make_r_type_inst(opcode: int, rd: int, rs: int, rt: int) -> Instruction:
    """An R-type instruction with these fields."""
    inst: Instruction = Instruction(opcode)
    inst.set_rs(rs)
    inst.set_rt(rt)
    inst.set_rd(rd)
    return inst


def r_type_inst(opcode: int, rd: int, rs: int, rt: int) -> None:
    """Store an R-type instruction."""
    _store_instruction(_make_r_type_inst(opcode, rd, rs, rt))


def r_co_type_inst(opcode: int, fd: int, fs: int, ft: int) -> None:
    """Store a coprocessor R-type instruction (FS in rd, FD in shamt)."""
    inst: Instruction = _make_r_type_inst(opcode, fs, 0, ft)
    inst.set_shamt(fd)
    _store_instruction(inst)


def r_sh_type_inst(opcode: int, rd: int, rt: int, shamt: int) -> None:
    """Store a shift (the amount is masked to 5 bits)."""
    inst: Instruction = _make_r_type_inst(opcode, rd, 0, rt)
    inst.set_shamt(shamt & 0x1F)
    _store_instruction(inst)


def r_cond_type_inst(opcode: int, fs: int, ft: int, cc: int) -> None:
    """Store an FP compare, with its condition bits in the rs byte."""
    inst: Instruction = _make_r_type_inst(opcode, fs, 0, ft)
    inst.set_shamt(cc << 2)
    if opcode in _COMPARE_CONDITIONS:
        inst.set_rs(_COMPARE_CONDITIONS[opcode])
    _store_instruction(inst)


# --- breakpoints --------------------------------------------------------------

_break_inst: Instruction | None = None


def _breakpoint_instruction() -> Instruction:
    """The one ``break 1`` instruction every breakpoint shares."""
    global _break_inst
    if _break_inst is None:
        _break_inst = _make_r_type_inst(TOK["BREAK_OPCODE"], 1, 0, 0)
    return _break_inst


def inst_is_breakpoint(addr: int) -> bool:
    """True if a breakpoint is set at addr."""
    return memory.read_inst(addr) is _breakpoint_instruction()


def set_breakpoint(addr: int) -> Instruction | None:
    """Put a breakpoint at addr; return the instruction it replaced, or None."""
    brk: Instruction = _breakpoint_instruction()
    traps.exception_occurred = False
    old: Instruction | None = memory.read_inst(addr)
    if old is brk:
        return None
    memory.write_inst(addr, brk)
    return None if traps.exception_occurred else old


# --- encoding and decoding ----------------------------------------------------


def _regs(value: int, shift: int) -> int:
    """A register number placed in its 5-bit field."""
    return (value & 0x1F) << shift


def inst_encode(inst: Instruction | None) -> int:
    """The 32-bit machine word of inst (signed), or 0 (C ``inst_encode``)."""
    if inst is None:
        return 0
    row: opcodes.OpcodeRow | None = opcodes.ROW_BY_TOKEN.get(inst.opcode)
    if row is None:
        return 0
    a: int = row.encoding
    kind: int = row.kind
    imm: int = inst.imm & 0xFFFF
    word: int
    if kind == BRANCH_COPROC_COND_INST:
        word = a | _regs((inst.rt >> 2) << 2, 16) | imm
    elif kind in (BRANCH_RS_LABEL_INST, RS_IMM_INST):
        word = a | _regs(inst.rs, 21) | imm
    elif (
        kind in (RT_IMM_INST, RT_RS_IMM_INST, BRANCH_RS_RT_LABEL_INST, RT_ADDR_INST)
        or kind == FP_FT_ADDR_INST
    ):
        word = a | _regs(inst.rs, 21) | _regs(inst.rt, 16) | imm
    elif kind == RS_INST:
        word = a | _regs(inst.rs, 21)
    elif kind == RD_INST:
        word = a | _regs(inst.rd, 11)
    elif kind in (RT_COPREG_INST, FP_RT_COPREG_INST):
        word = a | _regs(inst.rt, 16) | _regs(inst.rd, 11)
    elif kind == RS_RT_INST:
        word = a | _regs(inst.rs, 21) | _regs(inst.rt, 16)
    elif kind == RD_RS_INST:
        word = a | _regs(inst.rs, 21) | _regs(inst.rd, 11)
    elif kind == RD_RT_SHAMT_INST:
        word = a | _regs(inst.rt, 16) | _regs(inst.rd, 11) | _regs(inst.shamt, 6)
    elif kind in (RD_RS_RT_INST, RD_RT_RS_INST, MOVC_TYPE_INST):
        word = a | _regs(inst.rs, 21) | _regs(inst.rt, 16) | _regs(inst.rd, 11)
    elif kind == FP_FD_FS_INST:
        word = a | _regs(inst.rd, 11) | _regs(inst.shamt, 6)
    elif kind == FP_CMP_FS_FT_INST:
        word = (
            a | _regs(inst.rt, 16) | _regs(inst.rd, 11) | _regs(inst.shamt, 6) | inst.rs
        )
    elif kind in (FP_FD_FS_FT_INST, FP_MOVC_TYPE_INST):
        # For movf/movt the rt byte is cc<<2; their tf bit is in a already.
        word = a | _regs(inst.rt, 16) | _regs(inst.rd, 11) | _regs(inst.shamt, 6)
    elif kind == J_TYPE_INST:
        word = machine.u32(a) | inst.target
    elif kind == NOARG_TYPE_INST:
        word = a
    else:
        raise console.fatal_error("Unknown instruction type in inst_encoding\n")
    return machine.s32(word)


def _mk_inst(
    word: int, opcode: int, rs: int = 0, rt: int = 0, rd: int = 0, shamt: int = 0
) -> Instruction:
    """A decoded instruction with these bytes (C mk_r_inst/mk_co_r_inst)."""
    inst: Instruction = Instruction(opcode)
    inst.rs, inst.rt, inst.rd, inst.shamt = (
        rs & 0xFF,
        rt & 0xFF,
        rd & 0xFF,
        shamt & 0xFF,
    )
    inst._bytes_changed()
    inst.encoding = machine.s32(word)
    return inst


def inst_decode(word: int) -> Instruction:
    """The instruction a 32-bit machine word encodes (C ``inst_decode``).

    The major opcode (the top 6 bits) decides which other bits complete the
    opcode; that key is looked up in the table of encodings.  An unknown
    word decodes to an instruction with opcode 0, which prints as
    ``<unknown instruction 0>``.
    """
    val: int = machine.u32(word)
    a: int = val & 0xFC000000
    if a in (0, 0x70000000):  # SPECIAL, SPECIAL2: the function field
        a |= val & 0x3F
    elif a == 0x04000000:  # REGIMM: the rt field
        a |= val & 0x001F0000
    elif a == 0x40000000:  # COP0
        a |= (val & 0x03E00000) | (val & 0x1F)
    elif a == 0x44000000:  # COP1
        a |= val & 0x03E00000
        a |= (val & 0x00010000) if val & 0xFF000000 == 0x45000000 else (val & 0x3F)
    elif a in (0x48000000, 0x4C000000):  # COP2, COP1X
        a |= val & 0x03E00000
    token: int | None = opcodes.token_for_encoding(machine.s32(a))
    if token is None:
        return _mk_inst(val, 0)
    rs: int = (val >> 21) & 0x1F
    rt: int = (val >> 16) & 0x1F
    rd: int = (val >> 11) & 0x1F
    sh: int = (val >> 6) & 0x1F
    imm: int = val & 0xFFFF
    kind: int | None = kind_of(token)
    if kind in (BRANCH_COPROC_COND_INST, RT_IMM_INST, RT_RS_IMM_INST,
                BRANCH_RS_RT_LABEL_INST, RT_ADDR_INST, FP_FT_ADDR_INST):  # fmt: skip
        return _mk_inst(val, token, rs, rt, imm & 0xFF, imm >> 8)
    if kind in (BRANCH_RS_LABEL_INST, RS_IMM_INST):
        return _mk_inst(val, token, rs, 0, imm & 0xFF, imm >> 8)
    if kind == RS_INST:
        return _mk_inst(val, token, rs)
    if kind == RD_INST:
        return _mk_inst(val, token, rd=rd)
    if kind in (RT_COPREG_INST, FP_RT_COPREG_INST):
        return _mk_inst(val, token, rt=rt, rd=rd)
    if kind == RS_RT_INST:
        return _mk_inst(val, token, rs, rt)
    if kind == RD_RS_INST:
        return _mk_inst(val, token, rs, rd=rd)
    if kind == RD_RT_SHAMT_INST:
        return _mk_inst(val, token, 0, rt, rd, sh)
    if kind in (RD_RS_RT_INST, RD_RT_RS_INST, MOVC_TYPE_INST):
        return _mk_inst(val, token, rs, rt, rd)
    if kind == FP_FD_FS_INST:
        return _mk_inst(val, token, 0, 0, rd, sh)
    if kind == FP_CMP_FS_FT_INST:
        return _mk_inst(val, token, val & 0xF, rt, rd, sh)
    if kind in (FP_FD_FS_FT_INST, FP_MOVC_TYPE_INST):
        return _mk_inst(val, token, 0, rt, rd, sh)
    if kind == J_TYPE_INST:
        target: int = val & 0x3FFFFFF
        return _mk_inst(val, token, target, target >> 8, target >> 16, target >> 24)
    if kind == NOARG_TYPE_INST:
        return _mk_inst(val, token)
    return _mk_inst(val, 0)


# --- disassembly ------------------------------------------------------------


def _reg(number: int) -> str:
    """The ABI name of general register number ($0 is "r0")."""
    return regs.INT_REG_NAMES[number]


def _format_imm_expr(expr: ImmExpr, base_reg: int) -> str:
    """The ``[symbol...]`` annotation of a symbolic immediate."""
    text: str = expr.symbol.name if expr.symbol is not None else ""
    if expr.pc_relative:
        text += f"-0x{machine.u32(-expr.offset):08x}"
    elif expr.offset < -10:
        text += f"-{-expr.offset} (-0x{machine.u32(-expr.offset):08x})"
    elif expr.offset > 10:
        text += f"+{expr.offset} (0x{machine.u32(expr.offset):08x})"
    if (
        base_reg != -1
        and expr.symbol is not None
        and (expr.offset > 10 or expr.offset < -10)
        and (expr.offset != 0 or base_reg != 0)
    ):
        text += f"(${_reg(base_reg)})"
    return text


def _operands(inst: Instruction, kind: int) -> str:
    """The operand text of inst for its operand shape (format_an_inst)."""
    branch_offset: int = machine.s32(machine.s16(inst.imm) << 2)
    if kind == BRANCH_COPROC_COND_INST:
        return f"{inst.rt >> 2} {branch_offset}"
    if kind == BRANCH_RS_LABEL_INST:
        return f" ${_reg(inst.rs)} {branch_offset}"
    if kind == RS_IMM_INST:
        return f" ${_reg(inst.rs)}, {inst.imm}"
    if kind == RT_IMM_INST:
        return f" ${_reg(inst.rt)}, {inst.imm}"
    if kind == RT_RS_IMM_INST:
        return f" ${_reg(inst.rt)}, ${_reg(inst.rs)}, {inst.imm}"
    if kind == BRANCH_RS_RT_LABEL_INST:
        return f" ${_reg(inst.rs)}, ${_reg(inst.rt)}, {branch_offset}"
    if kind == RT_ADDR_INST:
        return f" ${_reg(inst.rt)}, {inst.imm}(${_reg(inst.rs)})"
    if kind == RS_INST:
        return f" ${_reg(inst.rs)}"
    if kind == RD_INST:
        return f" ${_reg(inst.rd)}"
    if kind == RT_COPREG_INST:
        return f" ${_reg(inst.rt)}, ${_reg(inst.rd)}"
    if kind == RS_RT_INST:
        return f" ${_reg(inst.rs)}, ${_reg(inst.rt)}"
    if kind == RD_RS_INST:
        return f" ${_reg(inst.rd)}, ${_reg(inst.rs)}"
    if kind == RD_RT_SHAMT_INST:
        return f" ${_reg(inst.rd)}, ${_reg(inst.rt)}, {inst.shamt}"
    if kind == RD_RS_RT_INST:
        return f" ${_reg(inst.rd)}, ${_reg(inst.rs)}, ${_reg(inst.rt)}"
    if kind == RD_RT_RS_INST:
        return f" ${_reg(inst.rd)}, ${_reg(inst.rt)}, ${_reg(inst.rs)}"
    if kind == FP_FT_ADDR_INST:
        return f" $f{inst.rt}, {inst.imm}(${_reg(inst.rs)})"
    if kind == FP_FD_FS_INST:
        return f" $f{inst.shamt}, $f{inst.rd}"
    if kind == FP_RT_COPREG_INST:
        return f" ${_reg(inst.rt)}, $f{inst.rd}"
    if kind == FP_CMP_FS_FT_INST:
        if inst.shamt == 0:
            return f" $f{inst.rd}, $f{inst.rt}"
        return f" {inst.shamt >> 2}, $f{inst.rd}, $f{inst.rt}"
    if kind == FP_FD_FS_FT_INST:
        return f" $f{inst.shamt}, $f{inst.rd}, $f{inst.rt}"
    if kind == MOVC_TYPE_INST:
        return f" ${_reg(inst.rd)}, ${_reg(inst.rs)}, {inst.rt >> 2}"
    if kind == FP_MOVC_TYPE_INST:
        if inst.opcode in _FP_MOVE_ON_GPR:
            return f" $f{inst.shamt}, $f{inst.rd}, ${_reg(inst.rt)}"
        return f" $f{inst.shamt}, $f{inst.rd}, {inst.rt >> 2}"
    if kind == J_TYPE_INST:
        return f" 0x{(inst.target << 2) & 0xFFFFFFFF:08x}"
    if kind == NOARG_TYPE_INST:
        return ""
    raise console.fatal_error("Unknown instruction type in print_inst\n")


def format_an_inst(inst: Instruction | None, addr: int) -> str:
    """One line of disassembly: address, encoding, mnemonic, operands, source.

    The source comment starts at column 57 (counting the tab as one column),
    and a breakpoint is shown as a ``*`` before the line.
    """
    if addr != 0 and inst_is_breakpoint(addr):
        spim_utils.delete_breakpoint(addr)
        text: str = "*" + format_an_inst(memory.read_inst(addr), addr)
        spim_utils.add_breakpoint(addr)
        return text
    line: str = f"[0x{addr:08x}]\t"
    if inst is None:
        return line + "<none>\n"
    row: opcodes.OpcodeRow | None = opcodes.ROW_BY_TOKEN.get(inst.opcode)
    if row is None:
        return line + f"<unknown instruction {machine.s16(inst.opcode)}>\n"
    line += f"0x{machine.u32(inst.encoding):08x}  {row.name}"
    if (
        row.kind == RD_RT_SHAMT_INST
        and inst.rd == 0
        and inst.rt == 0
        and inst.shamt == 0
    ):
        line = line[:-3] + "nop"  # sll $0, $0, 0 is the canonical nop
    else:
        line += _operands(inst, row.kind)
    if inst.expr is not None and inst.expr.symbol is not None:
        base: int = inst.rs if opcode_is_load_store(inst.opcode) else -1
        line += f" [{_format_imm_expr(inst.expr, base)}]"
    if inst.source is not None:
        line = line.ljust(57) + "; " + inst.source
    return line + "\n"


def inst_to_string(addr: int) -> str:
    """The disassembly line of the instruction at addr."""
    traps.exception_occurred = False
    inst: Instruction | None = memory.read_inst(addr)
    if traps.exception_occurred:
        console.error(f"Can't print instruction not in text segment (0x{addr:08x})\n")
        return ""
    return format_an_inst(inst, addr)


def print_inst(addr: int) -> None:
    """Print the instruction at addr on stderr."""
    console.message(inst_to_string(addr))
