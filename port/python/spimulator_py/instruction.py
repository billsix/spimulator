"""Building, encoding, decoding and printing instructions (instruction.c).

Three jobs, as in the C file:

* the assembler's back end: ``r_type``, ``i_type``, ... build an
  :class:`Instruction` and store it at the next text address.  An immediate
  that does not fit in 16 bits is synthesized with ``lui $at`` + ``ori``,
  exactly as the C engine does (so the addresses of everything after it
  match);
* ``instruction_encode`` / ``instruction_decode`` convert between an Instruction and its
  32-bit machine word;
* ``format_an_instruction`` prints one line of disassembly.
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
from .inst import AddressExpression, ImmediateExpression, Instruction
from .opcode_table import OpType, Tok

# Operand-shape tags (opcode-types.h), named once for the switches below.

IMMEDIATE_MIN: int = -0x8000
IMMEDIATE_MAX: int = 0x7FFF
UNSIGNED_IMMEDIATE_MIN: int = 0
UNSIGNED_IMMEDIATE_MAX: int = 0xFFFF

CONDITION_UNORDERED: int = 0x1
CONDITION_EQUAL: int = 0x2
CONDITION_LESS_THAN: int = 0x4
CONDITION_INVALID: int = 0x8


BRANCH_OPCODES: frozenset[int] = frozenset({
    Tok.BC1F_OPCODE, Tok.BC1FL_OPCODE, Tok.BC1T_OPCODE, Tok.BC1TL_OPCODE,
    Tok.BC2F_OPCODE, Tok.BC2FL_OPCODE, Tok.BC2T_OPCODE, Tok.BC2TL_OPCODE,
    Tok.BEQ_OPCODE, Tok.BEQL_OPCODE, Tok.BEQZ_PSEUDO_OP, Tok.BGE_PSEUDO_OP,
    Tok.BGEU_PSEUDO_OP, Tok.BGEZ_OPCODE, Tok.BGEZAL_OPCODE, Tok.BGEZALL_OPCODE,
    Tok.BGEZL_OPCODE, Tok.BGT_PSEUDO_OP, Tok.BGTU_PSEUDO_OP, Tok.BGTZ_OPCODE,
    Tok.BGTZL_OPCODE, Tok.BLE_PSEUDO_OP, Tok.BLEU_PSEUDO_OP, Tok.BLEZ_OPCODE,
    Tok.BLEZL_OPCODE, Tok.BLT_PSEUDO_OP, Tok.BLTU_PSEUDO_OP, Tok.BLTZ_OPCODE,
    Tok.BLTZAL_OPCODE, Tok.BLTZALL_OPCODE, Tok.BLTZL_OPCODE, Tok.BNE_OPCODE,
    Tok.BNEL_OPCODE, Tok.BNEZ_PSEUDO_OP,
})  # fmt: skip

NULLIFIED_BRANCH_OPCODES: frozenset[int] = frozenset({
    Tok.BC1FL_OPCODE, Tok.BC1TL_OPCODE, Tok.BC2FL_OPCODE, Tok.BC2TL_OPCODE,
    Tok.BEQL_OPCODE, Tok.BGEZALL_OPCODE, Tok.BGEZL_OPCODE, Tok.BGTZL_OPCODE,
    Tok.BLEZL_OPCODE, Tok.BLTZALL_OPCODE, Tok.BLTZL_OPCODE, Tok.BNEL_OPCODE,
})  # fmt: skip

TRUE_BRANCH_OPCODES: frozenset[int] = frozenset(
    {Tok.BC1T_OPCODE, Tok.BC1TL_OPCODE, Tok.BC2T_OPCODE, Tok.BC2TL_OPCODE}
)

JUMP_OPCODES: frozenset[int] = frozenset({Tok.J_OPCODE, Tok.JAL_OPCODE})

LOAD_STORE_OPCODES: frozenset[int] = frozenset({
    Tok.LB_OPCODE, Tok.LBU_OPCODE, Tok.LH_OPCODE, Tok.LHU_OPCODE, Tok.LL_OPCODE,
    Tok.LDC1_OPCODE, Tok.LDC2_OPCODE, Tok.LW_OPCODE, Tok.LWC1_OPCODE, Tok.LWC2_OPCODE,
    Tok.LWL_OPCODE, Tok.LWR_OPCODE, Tok.SB_OPCODE, Tok.SC_OPCODE, Tok.SH_OPCODE,
    Tok.SDC1_OPCODE, Tok.SDC2_OPCODE, Tok.SW_OPCODE, Tok.SWC1_OPCODE, Tok.SWC2_OPCODE,
    Tok.SWL_OPCODE, Tok.SWR_OPCODE,
})  # fmt: skip

# Immediates these instructions sign-extend; every other I-type zero-extends.
_SIGNED_IMMEDIATE_OPCODES: frozenset[int] = frozenset({
    Tok.ADDI_OPCODE, Tok.ADDIU_OPCODE, Tok.SLTI_OPCODE, Tok.SLTIU_OPCODE,
    Tok.TEQI_OPCODE, Tok.TGEI_OPCODE, Tok.TGEIU_OPCODE, Tok.TLTI_OPCODE,
    Tok.TLTIU_OPCODE, Tok.TNEI_OPCODE,
})  # fmt: skip

# The condition bits each FP compare stores (r_condition_type).
_COMPARE_CONDITIONS: dict[int, int] = {}
for _double_form, _single_form, _condition in (
    (Tok.C_EQ_D_OPCODE, Tok.C_EQ_S_OPCODE, CONDITION_EQUAL),
    (
        Tok.C_LE_D_OPCODE,
        Tok.C_LE_S_OPCODE,
        CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_EQUAL,
    ),
    (Tok.C_LT_D_OPCODE, Tok.C_LT_S_OPCODE, CONDITION_INVALID | CONDITION_LESS_THAN),
    (
        Tok.C_NGE_D_OPCODE,
        Tok.C_NGE_S_OPCODE,
        CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_UNORDERED,
    ),
    (Tok.C_NGLE_D_OPCODE, Tok.C_NGLE_S_OPCODE, CONDITION_INVALID | CONDITION_UNORDERED),
    (
        Tok.C_NGL_D_OPCODE,
        Tok.C_NGL_S_OPCODE,
        CONDITION_INVALID | CONDITION_EQUAL | CONDITION_UNORDERED,
    ),
    (
        Tok.C_NGT_D_OPCODE,
        Tok.C_NGT_S_OPCODE,
        CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_EQUAL | CONDITION_UNORDERED,
    ),
    (Tok.C_OLT_D_OPCODE, Tok.C_OLT_S_OPCODE, CONDITION_LESS_THAN),
    (Tok.C_OLE_D_OPCODE, Tok.C_OLE_S_OPCODE, CONDITION_LESS_THAN | CONDITION_EQUAL),
    (Tok.C_SEQ_D_OPCODE, Tok.C_SEQ_S_OPCODE, CONDITION_INVALID | CONDITION_EQUAL),
    (Tok.C_SF_D_OPCODE, Tok.C_SF_S_OPCODE, CONDITION_INVALID),
    (Tok.C_F_D_OPCODE, Tok.C_F_S_OPCODE, 0),
    (Tok.C_UEQ_D_OPCODE, Tok.C_UEQ_S_OPCODE, CONDITION_EQUAL | CONDITION_UNORDERED),
    (Tok.C_ULT_D_OPCODE, Tok.C_ULT_S_OPCODE, CONDITION_LESS_THAN | CONDITION_UNORDERED),
    (
        Tok.C_ULE_D_OPCODE,
        Tok.C_ULE_S_OPCODE,
        CONDITION_LESS_THAN | CONDITION_EQUAL | CONDITION_UNORDERED,
    ),
    (Tok.C_UN_D_OPCODE, Tok.C_UN_S_OPCODE, CONDITION_UNORDERED),
):
    _COMPARE_CONDITIONS[_double_form] = _condition
    _COMPARE_CONDITIONS[_single_form] = _condition

_FLOATING_POINT_MOVE_ON_GENERAL_PURPOSE_REGISTER: frozenset[int] = frozenset(
    {Tok.MOVN_S_OPCODE, Tok.MOVN_D_OPCODE, Tok.MOVZ_S_OPCODE, Tok.MOVZ_D_OPCODE}
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
_next_text_location: int = 0
_next_kernel_text_location: int = 0


def text_begins_at_point(address: int) -> None:
    """Start the user text segment at addr."""
    global _next_text_location, _in_kernel
    _next_text_location = address
    _in_kernel = False


def kernel_text_begins_at_point(address: int) -> None:
    """Start the kernel text segment at addr."""
    global _next_kernel_text_location, _in_kernel
    _next_kernel_text_location = address
    _in_kernel = False


def align_text(alignment: int) -> None:
    """Round the text PC up to a 2**alignment boundary (``.align`` in text)."""
    global _next_text_location, _next_kernel_text_location
    if alignment == 0:
        return  # disables auto-alignment, which text never does anyway
    mask: int = (0xFFFFFFFF << alignment) & 0xFFFFFFFF
    if _in_kernel:
        _next_kernel_text_location = (
            (_next_kernel_text_location + (1 << alignment) - 1) & mask
        ) & 0xFFFFFFFF
        parser.fix_current_label_address(_next_kernel_text_location)
    else:
        _next_text_location = (
            (_next_text_location + (1 << alignment) - 1) & mask
        ) & 0xFFFFFFFF
        parser.fix_current_label_address(_next_text_location)


def set_text_location(address: int) -> None:
    """Put the next instruction at addr (``.text ADDR``)."""
    global _next_text_location, _next_kernel_text_location
    if _in_kernel:
        _next_kernel_text_location = address
    else:
        _next_text_location = address


def current_text_location() -> int:
    """The address the next instruction will be stored at."""
    return _next_kernel_text_location if _in_kernel else _next_text_location


def _increment_text_location(delta: int) -> None:
    """Advance the text PC, which must stay inside its segment."""
    global _next_text_location, _next_kernel_text_location
    if _in_kernel:
        _next_kernel_text_location = (_next_kernel_text_location + delta) & 0xFFFFFFFF
        if memory.kernel_text_top <= _next_kernel_text_location:
            raise console.run_error("Can't expand kernel text segment\n")
    else:
        _next_text_location = (_next_text_location + delta) & 0xFFFFFFFF
        if memory.text_top <= _next_text_location:
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


def _store_instruction(instruction: Instruction) -> None:
    """Store inst at the next text address (or as a word in a data segment)."""
    if parser.data_directive:
        data.store_word(instruction_encode(instruction))
    elif parser.text_directive:
        at: int = current_text_location()
        traps.exception_occurred = False
        memory.write_instruction(at, instruction)
        if traps.exception_occurred:
            console.error(f"Invalid address (0x{at:08x}) for instruction\n")
        else:
            _increment_text_location(4)
        instruction.source = (
            _emit_source_text if _emit_source_active else scanner.source_line()
        )
        if instruction.encoding == 0:
            instruction.set_encoding(instruction_encode(instruction))
        assembler_event.text_instruction(at, machine.u32(instruction.encoding))
    else:
        # Never reached: the parser always sets data_directive and text_directive as a pair, one true.
        raise console.fatal_error(
            "internal error: store_instruction: neither the data nor the text segment is active\n"
        )


# --- the assembler's instruction builders ------------------------------------


def make_immediate_expression(
    offset: int, symbol: str | None, program_counter_relative: bool
) -> ImmediateExpression:
    """``symbol + offset``; naming a symbol creates its label if it is new."""
    label: symbol_table.Label | None = (
        symbol_table.lookup_label(symbol) if symbol is not None else None
    )
    return ImmediateExpression(machine.s32(offset), label, 0, program_counter_relative)


def constant_immediate_expression(value: int) -> ImmediateExpression:
    """A constant immediate."""
    return make_immediate_expression(value, None, False)


def increment_expression_offset(
    expression: ImmediateExpression, value: int
) -> ImmediateExpression:
    """A copy of expr with value added to its offset."""
    copy: ImmediateExpression = expression.copy()
    copy.offset = machine.s32(copy.offset + value)
    return copy


def _upper_bits_of_expression(expression: ImmediateExpression) -> ImmediateExpression:
    """A copy of expr that yields its upper 16 bits."""
    copy: ImmediateExpression = expression.copy()
    copy.bits = 1
    return copy


def _lower_bits_of_expression(expression: ImmediateExpression) -> ImmediateExpression:
    """A copy of expr that yields its lower 16 bits."""
    copy: ImmediateExpression = expression.copy()
    copy.bits = -1
    return copy


def is_zero_immediate(expression: ImmediateExpression) -> bool:
    """True if expr is the constant 0."""
    return expression.offset == 0 and expression.symbol is None


def evaluate_immediate_expression(expression: ImmediateExpression) -> int:
    """The value of expr (or its upper/lower 16 bits)."""
    value: int
    if expression.symbol is None:
        value = expression.offset
    elif expression.symbol.address != 0:
        value = machine.s32(expression.offset + expression.symbol.address)
    else:
        console.error(f"Evaluated undefined symbol: {expression.symbol.name}\n")
        value = 0
    if expression.bits > 0:
        return (value >> 16) & 0xFFFF
    elif expression.bits < 0:
        return value & 0xFFFF
    else:
        return value


def make_address_expression(
    offset: int, symbol: str | None, register_no: int
) -> AddressExpression:
    """``symbol + offset (register_no)``; a $gp-addressed symbol uses $gp."""
    if register_no == 0 and symbol is not None:
        label: symbol_table.Label = symbol_table.lookup_label(symbol)
        if label.global_pointer_flag:
            return AddressExpression(
                regs.GLOBAL_POINTER_REGISTER,
                make_immediate_expression(
                    offset + label.address - memory.global_pointer_midpoint, None, False
                ),
            )
    return AddressExpression(
        register_no & 0xFF, make_immediate_expression(offset, symbol, False)
    )


def _needs_full_word(opcode: int, value: int, expression: ImmediateExpression) -> bool:
    """True if value does not fit the instruction's 16-bit immediate."""
    if opcode in _SIGNED_IMMEDIATE_OPCODES or (
        opcode_is_load_store(opcode) and expression.bits == 0
    ):
        # The bits above the low 15: all clear or all set when the value fits.
        return (machine.u32(value) & 0xFFFF8000) not in (0, 0xFFFF8000)
    return machine.u32(value) & 0xFFFF0000 != 0


