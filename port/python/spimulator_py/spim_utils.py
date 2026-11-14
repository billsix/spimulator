"""Setting the machine up, loading files, and breakpoints (spim-utils.c).

``initialize_world`` makes fresh memory and registers and assembles the
exception handler into the kernel segments.  ``initialize_run_stack`` lays
out argc, argv and the environment on the stack the way a Unix kernel does
before ``__start`` runs.
"""

import io
import os
import pathlib
from dataclasses import dataclass
from typing import BinaryIO

from . import (
    console,
    data,
    machine,
    memory,
    options,
    parser,
    pseudo_op,
    run,
    scanner,
    symbol_table,
    traps,
)
from . import (
    instruction as instruction_module,
)
from . import memory_layout as layout
from . import registers as regs
from .inst import Instruction
from .registers import cpu

SPIM_VERSION: str = "Version 9.1.24 of August 1, 2023 (final)"

DEFAULT_EXCEPTIONS: str = "<embedded exceptions.s>"
"""Stands for the built-in exception handler (src/exceptions.s)."""

EXCEPTIONS_FILE: pathlib.Path = (
    pathlib.Path(__file__).resolve().parents[3] / "src" / "exceptions.s"
)
"""The C binary embeds this file with #embed; the port reads it from the repo."""


def _startup_environment() -> list[bytes]:
    """The environment the process started with, as ``NAME=value`` strings.

    Python changes its own environment at start-up (it may add
    ``LC_CTYPE=C.UTF-8``, PEP 538), and the strings end up on the MIPS
    stack, where a program sees their size in $sp.  Linux keeps the
    original block in /proc/self/environ, so read that.
    """
    try:
        block: bytes = pathlib.Path("/proc/self/environ").read_bytes()
    except OSError:
        return [key + b"=" + value for key, value in os.environb.items()]
    return [entry for entry in block.split(b"\0") if entry]


STARTUP_ENVIRONMENT: list[bytes] = _startup_environment()


def initialize_world(exception_files: str | None, print_message: bool) -> None:
    """Clear memory and registers, and load the exception handler.

    Args:
        exception_files: ``;``-separated handler files, DEFAULT_EXCEPTIONS
            for the built-in one, or None for no handler (``-noexception``).
        print_message: print the ``Loaded:`` line for each handler file.
    """
    memory.make_memory(
        options.initial_text_size,
        options.initial_data_size,
        options.initial_data_limit,
        options.initial_stack_size,
        options.initial_stack_limit,
        options.initial_kernel_text_size,
        options.initial_kernel_data_size,
        options.initial_kernel_data_limit,
    )
    initialize_registers()
    symbol_table.initialize_symbol_table()
    instruction_module.kernel_text_begins_at_point(layout.KERNEL_TEXT_BOTTOM)
    data.kernel_data_begins_at_point(layout.KERNEL_DATA_BOTTOM)
    data.data_begins_at_point(layout.DATA_BOTTOM)
    instruction_module.text_begins_at_point(layout.TEXT_BOTTOM)
    if exception_files is not None:
        old_bare: bool = options.bare_machine
        old_accept: bool = options.accept_pseudo_instructions
        options.bare_machine = False  # the handler is written for the extended machine
        options.accept_pseudo_instructions = True
        if exception_files == DEFAULT_EXCEPTIONS:
            _assemble(io.BytesIO(EXCEPTIONS_FILE.read_bytes()), DEFAULT_EXCEPTIONS)
            if print_message:
                console.message(f"Loaded: {DEFAULT_EXCEPTIONS}\n")
        else:
            for name in (part for part in exception_files.split(";") if part):
                if not read_assembly_file(name):
                    raise console.fatal_error(
                        f"Cannot read exception handler: {name}\n"
                    )
                if print_message:
                    console.message(f"Loaded: {name}\n")
        options.bare_machine = old_bare
        options.accept_pseudo_instructions = old_accept
        if not options.bare_machine:
            symbol_table.make_label_global("main")  # in case .globl main is missing
            symbol_table.record_label("main", 0, False)
    scanner.initialize(None)
    delete_all_breakpoints()


