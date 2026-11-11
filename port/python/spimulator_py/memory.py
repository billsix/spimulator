"""The simulated memory (the C engine's memory.c).

Five segments, each a Python object covering a range of addresses:

* text and kernel text hold :class:`Instruction` objects, one per word;
* data, stack and kernel data are ``bytearray`` s of little-endian bytes.

The read and write functions take a 32-bit unsigned address.  An access
outside every segment, or misaligned, goes to the "bad" path, which raises
the MIPS exception the hardware would (AdEL/AdES for misalignment, DBE/IBE
for a bad address), grows the stack when the address is just below it, or
reaches the memory-mapped console registers.
"""

import struct

from . import console, instruction, machine, traps
from . import memory_layout as layout
from . import registers as regs
from .inst import Instruction
from .traps import ExcCode

_WORD: struct.Struct = struct.Struct("<i")
_HALF: struct.Struct = struct.Struct("<h")

text_seg: list[Instruction | None] = []
text_top: int = layout.TEXT_BOT
k_text_seg: list[Instruction | None] = []
k_text_top: int = layout.K_TEXT_BOT

data_seg: bytearray = bytearray()
data_top: int = layout.DATA_BOT
gp_midpoint: int = 0
stack_seg: bytearray = bytearray()
stack_bot: int = layout.STACK_TOP
k_data_seg: bytearray = bytearray()
k_data_top: int = layout.K_DATA_BOT

text_modified: bool = False
data_modified: bool = False

_data_size_limit: int = 0
_stack_size_limit: int = 0
_k_data_size_limit: int = 0


def _round_up(value: int, boundary: int) -> int:
    """value rounded up to a multiple of boundary (a power of two)."""
    return (value + boundary - 1) & ~(boundary - 1)