def i_type(
    opcode: int,
    target_register: int,
    source_register: int,
    expression: ImmediateExpression,
) -> None:
    """Store an I-type instruction; a large immediate takes more than one.

    On the extended machine an immediate that does not fit 16 bits (or a
    still-undefined symbol whose full value is needed) is built in $at
    first: see :func:`_i_type_full_word`.
    """
    instruction: Instruction = Instruction(opcode)
    instruction.set_source_register(source_register)
    instruction.set_target_register(target_register)
    instruction.expression = expression.copy()
    if expression.symbol is None or expression.symbol.address != 0:
        value: int = evaluate_immediate_expression(expression)
        if not options.bare_machine and _needs_full_word(opcode, value, expression):
            _i_type_full_word(
                opcode, target_register, source_register, expression, True, value
            )
            return
        symbol_table.resolve_a_label(expression.symbol, instruction)
    elif options.bare_machine or expression.bits != 0:
        # Only 16 bits of the value are needed: patch them when it is known.
        symbol_table.record_instruction_uses_symbol(instruction, expression.symbol)
    else:
        # The whole value is needed and unknown: assume it is large.
        _i_type_full_word(
            opcode, target_register, source_register, expression, False, 0
        )
        return
    _store_instruction(instruction)


def _global_pointer_offset(
    expression: ImmediateExpression, source_register: int
) -> int | None:
    """The $gp-relative offset of a $gp-addressed symbol, if it fits."""
    if (
        expression.symbol is not None
        and expression.symbol.global_pointer_flag
        and source_register == 0
    ):
        offset: int = machine.s32(expression.symbol.address + expression.offset)
        if IMMEDIATE_MIN <= offset <= IMMEDIATE_MAX:
            return offset
    return None