def write_startup_message() -> None:
    """The version banner (printed by ``reinitialize``)."""
    console.message(
        f"SPIM {SPIM_VERSION}\n"
        "Copyright 1990-2023 by James Larus.\n"
        "All Rights Reserved.\n"
        "SPIM is distributed under a BSD license.\n"
        "See the file README for a full copyright notice.\n"
    )


def initialize_registers() -> None:
    """Zero every register, then set $sp, Status and the FPU's FIR."""
    for i in range(len(cpu.floating_point_registers)):
        cpu.floating_point_registers[i] = 0
    for i in range(regs.GENERAL_PURPOSE_REGISTER_COUNT):
        cpu.general_purpose_registers[i] = 0
    cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] = (
        layout.STACK_TOP - 4 - 4096
    )
    cpu.multiply_divide_high = cpu.multiply_divide_low = 0
    cpu.program_counter = 0
    coprocessor_0: list[int] = cpu.coprocessor_registers[0]
    coprocessor_0[regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS] = 0
    coprocessor_0[regs.COPROCESSOR_0_COUNT] = 0
    coprocessor_0[regs.COPROCESSOR_0_COMPARE] = 0
    coprocessor_0[regs.COPROCESSOR_0_STATUS] = (
        (regs.STATUS_COPROCESSOR_USABLE & 0x30000000)
        | regs.STATUS_INTERRUPT_MASK
        | regs.STATUS_USER_MODE
    )
    coprocessor_0[regs.COPROCESSOR_0_CAUSE] = 0
    coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER] = 0
    coprocessor_0[regs.COPROCESSOR_0_CONFIG] = 0
    cpu.coprocessor_registers[1][regs.FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER] = (
        regs.FLOATING_POINT_IMPLEMENTATION_HAS_WORD
        | regs.FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE
        | regs.FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE
    )
    cpu.coprocessor_registers[1][regs.FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER] = 0


def _assemble(source: BinaryIO, name: str) -> None:
    """Parse and emit one source file, then close its scope."""
    parser.parser_initialize(source, name)
    parser.parse_file()
    symbol_table.flush_local_labels(not pseudo_op.parse_error_occurred)
    data.end_of_assembly_file()


def read_assembly_file(name: str) -> bool:
    """Assemble the file name; False (with a message) if it cannot be opened."""
    try:
        source: io.BufferedReader = open(console.to_bytes(name), "rb")  # noqa: SIM115
    except OSError:
        console.error(f"Cannot open file: `{name}'\n")
        return False
    with source:
        _assemble(source, name)
    return True


def starting_address() -> int:
    """The address of ``__start``, or 0."""
    return symbol_table.find_symbol_address(layout.DEFAULT_RUN_LOCATION)


def _copy_string_to_stack(text: bytes) -> int:
    """Push text and its NUL byte by byte; return the string's address."""
    for byte in reversed(text + b"\0"):
        memory.write_byte(
            machine.u32(cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER]),
            byte,
        )
        cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] = machine.s32(
            cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] - 1
        )
    return machine.u32(cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] + 1)


def _copy_integer_to_stack(value: int) -> int:
    """Push a word; return its address."""
    memory.write_word(
        machine.u32(cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER]), value
    )
    cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] = machine.s32(
        cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] - 4
    )
    return machine.u32(cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] + 4)


