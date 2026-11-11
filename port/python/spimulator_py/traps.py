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


class ExcCode(IntEnum):
    """The ExcCode values of the MIPS32 Cause register."""

    INT = 0  # interrupt
    MOD = 1
    TLBL = 2
    TLBS = 3
    ADEL = 4  # address error on a load or fetch
    ADES = 5  # address error on a store
    IBE = 6  # bus error on an instruction fetch
    DBE = 7  # bus error on a data reference
    SYS = 8  # syscall
    BP = 9  # breakpoint
    RI = 10  # reserved instruction
    CPU = 11  # coprocessor unusable
    OV = 12  # arithmetic overflow
    TR = 13  # trap
    FPE = 15  # floating point


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
    cp0: list[int] = regs.cpu.cpr[0]
    status: int = machine.u32(cp0[regs.CP0_STATUS])
    if code == ExcCode.INT and not (
        status & regs.STATUS_IE and not status & regs.STATUS_EXL
    ):
        return
    exception_occurred = True
    if first_bad_exception == -1 and code not in (ExcCode.INT, ExcCode.SYS, ExcCode.BP):
        first_bad_exception = code
    if not status & regs.STATUS_EXL:
        if running_in_delay_slot:
            # Report the branch, and set BD to say the fault was in its slot.
            cp0[regs.CP0_EPC] = machine.s32((regs.cpu.pc - 4) & ~3)
            cp0[regs.CP0_CAUSE] = machine.s32(cp0[regs.CP0_CAUSE] | regs.CAUSE_BD)
        else:
            cp0[regs.CP0_EPC] = machine.s32(regs.cpu.pc & ~3)
    cause: int = machine.u32(cp0[regs.CP0_CAUSE])
    cp0[regs.CP0_CAUSE] = machine.s32((cause & ~regs.CAUSE_EXCCODE) | (code << 2))
    cp0[regs.CP0_STATUS] = machine.s32(status | regs.STATUS_EXL)
