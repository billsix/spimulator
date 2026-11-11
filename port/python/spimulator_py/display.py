"""Printing the machine: registers, memory and disassembly (display-utils.c).

These produce the REPL's ``print_all_regs`` and ``disasm`` output and the
``-dump`` files (text.asm, data.asm).
"""

from . import console, instruction, machine, memory
from . import memory_layout as layout
from . import registers as regs
from .cformat import c_format
from .registers import cpu

BYTES_PER_LINE: int = 16


def _hex(value: int) -> str:
    """value as eight hex digits (C ``%08x`` of a signed word)."""
    return f"{machine.u32(value):08x}"


def format_registers(gpr_hex: bool, fpr_hex: bool) -> str:
    """Every register, laid out as the C engine's print_all_regs does."""
    cp0: list[int] = cpu.cpr[0]
    text: str = (
        f" PC      = {_hex(cpu.pc)}   EPC     = {_hex(cp0[regs.CP0_EPC])}  "
        f" Cause   = {_hex(cp0[regs.CP0_CAUSE])}  "
        f" BadVAddr= {_hex(cp0[regs.CP0_BADVADDR])}\n"
        f" Status  = {_hex(cp0[regs.CP0_STATUS])}   HI      = {_hex(cpu.hi)}  "
        f" LO      = {_hex(cpu.lo)}\n"
        "\t\t\t\t General Registers\n"
    )

    def general(i: int) -> str:
        """One general register, decimal or hex."""
        name: str = regs.INT_REG_NAMES[i]
        value: str = _hex(cpu.gpr[i]) if gpr_hex else f"{cpu.gpr[i]:<10d}"
        return f"R{i:<2d} ({name:>2s}) = {value}"

    fill: str = "  " if gpr_hex else " "
    for i in range(8):
        text += fill.join(general(i + 8 * column) for column in range(4)) + "\n"
    fcr: list[int] = cpu.cpr[1]
    text += (
        f"\n FIR    = {_hex(fcr[regs.FIR_REG])}    FCSR    = {_hex(fcr[regs.FCSR_REG])}   "
        "\t\t\t      Double Floating Point Registers\n"
    )

    def double(pair: int) -> str:
        """The double in FP register pair number pair (registers 2*pair, +1)."""
        low: int = cpu.fpr[2 * pair]
        high: int = cpu.fpr[2 * pair + 1]
        if fpr_hex:
            return f"FP{2 * pair:<2d}={_hex(low)},{_hex(high)}"
        return f"FP{2 * pair:<2d} = " + c_format(
            "%#-13.6g", machine.words_double(low, high)
        )

    for i in range(4):
        text += " ".join(double(i + 4 * column) for column in range(4)) + "\n"
    text += "\t\t\t      Single Floating Point Registers\n"

    def single(reg: int) -> str:
        """FP register reg as a float."""
        if fpr_hex:
            return f"FP{reg:<2d}={_hex(cpu.fpr[reg])}"
        return f"FP{reg:<2d} = " + c_format("%#-13.6g", regs.fpr_s(reg))

    for i in range(8):
        text += " ".join(single(i + 8 * column) for column in range(4)) + "\n"
    return text


def format_insts(start: int, end: int) -> str:
    """The disassembly of every instruction in start..end."""
    lines: list[str] = []
    for addr in range(start, end, 4):
        inst: instruction.Instruction | None = memory.read_inst(addr)
        if inst is not None:
            lines.append(instruction.format_an_inst(inst, addr))
    return "".join(lines)


def print_text_listing() -> None:
    """The REPL's ``disasm``: the user text segment, on stderr."""
    text: str = format_insts(layout.TEXT_BOT, memory.text_top)
    if not text:
        console.message("No program loaded.\n")
        return
    console.message("\n\tUSER TEXT SEGMENT\n")
    console.message(text + "\n")


def format_data_segs() -> str:
    """The data, stack and kernel data segments (``-full_dump``)."""
    return (
        "\tDATA\n"
        + format_mem(layout.DATA_BOT, memory.data_top)
        + "\n\tSTACK\n"
        + format_mem(machine.u32(cpu.gpr[29]) & ~3, layout.STACK_TOP)
        + "\n\tKERNEL DATA\n"
        + format_mem(layout.K_DATA_BOT, memory.k_data_top)
    )


def _partial_line(addr: int) -> tuple[str, int]:
    """The words from addr up to the next 16-byte boundary, on one line."""
    if addr % BYTES_PER_LINE == 0:
        return "", addr
    text: str = f"[0x{addr:08x}]\t\t      "
    while addr % BYTES_PER_LINE != 0:
        text += f"  0x{_hex(memory.read_word(addr))}"
        addr += 4
    return text + "\n", addr


def format_mem(start: int, end: int) -> str:
    """Words start..end, four per line, a run of 4+ zero words as one line."""
    text: str
    addr: int
    text, addr = _partial_line((start + 3) & ~3)
    while addr < end:
        zeros: int = 0
        while addr + zeros * 4 < end and memory.read_word(addr + zeros * 4) == 0:
            zeros += 1
        if zeros >= 4:
            text += f"[0x{addr:08x}]...[0x{(addr + zeros * 4) & 0xFFFFFFFF:08x}]\t0x00000000\n"
            addr = (addr + zeros * 4) & 0xFFFFFFFF
            line: str
            line, addr = _partial_line(addr)
            text += line
        else:
            text += f"[0x{addr:08x}]\t\t      "
            while True:
                text += f"  0x{_hex(memory.read_word(addr))}"
                addr += 4
                if addr % BYTES_PER_LINE == 0:
                    break
            text += "\n"
    return text