def make_memory(
    text_size: int,
    data_size: int,
    data_limit: int,
    stack_size: int,
    stack_limit: int,
    k_text_size: int,
    k_data_size: int,
    k_data_limit: int,
) -> None:
    """Allocate (or clear) every segment with the given sizes and limits."""
    global text_seg, text_top, data_seg, data_top, stack_seg, stack_bot
    global k_text_seg, k_text_top, k_data_seg, k_data_top
    global _data_size_limit, _stack_size_limit, _k_data_size_limit
    global text_modified, data_modified
    data_size = _round_up(max(data_size, 65536), 4)
    text_seg = [None] * ((text_size + 3) // 4)
    text_top = layout.TEXT_BOT + text_size
    data_seg = bytearray(data_size)
    data_top = layout.DATA_BOT + data_size
    _data_size_limit = data_limit
    stack_size = _round_up(stack_size, 4)
    stack_seg = bytearray(stack_size)
    stack_bot = layout.STACK_TOP - stack_size
    _stack_size_limit = stack_limit
    k_text_seg = [None] * ((k_text_size + 3) // 4)
    k_text_top = layout.K_TEXT_BOT + k_text_size
    k_data_size = _round_up(k_data_size, 4)
    k_data_seg = bytearray(k_data_size)
    k_data_top = layout.K_DATA_BOT + k_data_size
    _k_data_size_limit = k_data_limit
    text_modified = True
    data_modified = True


def expand_data(addl_bytes: int) -> None:
    """Grow the data segment by addl_bytes (``sbrk``).

    Raises:
        RunError: the growth is negative or passes the data limit.
    """
    global data_top
    delta: int = _round_up(addl_bytes, 4)
    old_size: int = data_top - layout.DATA_BOT
    new_size: int = machine.s32(old_size + delta)
    if addl_bytes < 0 or new_size > _data_size_limit:
        console.error(
            f"Can't expand data segment by {addl_bytes} bytes to {new_size} bytes\n"
        )
        raise console.run_error(f"Use -ldata # with # > {new_size}\n")
    data_seg.extend(bytes(new_size - old_size))
    data_top += delta


def expand_stack(addl_bytes: int) -> None:
    """Grow the stack downward: at least double it, or by addl_bytes.

    Raises:
        RunError: the growth passes the stack limit.
    """
    global stack_seg, stack_bot
    delta: int = _round_up(addl_bytes, 4)
    old_size: int = layout.STACK_TOP - stack_bot
    new_size: int = old_size + max(delta, old_size)
    if addl_bytes < 0 or new_size > _stack_size_limit:
        raise console.run_error(
            f"Can't expand stack segment by {addl_bytes} bytes to {new_size}"
            f" bytes.\nUse -lstack # with # > {new_size}\n"
        )
    stack_seg = bytearray(new_size - old_size) + stack_seg
    stack_bot -= new_size - old_size


def expand_k_data(addl_bytes: int) -> None:
    """Grow the kernel data segment by addl_bytes.

    Raises:
        RunError: the growth passes the kernel data limit.
    """
    global k_data_top
    delta: int = _round_up(addl_bytes, 4)
    old_size: int = k_data_top - layout.K_DATA_BOT
    new_size: int = old_size + delta
    if addl_bytes < 0 or new_size > _k_data_size_limit:
        raise console.run_error(
            f"Can't expand kernel data segment by {addl_bytes} bytes to {new_size}"
            f" bytes.\nUse -lkdata # with # > {new_size}\n"
        )
    k_data_seg.extend(bytes(new_size - old_size))
    k_data_top += delta


def reference(addr: int) -> tuple[bytearray | None, int]:
    """The segment holding addr and the offset in it (C ``mem_reference``).

    Used by the syscalls that pass a buffer address (print_string, read,
    write).  A text address yields (None, offset): the C engine would hand
    out a pointer into its instruction array.

    Raises:
        RunError: addr is in no segment.
    """
    if layout.TEXT_BOT <= addr < text_top:
        return None, addr - layout.TEXT_BOT
    if layout.DATA_BOT <= addr < data_top:
        return data_seg, addr - layout.DATA_BOT
    if stack_bot <= addr < layout.STACK_TOP:
        return stack_seg, addr - stack_bot
    if layout.K_TEXT_BOT <= addr < k_text_top:
        return None, addr - layout.K_TEXT_BOT
    if layout.K_DATA_BOT <= addr < k_data_top:
        return k_data_seg, addr - layout.K_DATA_BOT
    raise console.run_error("Memory address out of bounds\n")


# --- reads -------------------------------------------------------------------


def read_inst(addr: int) -> Instruction | None:
    """The instruction at addr; a bad address raises IBE (C ``mem_read_inst``)."""
    if not addr & 3:
        if layout.TEXT_BOT <= addr < text_top:
            return text_seg[(addr - layout.TEXT_BOT) >> 2]
        if layout.K_TEXT_BOT <= addr < k_text_top:
            return k_text_seg[(addr - layout.K_TEXT_BOT) >> 2]
    traps.raise_exception(ExcCode.IBE)
    regs.cpu.cpr[0][regs.CP0_BADVADDR] = machine.s32(addr)
    return instruction.inst_decode(0)


def read_byte(addr: int) -> int:
    """The signed byte at addr (C ``mem_read_byte``)."""
    if layout.DATA_BOT <= addr < data_top:
        return machine.s8(data_seg[addr - layout.DATA_BOT])
    if stack_bot <= addr < layout.STACK_TOP:
        return machine.s8(stack_seg[addr - stack_bot])
    if layout.K_DATA_BOT <= addr < k_data_top:
        return machine.s8(k_data_seg[addr - layout.K_DATA_BOT])
    return _bad_read(addr, 0)


def read_half(addr: int) -> int:
    """The signed halfword at addr (C ``mem_read_half``)."""
    if not addr & 1:
        if layout.DATA_BOT <= addr < data_top:
            return _HALF.unpack_from(data_seg, addr - layout.DATA_BOT)[0]
        if stack_bot <= addr < layout.STACK_TOP:
            return _HALF.unpack_from(stack_seg, addr - stack_bot)[0]
        if layout.K_DATA_BOT <= addr < k_data_top:
            return _HALF.unpack_from(k_data_seg, addr - layout.K_DATA_BOT)[0]
    return _bad_read(addr, 1)


def read_word(addr: int) -> int:
    """The signed word at addr (C ``mem_read_word``)."""
    if not addr & 3:
        if layout.DATA_BOT <= addr < data_top:
            return _WORD.unpack_from(data_seg, addr - layout.DATA_BOT)[0]
        if stack_bot <= addr < layout.STACK_TOP:
            return _WORD.unpack_from(stack_seg, addr - stack_bot)[0]
        if layout.K_DATA_BOT <= addr < k_data_top:
            return _WORD.unpack_from(k_data_seg, addr - layout.K_DATA_BOT)[0]
    return _bad_read(addr, 3)


# --- writes ------------------------------------------------------------------


def write_inst(addr: int, inst: Instruction | None) -> None:
    """Store inst at addr; a bad address raises IBE (C ``mem_write_inst``)."""
    global text_modified
    text_modified = True
    if not addr & 3:
        if layout.TEXT_BOT <= addr < text_top:
            text_seg[(addr - layout.TEXT_BOT) >> 2] = inst
            return
        if layout.K_TEXT_BOT <= addr < k_text_top:
            k_text_seg[(addr - layout.K_TEXT_BOT) >> 2] = inst
            return
    traps.raise_exception(ExcCode.IBE)
    regs.cpu.cpr[0][regs.CP0_BADVADDR] = machine.s32(addr)
    write_word(addr, inst.encoding if inst is not None else 0)


def write_byte(addr: int, value: int) -> None:
    """Store the low byte of value at addr (C ``mem_write_byte``)."""
    global data_modified
    data_modified = True
    if layout.DATA_BOT <= addr < data_top:
        data_seg[addr - layout.DATA_BOT] = value & 0xFF
    elif stack_bot <= addr < layout.STACK_TOP:
        stack_seg[addr - stack_bot] = value & 0xFF
    elif layout.K_DATA_BOT <= addr < k_data_top:
        k_data_seg[addr - layout.K_DATA_BOT] = value & 0xFF
    else:
        _bad_write(addr, value, 0)


def write_half(addr: int, value: int) -> None:
    """Store the low halfword of value at addr (C ``mem_write_half``)."""
    global data_modified
    data_modified = True
    if not addr & 1:
        if layout.DATA_BOT <= addr < data_top:
            _HALF.pack_into(data_seg, addr - layout.DATA_BOT, machine.s16(value))
            return
        if stack_bot <= addr < layout.STACK_TOP:
            _HALF.pack_into(stack_seg, addr - stack_bot, machine.s16(value))
            return
        if layout.K_DATA_BOT <= addr < k_data_top:
            _HALF.pack_into(k_data_seg, addr - layout.K_DATA_BOT, machine.s16(value))
            return
    _bad_write(addr, value, 1)


def write_word(addr: int, value: int) -> None:
    """Store the word value at addr (C ``mem_write_word``)."""
    global data_modified
    data_modified = True
    if not addr & 3:
        if layout.DATA_BOT <= addr < data_top:
            _WORD.pack_into(data_seg, addr - layout.DATA_BOT, machine.s32(value))
            return
        if stack_bot <= addr < layout.STACK_TOP:
            _WORD.pack_into(stack_seg, addr - stack_bot, machine.s32(value))
            return
        if layout.K_DATA_BOT <= addr < k_data_top:
            _WORD.pack_into(k_data_seg, addr - layout.K_DATA_BOT, machine.s32(value))
            return
    _bad_write(addr, value, 3)


# --- the infrequent and erroneous cases --------------------------------------


def _bad_address(code: ExcCode, addr: int) -> None:
    """Raise exception code with BadVAddr = addr."""
    traps.raise_exception(code)
    regs.cpu.cpr[0][regs.CP0_BADVADDR] = machine.s32(addr)


def _is_just_below_stack(addr: int) -> bool:
    """True for an address the stack should grow to reach (within 16 MB)."""
    return data_top < addr < stack_bot and addr > stack_bot - 16 * layout.MEGA


def _text_word(addr: int) -> int:
    """The encoding of the instruction in the user text word holding addr."""
    inst: Instruction | None = text_seg[(addr - layout.TEXT_BOT) >> 2]
    return inst.encoding if inst is not None else 0


def _bad_read(addr: int, mask: int) -> int:
    """A read that missed the fast path: fault, text, stack growth, or MMIO.

    Reading text as data returns the instruction's encoding; a byte or half
    read from text is zero-extended, unlike from data (the C engine's quirk).
    """
    if addr & mask:
        _bad_address(ExcCode.ADEL, addr)
    elif layout.TEXT_BOT <= addr < text_top:
        word: int = machine.u32(_text_word(addr))
        if mask == 0:
            return (word >> (8 * (addr & 3))) & 0xFF
        if mask == 1:
            return (word >> (8 * (addr & 2))) & 0xFFFF
        return machine.s32(word)
    elif _is_just_below_stack(addr):
        expand_stack(stack_bot - addr + 4)
        return 0
    elif layout.MM_IO_BOT <= addr <= layout.MM_IO_TOP:
        return _read_memory_mapped_io(addr)
    else:
        _bad_address(ExcCode.DBE, addr)
    return 0


def _bad_write(addr: int, value: int, mask: int) -> None:
    """A write that missed the fast path: fault, text, stack growth, or MMIO.

    A write into the text segment rebuilds the instruction there from the
    modified encoding, so a program can patch its own code.
    """
    global data_modified, text_modified
    if addr & mask:
        _bad_address(ExcCode.ADES, addr)
    elif layout.TEXT_BOT <= addr < text_top:
        slot: int = (addr - layout.TEXT_BOT) >> 2
        word: int
        if text_seg[slot] is None:
            word = value if mask == 3 else 0  # only a full word makes code
        else:
            old: int = machine.u32(_text_word(addr))
            if mask == 0:
                shift: int = 8 * (addr & 3)
                word = (old & ~(0xFF << shift)) | ((value & 0xFF) << shift)
            elif mask == 1:
                shift = 8 * (addr & 2)
                word = (old & ~(0xFFFF << shift)) | ((value & 0xFFFF) << shift)
            else:
                word = value
        text_seg[slot] = instruction.inst_decode(machine.s32(word))
        text_modified = True
    elif _is_just_below_stack(addr):
        expand_stack(stack_bot - addr + 4)
        if addr >= stack_bot:
            if mask == 0:
                stack_seg[addr - stack_bot] = value & 0xFF
            elif mask == 1:
                _HALF.pack_into(stack_seg, addr - stack_bot, machine.s16(value))
            else:
                _WORD.pack_into(stack_seg, addr - stack_bot, machine.s32(value))
        else:
            _bad_address(ExcCode.DBE, addr)
        data_modified = True
    elif layout.MM_IO_BOT <= addr <= layout.MM_IO_TOP:
        _write_memory_mapped_io(addr, value)
    else:
        _bad_address(ExcCode.DBE, addr)


# --- the memory-mapped console -----------------------------------------------

_recv_control: int = 0  # no input
_recv_buffer: int = 0
_recv_buffer_full_timer: int = 0
_trans_control: int = layout.TRANS_READY
_trans_buffer: int = 0
_trans_buffer_full_timer: int = 0


def _raise_interrupt(level: int) -> None:
    """Set the Cause register's pending bit for hardware interrupt level."""
    cp0: list[int] = regs.cpu.cpr[0]
    cp0[regs.CP0_CAUSE] = machine.s32(cp0[regs.CP0_CAUSE] | (1 << (level + 8)))


def _clear_interrupt(level: int) -> None:
    """Clear the Cause register's pending bit for interrupt level."""
    cp0: list[int] = regs.cpu.cpr[0]
    cp0[regs.CP0_CAUSE] = machine.s32(cp0[regs.CP0_CAUSE] & ~(1 << (level + 8)))


def check_memory_mapped_io() -> None:
    """Move a typed byte into the receiver, finish a transmission (each 100 steps)."""
    global _recv_buffer, _recv_control, _recv_buffer_full_timer
    global _trans_control, _trans_buffer_full_timer
    if _recv_buffer_full_timer > 0:
        _recv_buffer_full_timer -= 1
    elif console.console_input_available():
        byte: bytes = console.read_byte()
        _recv_buffer = byte[0] if byte else 0
        if _recv_buffer == 3:  # ^C
            console.message("\nExecution interrupted\n")
            raise console.RunError("interrupted")
        _recv_control |= layout.RECV_READY
        _recv_buffer_full_timer = layout.RECV_INTERVAL
        if _recv_control & layout.RECV_INT_ENABLE:
            _raise_interrupt(layout.RECV_INT_LEVEL)
    if _trans_buffer_full_timer > 0:
        _trans_buffer_full_timer -= 1
    elif not _trans_control & layout.TRANS_READY:
        _trans_control |= layout.TRANS_READY
        if _trans_control & layout.TRANS_INT_ENABLE:
            _raise_interrupt(layout.TRANS_INT_LEVEL)


def _write_memory_mapped_io(addr: int, value: int) -> None:
    """A store to a console register."""
    global _recv_control, _trans_control, _trans_buffer, _trans_buffer_full_timer
    match addr:
        case layout.TRANS_CTRL_ADDR:
            if value & layout.TRANS_INT_ENABLE:
                _trans_control |= layout.TRANS_INT_ENABLE
                if _trans_control & layout.TRANS_READY:
                    _raise_interrupt(layout.TRANS_INT_LEVEL)
            else:
                _trans_control &= ~layout.TRANS_INT_ENABLE
                _clear_interrupt(layout.TRANS_INT_LEVEL)
        case layout.TRANS_BUFFER_ADDR:
            if _trans_control & layout.TRANS_READY:  # ignored while busy
                _trans_buffer = value & 0xFF
                console.put_console_char(_trans_buffer)
                _trans_control &= ~layout.TRANS_READY
                _trans_buffer_full_timer = layout.TRANS_LATENCY
                _clear_interrupt(layout.TRANS_INT_LEVEL)
        case layout.RECV_CTRL_ADDR:
            if value & layout.RECV_INT_ENABLE:
                _recv_control |= layout.RECV_INT_ENABLE
                if _recv_control & layout.RECV_READY:
                    _raise_interrupt(layout.RECV_INT_LEVEL)
            else:
                _recv_control &= ~layout.RECV_INT_ENABLE
                _clear_interrupt(layout.RECV_INT_LEVEL)
        case layout.RECV_BUFFER_ADDR:
            pass  # the program cannot change the receiver buffer
        case _:
            raise console.run_error(
                f"Write to unused memory-mapped IO address (0x{addr:x})\n"
            )


def _read_memory_mapped_io(addr: int) -> int:
    """A load from a console register."""
    global _recv_control, _recv_buffer_full_timer
    match addr:
        case layout.TRANS_CTRL_ADDR:
            return _trans_control
        case layout.TRANS_BUFFER_ADDR:
            return _trans_buffer & 0xFF
        case layout.RECV_CTRL_ADDR:
            return _recv_control
        case layout.RECV_BUFFER_ADDR:
            _recv_control &= ~layout.RECV_READY
            _recv_buffer_full_timer = 0
            _clear_interrupt(layout.RECV_INT_LEVEL)
            return _recv_buffer & 0xFF
        case _:
            raise console.run_error(
                f"Read from unused memory-mapped IO address (0x{addr:x})\n"
            )


def print_mem(addr: int) -> None:
    """Print the word at addr on stderr (the REPL's ``print ADDR``)."""
    addr &= ~3
    value: int
    if layout.TEXT_BOT <= addr < text_top or layout.K_TEXT_BOT <= addr < k_text_top:
        instruction.print_inst(addr)
        return
    segment: str
    if layout.DATA_BOT <= addr < data_top:
        segment = "Data seg"
    elif stack_bot <= addr < layout.STACK_TOP:
        segment = "Stack seg"
    elif layout.K_DATA_BOT <= addr < k_data_top:
        segment = "Kernel Data seg"
    else:
        console.error(
            f"Address 0x{addr:08x} ({machine.s32(addr)}) to print_mem is out of bounds\n"
        )
        return
    value = read_word(addr)
    console.message(
        f"{segment} @ 0x{addr:08x} ({machine.s32(addr)}) ="
        f" 0x{machine.u32(value):08x} ({value})\n"
    )
