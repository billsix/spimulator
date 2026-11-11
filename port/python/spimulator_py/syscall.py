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
from .traps import ExcCode


def _c_string(addr: int) -> bytes:
    """The NUL-terminated string at addr (C's ``(char*)mem_reference(addr)``).

    Raises:
        RunError: addr is in no segment.
    """
    segment: bytearray | None
    offset: int
    segment, offset = memory.reference(addr)
    if segment is None:
        return b""  # a text address: C would read its instruction pointers
    end: int = segment.find(b"\0", offset)
    return bytes(segment[offset : end if end >= 0 else len(segment)])


def _store_bytes(addr: int, data: bytes) -> None:
    """Copy data into memory at addr, as C does through a raw pointer."""
    segment: bytearray | None
    offset: int
    segment, offset = memory.reference(addr)
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
    gpr: list[int] = cpu.gpr
    number: int = gpr[regs.REG_V0]
    a0: int = gpr[regs.REG_A0]
    match number:
        case 1:  # print_int
            console.output(str(a0))
        case 2:  # print_float
            console.output(c_format("%.8f", regs.fpr_s(regs.REG_FA0)))
        case 3:  # print_double (the pair $f12/$f13, no odd-register check)
            double: float = machine.words_double(cpu.fpr[12], cpu.fpr[13])
            console.output(c_format("%.18g", double))
        case 4:  # print_string
            console.output(console.from_bytes(_c_string(machine.u32(a0))))
        case 5:  # read_int
            value: int | None = console.read_int_input()
            gpr[regs.REG_RES] = machine.s32(value if value is not None else 0)
            gpr[regs.REG_A3] = 0 if value is not None else 1
        case 6:  # read_float
            text: bytes = console.read_input(256)
            regs.set_fpr_s(regs.REG_FRES, machine.c_atof(text))
        case 7:  # read_double ($f0/$f1)
            text = console.read_input(256)
            cpu.fpr[0], cpu.fpr[1] = machine.double_words(machine.c_atof(text))
        case 8:  # read_string: like fgets into the buffer at $a0, size $a1
            buffer: int = machine.u32(a0)
            size: int = gpr[regs.REG_A1]
            memory.reference(buffer)  # a bad address is a run-time error
            line: bytes = console.read_input(size)
            _store_bytes(buffer, line + (b"\0" if size > 0 else b""))
            gpr[regs.REG_A3] = 1 if not line else 0  # 1 at end of file
            memory.data_modified = True
        case 9:  # sbrk
            old_top: int = memory.data_top
            memory.expand_data(a0)
            gpr[regs.REG_RES] = machine.s32(old_top)
            memory.data_modified = True
        case 10:  # exit
            options.spim_return_value = 0
            return False
        case 11:  # print_char
            console.output(chr(a0 & 0xFF))
        case 12:  # read_char: the byte read, or -1 at end of file
            byte: bytes = console.read_input(2)
            gpr[regs.REG_RES] = byte[0] if byte else -1
        case 13:  # open
            path: bytes = _c_string(machine.u32(a0))
            try:
                gpr[regs.REG_RES] = os.open(path, gpr[regs.REG_A1], gpr[regs.REG_A2])
            except (OSError, ValueError, OverflowError):
                gpr[regs.REG_RES] = -1
        case 14:  # read
            buffer = machine.u32(gpr[regs.REG_A1])
            count: int = gpr[regs.REG_A2]
            memory.reference(machine.u32(gpr[regs.REG_A1] + count - 1))
            memory.reference(buffer)
            try:
                if count < 0:
                    raise OverflowError(count)  # C passes a huge size_t; give up
                received: bytes = os.read(a0, count)
                _store_bytes(buffer, received)
                gpr[regs.REG_RES] = len(received)
            except (OSError, ValueError, OverflowError):
                gpr[regs.REG_RES] = -1
            memory.data_modified = True
        case 15:  # write
            buffer = machine.u32(gpr[regs.REG_A1])
            count = gpr[regs.REG_A2]
            memory.reference(machine.u32(gpr[regs.REG_A1] + count - 1))
            segment: bytearray | None
            offset: int
            segment, offset = memory.reference(buffer)
            payload: bytes = bytes(segment[offset : offset + count]) if segment else b""
            try:
                gpr[regs.REG_RES] = os.write(a0, payload)
            except (OSError, ValueError, OverflowError):
                gpr[regs.REG_RES] = -1
        case 16:  # close
            try:
                os.close(a0)
                gpr[regs.REG_RES] = 0
            except OSError:
                gpr[regs.REG_RES] = -1
        case 17:  # exit2
            options.spim_return_value = a0
            return False
        case _:
            raise console.run_error(f"Unknown system call: {number}\n")
    return True


_EXCEPTION_MESSAGES: dict[int, str] = {
    ExcCode.ADEL: "  Unaligned address in instruction/data fetch: 0x{:08x}\n",
    ExcCode.ADES: "  Unaligned address in store: 0x{:08x}\n",
    ExcCode.IBE: "  Bad address in text read: 0x{:08x}\n",
    ExcCode.DBE: "  Bad address in data/stack read: 0x{:08x}\n",
    ExcCode.SYS: "  Error in syscall\n",
    ExcCode.RI: "  Reserved instruction execution\n",
    ExcCode.CPU: "  Coprocessor unuable\n",  # sic: the C engine's spelling
    ExcCode.OV: "  Arithmetic overflow\n",
    ExcCode.TR: "  Trap\n",
    ExcCode.FPE: "  Floating point\n",
}


def handle_exception() -> None:
    """Report the exception (unless quiet) and jump to the handler at 0x80000180."""
    code: int = regs.exc_code()
    cp0: list[int] = cpu.cpr[0]
    if not options.quiet and code != ExcCode.INT:
        console.error(
            f"Exception occurred at PC=0x{machine.u32(cp0[regs.CP0_EPC]):08x}\n"
        )
    traps.exception_occurred = False
    cpu.pc = layout.EXCEPTION_ADDR
    if code in (ExcCode.INT, ExcCode.BP):
        return
    if options.quiet:
        return
    if code in _EXCEPTION_MESSAGES:
        console.error(
            _EXCEPTION_MESSAGES[code].format(machine.u32(cp0[regs.CP0_BADVADDR]))
        )
    else:
        console.error(f"Unknown exception: {code}\n")