def _i_type_full_word(
    opcode: int,
    target_register: int,
    source_register: int,
    expression: ImmediateExpression,
    value_known: bool,
    value: int,
) -> None:
    """Build an immediate that may not fit 16 bits from separate instructions."""
    global_pointer_offset: int | None = _global_pointer_offset(
        expression, source_register
    )
    if opcode_is_load_store(opcode):
        if global_pointer_offset is not None:
            i_type(
                opcode,
                target_register,
                regs.GLOBAL_POINTER_REGISTER,
                make_immediate_expression(global_pointer_offset, None, False),
            )
        elif value_known:
            high: int = (value >> 16) & 0xFFFF
            low: int = value & 0xFFFF
            if not (high == 0 and not low & 0x8000) and not (
                high == 0xFFFF and low & 0x8000
            ):
                if low & 0x8000:
                    high += 1  # the load sign-extends the low half
                i_type(Tok.LUI_OPCODE, 1, 0, constant_immediate_expression(high))
                if source_register != 0:
                    r_type(Tok.ADDU_OPCODE, 1, 1, source_register)
                i_type(
                    opcode,
                    target_register,
                    1,
                    _lower_bits_of_expression(constant_immediate_expression(low)),
                )
            else:
                # Sign-extending the low half already gives the high half.
                i_type(
                    opcode,
                    target_register,
                    source_register,
                    constant_immediate_expression(low),
                )
        else:
            i_type(Tok.LUI_OPCODE, 1, 0, _upper_bits_of_expression(expression))
            if source_register != 0:
                r_type(Tok.ADDU_OPCODE, 1, 1, source_register)
            i_type(opcode, target_register, 1, _lower_bits_of_expression(expression))
    elif opcode_is_branch(opcode):
        # This only allows branches +/- 32K, which is not correct!
        i_type(
            opcode,
            target_register,
            source_register,
            _lower_bits_of_expression(expression),
        )
    elif global_pointer_offset is not None:
        i_type(
            Tok.ADDIU_OPCODE if opcode == Tok.LUI_OPCODE else opcode,
            target_register,
            regs.GLOBAL_POINTER_REGISTER,
            make_immediate_expression(global_pointer_offset, None, False),
        )
    elif source_register == 0 and opcode in frozenset(
        {Tok.ORI_OPCODE, Tok.ADDI_OPCODE, Tok.ADDIU_OPCODE, Tok.LUI_OPCODE}
    ):
        _produce_immediate(expression, target_register, value_known, value)
    else:
        _produce_immediate(expression, 1, value_known, value)
        r_type(
            pseudo_op.immediate_opcode_to_opcode(opcode),
            target_register,
            source_register,
            1,
        )


