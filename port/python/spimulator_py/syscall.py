"""System calls and the exception report (the C engine's syscall.c).

A MIPS program asks the simulator for a service by putting its number in
$v0 and executing ``syscall``:

    1 print_int ($a0)       2 print_float ($f12)    3 print_double ($f12)
    4 print_string ($a0)    5 read_int -> $v0       6 read_float -> $f0
    7 read_double -> $f0    8 read_string ($a0, $a1) 9 sbrk ($a0) -> $v0
    10 exit                 11 print_char ($a0)     12 read_char -> $v0
    13 open  14 read  15 write  16 close (the host's own calls)
    17 exit2 ($a0 is the exit status)
"""

import os

from . import console, machine, memory, options, traps
from . import memory_layout as layout
from . import registers as regs
from .cformat import c_format
from .registers import cpu
from .traps import ExceptionCode


def _c_string(address: int) -> bytes:
    """The NUL-terminated string at addr (C's ``(char*)mem_reference(addr)``).

    Raises:
        RunError: addr is in no segment.
    """
    segment: bytearray | None
    offset: int
    segment, offset = memory.reference(address)
    if segment is None:
        return b""  # a text address: C would read its instruction pointers
    end: int = segment.find(b"\0", offset)
    return bytes(segment[offset : end if end >= 0 else len(segment)])


def _store_bytes(address: int, data: bytes) -> None:
    """Copy data into memory at addr, as C does through a raw pointer."""
    segment: bytearray | None
    offset: int
    segment, offset = memory.reference(address)
    if segment is not None:
        room: int = len(segment) - offset
        segment[offset : offset + min(len(data), room)] = data[:room]


def do_syscall() -> bool:
    """Perform the syscall in $v0.

    Returns:
        bool: False for the exit syscalls, True to go on.

    Raises:
        RunError: an unknown syscall number, or a bad buffer address.
    """
    general_purpose_registers: list[int] = cpu.general_purpose_registers
    number: int = general_purpose_registers[regs.VALUE_REGISTER_0]
    a0: int = general_purpose_registers[regs.ARGUMENT_REGISTER_0]
    match number:
        case 1:  # print_int
            console.output(str(a0))
        case 2:  # print_float
            console.output(
                c_format(
                    "%.8f",
                    regs.floating_point_register_single(
                        regs.FLOATING_POINT_ARGUMENT_REGISTER_0
                    ),
                )
            )
        case 3:  # print_double (the pair $f12/$f13, no odd-register check)
            double: float = machine.words_double(
                cpu.floating_point_registers[12], cpu.floating_point_registers[13]
            )
            console.output(c_format("%.18g", double))
        case 4:  # print_string
            console.output(console.from_bytes(_c_string(machine.u32(a0))))
        case 5:  # read_int
            value: int | None = console.read_integer_input()
            general_purpose_registers[regs.RESULT_REGISTER] = machine.s32(
                value if value is not None else 0
            )
            general_purpose_registers[regs.ARGUMENT_REGISTER_3] = (
                0 if value is not None else 1
            )
        case 6:  # read_float
            text: bytes = console.read_input(256)
            regs.set_floating_point_register_single(
                regs.FLOATING_POINT_RESULT_REGISTER, machine.c_atof(text)
            )
        case 7:  # read_double ($f0/$f1)
            text = console.read_input(256)
            cpu.floating_point_registers[0], cpu.floating_point_registers[1] = (
                machine.double_words(machine.c_atof(text))
            )
        case 8:  # read_string: like fgets into the buffer at $a0, size $a1
            buffer: int = machine.u32(a0)
            size: int = general_purpose_registers[regs.ARGUMENT_REGISTER_1]
            memory.reference(buffer)  # a bad address is a run-time error
            line: bytes = console.read_input(size)
            _store_bytes(buffer, line + (b"\0" if size > 0 else b""))
            general_purpose_registers[regs.ARGUMENT_REGISTER_3] = (
                1 if not line else 0
            )  # 1 at end of file
            memory.data_modified = True
        case 9:  # sbrk
            old_top: int = memory.data_top
            memory.expand_data(a0)
            general_purpose_registers[regs.RESULT_REGISTER] = machine.s32(old_top)
            memory.data_modified = True
        case 10:  # exit
            options.spim_return_value = 0
            return False
        case 11:  # print_char
            console.output(chr(a0 & 0xFF))
        case 12:  # read_char: the byte read, or -1 at end of file
            byte: bytes = console.read_input(2)
            general_purpose_registers[regs.RESULT_REGISTER] = byte[0] if byte else -1
        case 13:  # open
            path: bytes = _c_string(machine.u32(a0))
            try:
                general_purpose_registers[regs.RESULT_REGISTER] = os.open(
                    path,
                    general_purpose_registers[regs.ARGUMENT_REGISTER_1],
                    general_purpose_registers[regs.ARGUMENT_REGISTER_2],
                )
            except (OSError, ValueError, OverflowError):
                general_purpose_registers[regs.RESULT_REGISTER] = -1
        case 14:  # read
            buffer = machine.u32(general_purpose_registers[regs.ARGUMENT_REGISTER_1])
            count: int = general_purpose_registers[regs.ARGUMENT_REGISTER_2]
            memory.reference(
                machine.u32(
                    general_purpose_registers[regs.ARGUMENT_REGISTER_1] + count - 1
                )
            )
            memory.reference(buffer)
            try:
                if count < 0:
                    raise OverflowError(count)  # C passes a huge size_t; give up
                received: bytes = os.read(a0, count)
                _store_bytes(buffer, received)
                general_purpose_registers[regs.RESULT_REGISTER] = len(received)
            except (OSError, ValueError, OverflowError):
                general_purpose_registers[regs.RESULT_REGISTER] = -1
            memory.data_modified = True
        case 15:  # write
            buffer = machine.u32(general_purpose_registers[regs.ARGUMENT_REGISTER_1])
            count = general_purpose_registers[regs.ARGUMENT_REGISTER_2]
            memory.reference(
                machine.u32(
                    general_purpose_registers[regs.ARGUMENT_REGISTER_1] + count - 1
                )
            )
            segment: bytearray | None
            offset: int
            segment, offset = memory.reference(buffer)
            payload: bytes = bytes(segment[offset : offset + count]) if segment else b""
            try:
                general_purpose_registers[regs.RESULT_REGISTER] = os.write(a0, payload)
            except (OSError, ValueError, OverflowError):
                general_purpose_registers[regs.RESULT_REGISTER] = -1
        case 16:  # close
            try:
                os.close(a0)
                general_purpose_registers[regs.RESULT_REGISTER] = 0
            except OSError:
                general_purpose_registers[regs.RESULT_REGISTER] = -1
        case 17:  # exit2
            options.spim_return_value = a0
            return False
        case _:
            raise console.run_error(f"Unknown system call: {number}\n")
    return True


