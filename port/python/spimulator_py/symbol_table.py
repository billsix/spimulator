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
    instruction,
    machine,
    memory,
    options,
    parser,
    pseudo_op,
)
from . import memory_layout as layout
from .inst import ImmExpr, Instruction

HASHBITS: int = 30
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

    instruction: Instruction | None
    addr: int
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
        gp_flag: addressed off $gp.
        const_flag: a constant, not an address.
        uses: forward references waiting for the definition (newest first).
    """

    name: str
    addr: int = 0
    global_flag: bool = False
    gp_flag: bool = False
    const_flag: bool = False
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
    return (h & ((1 << HASHBITS) - 1)) % LABEL_HASH_TABLE_SIZE


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
    if not label.gp_flag:
        if label.addr != 0:
            pseudo_op.parse_error("Label is defined for the second time")
            return label
        label.addr = address
    assembler_event.label_def(address, label.name, label.global_flag)
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


def record_inst_uses_symbol(inst: Instruction, sym: Label) -> None:
    """Remember that inst uses the still-undefined sym."""
    use: LabelUse
    if parser.data_dir:
        use = LabelUse(inst.copy(), data.current_data_pc())
    else:
        use = LabelUse(inst, instruction.current_text_pc())
    sym.uses.insert(0, use)
    assembler_event.forward_ref(use.addr, sym.name)


def record_data_uses_symbol(location: int, sym: Label, offset: int, width: int) -> None:
    """Remember that the width-byte datum at location holds sym + offset."""
    sym.uses.insert(0, LabelUse(None, location, offset, width))
    assembler_event.forward_ref(location, sym.name)


def _write_data_value(at: int, value: int, width: int) -> None:
    """Store the low width bytes of value at at, little-endian, bytewise."""
    for i in range(width):
        memory.write_byte(machine.u32(at + i), (machine.u32(value) >> (8 * i)) & 0xFF)


def resolve_label_uses(sym: Label) -> None:
    """Patch every recorded use of the now-defined sym."""
    for use in sym.uses:
        if use.instruction is None:
            _write_data_value(use.addr, machine.s32(sym.addr) + use.offset, use.width)
        else:
            _resolve_a_label_sub(sym, use.instruction, use.addr)
        if (
            use.instruction is not None
            and layout.DATA_BOT <= use.addr < memory.stack_bot
        ):
            memory.write_word(use.addr, instruction.inst_encode(use.instruction))
        assembler_event.forward_resolved(use.addr, sym.name, machine.s32(sym.addr))
    sym.uses = []


def resolve_a_label(sym: Label | None, inst: Instruction) -> None:
    """Resolve inst's expression now, at the current text or data PC."""
    pc: int = (
        data.current_data_pc() if parser.data_dir else instruction.current_text_pc()
    )
    _resolve_a_label_sub(sym, inst, pc)


def _resolve_a_label_sub(sym: Label | None, inst: Instruction, pc: int) -> None:
    """Fill inst's immediate (or target) from its expression, for address pc."""
    expr: ImmExpr | None = inst.expr
    assert expr is not None
    if expr.pc_relative:
        expr.offset = machine.s32(-pc)  # the instruction may have moved
    if expr.symbol is not None and expr.symbol.addr == 0:
        console.error(f"Resolving undefined symbol: {expr.symbol.name}\n")
        return
    value: int
    field_mask: int
    if instruction.opcode_is_branch(inst.opcode):
        # The whole byte offset: the expression asks for its low 16 bits,
        # which would truncate a branch more than 32 KB away.
        whole: ImmExpr = expr.copy()
        whole.bits = 0
        word_offset: int = machine.c_div(instruction.eval_imm_expr(whole), 4)
        if options.delayed_branches:
            word_offset -= 1
        if not -32768 <= word_offset <= 32767:
            console.error(
                "Branch target is out of range (more than 128 KB away) at pc"
                f" 0x{pc:x}\n"
            )
        value = word_offset & 0xFFFF
        field_mask = 0xFFFF
    elif instruction.opcode_is_jump(inst.opcode):
        value = instruction.eval_imm_expr(expr)
        if machine.u32(value) & 0xF0000000 != pc & 0xF0000000:
            console.error(
                "Target of jump differs in high-order 4 bits from instruction pc"
                f" 0x{pc:x}\n"
            )
        value = (value & 0x0FFFFFFF) >> 2
        field_mask = -1
    elif instruction.opcode_is_load_store(inst.opcode):
        value = instruction.eval_imm_expr(expr)
        field_mask = 0xFFFF
        if value & 0x8000:
            # The load sign-extends its offset: add 1 to the lui's upper half.
            _compensate_lui(inst, pc)
    else:
        value = instruction.eval_imm_expr(expr)
        field_mask = 0xFFFF
    high: int = machine.s32(value & ~field_mask)
    if high not in (0, machine.s32(0xFFFF0000)):
        console.error("Immediate value is too large for field: ")
        instruction.print_inst(pc)
    if instruction.opcode_is_jump(inst.opcode):
        inst.set_target(value)
    else:
        inst.set_imm(value)
    inst.set_encoding(instruction.inst_encode(inst))


def _compensate_lui(inst: Instruction, pc: int) -> None:
    """Bump the matching ``lui`` before a load/store whose offset is negative."""
    assert inst.expr is not None
    prev: Instruction | None = memory.read_inst(machine.u32(pc - 4))
    prev_prev: Instruction | None = memory.read_inst(machine.u32(pc - 8))
    lui: int = instruction.Tok.LUI_OPCODE
    if (
        prev is not None
        and prev.opcode == lui
        and prev.expr is not None
        and inst.expr.symbol is prev.expr.symbol
        and prev.imm == 0
    ):
        prev.expr.offset += 0x10000
    elif (
        prev_prev is not None
        and prev_prev.opcode == lui
        and prev_prev.expr is not None
        and inst.expr.symbol is prev_prev.expr.symbol
        and prev_prev.imm == 0
    ):
        # An addu of the index register sits between the lui and the load.
        prev_prev.expr.offset += 0x10000


def flush_local_labels(issue_undef_warnings: bool) -> None:
    """Drop the file's non-global labels, so the next file cannot see them."""
    for label in _local_labels:
        entry: Label | None = _by_name.get(label.name)
        if entry is None:
            continue
        bucket: list[Label] = _buckets[hash_slot(label.name)]
        bucket.remove(entry)
        del _by_name[label.name]
        if issue_undef_warnings and entry.addr == 0 and not entry.const_flag:
            console.error(f"Warning: local symbol {entry.name} was not defined\n")
    _local_labels.clear()


def find_symbol_address(name: str) -> int:
    """The address of the label name, or 0 if it is undefined."""
    return lookup_label(name).addr


def all_labels() -> list[Label]:
    """Every label, in the C engine's bucket order."""
    return [label for bucket in _buckets for label in bucket]


def print_symbols() -> None:
    """List every label on stderr (the REPL's print_symbols)."""
    for label in all_labels():
        console.message(
            f"{'g\t' if label.global_flag else '\t'}{label.name}"
            f" at 0x{machine.u32(label.addr):08x}\n"
        )


def undefined_symbol_string() -> str | None:
    """The names of the undefined labels, one per line, or None if none."""
    names: str = "".join(f"{label.name}\n" for label in all_labels() if label.addr == 0)
    return names or None