def _produce_immediate(
    expression: ImmediateExpression, target_register: int, value_known: bool, value: int
) -> None:
    """Load expr's 32-bit value into rt with lui and/or ori."""
    if value_known and value & 0xFFFF == 0:
        i_type(
            Tok.LUI_OPCODE, target_register, 0, _upper_bits_of_expression(expression)
        )
    elif value_known and machine.u32(value) & 0xFFFF0000 == 0:
        i_type(
            Tok.ORI_OPCODE, target_register, 0, _lower_bits_of_expression(expression)
        )
    else:
        i_type(Tok.LUI_OPCODE, 1, 0, _upper_bits_of_expression(expression))
        i_type(
            Tok.ORI_OPCODE, target_register, 1, _lower_bits_of_expression(expression)
        )


def j_type(opcode: int, target: ImmediateExpression) -> None:
    """Store a J-type instruction (the target is not PC-relative)."""
    instruction: Instruction = Instruction(opcode)
    target.offset = 0
    target.program_counter_relative = False
    instruction.expression = target.copy()
    if target.symbol is None or target.symbol.address != 0:
        symbol_table.resolve_a_label(target.symbol, instruction)
    else:
        symbol_table.record_instruction_uses_symbol(instruction, target.symbol)
    _store_instruction(instruction)