def initialize_run_stack(argv: list[str]) -> None:
    """Lay out the environment, argv and argc on the stack, as Unix does.

    From the top down: the environment strings, the argv strings, then
    (word-aligned) the NULL-terminated envp vector, the NULL-terminated
    argv vector, and argc, where $sp points.  $a0, $a1 and $a2 get argc,
    argv and envp.
    """
    cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] = machine.s32(
        layout.STACK_TOP - 1
    )
    environment_addresses: list[int] = [
        _copy_string_to_stack(entry) for entry in STARTUP_ENVIRONMENT
    ]
    argument_addresses: list[int] = [
        _copy_string_to_stack(console.to_bytes(argument)) for argument in argv
    ]
    sp: int = cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] & ~3
    sp -= 4
    cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER] = machine.s32(sp & ~7)
    _copy_integer_to_stack(0)
    for address in reversed(environment_addresses):
        cpu.general_purpose_registers[regs.ARGUMENT_REGISTER_2] = machine.s32(
            _copy_integer_to_stack(address)
        )
    _copy_integer_to_stack(0)
    for address in reversed(argument_addresses):
        cpu.general_purpose_registers[regs.ARGUMENT_REGISTER_1] = machine.s32(
            _copy_integer_to_stack(address)
        )
    cpu.general_purpose_registers[regs.ARGUMENT_REGISTER_0] = len(argv)
    memory.write_word(
        machine.u32(cpu.general_purpose_registers[regs.STACK_POINTER_REGISTER]),
        len(argv),
    )


def run_program(
    program_counter: int, steps: int, display: bool, continue_breakpoint: bool
) -> bool:
    """Run from pc for steps instructions; True if it stopped at a breakpoint.

    With continue_breakpoint, first step over a breakpoint at pc.
    """
    if continue_breakpoint and instruction_module.instruction_is_breakpoint(
        program_counter
    ):
        address: int = (
            program_counter if cpu.program_counter == 0 else cpu.program_counter
        )
        delete_breakpoint(address)
        traps.exception_occurred = False
        run.run_spim(address, 1, display)
        add_breakpoint(address)
        steps -= 1
        program_counter = cpu.program_counter
    traps.exception_occurred = False
    run.run_spim(program_counter, steps, display)
    if (
        traps.exception_occurred
        and regs.exception_code() == traps.ExceptionCode.BREAKPOINT
    ):
        # The break is handled by the simulator, not MIPS code: clear EXL so
        # later interrupts set EPC again.
        coprocessor_0: list[int] = cpu.coprocessor_registers[0]
        coprocessor_0[regs.COPROCESSOR_0_STATUS] = machine.s32(
            coprocessor_0[regs.COPROCESSOR_0_STATUS] & ~regs.STATUS_EXCEPTION_LEVEL
        )
        return True
    return False


@dataclass
class _Breakpoint:
    """A breakpoint and the instruction it replaced."""

    address: int
    instruction_module: Instruction


_breakpoints: list[_Breakpoint] = []  # newest first


def add_breakpoint(address: int) -> None:
    """Set a breakpoint at addr."""
    old: Instruction | None = instruction_module.set_breakpoint(address)
    if old is not None:
        _breakpoints.insert(0, _Breakpoint(address, old))
    elif traps.exception_occurred:
        console.error(f"Cannot put a breakpoint at address 0x{address:08x}\n")
    else:
        console.error(f"No instruction to breakpoint at address 0x{address:08x}\n")


def delete_breakpoint(address: int) -> None:
    """Remove every breakpoint at addr, restoring the instruction."""
    found: list[_Breakpoint] = [b for b in _breakpoints if b.address == address]
    for breakpoint in found:
        memory.write_instruction(address, breakpoint.instruction_module)
        _breakpoints.remove(breakpoint)
    if not found:
        console.error(f"No breakpoint to delete at 0x{address:08x}\n")


def delete_all_breakpoints() -> None:
    """Forget every breakpoint (without restoring instructions)."""
    _breakpoints.clear()


def list_breakpoints() -> None:
    """The REPL's ``list``."""
    if not _breakpoints:
        console.message("No breakpoints set\n")
    for breakpoint in _breakpoints:
        console.message(f"Breakpoint at 0x{breakpoint.address:08x}\n")
