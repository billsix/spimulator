"""The data-segment directives: where data goes and how it is stored (data.c).

The first 64 KB of the user data segment are the "small data" area that
$gp points into (its middle); ordinary data starts after it, at
DATA_BOT + 64K.  A ``.word``, ``.half``, ``.float`` or ``.double`` is
aligned to its size unless ``.align 0`` turned automatic alignment off; a
misaligned store is split into smaller stores, little-endian.
"""

from . import assembler_event, machine, memory, options, parser
from . import memory_layout as layout
from . import registers as regs

_next_data_pc: int = 0
_next_k_data_pc: int = 0
_in_kernel: bool = False
_next_gp_item_addr: int = 0
_enable_data_auto_alignment: bool = True

# One directive fires one assembler event, even when a misaligned .word is
# stored as two halves: the depth counts the nested store calls.
_data_store_depth: int = 0


def _data_pc() -> int:
    """The address of the next datum in the current data segment."""
    return _next_k_data_pc if _in_kernel else _next_data_pc


def user_kernel_data_segment(to_kernel: bool) -> None:
    """Send the next data to the kernel (True) or the user data segment."""
    global _in_kernel
    _in_kernel = to_kernel


def end_of_assembly_file() -> None:
    """Reset the per-file state: back to user data, auto-alignment on."""
    global _in_kernel, _enable_data_auto_alignment
    _in_kernel = False
    _enable_data_auto_alignment = True


def data_begins_at_point(addr: int) -> None:
    """Start the user data at addr (after the 64 KB $gp area when not bare)."""
    global _next_data_pc, _next_gp_item_addr
    if options.bare_machine:
        _next_data_pc = addr
    else:
        _next_gp_item_addr = addr
        memory.gp_midpoint = addr + 32 * layout.KILO
        regs.cpu.gpr[regs.REG_GP] = machine.s32(memory.gp_midpoint)
        _next_data_pc = addr + 64 * layout.KILO


def k_data_begins_at_point(addr: int) -> None:
    """Start the kernel data at addr."""
    global _next_k_data_pc
    _next_k_data_pc = addr


def align_data(alignment: int) -> None:
    """Round the data PC up to a 2**alignment boundary; 0 disables auto-align."""
    global _enable_data_auto_alignment, _next_data_pc, _next_k_data_pc
    if alignment == 0:
        _enable_data_auto_alignment = False
        return
    mask: int = (0xFFFFFFFF << alignment) & 0xFFFFFFFF
    if _in_kernel:
        _next_k_data_pc = (_next_k_data_pc + (1 << alignment) - 1) & mask
        parser.fix_current_label_address(_next_k_data_pc)
    else:
        _next_data_pc = (_next_data_pc + (1 << alignment) - 1) & mask
        parser.fix_current_label_address(_next_data_pc)


def set_data_alignment(alignment: int) -> None:
    """Align the data PC, unless ``.align 0`` turned auto-alignment off."""
    if _enable_data_auto_alignment:
        align_data(alignment)


def enable_data_alignment() -> None:
    """Turn automatic alignment back on (``.data``/``.kdata``)."""
    global _enable_data_auto_alignment
    _enable_data_auto_alignment = True


def set_data_pc(addr: int) -> None:
    """Put the next datum at addr (``.data ADDR``)."""
    global _next_data_pc, _next_k_data_pc
    if _in_kernel:
        _next_k_data_pc = addr
    else:
        _next_data_pc = addr


def current_data_pc() -> int:
    """The address the next datum will be stored at."""
    return _data_pc()


def increment_data_pc(delta: int) -> None:
    """Advance the data PC by delta bytes, growing the segment if needed."""
    global _next_data_pc, _next_k_data_pc
    if _in_kernel:
        _next_k_data_pc = (_next_k_data_pc + delta) & 0xFFFFFFFF
        if memory.k_data_top <= _next_k_data_pc:
            memory.expand_k_data(_round_up_64k(_next_k_data_pc - memory.k_data_top + 1))
    else:
        _next_data_pc = (_next_data_pc + delta) & 0xFFFFFFFF
        if memory.data_top <= _next_data_pc:
            memory.expand_data(_round_up_64k(_next_data_pc - memory.data_top + 1))


def _round_up_64k(value: int) -> int:
    """value rounded up to a multiple of 64 KB, as a signed int (C ``(int)``)."""
    return machine.s32((value + 0xFFFF) & ~0xFFFF)


def store_string(string: bytes, null_terminate: bool) -> None:
    """Store a ``.ascii``/``.asciiz`` string, byte by byte."""
    global _data_store_depth
    at: int = _data_pc()
    _data_store_depth += 1
    for byte in string:
        store_byte(machine.s8(byte))
    if null_terminate:
        store_byte(0)
    _data_store_depth -= 1
    if _data_store_depth == 0:
        assembler_event.data_string(at, len(string), null_terminate)


def store_byte(value: int) -> None:
    """Store a ``.byte``."""
    at: int = _data_pc()
    memory.write_byte(at, value)
    increment_data_pc(1)
    if _data_store_depth == 0:
        assembler_event.data_byte(at, value)


def store_half(value: int) -> None:
    """Store a ``.half``, as two bytes if the PC is odd."""
    global _data_store_depth
    at: int = _data_pc()
    outer: bool = _data_store_depth == 0
    _data_store_depth += 1
    if _data_pc() & 1:
        store_byte(value & 0xFF)
        store_byte((value >> 8) & 0xFF)
    else:
        memory.write_half(at, value)
        increment_data_pc(2)
    _data_store_depth -= 1
    if outer:
        assembler_event.data_half(at, value)


def store_word(value: int) -> None:
    """Store a ``.word``, as two halves if the PC is not word-aligned."""
    global _data_store_depth
    at: int = _data_pc()
    outer: bool = _data_store_depth == 0
    _data_store_depth += 1
    if _data_pc() & 3:
        store_half(value & 0xFFFF)
        store_half((value >> 16) & 0xFFFF)
    else:
        memory.write_word(at, value)
        increment_data_pc(4)
    _data_store_depth -= 1
    if outer:
        assembler_event.data_word(at, value)


def store_double(value: float) -> None:
    """Store a ``.double`` as its two little-endian words."""
    global _data_store_depth
    at: int = _data_pc()
    outer: bool = _data_store_depth == 0
    _data_store_depth += 1
    low: int
    high: int
    low, high = machine.double_words(value)
    if _data_pc() & 7:
        store_word(low)
        store_word(high)
    else:
        memory.write_word(_data_pc(), low)
        increment_data_pc(4)
        memory.write_word(_data_pc(), high)
        increment_data_pc(4)
    _data_store_depth -= 1
    if outer:
        assembler_event.data_double(at, value)


def store_float(value: float) -> None:
    """Store a ``.float`` (the double rounded to single precision)."""
    bits: int = machine.float_bits(value)
    if _data_pc() & 3:
        store_half(bits & 0xFFFF)
        store_half((bits >> 16) & 0xFFFF)
    else:
        memory.write_word(_data_pc(), bits)
        increment_data_pc(4)