def _make_r_type(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> Instruction:
    """An R-type instruction with these fields."""
    instruction: Instruction = Instruction(opcode)
    instruction.set_source_register(source_register)
    instruction.set_target_register(target_register)
    instruction.set_destination_register(destination_register)
    return instruction


def r_type(
    opcode: int, destination_register: int, source_register: int, target_register: int
) -> None:
    """Store an R-type instruction."""
    _store_instruction(
        _make_r_type(opcode, destination_register, source_register, target_register)
    )


def r_coprocessor_type(
    opcode: int,
    floating_point_destination: int,
    floating_point_source: int,
    floating_point_target: int,
) -> None:
    """Store a coprocessor R-type instruction (FS in rd, FD in shamt)."""
    instruction: Instruction = _make_r_type(
        opcode, floating_point_source, 0, floating_point_target
    )
    instruction.set_shift_amount(floating_point_destination)
    _store_instruction(instruction)


def r_shift_amount_type(
    opcode: int, destination_register: int, target_register: int, shift_amount: int
) -> None:
    """Store a shift (the amount is masked to 5 bits)."""
    instruction: Instruction = _make_r_type(
        opcode, destination_register, 0, target_register
    )
    instruction.set_shift_amount(shift_amount & 0x1F)
    _store_instruction(instruction)


def r_condition_type(
    opcode: int,
    floating_point_source: int,
    floating_point_target: int,
    condition_code: int,
) -> None:
    """Store an FP compare, with its condition bits in the rs byte."""
    instruction: Instruction = _make_r_type(
        opcode, floating_point_source, 0, floating_point_target
    )
    instruction.set_shift_amount(condition_code << 2)
    if opcode not in _COMPARE_CONDITIONS:
        # Never reached: only FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION rows reach here, and the table holds all 32
        # of them (16 conditions, .s and .d).
        raise console.fatal_error(
            f"internal error: r_cond_type: not an FP compare: {opcode}\n"
        )
    else:
        instruction.set_source_register(_COMPARE_CONDITIONS[opcode])
    _store_instruction(instruction)


# --- breakpoints --------------------------------------------------------------

_break_instruction: Instruction | None = None


def _breakpoint_instruction() -> Instruction:
    """The one ``break 1`` instruction every breakpoint shares."""
    global _break_instruction
    if _break_instruction is None:
        _break_instruction = _make_r_type(Tok.BREAK_OPCODE, 1, 0, 0)
    return _break_instruction


def instruction_is_breakpoint(address: int) -> bool:
    """True if a breakpoint is set at addr."""
    return memory.read_instruction(address) is _breakpoint_instruction()


def set_breakpoint(address: int) -> Instruction | None:
    """Put a breakpoint at addr; return the instruction it replaced, or None."""
    breakpoint_instruction: Instruction = _breakpoint_instruction()
    traps.exception_occurred = False
    old: Instruction | None = memory.read_instruction(address)
    if old is breakpoint_instruction:
        return None
    memory.write_instruction(address, breakpoint_instruction)
    return None if traps.exception_occurred else old


# --- encoding and decoding ----------------------------------------------------


def _registers(value: int, shift: int) -> int:
    """A register number placed in its 5-bit field."""
    return (value & 0x1F) << shift


def instruction_encode(instruction: Instruction | None) -> int:
    """The 32-bit machine word of inst (signed), or 0 (C ``instruction_encode``)."""
    if instruction is None:
        return 0
    row: opcodes.OpcodeRow | None = opcodes.ROW_BY_TOKEN.get(instruction.opcode)
    if row is None:
        return 0
    a: int = row.encoding
    kind: int = row.kind
    immediate: int = instruction.immediate & 0xFFFF
    word: int
    match kind:
        case OpType.BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
            word = (
                a | _registers((instruction.target_register >> 2) << 2, 16) | immediate
            )
        case (
            OpType.BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION
            | OpType.SOURCE_REGISTER_IMMEDIATE_INSTRUCTION
        ):
            word = a | _registers(instruction.source_register, 21) | immediate
        case (
            OpType.TARGET_REGISTER_IMMEDIATE_INSTRUCTION
            | OpType.TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION
            | OpType.BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION
            | OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION
            | OpType.FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION
        ):
            word = (
                a
                | _registers(instruction.source_register, 21)
                | _registers(instruction.target_register, 16)
                | immediate
            )
        case OpType.SOURCE_REGISTER_INSTRUCTION:
            word = a | _registers(instruction.source_register, 21)
        case OpType.DESTINATION_REGISTER_INSTRUCTION:
            word = a | _registers(instruction.destination_register, 11)
        case (
            OpType.TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
            | OpType.FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
        ):
            word = (
                a
                | _registers(instruction.target_register, 16)
                | _registers(instruction.destination_register, 11)
            )
        case OpType.SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            word = (
                a
                | _registers(instruction.source_register, 21)
                | _registers(instruction.target_register, 16)
            )
        case OpType.DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
            word = (
                a
                | _registers(instruction.source_register, 21)
                | _registers(instruction.destination_register, 11)
            )
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
            word = (
                a
                | _registers(instruction.target_register, 16)
                | _registers(instruction.destination_register, 11)
                | _registers(instruction.shift_amount, 6)
            )
        case (
            OpType.DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION
            | OpType.DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION
            | OpType.MOVE_CONDITIONAL_TYPE_INSTRUCTION
        ):
            word = (
                a
                | _registers(instruction.source_register, 21)
                | _registers(instruction.target_register, 16)
                | _registers(instruction.destination_register, 11)
            )
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
            word = (
                a
                | _registers(instruction.destination_register, 11)
                | _registers(instruction.shift_amount, 6)
            )
        case OpType.FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
            word = (
                a
                | _registers(instruction.target_register, 16)
                | _registers(instruction.destination_register, 11)
                | _registers(instruction.shift_amount, 6)
                | instruction.source_register
            )
        case (
            OpType.FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION
            | OpType.FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION
        ):
            # For movf/movt the rt byte is cc<<2; their tf bit is in a already.
            word = (
                a
                | _registers(instruction.target_register, 16)
                | _registers(instruction.destination_register, 11)
                | _registers(instruction.shift_amount, 6)
            )
        case OpType.J_TYPE_INSTRUCTION:
            word = machine.u32(a) | instruction.target
        case OpType.NO_ARGUMENT_TYPE_INSTRUCTION:
            word = a
        case _:
            raise console.fatal_error("Unknown instruction type in inst_encoding\n")
    return machine.s32(word)


def _make_instruction(
    word: int,
    opcode: int,
    source_register: int = 0,
    target_register: int = 0,
    destination_register: int = 0,
    shift_amount: int = 0,
) -> Instruction:
    """A decoded instruction with these bytes (C mk_r_inst/mk_co_r_inst)."""
    instruction: Instruction = Instruction(opcode)
    (
        instruction.source_register,
        instruction.target_register,
        instruction.destination_register,
        instruction.shift_amount,
    ) = (
        source_register & 0xFF,
        target_register & 0xFF,
        destination_register & 0xFF,
        shift_amount & 0xFF,
    )
    instruction._bytes_changed()
    instruction.encoding = machine.s32(word)
    return instruction


def instruction_decode(word: int) -> Instruction:
    """The instruction a 32-bit machine word encodes (C ``instruction_decode``).

    The major opcode (the top 6 bits) decides which other bits complete the
    opcode; that key is looked up in the table of encodings.  An unknown
    word decodes to an instruction with opcode 0, which prints as
    ``<unknown instruction 0>``.
    """
    value: int = machine.u32(word)
    a: int = value & 0xFC000000
    match a:
        case 0 | 0x70000000:  # SPECIAL, SPECIAL2: the function field
            a |= value & 0x3F
        case 0x04000000:  # REGIMM: the rt field
            a |= value & 0x001F0000
        case 0x40000000:  # COP0
            a |= (value & 0x03E00000) | (value & 0x1F)
        case 0x44000000:  # COP1
            a |= value & 0x03E00000
            a |= (
                (value & 0x00010000)
                if value & 0xFF000000 == 0x45000000
                else (value & 0x3F)
            )
        case 0x48000000:  # COP2: the rs field
            a |= value & 0x03E00000
        case 0x4C000000:  # COP1X: the funct field
            a |= value & 0x3F
        case _:
            pass  # other major opcodes: the top 6 bits alone are the key
    token: int | None = opcodes.token_for_encoding(machine.s32(a))
    if token is None:
        return _make_instruction(value, 0)
    source_register: int = (value >> 21) & 0x1F
    target_register: int = (value >> 16) & 0x1F
    destination_register: int = (value >> 11) & 0x1F
    shift_amount: int = (value >> 6) & 0x1F
    immediate: int = value & 0xFFFF
    match kind_of(token):
        case OpType.BRANCH_COPROCESSOR_CONDITION_INSTRUCTION | OpType.TARGET_REGISTER_IMMEDIATE_INSTRUCTION | OpType.TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION | OpType.BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION | OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION | OpType.FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION:  # fmt: skip
            return _make_instruction(
                value,
                token,
                source_register,
                target_register,
                immediate & 0xFF,
                immediate >> 8,
            )
        case (
            OpType.BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION
            | OpType.SOURCE_REGISTER_IMMEDIATE_INSTRUCTION
        ):
            return _make_instruction(
                value, token, source_register, 0, immediate & 0xFF, immediate >> 8
            )
        case OpType.SOURCE_REGISTER_INSTRUCTION:
            return _make_instruction(value, token, source_register)
        case OpType.DESTINATION_REGISTER_INSTRUCTION:
            return _make_instruction(
                value, token, destination_register=destination_register
            )
        case (
            OpType.TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
            | OpType.FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION
        ):
            return _make_instruction(
                value,
                token,
                target_register=target_register,
                destination_register=destination_register,
            )
        case OpType.SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            return _make_instruction(value, token, source_register, target_register)
        case OpType.DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
            return _make_instruction(
                value, token, source_register, destination_register=destination_register
            )
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
            return _make_instruction(
                value, token, 0, target_register, destination_register, shift_amount
            )
        case (
            OpType.DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION
            | OpType.DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION
            | OpType.MOVE_CONDITIONAL_TYPE_INSTRUCTION
        ):
            return _make_instruction(
                value, token, source_register, target_register, destination_register
            )
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
            return _make_instruction(
                value, token, 0, 0, destination_register, shift_amount
            )
        case OpType.FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
            return _make_instruction(
                value,
                token,
                value & 0xF,
                target_register,
                destination_register,
                shift_amount,
            )
        case (
            OpType.FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION
            | OpType.FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION
        ):
            return _make_instruction(
                value, token, 0, target_register, destination_register, shift_amount
            )
        case OpType.J_TYPE_INSTRUCTION:
            target: int = value & 0x3FFFFFF
            return _make_instruction(
                value, token, target, target >> 8, target >> 16, target >> 24
            )
        case OpType.NO_ARGUMENT_TYPE_INSTRUCTION:
            return _make_instruction(value, token)
        case _:
            return _make_instruction(value, 0)


# --- disassembly ------------------------------------------------------------


def _register_number(number: int) -> str:
    """The ABI name of general register number ($0 is "r0")."""
    return regs.INTEGER_REGISTER_NAMES[number]


def _format_immediate_expression(
    expression: ImmediateExpression, base_register: int
) -> str:
    """The ``[symbol...]`` annotation of a symbolic immediate."""
    text: str = expression.symbol.name if expression.symbol is not None else ""
    if expression.program_counter_relative:
        text += f"-0x{machine.u32(-expression.offset):08x}"
    elif expression.offset < -10:
        text += f"-{-expression.offset} (-0x{machine.u32(-expression.offset):08x})"
    elif expression.offset > 10:
        text += f"+{expression.offset} (0x{machine.u32(expression.offset):08x})"
    else:
        pass  # an offset from -10 to 10 is not printed
    if (
        base_register != -1
        and expression.symbol is not None
        and (expression.offset > 10 or expression.offset < -10)
        and (expression.offset != 0 or base_register != 0)
    ):
        text += f"(${_register_number(base_register)})"
    return text


def _operands(instruction: Instruction, kind: int) -> str:
    """The operand text of inst for its operand shape (format_an_instruction)."""
    branch_offset: int = machine.s32(machine.s16(instruction.immediate) << 2)
    match kind:
        case OpType.BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
            return f"{instruction.target_register >> 2} {branch_offset}"
        case OpType.BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION:
            return f" ${_register_number(instruction.source_register)} {branch_offset}"
        case OpType.SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
            return f" ${_register_number(instruction.source_register)}, {instruction.immediate}"
        case OpType.TARGET_REGISTER_IMMEDIATE_INSTRUCTION:
            return f" ${_register_number(instruction.target_register)}, {instruction.immediate}"
        case OpType.TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
            return f" ${_register_number(instruction.target_register)}, ${_register_number(instruction.source_register)}, {instruction.immediate}"
        case OpType.BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION:
            return f" ${_register_number(instruction.source_register)}, ${_register_number(instruction.target_register)}, {branch_offset}"
        case OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION:
            return f" ${_register_number(instruction.target_register)}, {instruction.immediate}(${_register_number(instruction.source_register)})"
        case OpType.SOURCE_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.source_register)}"
        case OpType.DESTINATION_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}"
        case OpType.TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.target_register)}, ${_register_number(instruction.destination_register)}"
        case OpType.SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.source_register)}, ${_register_number(instruction.target_register)}"
        case OpType.DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}, ${_register_number(instruction.source_register)}"
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}, ${_register_number(instruction.target_register)}, {instruction.shift_amount}"
        case OpType.DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}, ${_register_number(instruction.source_register)}, ${_register_number(instruction.target_register)}"
        case OpType.DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}, ${_register_number(instruction.target_register)}, ${_register_number(instruction.source_register)}"
        case OpType.FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION:
            return f" $f{instruction.target_register}, {instruction.immediate}(${_register_number(instruction.source_register)})"
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
            return (
                f" $f{instruction.shift_amount}, $f{instruction.destination_register}"
            )
        case OpType.FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
            return f" ${_register_number(instruction.target_register)}, $f{instruction.destination_register}"
        case OpType.FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
            if instruction.shift_amount == 0:
                return f" $f{instruction.destination_register}, $f{instruction.target_register}"
            return f" {instruction.shift_amount >> 2}, $f{instruction.destination_register}, $f{instruction.target_register}"
        case OpType.FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION:
            return f" $f{instruction.shift_amount}, $f{instruction.destination_register}, $f{instruction.target_register}"
        case OpType.MOVE_CONDITIONAL_TYPE_INSTRUCTION:
            return f" ${_register_number(instruction.destination_register)}, ${_register_number(instruction.source_register)}, {instruction.target_register >> 2}"
        case OpType.FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION:
            if instruction.opcode in _FLOATING_POINT_MOVE_ON_GENERAL_PURPOSE_REGISTER:
                return f" $f{instruction.shift_amount}, $f{instruction.destination_register}, ${_register_number(instruction.target_register)}"
            return f" $f{instruction.shift_amount}, $f{instruction.destination_register}, {instruction.target_register >> 2}"
        case OpType.J_TYPE_INSTRUCTION:
            return f" 0x{(instruction.target << 2) & 0xFFFFFFFF:08x}"
        case OpType.NO_ARGUMENT_TYPE_INSTRUCTION:
            return ""
        case _:
            raise console.fatal_error("Unknown instruction type in print_inst\n")


