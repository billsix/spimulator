"""Labels and forward references (the C engine's symbol-table.c).

A label is created the first time it is mentioned.  Its address is 0 until it
is defined; an instruction or datum that uses it before then is recorded as a
"use" and patched when the definition arrives (resolve_label_uses).

The table is a hash table with the C engine's own hash function and chain
order, because ``print_symbols`` and the "undefined symbols" message list
labels in bucket order.  A Python ``dict`` gives the fast lookup; the
buckets keep the order.
"""

from dataclasses import dataclass, field

from . import (
    assembler_event,
    console,
    data,
    machine,
    memory,
    options,
    parser,
    pseudo_op,
)
from . import (
    instruction as instruction_module,
)
from . import memory_layout as layout
from .inst import ImmediateExpression, Instruction

HASH_BITS: int = 30
LABEL_HASH_TABLE_SIZE: int = 8191


@dataclass(eq=False)
class LabelUse:
    """An instruction or datum that used a label before it was defined.

    Attributes:
        instruction: the instruction to patch, or None for data.
        addr: where the instruction or datum is.
        offset: data only, the constant added to the label (``lab+4``).
        width: data only, the bytes stored (1, 2 or 4).
    """

    instruction_module: Instruction | None
    address: int
    offset: int = 0
    width: int = 0


@dataclass(eq=False)
class Label:
    """A symbol (C ``label``).

    Attributes:
        name: the label's name.
        addr: its address, 0 while undefined.  For a constant
            (``X = 5``) the constant's value (as an unsigned word).
        global_flag: declared ``.globl``.
        global_pointer_flag: addressed off $gp.
        constant_flag: a constant, not an address.
        uses: forward references waiting for the definition (newest first).
    """

    name: str
    address: int = 0
    global_flag: bool = False
    global_pointer_flag: bool = False
    constant_flag: bool = False
    uses: list[LabelUse] = field(default_factory=list)


_buckets: list[list[Label]] = [[] for _ in range(LABEL_HASH_TABLE_SIZE)]
_by_name: dict[str, Label] = {}
_local_labels: list[Label] = []  # newest first, as C's linked list


def initialize_symbol_table() -> None:
    """Forget every label."""
    for bucket in _buckets:
        bucket.clear()
    _by_name.clear()
    _local_labels.clear()


def hash_slot(name: str) -> int:
    """The bucket of name, computed as the C engine computes it.

    C adds each ``char`` as ``(unsigned)``, and ``char`` is signed on x86,
    so a byte >= 0x80 adds ``0xffffff80 + ...``: hence the sign fix below.

    Example:
        >>> hash_slot("main")
        6536
    """
    h: int = len(name)
    for char in name:
        code: int = ord(char)
        h = (h * 613 + (code - 256 if code >= 128 else code)) & 0xFFFFFFFF
    return (h & ((1 << HASH_BITS) - 1)) % LABEL_HASH_TABLE_SIZE


def label_is_defined(name: str) -> Label | None:
    """The label called name, or None if it has never been mentioned."""
    return _by_name.get(name)


def lookup_label(name: str) -> Label:
    """The label called name, created (undefined) if it is new."""
    found: Label | None = _by_name.get(name)
    if found is not None:
        return found
    created: Label = Label(name)
    _buckets[hash_slot(name)].insert(0, created)  # new labels go first
    _by_name[name] = created
    return created


def record_label(name: str, address: int, resolve_uses: bool) -> Label:
    """Define name at address; with resolve_uses, patch its forward uses."""
    label: Label = lookup_label(name)
    if not label.global_pointer_flag:
        if label.address != 0:
            pseudo_op.parse_error("Label is defined for the second time")
            return label
        label.address = address
    assembler_event.label_definition(address, label.name, label.global_flag)
    if resolve_uses:
        resolve_label_uses(label)
    if not label.global_flag:
        _local_labels.insert(0, label)
    return label


def make_label_global(name: str) -> Label:
    """Mark name as global (``.globl``)."""
    label: Label = lookup_label(name)
    label.global_flag = True
    return label


def record_instruction_uses_symbol(instruction: Instruction, symbol: Label) -> None:
    """Remember that inst uses the still-undefined sym."""
    use: LabelUse
    if parser.data_directive:
        use = LabelUse(instruction.copy(), data.current_data_location())
    else:
        use = LabelUse(instruction, instruction_module.current_text_location())
    symbol.uses.insert(0, use)
    assembler_event.forward_ref(use.address, symbol.name)


def record_data_uses_symbol(
    location: int, symbol: Label, offset: int, width: int
) -> None:
    """Remember that the width-byte datum at location holds sym + offset."""
    symbol.uses.insert(0, LabelUse(None, location, offset, width))
    assembler_event.forward_ref(location, symbol.name)


def _write_data_value(at: int, value: int, width: int) -> None:
    """Store the low width bytes of value at at, little-endian, bytewise."""
    for i in range(width):
        memory.write_byte(machine.u32(at + i), (machine.u32(value) >> (8 * i)) & 0xFF)


def resolve_label_uses(symbol: Label) -> None:
    """Patch every recorded use of the now-defined sym."""
    for use in symbol.uses:
        if use.instruction_module is None:
            _write_data_value(
                use.address, machine.s32(symbol.address) + use.offset, use.width
            )
        else:
            _resolve_a_label_subtract(symbol, use.instruction_module, use.address)
        if (
            use.instruction_module is not None
            and layout.DATA_BOTTOM <= use.address < memory.stack_bottom
        ):
            memory.write_word(
                use.address,
                instruction_module.instruction_encode(use.instruction_module),
            )
        assembler_event.forward_resolved(
            use.address, symbol.name, machine.s32(symbol.address)
        )
    symbol.uses = []


