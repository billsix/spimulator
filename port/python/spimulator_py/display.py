"""Printing the machine: registers, memory and disassembly (display-utils.c).

These produce the REPL's ``print_all_regs`` and ``disasm`` output and the
``-dump`` files (text.asm, data.asm).
"""

from . import console, machine, memory
from . import instruction as instruction_module
from . import memory_layout as layout
from . import registers as regs
from .cformat import c_format
from .registers import cpu

BYTES_PER_LINE: int = 16


def _hex(value: int) -> str:
    """value as eight hex digits (C ``%08x`` of a signed word)."""
    return f"{machine.u32(value):08x}"


def format_registers(
    general_purpose_registers_hex: bool, floating_point_register_hex: bool
) -> str:
    """Every register, laid out as the C engine's print_all_regs does."""
    coprocessor_0: list[int] = cpu.coprocessor_registers[0]
    text: str = (
        f" PC      = {_hex(cpu.program_counter)}   EPC     = {_hex(coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER])}  "
        f" Cause   = {_hex(coprocessor_0[regs.COPROCESSOR_0_CAUSE])}  "
        f" BadVAddr= {_hex(coprocessor_0[regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS])}\n"
        f" Status  = {_hex(coprocessor_0[regs.COPROCESSOR_0_STATUS])}   HI      = {_hex(cpu.multiply_divide_high)}  "
        f" LO      = {_hex(cpu.multiply_divide_low)}\n"
        "\t\t\t\t General Registers\n"
    )

    def general(i: int) -> str:
        """One general register, decimal or hex."""
        name: str = regs.INTEGER_REGISTER_NAMES[i]
        value: str = (
            _hex(cpu.general_purpose_registers[i])
            if general_purpose_registers_hex
            else f"{cpu.general_purpose_registers[i]:<10d}"
        )
        return f"R{i:<2d} ({name:>2s}) = {value}"

    fill: str = "  " if general_purpose_registers_hex else " "
    for i in range(8):
        text += fill.join(general(i + 8 * column) for column in range(4)) + "\n"
    floating_point_control_registers: list[int] = cpu.coprocessor_registers[1]
    text += (
        f"\n FIR    = {_hex(floating_point_control_registers[regs.FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER])}    FCSR    = {_hex(floating_point_control_registers[regs.FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER])}   "
        "\t\t\t      Double Floating Point Registers\n"
    )

    def double(pair: int) -> str:
        """The double in FP register pair number pair (registers 2*pair, +1)."""
        low: int = cpu.floating_point_registers[2 * pair]
        high: int = cpu.floating_point_registers[2 * pair + 1]
        if floating_point_register_hex:
            return f"FP{2 * pair:<2d}={_hex(low)},{_hex(high)}"
        return f"FP{2 * pair:<2d} = " + c_format(
            "%#-13.6g", machine.words_double(low, high)
        )

    for i in range(4):
        text += " ".join(double(i + 4 * column) for column in range(4)) + "\n"
    text += "\t\t\t      Single Floating Point Registers\n"

    def single(register_number: int) -> str:
        """FP register reg as a float."""
        if floating_point_register_hex:
            return f"FP{register_number:<2d}={_hex(cpu.floating_point_registers[register_number])}"
        return f"FP{register_number:<2d} = " + c_format(
            "%#-13.6g", regs.floating_point_register_single(register_number)
        )

    for i in range(8):
        text += " ".join(single(i + 8 * column) for column in range(4)) + "\n"
    return text


def format_instructions(start: int, end: int) -> str:
    """The disassembly of every instruction in start..end."""
    lines: list[str] = []
    for address in range(start, end, 4):
        instruction: instruction_module.Instruction | None = memory.read_instruction(
            address
        )
        if instruction is not None:
            lines.append(instruction_module.format_an_instruction(instruction, address))
    return "".join(lines)


def print_text_listing() -> None:
    """The REPL's ``disasm``: the user text segment, on stderr."""
    text: str = format_instructions(layout.TEXT_BOTTOM, memory.text_top)
    if not text:
        console.message("No program loaded.\n")
        return
    console.message("\n\tUSER TEXT SEGMENT\n")
    console.message(text + "\n")


def format_data_segments() -> str:
    """The data, stack and kernel data segments (``-full_dump``)."""
    return (
        "\tDATA\n"
        + format_memory(layout.DATA_BOTTOM, memory.data_top)
        + "\n\tSTACK\n"
        + format_memory(
            machine.u32(cpu.general_purpose_registers[29]) & ~3, layout.STACK_TOP
        )
        + "\n\tKERNEL DATA\n"
        + format_memory(layout.KERNEL_DATA_BOTTOM, memory.kernel_data_top)
    )


def _partial_line(address: int) -> tuple[str, int]:
    """The words from addr up to the next 16-byte boundary, on one line."""
    if address % BYTES_PER_LINE == 0:
        return "", address
    text: str = f"[0x{address:08x}]\t\t      "
    while address % BYTES_PER_LINE != 0:
        text += f"  0x{_hex(memory.read_word(address))}"
        address += 4
    return text + "\n", address


def format_memory(start: int, end: int) -> str:
    """Words start..end, four per line, a run of 4+ zero words as one line."""
    text: str
    address: int
    text, address = _partial_line((start + 3) & ~3)
    while address < end:
        zeros: int = 0
        while address + zeros * 4 < end and memory.read_word(address + zeros * 4) == 0:
            zeros += 1
        if zeros >= 4:
            text += f"[0x{address:08x}]...[0x{(address + zeros * 4) & 0xFFFFFFFF:08x}]\t0x00000000\n"
            address = (address + zeros * 4) & 0xFFFFFFFF
            line: str
            line, address = _partial_line(address)
            text += line
        else:
            text += f"[0x{address:08x}]\t\t      "
            while True:
                text += f"  0x{_hex(memory.read_word(address))}"
                address += 4
                if address % BYTES_PER_LINE == 0:
                    break
            text += "\n"
    return text