def format_an_instruction(instruction: Instruction | None, address: int) -> str:
    """One line of disassembly: address, encoding, mnemonic, operands, source.

    The source comment starts at column 57 (counting the tab as one column),
    and a breakpoint is shown as a ``*`` before the line.
    """
    if address != 0 and instruction_is_breakpoint(address):
        spim_utils.delete_breakpoint(address)
        text: str = "*" + format_an_instruction(
            memory.read_instruction(address), address
        )
        spim_utils.add_breakpoint(address)
        return text
    line: str = f"[0x{address:08x}]\t"
    if instruction is None:
        return line + "<none>\n"
    row: opcodes.OpcodeRow | None = opcodes.ROW_BY_TOKEN.get(instruction.opcode)
    if row is None:
        return line + f"<unknown instruction {machine.s16(instruction.opcode)}>\n"
    line += f"0x{machine.u32(instruction.encoding):08x}  {row.name}"
    if (
        row.kind == OpType.DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION
        and instruction.destination_register == 0
        and instruction.target_register == 0
        and instruction.shift_amount == 0
    ):
        line = line[:-3] + "nop"  # sll $0, $0, 0 is the canonical nop
    else:
        line += _operands(instruction, row.kind)
    if instruction.expression is not None and instruction.expression.symbol is not None:
        base: int = (
            instruction.source_register
            if opcode_is_load_store(instruction.opcode)
            else -1
        )
        line += f" [{_format_immediate_expression(instruction.expression, base)}]"
    if instruction.source is not None:
        line = line.ljust(57) + "; " + instruction.source
    return line + "\n"


def instruction_to_string(address: int) -> str:
    """The disassembly line of the instruction at addr.

    The read is side-effect free: showing an instruction neither raises nor
    clears the exception the program's own step raised.
    """
    in_text: bool
    instruction: Instruction | None
    in_text, instruction = memory.peek_instruction(address)
    if not in_text:
        console.error(
            f"Can't print instruction not in text segment (0x{address:08x})\n"
        )
        return ""
    return format_an_instruction(instruction, address)


def print_instruction(address: int) -> None:
    """Print the instruction at addr on stderr."""
    console.message(instruction_to_string(address))
