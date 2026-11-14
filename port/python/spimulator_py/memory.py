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

from . import console, machine, traps
from . import instruction as instruction_module
from . import memory_layout as layout
from . import registers as regs
from .inst import Instruction
from .traps import ExceptionCode

_WORD: struct.Struct = struct.Struct("<i")
_HALF: struct.Struct = struct.Struct("<h")

text_segment: list[Instruction | None] = []
text_top: int = layout.TEXT_BOTTOM
kernel_text_segment: list[Instruction | None] = []
kernel_text_top: int = layout.KERNEL_TEXT_BOTTOM

data_segment: bytearray = bytearray()
data_top: int = layout.DATA_BOTTOM
global_pointer_midpoint: int = 0
stack_segment: bytearray = bytearray()
stack_bottom: int = layout.STACK_TOP
kernel_data_segment: bytearray = bytearray()
kernel_data_top: int = layout.KERNEL_DATA_BOTTOM

text_modified: bool = False
data_modified: bool = False

_data_size_limit: int = 0
_stack_size_limit: int = 0
_kernel_data_size_limit: int = 0


def _round_up(value: int, boundary: int) -> int:
    """value rounded up to a multiple of boundary (a power of two)."""
    return (value + boundary - 1) & ~(boundary - 1)


def make_memory(
    text_size: int,
    data_size: int,
    data_limit: int,
    stack_size: int,
    stack_limit: int,
    kernel_text_size: int,
    kernel_data_size: int,
    kernel_data_limit: int,
) -> None:
    """Allocate (or clear) every segment with the given sizes and limits."""
    global text_segment, text_top, data_segment, data_top, stack_segment, stack_bottom
    global kernel_text_segment, kernel_text_top, kernel_data_segment, kernel_data_top
    global _data_size_limit, _stack_size_limit, _kernel_data_size_limit
    global text_modified, data_modified
    # A text segment that can hold no instruction, or a kernel text segment
    # that stops short of the exception vector, would make every exception
    # jump outside its segment, and the run would loop.
    if text_size < 4:
        raise console.fatal_error(
            f"-stext {text_size}: the text segment needs at least one word (4 bytes)\n"
        )
    if kernel_text_size < 0x180 + 4:
        raise console.fatal_error(
            f"-sktext {kernel_text_size}: the kernel text segment must hold the exception"
            " handler at 0x80000180 (at least 388 bytes)\n"
        )
    data_size = _round_up(max(data_size, 65536), 4)
    text_segment = [None] * ((text_size + 3) // 4)
    text_top = layout.TEXT_BOTTOM + text_size
    data_segment = bytearray(data_size)
    data_top = layout.DATA_BOTTOM + data_size
    _data_size_limit = data_limit
    stack_size = _round_up(stack_size, 4)
    stack_segment = bytearray(stack_size)
    stack_bottom = layout.STACK_TOP - stack_size
    _stack_size_limit = stack_limit
    kernel_text_segment = [None] * ((kernel_text_size + 3) // 4)
    kernel_text_top = layout.KERNEL_TEXT_BOTTOM + kernel_text_size
    kernel_data_size = _round_up(kernel_data_size, 4)
    kernel_data_segment = bytearray(kernel_data_size)
    kernel_data_top = layout.KERNEL_DATA_BOTTOM + kernel_data_size
    _kernel_data_size_limit = kernel_data_limit
    text_modified = True
    data_modified = True


def expand_data(additional_bytes: int) -> None:
    """Grow the data segment by additional_bytes (``sbrk``).

    The sizes are C ints: a growth that wraps negative passes the limit check, and
    C's ``realloc`` of that size then fails.

    Raises:
        RunError: the growth is negative or passes the data limit.
        SystemExit: the new size wrapped negative (C: realloc fails).
    """
    global data_top
    delta: int = machine.s32(_round_up(additional_bytes, 4))
    old_size: int = data_top - layout.DATA_BOTTOM
    new_size: int = machine.s32(old_size + delta)
    if additional_bytes < 0 or new_size > _data_size_limit:
        console.error(
            f"Can't expand data segment by {additional_bytes} bytes to {new_size} bytes\n"
        )
        raise console.run_error(f"Use -ldata # with # > {new_size}\n")
    elif new_size < 0:
        raise console.fatal_error("realloc failed in expand_data\n")
    else:
        data_segment.extend(bytes(new_size - old_size))
    data_top += delta


def expand_stack(additional_bytes: int) -> None:
    """Grow the stack downward: at least double it, or by additional_bytes.

    The sizes are C ints: a growth that wraps negative passes the limit check, and
    C's ``xmalloc`` of that size (as a ``size_t``) then fails.

    Raises:
        RunError: the growth passes the stack limit.
        SystemExit: the new size wrapped negative (C: xmalloc fails).
    """
    global stack_segment, stack_bottom
    delta: int = machine.s32(_round_up(additional_bytes, 4))
    old_size: int = layout.STACK_TOP - stack_bottom
    new_size: int = machine.s32(old_size + max(delta, old_size))
    if additional_bytes < 0 or new_size > _stack_size_limit:
        raise console.run_error(
            f"Can't expand stack segment by {additional_bytes} bytes to {new_size}"
            f" bytes.\nUse -lstack # with # > {new_size}\n"
        )
    elif new_size < 0:
        # C passes the negative int to xmalloc as a size_t.
        raise console.fatal_error(
            f"Out of memory at request for {new_size % (1 << 64)} bytes.\n"
        )
    else:
        stack_segment = bytearray(new_size - old_size) + stack_segment
    stack_bottom -= new_size - old_size


def expand_kernel_data(additional_bytes: int) -> None:
    """Grow the kernel data segment by additional_bytes.

    The sizes are C ints, as in `expand_data`.

    Raises:
        RunError: the growth passes the kernel data limit.
        SystemExit: the new size wrapped negative (C: realloc fails).
    """
    global kernel_data_top
    delta: int = machine.s32(_round_up(additional_bytes, 4))
    old_size: int = kernel_data_top - layout.KERNEL_DATA_BOTTOM
    new_size: int = machine.s32(old_size + delta)
    if additional_bytes < 0 or new_size > _kernel_data_size_limit:
        raise console.run_error(
            f"Can't expand kernel data segment by {additional_bytes} bytes to {new_size}"
            f" bytes.\nUse -lkdata # with # > {new_size}\n"
        )
    elif new_size < 0:
        raise console.fatal_error("realloc failed in expand_k_data\n")
    else:
        kernel_data_segment.extend(bytes(new_size - old_size))
    kernel_data_top += delta


def reference(address: int) -> tuple[bytearray | None, int]:
    """The segment holding addr and the offset in it (C ``mem_reference``).

    Used by the syscalls that pass a buffer address (print_string, read,
    write).  A text address yields (None, offset): the C engine would hand
    out a pointer into its instruction array.

    Raises:
        RunError: addr is in no segment.
    """
    if layout.TEXT_BOTTOM <= address < text_top:
        return None, address - layout.TEXT_BOTTOM
    elif layout.DATA_BOTTOM <= address < data_top:
        return data_segment, address - layout.DATA_BOTTOM
    elif stack_bottom <= address < layout.STACK_TOP:
        return stack_segment, address - stack_bottom
    elif layout.KERNEL_TEXT_BOTTOM <= address < kernel_text_top:
        return None, address - layout.KERNEL_TEXT_BOTTOM
    elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
        return kernel_data_segment, address - layout.KERNEL_DATA_BOTTOM
    else:
        raise console.run_error("Memory address out of bounds\n")


# --- reads -------------------------------------------------------------------


def read_instruction(address: int) -> Instruction | None:
    """The instruction at addr; a bad address raises IBE (C ``mem_read_inst``)."""
    if not address & 3:
        if layout.TEXT_BOTTOM <= address < text_top:
            return text_segment[(address - layout.TEXT_BOTTOM) >> 2]
        elif layout.KERNEL_TEXT_BOTTOM <= address < kernel_text_top:
            return kernel_text_segment[(address - layout.KERNEL_TEXT_BOTTOM) >> 2]
    traps.raise_exception(ExceptionCode.INSTRUCTION_BUS_ERROR)
    regs.cpu.coprocessor_registers[0][regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS] = (
        machine.s32(address)
    )
    return instruction_module.instruction_decode(0)


def peek_instruction(address: int) -> tuple[bool, Instruction | None]:
    """The instruction at addr, read without side effects (C ``mem_peek_inst``).

    For code that only shows memory, such as the disassembler: it raises no
    exception and touches no register.

    Returns:
        ``(True, the instruction)`` (which may be ``None`` for an empty slot)
        when addr is an aligned text-segment location, else ``(False, None)``.
    """
    if address & 3:
        return (False, None)
    elif layout.TEXT_BOTTOM <= address < text_top:
        return (True, text_segment[(address - layout.TEXT_BOTTOM) >> 2])
    elif layout.KERNEL_TEXT_BOTTOM <= address < kernel_text_top:
        return (True, kernel_text_segment[(address - layout.KERNEL_TEXT_BOTTOM) >> 2])
    else:
        return (False, None)


def read_byte(address: int) -> int:
    """The signed byte at addr (C ``mem_read_byte``)."""
    if layout.DATA_BOTTOM <= address < data_top:
        return machine.s8(data_segment[address - layout.DATA_BOTTOM])
    elif stack_bottom <= address < layout.STACK_TOP:
        return machine.s8(stack_segment[address - stack_bottom])
    elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
        return machine.s8(kernel_data_segment[address - layout.KERNEL_DATA_BOTTOM])
    else:
        return _bad_read(address, 0)


def read_half(address: int) -> int:
    """The signed halfword at addr (C ``mem_read_half``)."""
    if not address & 1:
        if layout.DATA_BOTTOM <= address < data_top:
            return _HALF.unpack_from(data_segment, address - layout.DATA_BOTTOM)[0]
        elif stack_bottom <= address < layout.STACK_TOP:
            return _HALF.unpack_from(stack_segment, address - stack_bottom)[0]
        elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
            return _HALF.unpack_from(
                kernel_data_segment, address - layout.KERNEL_DATA_BOTTOM
            )[0]
    return _bad_read(address, 1)


def read_word(address: int) -> int:
    """The signed word at addr (C ``mem_read_word``)."""
    if not address & 3:
        if layout.DATA_BOTTOM <= address < data_top:
            return _WORD.unpack_from(data_segment, address - layout.DATA_BOTTOM)[0]
        elif stack_bottom <= address < layout.STACK_TOP:
            return _WORD.unpack_from(stack_segment, address - stack_bottom)[0]
        elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
            return _WORD.unpack_from(
                kernel_data_segment, address - layout.KERNEL_DATA_BOTTOM
            )[0]
    return _bad_read(address, 3)


# --- writes ------------------------------------------------------------------


def write_instruction(address: int, instruction: Instruction | None) -> None:
    """Store inst at addr; a bad address raises IBE (C ``mem_write_inst``)."""
    global text_modified
    text_modified = True
    if not address & 3:
        if layout.TEXT_BOTTOM <= address < text_top:
            text_segment[(address - layout.TEXT_BOTTOM) >> 2] = instruction
            return
        elif layout.KERNEL_TEXT_BOTTOM <= address < kernel_text_top:
            kernel_text_segment[(address - layout.KERNEL_TEXT_BOTTOM) >> 2] = (
                instruction
            )
            return
    traps.raise_exception(ExceptionCode.INSTRUCTION_BUS_ERROR)
    regs.cpu.coprocessor_registers[0][regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS] = (
        machine.s32(address)
    )
    write_word(address, instruction.encoding if instruction is not None else 0)


def write_byte(address: int, value: int) -> None:
    """Store the low byte of value at addr (C ``mem_write_byte``)."""
    global data_modified
    data_modified = True
    if layout.DATA_BOTTOM <= address < data_top:
        data_segment[address - layout.DATA_BOTTOM] = value & 0xFF
    elif stack_bottom <= address < layout.STACK_TOP:
        stack_segment[address - stack_bottom] = value & 0xFF
    elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
        kernel_data_segment[address - layout.KERNEL_DATA_BOTTOM] = value & 0xFF
    else:
        _bad_write(address, value, 0)


def write_half(address: int, value: int) -> None:
    """Store the low halfword of value at addr (C ``mem_write_half``)."""
    global data_modified
    data_modified = True
    if not address & 1:
        if layout.DATA_BOTTOM <= address < data_top:
            _HALF.pack_into(
                data_segment, address - layout.DATA_BOTTOM, machine.s16(value)
            )
            return
        elif stack_bottom <= address < layout.STACK_TOP:
            _HALF.pack_into(stack_segment, address - stack_bottom, machine.s16(value))
            return
        elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
            _HALF.pack_into(
                kernel_data_segment,
                address - layout.KERNEL_DATA_BOTTOM,
                machine.s16(value),
            )
            return
    _bad_write(address, value, 1)


def write_word(address: int, value: int) -> None:
    """Store the word value at addr (C ``mem_write_word``)."""
    global data_modified
    data_modified = True
    if not address & 3:
        if layout.DATA_BOTTOM <= address < data_top:
            _WORD.pack_into(
                data_segment, address - layout.DATA_BOTTOM, machine.s32(value)
            )
            return
        elif stack_bottom <= address < layout.STACK_TOP:
            _WORD.pack_into(stack_segment, address - stack_bottom, machine.s32(value))
            return
        elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
            _WORD.pack_into(
                kernel_data_segment,
                address - layout.KERNEL_DATA_BOTTOM,
                machine.s32(value),
            )
            return
    _bad_write(address, value, 3)


# --- the infrequent and erroneous cases --------------------------------------


def _bad_address(code: ExceptionCode, address: int) -> None:
    """Raise exception code with BadVAddr = addr."""
    traps.raise_exception(code)
    regs.cpu.coprocessor_registers[0][regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS] = (
        machine.s32(address)
    )


def _is_just_below_stack(address: int) -> bool:
    """True for an address the stack should grow to reach (within 16 MB)."""
    return (
        data_top < address < stack_bottom and address > stack_bottom - 16 * layout.MEGA
    )


def _text_word(address: int) -> int:
    """The encoding of the instruction in the user text word holding addr."""
    instruction: Instruction | None = text_segment[(address - layout.TEXT_BOTTOM) >> 2]
    return instruction.encoding if instruction is not None else 0


def _bad_read(address: int, mask: int) -> int:
    """A read that missed the fast path: fault, text, stack growth, or MMIO.

    Reading text as data returns the instruction's encoding; a byte or half
    read from text is zero-extended, unlike from data (the C engine's quirk).
    """
    if address & mask:
        _bad_address(ExceptionCode.ADDRESS_ERROR_LOAD, address)
    elif layout.TEXT_BOTTOM <= address < text_top:
        word: int = machine.u32(_text_word(address))
        if mask == 0:
            return (word >> (8 * (address & 3))) & 0xFF
        elif mask == 1:
            return (word >> (8 * (address & 2))) & 0xFFFF
        else:
            return machine.s32(word)
    elif _is_just_below_stack(address):
        expand_stack(stack_bottom - address + 4)
        return 0
    elif layout.MEMORY_MAPPED_IO_BOTTOM <= address <= layout.MEMORY_MAPPED_IO_TOP:
        return _read_memory_mapped_io(address)
    else:
        _bad_address(ExceptionCode.DATA_BUS_ERROR, address)
    return 0


def _bad_write(address: int, value: int, mask: int) -> None:
    """A write that missed the fast path: fault, text, stack growth, or MMIO.

    A write into the text segment rebuilds the instruction there from the
    modified encoding, so a program can patch its own code.
    """
    global data_modified, text_modified
    if address & mask:
        _bad_address(ExceptionCode.ADDRESS_ERROR_STORE, address)
    elif layout.TEXT_BOTTOM <= address < text_top:
        slot: int = (address - layout.TEXT_BOTTOM) >> 2
        word: int
        if text_segment[slot] is None:
            word = value if mask == 3 else 0  # only a full word makes code
        else:
            old: int = machine.u32(_text_word(address))
            if mask == 0:
                shift: int = 8 * (address & 3)
                word = (old & ~(0xFF << shift)) | ((value & 0xFF) << shift)
            elif mask == 1:
                shift = 8 * (address & 2)
                word = (old & ~(0xFFFF << shift)) | ((value & 0xFFFF) << shift)
            else:
                word = value
        text_segment[slot] = instruction_module.instruction_decode(machine.s32(word))
        text_modified = True
    elif _is_just_below_stack(address):
        expand_stack(stack_bottom - address + 4)
        if address >= stack_bottom:
            if mask == 0:
                stack_segment[address - stack_bottom] = value & 0xFF
            elif mask == 1:
                _HALF.pack_into(
                    stack_segment, address - stack_bottom, machine.s16(value)
                )
            else:
                _WORD.pack_into(
                    stack_segment, address - stack_bottom, machine.s32(value)
                )
        else:
            _bad_address(ExceptionCode.DATA_BUS_ERROR, address)
        data_modified = True
    elif layout.MEMORY_MAPPED_IO_BOTTOM <= address <= layout.MEMORY_MAPPED_IO_TOP:
        _write_memory_mapped_io(address, value)
    else:
        _bad_address(ExceptionCode.DATA_BUS_ERROR, address)


# --- the memory-mapped console -----------------------------------------------

_receiver_control: int = 0  # no input
_receiver_buffer: int = 0
_receiver_buffer_full_timer: int = 0
_transmitter_control: int = layout.TRANSMITTER_READY
_transmitter_buffer: int = 0
_transmitter_buffer_full_timer: int = 0


def _raise_interrupt(level: int) -> None:
    """Set the Cause register's pending bit for hardware interrupt level."""
    coprocessor_0: list[int] = regs.cpu.coprocessor_registers[0]
    coprocessor_0[regs.COPROCESSOR_0_CAUSE] = machine.s32(
        coprocessor_0[regs.COPROCESSOR_0_CAUSE] | (1 << (level + 8))
    )


def _clear_interrupt(level: int) -> None:
    """Clear the Cause register's pending bit for interrupt level."""
    coprocessor_0: list[int] = regs.cpu.coprocessor_registers[0]
    coprocessor_0[regs.COPROCESSOR_0_CAUSE] = machine.s32(
        coprocessor_0[regs.COPROCESSOR_0_CAUSE] & ~(1 << (level + 8))
    )


def check_memory_mapped_io() -> None:
    """Move a typed byte into the receiver, finish a transmission (each 100 steps)."""
    global _receiver_buffer, _receiver_control, _receiver_buffer_full_timer
    global _transmitter_control, _transmitter_buffer_full_timer
    if _receiver_buffer_full_timer > 0:
        _receiver_buffer_full_timer -= 1
    elif console.console_input_available():
        byte: bytes = console.read_byte()
        _receiver_buffer = byte[0] if byte else 0
        if _receiver_buffer == 3:  # ^C
            console.message("\nExecution interrupted\n")
            raise console.RunError("interrupted")
        _receiver_control |= layout.RECEIVER_READY
        _receiver_buffer_full_timer = layout.RECEIVER_INTERVAL
        if _receiver_control & layout.RECEIVER_INTERRUPT_ENABLE:
            _raise_interrupt(layout.RECEIVER_INTERRUPT_LEVEL)
    else:
        pass  # no console input is waiting: nothing to receive
    if _transmitter_buffer_full_timer > 0:
        _transmitter_buffer_full_timer -= 1
    elif not _transmitter_control & layout.TRANSMITTER_READY:
        _transmitter_control |= layout.TRANSMITTER_READY
        if _transmitter_control & layout.TRANSMITTER_INTERRUPT_ENABLE:
            _raise_interrupt(layout.TRANSMITTER_INTERRUPT_LEVEL)
    else:
        pass  # the transmitter is already ready: nothing to finish


def _write_memory_mapped_io(address: int, value: int) -> None:
    """A store to a console register."""
    global \
        _receiver_control, \
        _transmitter_control, \
        _transmitter_buffer, \
        _transmitter_buffer_full_timer
    match address:
        case layout.TRANSMITTER_CONTROL_ADDRESS:
            if value & layout.TRANSMITTER_INTERRUPT_ENABLE:
                _transmitter_control |= layout.TRANSMITTER_INTERRUPT_ENABLE
                if _transmitter_control & layout.TRANSMITTER_READY:
                    _raise_interrupt(layout.TRANSMITTER_INTERRUPT_LEVEL)
            else:
                _transmitter_control &= ~layout.TRANSMITTER_INTERRUPT_ENABLE
                _clear_interrupt(layout.TRANSMITTER_INTERRUPT_LEVEL)
        case layout.TRANSMITTER_BUFFER_ADDRESS:
            if _transmitter_control & layout.TRANSMITTER_READY:  # ignored while busy
                _transmitter_buffer = value & 0xFF
                console.put_console_char(_transmitter_buffer)
                _transmitter_control &= ~layout.TRANSMITTER_READY
                _transmitter_buffer_full_timer = layout.TRANSMITTER_LATENCY
                _clear_interrupt(layout.TRANSMITTER_INTERRUPT_LEVEL)
        case layout.RECEIVER_CONTROL_ADDRESS:
            if value & layout.RECEIVER_INTERRUPT_ENABLE:
                _receiver_control |= layout.RECEIVER_INTERRUPT_ENABLE
                if _receiver_control & layout.RECEIVER_READY:
                    _raise_interrupt(layout.RECEIVER_INTERRUPT_LEVEL)
            else:
                _receiver_control &= ~layout.RECEIVER_INTERRUPT_ENABLE
                _clear_interrupt(layout.RECEIVER_INTERRUPT_LEVEL)
        case layout.RECEIVER_BUFFER_ADDRESS:
            pass  # the program cannot change the receiver buffer
        case _:
            raise console.run_error(
                f"Write to unused memory-mapped IO address (0x{address:x})\n"
            )


def _read_memory_mapped_io(address: int) -> int:
    """A load from a console register."""
    global _receiver_control, _receiver_buffer_full_timer
    match address:
        case layout.TRANSMITTER_CONTROL_ADDRESS:
            return _transmitter_control
        case layout.TRANSMITTER_BUFFER_ADDRESS:
            return _transmitter_buffer & 0xFF
        case layout.RECEIVER_CONTROL_ADDRESS:
            return _receiver_control
        case layout.RECEIVER_BUFFER_ADDRESS:
            _receiver_control &= ~layout.RECEIVER_READY
            _receiver_buffer_full_timer = 0
            _clear_interrupt(layout.RECEIVER_INTERRUPT_LEVEL)
            return _receiver_buffer & 0xFF
        case _:
            raise console.run_error(
                f"Read from unused memory-mapped IO address (0x{address:x})\n"
            )


def print_memory(address: int) -> None:
    """Print the word at addr on stderr (the REPL's ``print ADDR``)."""
    address &= ~3
    value: int
    if (
        layout.TEXT_BOTTOM <= address < text_top
        or layout.KERNEL_TEXT_BOTTOM <= address < kernel_text_top
    ):
        instruction_module.print_instruction(address)
        return
    segment: str
    if layout.DATA_BOTTOM <= address < data_top:
        segment = "Data seg"
    elif stack_bottom <= address < layout.STACK_TOP:
        segment = "Stack seg"
    elif layout.KERNEL_DATA_BOTTOM <= address < kernel_data_top:
        segment = "Kernel Data seg"
    else:
        console.error(
            f"Address 0x{address:08x} ({machine.s32(address)}) to print_mem is out of bounds\n"
        )
        return
    value = read_word(address)
    console.message(
        f"{segment} @ 0x{address:08x} ({machine.s32(address)}) ="
        f" 0x{machine.u32(value):08x} ({value})\n"
    )
