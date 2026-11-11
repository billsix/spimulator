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
    instruction,
    machine,
    memory,
    options,
    parser,
    run,
    scanner,
    symbol_table,
    traps,
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
        options.initial_k_text_size,
        options.initial_k_data_size,
        options.initial_k_data_limit,
    )
    initialize_registers()
    symbol_table.initialize_symbol_table()
    instruction.k_text_begins_at_point(layout.K_TEXT_BOT)
    data.k_data_begins_at_point(layout.K_DATA_BOT)
    data.data_begins_at_point(layout.DATA_BOT)
    instruction.text_begins_at_point(layout.TEXT_BOT)
    if exception_files is not None:
        old_bare: bool = options.bare_machine
        old_accept: bool = options.accept_pseudo_insts
        options.bare_machine = False  # the handler is written for the extended machine
        options.accept_pseudo_insts = True
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
        options.accept_pseudo_insts = old_accept
        if not options.bare_machine:
            symbol_table.make_label_global("main")  # in case .globl main is missing
            symbol_table.record_label("main", 0, False)
    scanner.init(None)
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
    for i in range(len(cpu.fpr)):
        cpu.fpr[i] = 0
    for i in range(regs.R_LENGTH):
        cpu.gpr[i] = 0
    cpu.gpr[regs.REG_SP] = layout.STACK_TOP - 4 - 4096
    cpu.hi = cpu.lo = 0
    cpu.pc = 0
    cp0: list[int] = cpu.cpr[0]
    cp0[regs.CP0_BADVADDR] = 0
    cp0[regs.CP0_COUNT] = 0
    cp0[regs.CP0_COMPARE] = 0
    cp0[regs.CP0_STATUS] = (
        (regs.STATUS_CU & 0x30000000) | regs.STATUS_IM | regs.STATUS_UM
    )
    cp0[regs.CP0_CAUSE] = 0
    cp0[regs.CP0_EPC] = 0
    cp0[regs.CP0_CONFIG] = 0
    cpu.cpr[1][regs.FIR_REG] = regs.FIR_W | regs.FIR_D | regs.FIR_S
    cpu.cpr[1][regs.FCSR_REG] = 0


def _assemble(source: BinaryIO, name: str) -> None:
    """Parse and emit one source file, then close its scope."""
    parser.parser_init(source, name)
    parser.parse_file()
    from . import pseudo_op

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


def _copy_str_to_stack(text: bytes) -> int:
    """Push text and its NUL byte by byte; return the string's address."""
    for byte in reversed(text + b"\0"):
        memory.write_byte(machine.u32(cpu.gpr[regs.REG_SP]), byte)
        cpu.gpr[regs.REG_SP] = machine.s32(cpu.gpr[regs.REG_SP] - 1)
    return machine.u32(cpu.gpr[regs.REG_SP] + 1)


def _copy_int_to_stack(value: int) -> int:
    """Push a word; return its address."""
    memory.write_word(machine.u32(cpu.gpr[regs.REG_SP]), value)
    cpu.gpr[regs.REG_SP] = machine.s32(cpu.gpr[regs.REG_SP] - 4)
    return machine.u32(cpu.gpr[regs.REG_SP] + 4)


def initialize_run_stack(argv: list[str]) -> None:
    """Lay out the environment, argv and argc on the stack, as Unix does.

    From the top down: the environment strings, the argv strings, then
    (word-aligned) the NULL-terminated envp vector, the NULL-terminated
    argv vector, and argc, where $sp points.  $a0, $a1 and $a2 get argc,
    argv and envp.
    """
    cpu.gpr[regs.REG_SP] = machine.s32(layout.STACK_TOP - 1)
    env_addrs: list[int] = [_copy_str_to_stack(entry) for entry in STARTUP_ENVIRONMENT]
    arg_addrs: list[int] = [_copy_str_to_stack(console.to_bytes(arg)) for arg in argv]
    sp: int = cpu.gpr[regs.REG_SP] & ~3
    sp -= 4
    cpu.gpr[regs.REG_SP] = machine.s32(sp & ~7)
    _copy_int_to_stack(0)
    for addr in reversed(env_addrs):
        cpu.gpr[regs.REG_A2] = machine.s32(_copy_int_to_stack(addr))
    _copy_int_to_stack(0)
    for addr in reversed(arg_addrs):
        cpu.gpr[regs.REG_A1] = machine.s32(_copy_int_to_stack(addr))
    cpu.gpr[regs.REG_A0] = len(argv)
    memory.write_word(machine.u32(cpu.gpr[regs.REG_SP]), len(argv))


def run_program(pc: int, steps: int, display: bool, cont_bkpt: bool) -> bool:
    """Run from pc for steps instructions; True if it stopped at a breakpoint.

    With cont_bkpt, first step over a breakpoint at pc.
    """
    if cont_bkpt and instruction.inst_is_breakpoint(pc):
        addr: int = pc if cpu.pc == 0 else cpu.pc
        delete_breakpoint(addr)
        traps.exception_occurred = False
        run.run_spim(addr, 1, display)
        add_breakpoint(addr)
        steps -= 1
        pc = cpu.pc
    traps.exception_occurred = False
    run.run_spim(pc, steps, display)
    if traps.exception_occurred and regs.exc_code() == traps.ExcCode.BP:
        # The break is handled by the simulator, not MIPS code: clear EXL so
        # later interrupts set EPC again.
        cp0: list[int] = cpu.cpr[0]
        cp0[regs.CP0_STATUS] = machine.s32(cp0[regs.CP0_STATUS] & ~regs.STATUS_EXL)
        return True
    return False


@dataclass
class _Breakpoint:
    """A breakpoint and the instruction it replaced."""

    addr: int
    instruction: Instruction


_breakpoints: list[_Breakpoint] = []  # newest first


def add_breakpoint(addr: int) -> None:
    """Set a breakpoint at addr."""
    old: Instruction | None = instruction.set_breakpoint(addr)
    if old is not None:
        _breakpoints.insert(0, _Breakpoint(addr, old))
    elif traps.exception_occurred:
        console.error(f"Cannot put a breakpoint at address 0x{addr:08x}\n")
    else:
        console.error(f"No instruction to breakpoint at address 0x{addr:08x}\n")


def delete_breakpoint(addr: int) -> None:
    """Remove every breakpoint at addr, restoring the instruction."""
    found: list[_Breakpoint] = [b for b in _breakpoints if b.addr == addr]
    for bkpt in found:
        memory.write_inst(addr, bkpt.instruction)
        _breakpoints.remove(bkpt)
    if not found:
        console.error(f"No breakpoint to delete at 0x{addr:08x}\n")


def delete_all_breakpoints() -> None:
    """Forget every breakpoint (without restoring instructions)."""
    _breakpoints.clear()


def list_breakpoints() -> None:
    """The REPL's ``list``."""
    if not _breakpoints:
        console.message("No breakpoints set\n")
    for bkpt in _breakpoints:
        console.message(f"Breakpoint at 0x{bkpt.addr:08x}\n")