def resolve_a_label(symbol: Label | None, instruction: Instruction) -> None:
    """Resolve inst's expression now, at the current text or data PC."""
    program_counter: int = (
        data.current_data_location()
        if parser.data_directive
        else instruction_module.current_text_location()
    )
    _resolve_a_label_subtract(symbol, instruction, program_counter)


def _resolve_a_label_subtract(
    symbol: Label | None, instruction: Instruction, program_counter: int
) -> None:
    """Fill inst's immediate (or target) from its expression, for address pc."""
    expression: ImmediateExpression | None = instruction.expression
    if expression is None:
        raise AssertionError("_resolve_a_label_sub: expr is None")
    if expression.program_counter_relative:
        expression.offset = machine.s32(
            -program_counter
        )  # the instruction may have moved
    if expression.symbol is not None and expression.symbol.address == 0:
        console.error(f"Resolving undefined symbol: {expression.symbol.name}\n")
        return
    value: int
    field_mask: int
    if instruction_module.opcode_is_branch(instruction.opcode):
        # The whole byte offset: the expression asks for its low 16 bits,
        # which would truncate a branch more than 32 KB away.
        whole: ImmediateExpression = expression.copy()
        whole.bits = 0
        word_offset: int = machine.c_divide(
            instruction_module.evaluate_immediate_expression(whole), 4
        )
        if options.delayed_branches:
            word_offset -= 1
        if not -32768 <= word_offset <= 32767:
            console.error(
                "Branch target is out of range (more than 128 KB away) at pc"
                f" 0x{program_counter:x}\n"
            )
        value = word_offset & 0xFFFF
        field_mask = 0xFFFF
    elif instruction_module.opcode_is_jump(instruction.opcode):
        value = instruction_module.evaluate_immediate_expression(expression)
        if machine.u32(value) & 0xF0000000 != program_counter & 0xF0000000:
            console.error(
                "Target of jump differs in high-order 4 bits from instruction pc"
                f" 0x{program_counter:x}\n"
            )
        value = (value & 0x0FFFFFFF) >> 2
        field_mask = -1
    elif instruction_module.opcode_is_load_store(instruction.opcode):
        value = instruction_module.evaluate_immediate_expression(expression)
        field_mask = 0xFFFF
        if value & 0x8000:
            # The load sign-extends its offset: add 1 to the lui's upper half.
            _compensate_lui(instruction, program_counter)
    else:
        value = instruction_module.evaluate_immediate_expression(expression)
        field_mask = 0xFFFF
    # The bits above the field: all clear or all set when the value fits.
    if machine.s32(value & ~field_mask) not in (0, machine.s32(0xFFFF0000)):
        console.error("Immediate value is too large for field: ")
        instruction_module.print_instruction(program_counter)
    if instruction_module.opcode_is_jump(instruction.opcode):
        instruction.set_target(value)
    else:
        instruction.set_immediate(value)
    instruction.set_encoding(instruction_module.instruction_encode(instruction))


def _compensate_lui(instruction: Instruction, program_counter: int) -> None:
    """Bump the matching ``lui`` before a load/store whose offset is negative."""
    if instruction.expression is None:
        raise AssertionError("_compensate_lui: inst.expr is None")
    previous: Instruction | None = memory.read_instruction(
        machine.u32(program_counter - 4)
    )
    previous_previous: Instruction | None = memory.read_instruction(
        machine.u32(program_counter - 8)
    )
    lui: int = instruction_module.Tok.LUI_OPCODE
    if (
        previous is not None
        and previous.opcode == lui
        and previous.expression is not None
        and instruction.expression.symbol is previous.expression.symbol
        and previous.immediate == 0
    ):
        previous.expression.offset += 0x10000
    elif (
        previous_previous is not None
        and previous_previous.opcode == lui
        and previous_previous.expression is not None
        and instruction.expression.symbol is previous_previous.expression.symbol
        and previous_previous.immediate == 0
    ):
        # An addu of the index register sits between the lui and the load.
        previous_previous.expression.offset += 0x10000
    else:
        pass  # no matching lui: the offset needs no compensation


def flush_local_labels(issue_undefined_warnings: bool) -> None:
    """Drop the file's non-global labels, so the next file cannot see them."""
    for label in _local_labels:
        entry: Label | None = _by_name.get(label.name)
        if entry is None:
            continue
        _buckets[hash_slot(label.name)].remove(entry)
        del _by_name[label.name]
        if issue_undefined_warnings and entry.address == 0 and not entry.constant_flag:
            console.error(f"Warning: local symbol {entry.name} was not defined\n")
    _local_labels.clear()


def find_symbol_address(name: str) -> int:
    """The address of the label name, or 0 if it is undefined."""
    return lookup_label(name).address


def all_labels() -> list[Label]:
    """Every label, in the C engine's bucket order."""
    return [label for bucket in _buckets for label in bucket]


def print_symbols() -> None:
    """List every label on stderr (the REPL's print_symbols)."""
    for label in all_labels():
        console.message(
            f"{'g\t' if label.global_flag else '\t'}{label.name}"
            f" at 0x{machine.u32(label.address):08x}\n"
        )


def undefined_symbol_string() -> str | None:
    """The names of the undefined labels, one per line, or None if none."""
    names: str = "".join(
        f"{label.name}\n" for label in all_labels() if label.address == 0
    )
    return names or None
