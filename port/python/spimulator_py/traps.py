"""Raising MIPS exceptions (``raise_exception`` of the C engine's run.c).

An exception here is the processor's, not Python's: an instruction that
faults (a bad address, an overflow, a syscall, a break) records the cause in
coprocessor 0's Cause register, saves the faulting PC in EPC, and sets
:data:`exception_occurred`.  After the instruction the executor sees the flag
and jumps to the exception handler (syscall.handle_exception).
"""

from enum import IntEnum

from . import machine
from . import registers as regs


class ExceptionCode(IntEnum):
    """The values of the MIPS32 Cause register's ExcCode field."""

    INTERRUPT = 0  # Int: interrupt
    TRANSLATION_LOOKASIDE_BUFFER_MODIFIED = 1  # Mod
    TRANSLATION_LOOKASIDE_BUFFER_LOAD = 2  # TLBL
    TRANSLATION_LOOKASIDE_BUFFER_STORE = 3  # TLBS
    ADDRESS_ERROR_LOAD = 4  # AdEL: address error on a load or fetch
    ADDRESS_ERROR_STORE = 5  # AdES: address error on a store
    INSTRUCTION_BUS_ERROR = 6  # IBE: bus error on an instruction fetch
    DATA_BUS_ERROR = 7  # DBE: bus error on a data reference
    SYSTEM_CALL = 8  # Sys: syscall
    BREAKPOINT = 9  # Bp: breakpoint
    RESERVED_INSTRUCTION = 10  # RI: reserved instruction
    COPROCESSOR_UNUSABLE = 11  # CpU: coprocessor unusable
    ARITHMETIC_OVERFLOW = 12  # Ov: arithmetic overflow
    TRAP = 13  # Tr: trap
    FLOATING_POINT_EXCEPTION = 15  # FPE: floating point


exception_occurred: bool = False
"""Set when the current instruction raised an exception."""

first_bad_exception: int = -1
"""The ExcCode of the first fault of the run (not a syscall, break or
interrupt), or -1.  The simulator exits with 128 + it."""

running_in_delay_slot: bool = False
"""True while a delayed branch executes the instruction in its delay slot."""


def raise_exception(code: int) -> None:
    """Record exception code in CP0 the way a MIPS32 processor does.

    An interrupt is ignored while interrupts are disabled (IE clear or EXL
    set).  EPC is only written when EXL is clear, so a nested exception keeps
    the first EPC; the cause is always overwritten.
    """
    global exception_occurred, first_bad_exception
    coprocessor_0: list[int] = regs.cpu.coprocessor_registers[0]
    status: int = machine.u32(coprocessor_0[regs.COPROCESSOR_0_STATUS])
    if code == ExceptionCode.INTERRUPT and not (
        status & regs.STATUS_INTERRUPT_ENABLE
        and not status & regs.STATUS_EXCEPTION_LEVEL
    ):
        return
    exception_occurred = True
    if first_bad_exception == -1 and code not in (
        ExceptionCode.INTERRUPT,
        ExceptionCode.SYSTEM_CALL,
        ExceptionCode.BREAKPOINT,
    ):
        first_bad_exception = code
    if not status & regs.STATUS_EXCEPTION_LEVEL:
        if running_in_delay_slot:
            # Report the branch, and set BD to say the fault was in its slot.
            coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER] = machine.s32(
                (regs.cpu.program_counter - 4) & ~3
            )
            coprocessor_0[regs.COPROCESSOR_0_CAUSE] = machine.s32(
                coprocessor_0[regs.COPROCESSOR_0_CAUSE] | regs.CAUSE_BD
            )
        else:
            coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER] = machine.s32(
                regs.cpu.program_counter & ~3
            )
    coprocessor_0[regs.COPROCESSOR_0_CAUSE] = machine.s32(
        (
            machine.u32(coprocessor_0[regs.COPROCESSOR_0_CAUSE])
            & ~regs.CAUSE_EXCEPTION_CODE
        )
        | (code << 2)
    )
    coprocessor_0[regs.COPROCESSOR_0_STATUS] = machine.s32(
        status | regs.STATUS_EXCEPTION_LEVEL
    )