_EXCEPTION_MESSAGES: dict[int, str] = {
    ExceptionCode.ADDRESS_ERROR_LOAD: "  Unaligned address in instruction/data fetch: 0x{:08x}\n",
    ExceptionCode.ADDRESS_ERROR_STORE: "  Unaligned address in store: 0x{:08x}\n",
    ExceptionCode.INSTRUCTION_BUS_ERROR: "  Bad address in text read: 0x{:08x}\n",
    ExceptionCode.DATA_BUS_ERROR: "  Bad address in data/stack read: 0x{:08x}\n",
    ExceptionCode.SYSTEM_CALL: "  Error in syscall\n",
    ExceptionCode.RESERVED_INSTRUCTION: "  Reserved instruction execution\n",
    ExceptionCode.COPROCESSOR_UNUSABLE: "  Coprocessor unuable\n",  # sic: the C engine's spelling
    ExceptionCode.ARITHMETIC_OVERFLOW: "  Arithmetic overflow\n",
    ExceptionCode.TRAP: "  Trap\n",
    ExceptionCode.FLOATING_POINT_EXCEPTION: "  Floating point\n",
}


def handle_exception() -> None:
    """Report the exception (unless quiet) and jump to the handler at 0x80000180."""
    code: int = regs.exception_code()
    coprocessor_0: list[int] = cpu.coprocessor_registers[0]
    if not options.quiet and code != ExceptionCode.INTERRUPT:
        console.error(
            f"Exception occurred at PC=0x{machine.u32(coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER]):08x}\n"
        )
    traps.exception_occurred = False
    cpu.program_counter = layout.EXCEPTION_ADDRESS
    if code in (ExceptionCode.INTERRUPT, ExceptionCode.BREAKPOINT) or options.quiet:
        return
    else:
        if code in _EXCEPTION_MESSAGES:
            console.error(
                _EXCEPTION_MESSAGES[code].format(
                    machine.u32(coprocessor_0[regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS])
                )
            )
        else:
            console.error(f"Unknown exception: {code}\n")
