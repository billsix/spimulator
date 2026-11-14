"""The executor: fetch, decode and execute instructions (the C engine's run.c).

The C engine executes with one big ``switch`` on the opcode, re-reading the
instruction's fields every time.  Python is much slower at that, so this
port **decodes once**: the first time an instruction executes,
:func:`_compile` turns it into a small function with its register numbers
and immediate already pulled out, and caches it on the instruction.  Each
``@_executes(...)`` function below is one case of the C ``switch``.

A step function returns None to go on, or a bool to stop the run: False
when the program exits (syscall 10 or 17), True at a breakpoint.

Delayed branches (``-delayed_branches``) execute the instruction after a
branch before jumping, by running one step recursively, as the C engine
does.  Delayed loads keep a two-entry queue of pending register writes.
"""

import math
import time
from collections.abc import Callable

from . import console, machine, memory, opcodes, options, syscall, traps
from . import instruction as instruction_module
from . import memory_layout as layout
from . import registers as regs
from .inst import ImmediateExpression, Instruction
from .machine import M32
from .opcode_table import OpType, Tok
from .registers import cpu
from .traps import ExceptionCode

type Step = Callable[[], bool | None]
"""An instruction's compiled form: run it once."""

SIGN: int = 0x80000000

general_purpose_registers: list[int] = cpu.general_purpose_registers
floating_point_register: list[int] = cpu.floating_point_registers

# A pending delayed load: (register list, register number, value).
type _Pending = tuple[list[int], int, int]
_delayed_load_1: _Pending | None = None
_delayed_load_2: _Pending | None = None

_display: bool = False  # print each instruction (the REPL's step)

_timer_deadline: float | None = None


def _wrap(value: int) -> int:
    """value as a signed 32-bit integer (the fast spelling of machine.s32)."""
    return ((value + SIGN) & M32) - SIGN


# --- the CP0 timer ----------------------------------------------------------------


def _start_coprocessor_0_timer() -> None:
    """Start the 10 ms timer unless it is already running."""
    global _timer_deadline
    now: float = time.monotonic()
    if _timer_deadline is None or now >= _timer_deadline:
        _timer_deadline = now + layout.TIMER_TICK_MS / 1000


def _poll_coprocessor_0_timer() -> None:
    """Count one tick if the timer expired, raising interrupt 7 on Compare."""
    global _timer_deadline
    now: float = time.monotonic()
    if _timer_deadline is not None and now >= _timer_deadline:
        coprocessor_0: list[int] = cpu.coprocessor_registers[0]
        coprocessor_0[regs.COPROCESSOR_0_COUNT] = _wrap(
            coprocessor_0[regs.COPROCESSOR_0_COUNT] + 1
        )
        if (
            coprocessor_0[regs.COPROCESSOR_0_COUNT]
            == coprocessor_0[regs.COPROCESSOR_0_COMPARE]
        ):
            memory._raise_interrupt(7)
        _timer_deadline = now + layout.TIMER_TICK_MS / 1000


# --- the main loop ------------------------------------------------------------------


def run_spim(initial_program_counter: int, steps_to_run: int, display: bool) -> bool:
    """Run from initial_program_counter for up to steps_to_run instructions.

    Returns:
        bool: True if the program can continue (steps ran out, or a
        breakpoint), False if it exited.

    Raises:
        RunError: a run-time error (a non-instruction, an undefined symbol,
            a bad syscall, ...).
    """
    global _display, _delayed_load_1, _delayed_load_2
    saved_display: bool = _display
    _display = display
    try:
        cpu.program_counter = initial_program_counter
        mapped: bool = not options.bare_machine and options.mapped_io
        chunk: int = layout.IO_INTERVAL if mapped else steps_to_run
        _start_coprocessor_0_timer()
        delayed_loads: bool = options.delayed_loads
        text_segment: list[Instruction | None] = memory.text_segment
        text_top: int = memory.text_top
        text_bottom: int = layout.TEXT_BOTTOM
        poll: int = 0
        while steps_to_run > 0:
            step_size: int = min(chunk, steps_to_run)
            steps_to_run -= step_size
            if mapped:
                memory.check_memory_mapped_io()
            coprocessor_0: list[int] = cpu.coprocessor_registers[0]
            status: int = coprocessor_0[regs.COPROCESSOR_0_STATUS]
            if (
                status & regs.STATUS_INTERRUPT_ENABLE
                and not status & regs.STATUS_EXCEPTION_LEVEL
                and coprocessor_0[regs.COPROCESSOR_0_CAUSE]
                & regs.CAUSE_INTERRUPTS_PENDING
                & status
                & regs.STATUS_INTERRUPT_MASK
            ):
                # Take the interrupt before the next instruction, so EPC
                # points at the instruction to return to.
                traps.raise_exception(ExceptionCode.INTERRUPT)
                syscall.handle_exception()
            for _ in range(step_size):
                general_purpose_registers[0] = 0  # $0 is always zero
                poll -= 1
                if poll < 0:
                    poll = 63
                    _poll_coprocessor_0_timer()
                program_counter: int = cpu.program_counter
                instruction: Instruction | None
                if (
                    text_bottom <= program_counter < text_top
                    and not program_counter & 3
                ):
                    instruction = text_segment[(program_counter - text_bottom) >> 2]
                else:
                    traps.exception_occurred = False
                    instruction = memory.read_instruction(program_counter)
                    if traps.exception_occurred:  # in fetching the instruction
                        traps.exception_occurred = False
                        syscall.handle_exception()
                        continue
                if instruction is None:
                    raise console.run_error(
                        f"Attempt to execute non-instruction at 0x{program_counter:08x}\n"
                    )
                expression: ImmediateExpression | None = instruction.expression
                if (
                    expression is not None
                    and expression.symbol is not None
                    and expression.symbol.address == 0
                ):
                    raise console.run_error(
                        f"Instruction references undefined symbol at 0x{program_counter:08x}\n"
                        f"  {instruction_module.instruction_to_string(program_counter)}"
                    )
                if delayed_loads:
                    if _delayed_load_2 is not None:
                        registers, number, value = _delayed_load_2
                        registers[number] = value
                    _delayed_load_2 = _delayed_load_1
                    _delayed_load_1 = None
                step: Step | None = instruction.exec_cache
                if step is None:
                    step = _compile(instruction)
                    instruction.exec_cache = step
                traps.exception_occurred = False
                stop: bool | None = step()
                if stop is not None:
                    return stop
                cpu.program_counter = (cpu.program_counter + 4) & M32
                if display:
                    instruction_module.print_instruction(cpu.program_counter)
                if traps.exception_occurred:
                    syscall.handle_exception()
        return True
    finally:
        _display = saved_display


def _compile(instruction: Instruction) -> Step:
    """The step function of inst (decoded once, then cached on it).

    Raises:
        SystemExit: the opcode cannot be executed (the C engine's fatal
            "Unknown instruction type").
    """
    factory: Callable[[Instruction], Step] | None = _FACTORIES.get(instruction.opcode)
    if factory is None:
        opcode: int = machine.s16(instruction.opcode)

        def unknown() -> None:
            raise console.fatal_error(f"Unknown instruction type: {opcode}\n")

        return unknown
    return factory(instruction)


_FACTORIES: dict[int, Callable[[Instruction], Step]] = {}


def _executes(
    *tokens: Tok,
) -> Callable[[Callable[[Instruction], Step]], Callable[[Instruction], Step]]:
    """Register the decorated function as the compiler for these opcodes."""

    def register(
        factory: Callable[[Instruction], Step],
    ) -> Callable[[Instruction], Step]:
        for token in tokens:
            _FACTORIES[token] = factory
        return factory

    return register


def _raise(code: ExceptionCode) -> None:
    """Raise a MIPS exception; the main loop handles it after this step."""
    traps.raise_exception(code)


# --- branches and loads ---------------------------------------------------------------


def _jump(target: int) -> None:
    """Jump to target, first running the delay-slot instruction if delayed."""
    if options.delayed_branches:
        traps.running_in_delay_slot = True
        run_spim((cpu.program_counter + 4) & M32, 1, _display)
        traps.running_in_delay_slot = False
    cpu.program_counter = (target - 4) & M32  # the main loop adds 4


def _branch(taken: bool, offset: int, nullify: bool) -> None:
    """A conditional branch of offset bytes from its own address.

    A branch-likely (nullify) that is not taken skips its delay slot.
    """
    if taken:
        target: int = cpu.program_counter + offset
        if options.delayed_branches:
            target += 4
        _jump(target)
    elif nullify:
        cpu.program_counter = (cpu.program_counter + 4) & M32
    else:
        pass  # not taken and not nullified: the delay slot runs as the next instruction


def _load(registers: list[int], number: int, value: int) -> None:
    """Write a loaded value now, or one instruction later with delayed loads."""
    global _delayed_load_1
    if options.delayed_loads:
        _delayed_load_1 = (registers, number, value)
    else:
        registers[number] = value


def _coprocessor_unusable(instruction: Instruction) -> Step:
    """Coprocessor 2 does not exist: raise CpU."""

    def step() -> None:
        _raise(ExceptionCode.COPROCESSOR_UNUSABLE)

    return step


for _token in (
    Tok.BC2F_OPCODE, Tok.BC2FL_OPCODE, Tok.BC2T_OPCODE, Tok.BC2TL_OPCODE,
    Tok.CFC2_OPCODE, Tok.COP2_OPCODE, Tok.CTC2_OPCODE, Tok.LDC2_OPCODE,
    Tok.LWC2_OPCODE, Tok.MFC2_OPCODE, Tok.MTC2_OPCODE, Tok.SDC2_OPCODE,
    Tok.SWC2_OPCODE,
):  # fmt: skip
    _FACTORIES[_token] = _coprocessor_unusable


def _reserved(instruction: Instruction) -> Step:
    """Not a MIPS32 instruction (rfe) or the TLB, which is not simulated: RI."""

    def step() -> None:
        _raise(ExceptionCode.RESERVED_INSTRUCTION)

    return step


for _token in (
    Tok.RFE_OPCODE,
    Tok.TLBP_OPCODE,
    Tok.TLBR_OPCODE,
    Tok.TLBWI_OPCODE,
    Tok.TLBWR_OPCODE,
):
    _FACTORIES[_token] = _reserved


@_executes(Tok.CACHE_OPCODE, Tok.PREF_OPCODE, Tok.SYNC_OPCODE)
def _no_operation(instruction: Instruction) -> Step:
    """Cache and memory-ordering details are not simulated."""

    def step() -> None:
        return None

    return step


# --- integer arithmetic and logic ----------------------------------------------------


@_executes(Tok.ADD_OPCODE)
def _add(instruction: Instruction) -> Step:
    """add: rd = rs + rt, trapping on signed overflow."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        total: int = (
            general_purpose_registers[source_register]
            + general_purpose_registers[target_register]
        )
        if not -SIGN <= total < SIGN:
            _raise(ExceptionCode.ARITHMETIC_OVERFLOW)
        else:
            general_purpose_registers[destination_register] = total

    return step


@_executes(Tok.ADDI_OPCODE)
def _addi(instruction: Instruction) -> Step:
    """addi: rt = rs + imm, trapping on signed overflow."""
    target_register, source_register, immediate = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )

    def step() -> None:
        total: int = general_purpose_registers[source_register] + immediate
        if not -SIGN <= total < SIGN:
            _raise(ExceptionCode.ARITHMETIC_OVERFLOW)
        else:
            general_purpose_registers[target_register] = total

    return step


@_executes(Tok.ADDIU_OPCODE)
def _addiu(instruction: Instruction) -> Step:
    """addiu: rt = rs + imm, wrapping (no trap, despite the "u")."""
    target_register, source_register, immediate = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )

    def step() -> None:
        general_purpose_registers[target_register] = (
            (general_purpose_registers[source_register] + immediate + SIGN) & M32
        ) - SIGN

    return step


@_executes(Tok.ADDU_OPCODE)
def _addu(instruction: Instruction) -> Step:
    """addu: rd = rs + rt, wrapping."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        general_purpose_registers[destination_register] = (
            (
                general_purpose_registers[source_register]
                + general_purpose_registers[target_register]
                + SIGN
            )
            & M32
        ) - SIGN

    return step


@_executes(Tok.SUB_OPCODE)
def _subtract(instruction: Instruction) -> Step:
    """sub: rd = rs - rt, trapping on signed overflow."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        difference: int = (
            general_purpose_registers[source_register]
            - general_purpose_registers[target_register]
        )
        if not -SIGN <= difference < SIGN:
            _raise(ExceptionCode.ARITHMETIC_OVERFLOW)
        else:
            general_purpose_registers[destination_register] = difference

    return step


@_executes(Tok.SUBU_OPCODE)
def _subu(instruction: Instruction) -> Step:
    """subu: rd = rs - rt, wrapping."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        general_purpose_registers[destination_register] = (
            (
                general_purpose_registers[source_register]
                - general_purpose_registers[target_register]
                + SIGN
            )
            & M32
        ) - SIGN

    return step


def _logic(operation: Callable[[int, int], int]) -> Callable[[Instruction], Step]:
    """The factory of a three-register logical instruction."""

    def factory(instruction: Instruction) -> Step:
        destination_register, source_register, target_register = (
            instruction.destination_register,
            instruction.source_register,
            instruction.target_register,
        )

        def step() -> None:
            general_purpose_registers[destination_register] = operation(
                general_purpose_registers[source_register],
                general_purpose_registers[target_register],
            )

        return step

    return factory


_FACTORIES[Tok.AND_OPCODE] = _logic(lambda a, b: a & b)
_FACTORIES[Tok.OR_OPCODE] = _logic(lambda a, b: a | b)
_FACTORIES[Tok.XOR_OPCODE] = _logic(lambda a, b: a ^ b)
_FACTORIES[Tok.NOR_OPCODE] = _logic(lambda a, b: ~(a | b))
_FACTORIES[Tok.SLT_OPCODE] = _logic(lambda a, b: 1 if a < b else 0)
_FACTORIES[Tok.SLTU_OPCODE] = _logic(lambda a, b: 1 if a & M32 < b & M32 else 0)


@_executes(Tok.ANDI_OPCODE, Tok.ORI_OPCODE, Tok.XORI_OPCODE)
def _logic_immediate(instruction: Instruction) -> Step:
    """andi/ori/xori: the immediate is zero-extended."""
    target_register, source_register, immediate = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate & 0xFFFF,
    )
    opcode: int = instruction.opcode

    if opcode == Tok.ANDI_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = (
                general_purpose_registers[source_register] & immediate
            )

    elif opcode == Tok.ORI_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = (
                general_purpose_registers[source_register] | immediate
            )

    elif opcode == Tok.XORI_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = (
                general_purpose_registers[source_register] ^ immediate
            )

    else:
        raise AssertionError(
            f"_logic_immediate: opcode {opcode} is not andi/ori/xori; only they are"
            " registered"
        )

    return step


@_executes(Tok.SLTI_OPCODE)
def _slti(instruction: Instruction) -> Step:
    """slti: rt = 1 if rs < imm (signed)."""
    target_register, source_register, immediate = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )

    def step() -> None:
        general_purpose_registers[target_register] = (
            1 if general_purpose_registers[source_register] < immediate else 0
        )

    return step


@_executes(Tok.SLTIU_OPCODE)
def _sltiu(instruction: Instruction) -> Step:
    """sltiu: the immediate is sign-extended, then compared unsigned."""
    target_register, source_register, immediate = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate & M32,
    )

    def step() -> None:
        general_purpose_registers[target_register] = (
            1 if general_purpose_registers[source_register] & M32 < immediate else 0
        )

    return step


@_executes(Tok.LUI_OPCODE)
def _lui(instruction: Instruction) -> Step:
    """lui: rt = imm << 16."""
    target_register, value = (
        instruction.target_register,
        machine.s32(instruction.immediate << 16),
    )

    def step() -> None:
        general_purpose_registers[target_register] = value

    return step


@_executes(Tok.CLO_OPCODE, Tok.CLZ_OPCODE)
def _count_leading(instruction: Instruction) -> Step:
    """clo/clz: count the leading ones/zeros of rs."""
    destination_register, source_register = (
        instruction.destination_register,
        instruction.source_register,
    )
    count: Callable[[int], int] = (
        machine.leading_ones
        if instruction.opcode == Tok.CLO_OPCODE
        else machine.leading_zeros
    )

    def step() -> None:
        general_purpose_registers[destination_register] = count(
            general_purpose_registers[source_register]
        )

    return step


@_executes(Tok.MOVN_OPCODE, Tok.MOVZ_OPCODE)
def _move_conditional(instruction: Instruction) -> Step:
    """movn/movz: rd = rs if rt is nonzero/zero."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )
    if instruction.opcode == Tok.MOVN_OPCODE:

        def step() -> None:
            if general_purpose_registers[target_register] != 0:
                general_purpose_registers[destination_register] = (
                    general_purpose_registers[source_register]
                )

    else:

        def step() -> None:
            if general_purpose_registers[target_register] == 0:
                general_purpose_registers[destination_register] = (
                    general_purpose_registers[source_register]
                )

    return step


# --- shifts ----------------------------------------------------------------------------


@_executes(Tok.SLL_OPCODE, Tok.SRL_OPCODE, Tok.SRA_OPCODE)
def _shift(instruction: Instruction) -> Step:
    """sll/srl/sra by the constant shamt (srl is logical, sra arithmetic)."""
    destination_register, target_register, shift_amount = (
        instruction.destination_register,
        instruction.target_register,
        instruction.shift_amount,
    )
    opcode: int = instruction.opcode
    if shift_amount >= 32:  # cannot be encoded; the C engine copies rt

        def step() -> None:
            general_purpose_registers[destination_register] = general_purpose_registers[
                target_register
            ]

    elif opcode == Tok.SLL_OPCODE:

        def step() -> None:
            general_purpose_registers[destination_register] = (
                ((general_purpose_registers[target_register] << shift_amount) + SIGN)
                & M32
            ) - SIGN

    elif opcode == Tok.SRL_OPCODE:

        def step() -> None:
            general_purpose_registers[destination_register] = (
                ((general_purpose_registers[target_register] & M32) >> shift_amount)
                + SIGN
                & M32
            ) - SIGN

    else:

        def step() -> None:
            general_purpose_registers[destination_register] = (
                general_purpose_registers[target_register] >> shift_amount
            )  # Python's >> is arithmetic

    return step


@_executes(Tok.SLLV_OPCODE, Tok.SRLV_OPCODE, Tok.SRAV_OPCODE)
def _shift_variable(instruction: Instruction) -> Step:
    """sllv/srlv/srav by the low 5 bits of rs."""
    destination_register, target_register, source_register = (
        instruction.destination_register,
        instruction.target_register,
        instruction.source_register,
    )
    opcode: int = instruction.opcode
    if opcode == Tok.SLLV_OPCODE:

        def step() -> None:
            general_purpose_registers[destination_register] = (
                (
                    (
                        general_purpose_registers[target_register]
                        << (general_purpose_registers[source_register] & 31)
                    )
                    + SIGN
                )
                & M32
            ) - SIGN

    elif opcode == Tok.SRLV_OPCODE:

        def step() -> None:
            general_purpose_registers[destination_register] = (
                (
                    (general_purpose_registers[target_register] & M32)
                    >> (general_purpose_registers[source_register] & 31)
                )
                + SIGN
                & M32
            ) - SIGN

    else:

        def step() -> None:
            general_purpose_registers[destination_register] = general_purpose_registers[
                target_register
            ] >> (general_purpose_registers[source_register] & 31)

    return step


# --- multiply and divide ----------------------------------------------------------------


def _set_multiply_divide(product: int) -> None:
    """Split a 64-bit product (any sign) into HI and LO."""
    cpu.multiply_divide_low = _wrap(product)
    cpu.multiply_divide_high = _wrap(product >> 32)


@_executes(Tok.MULT_OPCODE, Tok.MULTU_OPCODE, Tok.MUL_OPCODE)
def _multiply(instruction: Instruction) -> Step:
    """mult/multu: HI:LO = rs * rt; mul also puts LO in rd (and clobbers HI)."""
    destination_register, source_register, target_register = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register,
    )
    opcode: int = instruction.opcode
    if opcode == Tok.MULTU_OPCODE:

        def step() -> None:
            _set_multiply_divide(
                (general_purpose_registers[source_register] & M32)
                * (general_purpose_registers[target_register] & M32)
            )

    elif opcode == Tok.MULT_OPCODE:

        def step() -> None:
            _set_multiply_divide(
                general_purpose_registers[source_register]
                * general_purpose_registers[target_register]
            )

    else:

        def step() -> None:
            _set_multiply_divide(
                general_purpose_registers[source_register]
                * general_purpose_registers[target_register]
            )
            general_purpose_registers[destination_register] = cpu.multiply_divide_low

    return step


@_executes(Tok.MADD_OPCODE, Tok.MADDU_OPCODE, Tok.MSUB_OPCODE, Tok.MSUBU_OPCODE)
def _multiply_accumulate(instruction: Instruction) -> Step:
    """madd/msub(u): HI:LO += or -= rs * rt, as 64-bit arithmetic."""
    source_register, target_register = (
        instruction.source_register,
        instruction.target_register,
    )
    opcode: int = instruction.opcode
    is_unsigned: bool = opcode in (Tok.MADDU_OPCODE, Tok.MSUBU_OPCODE)
    sign: int = 1 if opcode in (Tok.MADD_OPCODE, Tok.MADDU_OPCODE) else -1

    def step() -> None:
        a: int = (
            general_purpose_registers[source_register] & M32
            if is_unsigned
            else general_purpose_registers[source_register]
        )
        b: int = (
            general_purpose_registers[target_register] & M32
            if is_unsigned
            else general_purpose_registers[target_register]
        )
        accumulator: int = ((cpu.multiply_divide_high & M32) << 32) | (
            cpu.multiply_divide_low & M32
        )
        _set_multiply_divide(accumulator + sign * a * b)

    return step


@_executes(Tok.DIV_OPCODE)
def _divide(instruction: Instruction) -> Step:
    """div: LO = rs / rt, HI = rs % rt, truncating toward zero.

    Dividing by zero or INT_MIN by -1 leaves HI and LO unchanged.
    """
    source_register, target_register = (
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        divisor: int = general_purpose_registers[target_register]
        dividend: int = general_purpose_registers[source_register]
        if divisor != 0 and not (dividend == -SIGN and divisor == -1):
            cpu.multiply_divide_low = machine.c_divide(dividend, divisor)
            cpu.multiply_divide_high = machine.c_rem(dividend, divisor)

    return step


@_executes(Tok.DIVU_OPCODE)
def _divu(instruction: Instruction) -> Step:
    """divu: unsigned division; by zero leaves HI and LO unchanged."""
    source_register, target_register = (
        instruction.source_register,
        instruction.target_register,
    )

    def step() -> None:
        divisor: int = general_purpose_registers[target_register] & M32
        if divisor != 0:
            dividend: int = general_purpose_registers[source_register] & M32
            cpu.multiply_divide_low = _wrap(dividend // divisor)
            cpu.multiply_divide_high = _wrap(dividend % divisor)

    return step


@_executes(Tok.MFHI_OPCODE, Tok.MFLO_OPCODE)
def _move_from_multiply_divide(instruction: Instruction) -> Step:
    """mfhi/mflo: rd = HI or LO."""
    destination_register: int = instruction.destination_register
    if instruction.opcode == Tok.MFHI_OPCODE:

        def step() -> None:
            general_purpose_registers[destination_register] = cpu.multiply_divide_high

    else:

        def step() -> None:
            general_purpose_registers[destination_register] = cpu.multiply_divide_low

    return step


@_executes(Tok.MTHI_OPCODE, Tok.MTLO_OPCODE)
def _move_to_multiply_divide(instruction: Instruction) -> Step:
    """mthi/mtlo: HI or LO = rs."""
    source_register: int = instruction.source_register
    if instruction.opcode == Tok.MTHI_OPCODE:

        def step() -> None:
            cpu.multiply_divide_high = general_purpose_registers[source_register]

    else:

        def step() -> None:
            cpu.multiply_divide_low = general_purpose_registers[source_register]

    return step


# --- branches and jumps -----------------------------------------------------------------


def _branch_offset(instruction: Instruction) -> int:
    """The branch's displacement in bytes (the 16-bit field times 4)."""
    return instruction.immediate << 2


@_executes(Tok.BEQ_OPCODE, Tok.BEQL_OPCODE, Tok.BNE_OPCODE, Tok.BNEL_OPCODE)
def _branch_compare(instruction: Instruction) -> Step:
    """beq/bne (and their branch-likely forms): compare rs with rt."""
    source_register, target_register, offset = (
        instruction.source_register,
        instruction.target_register,
        _branch_offset(instruction),
    )
    opcode: int = instruction.opcode
    nullify: bool = opcode in (Tok.BEQL_OPCODE, Tok.BNEL_OPCODE)
    if opcode in (Tok.BEQ_OPCODE, Tok.BEQL_OPCODE):

        def step() -> None:
            _branch(
                general_purpose_registers[source_register]
                == general_purpose_registers[target_register],
                offset,
                nullify,
            )

    else:

        def step() -> None:
            _branch(
                general_purpose_registers[source_register]
                != general_purpose_registers[target_register],
                offset,
                nullify,
            )

    return step


def _non_negative(value: int) -> bool:
    """bgez's test."""
    return value >= 0


def _positive(value: int) -> bool:
    """bgtz's test."""
    return value > 0


def _non_positive(value: int) -> bool:
    """blez's test."""
    return value <= 0


def _negative(value: int) -> bool:
    """bltz's test."""
    return value < 0


_SIGN_TESTS: dict[int, Callable[[int], bool]] = {
    Tok.BGEZ_OPCODE: _non_negative,
    Tok.BGEZL_OPCODE: _non_negative,
    Tok.BGEZAL_OPCODE: _non_negative,
    Tok.BGEZALL_OPCODE: _non_negative,
    Tok.BGTZ_OPCODE: _positive,
    Tok.BGTZL_OPCODE: _positive,
    Tok.BLEZ_OPCODE: _non_positive,
    Tok.BLEZL_OPCODE: _non_positive,
    Tok.BLTZ_OPCODE: _negative,
    Tok.BLTZL_OPCODE: _negative,
    Tok.BLTZAL_OPCODE: _negative,
    Tok.BLTZALL_OPCODE: _negative,
}

# The sign branches that link $ra.
_LINKING_SIGN_BRANCHES: frozenset[int] = frozenset(
    {Tok.BGEZAL_OPCODE, Tok.BGEZALL_OPCODE, Tok.BLTZAL_OPCODE, Tok.BLTZALL_OPCODE}
)


@_executes(
    Tok.BGEZ_OPCODE,
    Tok.BGEZL_OPCODE,
    Tok.BGTZ_OPCODE,
    Tok.BGTZL_OPCODE,
    Tok.BLEZ_OPCODE,
    Tok.BLEZL_OPCODE,
    Tok.BLTZ_OPCODE,
    Tok.BLTZL_OPCODE,
    Tok.BGEZAL_OPCODE,
    Tok.BGEZALL_OPCODE,
    Tok.BLTZAL_OPCODE,
    Tok.BLTZALL_OPCODE,
)
def _branch_sign(instruction: Instruction) -> Step:
    """The branches on the sign of rs; the -AL forms link $ra first."""
    source_register, offset = instruction.source_register, _branch_offset(instruction)
    link: bool = instruction.opcode in _LINKING_SIGN_BRANCHES
    nullify: bool = instruction_module.opcode_is_nullified_branch(instruction.opcode)
    test: Callable[[int], bool] = _SIGN_TESTS[instruction.opcode]

    def step() -> None:
        if link:
            # Set before the test, so `bgezal $ra, ...` tests the new $ra.
            general_purpose_registers[31] = _wrap(
                cpu.program_counter + (8 if options.delayed_branches else 4)
            )
        _branch(test(general_purpose_registers[source_register]), offset, nullify)

    return step


@_executes(Tok.BC1F_OPCODE, Tok.BC1FL_OPCODE, Tok.BC1T_OPCODE, Tok.BC1TL_OPCODE)
def _branch_floating_point_condition(instruction: Instruction) -> Step:
    """bc1f/bc1t: branch on FP condition code cc (rt holds cc, nd, tf)."""
    offset: int = _branch_offset(instruction)
    condition_code, nullify, on_true = (
        instruction.target_register >> 2,
        bool((instruction.target_register >> 1) & 1),
        instruction.target_register & 1,
    )

    def step() -> None:
        _branch(
            regs.floating_point_condition_code(condition_code) == on_true,
            offset,
            nullify,
        )

    return step


@_executes(Tok.J_OPCODE, Tok.JAL_OPCODE)
def _jump_absolute(instruction: Instruction) -> Step:
    """j/jal: jump within the current 256 MB region; jal links $ra."""
    target: int = (instruction.target << 2) & M32
    link: bool = instruction.opcode == Tok.JAL_OPCODE

    def step() -> None:
        if link:
            general_purpose_registers[31] = _wrap(
                cpu.program_counter + (8 if options.delayed_branches else 4)
            )
        _jump((cpu.program_counter & 0xF0000000) | target)

    return step


# The hazard-barrier forms (.hb) run as the plain ones: the simulator has no pipeline
# hazards for a barrier to clear.
@_executes(Tok.JR_OPCODE, Tok.JR_HB_OPCODE)
def _jr(instruction: Instruction) -> Step:
    """jr: jump to the address in rs."""
    source_register: int = instruction.source_register

    def step() -> None:
        _jump(general_purpose_registers[source_register] & M32)

    return step


@_executes(Tok.JALR_OPCODE, Tok.JALR_HB_OPCODE)
def _jalr(instruction: Instruction) -> Step:
    """jalr: rd = return address, jump to rs (read before rd is written)."""
    destination_register, source_register = (
        instruction.destination_register,
        instruction.source_register,
    )

    def step() -> None:
        target: int = general_purpose_registers[source_register] & M32
        general_purpose_registers[destination_register] = _wrap(
            cpu.program_counter + (8 if options.delayed_branches else 4)
        )
        _jump(target)

    return step


@_executes(Tok.ERET_OPCODE)
def _eret(instruction: Instruction) -> Step:
    """eret: clear EXL and return to EPC (with a delay slot if delayed)."""

    def step() -> None:
        coprocessor_0: list[int] = cpu.coprocessor_registers[0]
        coprocessor_0[regs.COPROCESSOR_0_STATUS] = _wrap(
            coprocessor_0[regs.COPROCESSOR_0_STATUS] & ~regs.STATUS_EXCEPTION_LEVEL
        )
        _jump(coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER] & M32)

    return step


# --- loads and stores --------------------------------------------------------------------


@_executes(
    Tok.LB_OPCODE,
    Tok.LBU_OPCODE,
    Tok.LH_OPCODE,
    Tok.LHU_OPCODE,
    Tok.LW_OPCODE,
    Tok.LL_OPCODE,
)
def _load_integer(instruction: Instruction) -> Step:
    """The loads; a faulting load still writes 0 to rt."""
    target_register, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    opcode: int = instruction.opcode
    read: Callable[[int], int]
    mask: int = -1
    if opcode in (Tok.LB_OPCODE, Tok.LBU_OPCODE):
        read = memory.read_byte
        mask = 0xFF if opcode == Tok.LBU_OPCODE else -1
    elif opcode in (Tok.LH_OPCODE, Tok.LHU_OPCODE):
        read = memory.read_half
        mask = 0xFFFF if opcode == Tok.LHU_OPCODE else -1
    else:
        read = memory.read_word

    def step() -> None:
        _load(
            general_purpose_registers,
            target_register,
            read((general_purpose_registers[base] + offset) & M32) & mask,
        )

    return step


@_executes(Tok.LWL_OPCODE, Tok.LWR_OPCODE)
def _load_unaligned(instruction: Instruction) -> Step:
    """lwl/lwr: merge part of an aligned word into rt (little-endian)."""
    target_register, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    is_left: bool = instruction.opcode == Tok.LWL_OPCODE

    def step() -> None:
        address: int = (general_purpose_registers[base] + offset) & M32
        byte: int = address & 3
        register_number: int = general_purpose_registers[target_register] & M32
        word: int = memory.read_word(address & 0xFFFFFFFC) & M32
        if not traps.exception_occurred:
            if is_left:
                keep: int = 8 * (3 - byte)  # bits of rt that survive
                word = ((word << keep) & M32) | (register_number & ((1 << keep) - 1))
            else:
                drop: int = 8 * byte
                word = (register_number & ~(M32 >> drop) & M32) | (word >> drop)
        _load(general_purpose_registers, target_register, _wrap(word))

    return step


@_executes(Tok.SB_OPCODE, Tok.SH_OPCODE, Tok.SW_OPCODE, Tok.SC_OPCODE)
def _store_integer(instruction: Instruction) -> Step:
    """The stores (sc is a plain store: one processor, so it always succeeds)."""
    target_register, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    write: Callable[[int, int], None]
    match instruction.opcode:
        case Tok.SB_OPCODE:
            write = memory.write_byte
        case Tok.SH_OPCODE:
            write = memory.write_half
        case Tok.SW_OPCODE | Tok.SC_OPCODE:
            write = memory.write_word
        case _:
            raise AssertionError(
                f"_store_integer: opcode {instruction.opcode} is not a store; only they are"
                " registered"
            )

    def step() -> None:
        write(
            (general_purpose_registers[base] + offset) & M32,
            general_purpose_registers[target_register],
        )

    return step


@_executes(Tok.SWL_OPCODE, Tok.SWR_OPCODE)
def _store_unaligned(instruction: Instruction) -> Step:
    """swl/swr: store part of rt into an aligned word (little-endian)."""
    target_register, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    is_left: bool = instruction.opcode == Tok.SWL_OPCODE

    def step() -> None:
        address: int = (general_purpose_registers[base] + offset) & M32
        byte: int = address & 3
        register_number: int = general_purpose_registers[target_register] & M32
        aligned: int = address & 0xFFFFFFFC
        data: int = memory.read_word(aligned) & M32
        if is_left:
            shift: int = 8 * (3 - byte)
            data = (data & ~(M32 >> shift) & M32) | (register_number >> shift)
        else:
            shift = 8 * byte
            data = ((register_number << shift) & M32) | (data & ((1 << shift) - 1))
        memory.write_word(aligned, _wrap(data))

    return step


# --- traps, break and syscall ---------------------------------------------------------------

# Each trap's comparison, shared by its register and its immediate form.
_TRAP_TESTS: dict[int, Callable[[int, int], bool]] = {}
_trap_test: Callable[[int, int], bool]
for _register_form, _immediate_form, _trap_test in (
    (Tok.TEQ_OPCODE, Tok.TEQI_OPCODE, lambda a, b: a == b),
    (Tok.TNE_OPCODE, Tok.TNEI_OPCODE, lambda a, b: a != b),
    (Tok.TGE_OPCODE, Tok.TGEI_OPCODE, lambda a, b: a >= b),
    (Tok.TLT_OPCODE, Tok.TLTI_OPCODE, lambda a, b: a < b),
    (Tok.TGEU_OPCODE, Tok.TGEIU_OPCODE, lambda a, b: a & M32 >= b & M32),
    (Tok.TLTU_OPCODE, Tok.TLTIU_OPCODE, lambda a, b: a & M32 < b & M32),
):
    _TRAP_TESTS[_register_form] = _trap_test
    _TRAP_TESTS[_immediate_form] = _trap_test


@_executes(
    Tok.TEQ_OPCODE,
    Tok.TNE_OPCODE,
    Tok.TGE_OPCODE,
    Tok.TLT_OPCODE,
    Tok.TGEU_OPCODE,
    Tok.TLTU_OPCODE,
)
def _trap_register(instruction: Instruction) -> Step:
    """Trap if the comparison of rs with rt holds."""
    source_register, target_register = (
        instruction.source_register,
        instruction.target_register,
    )
    test: Callable[[int, int], bool] = _TRAP_TESTS[instruction.opcode]

    def step() -> None:
        if test(
            general_purpose_registers[source_register],
            general_purpose_registers[target_register],
        ):
            _raise(ExceptionCode.TRAP)

    return step


@_executes(
    Tok.TEQI_OPCODE,
    Tok.TNEI_OPCODE,
    Tok.TGEI_OPCODE,
    Tok.TLTI_OPCODE,
    Tok.TGEIU_OPCODE,
    Tok.TLTIU_OPCODE,
)
def _trap_immediate(instruction: Instruction) -> Step:
    """Trap if the comparison of rs with the sign-extended immediate holds."""
    source_register, immediate = instruction.source_register, instruction.immediate
    test: Callable[[int, int], bool] = _TRAP_TESTS[instruction.opcode]

    def step() -> None:
        if test(general_purpose_registers[source_register], immediate):
            _raise(ExceptionCode.TRAP)

    return step


@_executes(Tok.BREAK_OPCODE)
def _break(instruction: Instruction) -> Step:
    """break: a Bp exception; ``break 1`` is a debugger breakpoint (stop)."""
    is_debugger: bool = instruction.destination_register == 1

    def step() -> bool | None:
        _raise(ExceptionCode.BREAKPOINT)
        return True if is_debugger else None

    return step


@_executes(Tok.SYSCALL_OPCODE)
def _syscall(instruction: Instruction) -> Step:
    """syscall: the service number is in $v0 (syscall.py)."""

    def step() -> bool | None:
        return None if syscall.do_syscall() else False

    return step


# --- coprocessor 0 ----------------------------------------------------------------------------


@_executes(Tok.MFC0_OPCODE)
def _mfc0(instruction: Instruction) -> Step:
    """mfc0: rt = a CP0 register (its number is in the rd byte)."""
    target_register, number = (
        instruction.target_register,
        instruction.destination_register,
    )

    def step() -> None:
        general_purpose_registers[target_register] = cpu.coprocessor_registers[0][
            number
        ]

    return step


@_executes(Tok.MTC0_OPCODE)
def _mtc0(instruction: Instruction) -> Step:
    """mtc0: a CP0 register = rt, keeping only its writable bits."""
    target_register, number = (
        instruction.target_register,
        instruction.destination_register,
    )

    def step() -> None:
        coprocessor_0: list[int] = cpu.coprocessor_registers[0]
        coprocessor_0[number] = general_purpose_registers[target_register]
        match number:
            case regs.COPROCESSOR_0_COMPARE:
                coprocessor_0[regs.COPROCESSOR_0_CAUSE] = _wrap(
                    coprocessor_0[regs.COPROCESSOR_0_CAUSE]
                    & ~regs.CAUSE_INTERRUPT_PENDING_7
                )
            case regs.COPROCESSOR_0_STATUS:
                coprocessor_0[number] = _wrap(
                    (coprocessor_0[number] & regs.STATUS_MASK)
                    | (regs.STATUS_COPROCESSOR_USABLE & 0x30000000)
                    | regs.STATUS_USER_MODE
                )
            case regs.COPROCESSOR_0_CAUSE:
                coprocessor_0[number] = _wrap(coprocessor_0[number] & regs.CAUSE_MASK)
            case regs.COPROCESSOR_0_CONFIG:
                coprocessor_0[number] = _wrap(coprocessor_0[number] & regs.CONFIG_MASK)
            case _:
                pass  # other CP0 registers keep the whole value written

    return step


@_executes(Tok.CFC0_OPCODE, Tok.CTC0_OPCODE)
def _coprocessor0_control(instruction: Instruction) -> Step:
    """cfc0/ctc0: move from/to a CP0 control register."""
    target_register, number = (
        instruction.target_register,
        instruction.destination_register,
    )
    if instruction.opcode == Tok.CFC0_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = (
                cpu.coprocessor_control_registers[0][number]
            )

    else:

        def step() -> None:
            cpu.coprocessor_control_registers[0][number] = general_purpose_registers[
                target_register
            ]

    return step


# --- the floating-point unit ---------------------------------------------------------------


def _floating_point_binary(
    operation: Callable[[float, float], float], double: bool
) -> Callable[[Instruction], Step]:
    """The factory of add/sub/mul/div.s or .d: fd = fs op ft."""

    def factory(instruction: Instruction) -> Step:
        floating_point_destination, floating_point_source, floating_point_target = (
            instruction.shift_amount,
            instruction.destination_register,
            instruction.target_register,
        )
        if double:

            def step() -> None:
                if floating_point_destination & 1:
                    raise console.run_error("Odd FP double register number\n")
                regs.set_floating_point_register_double(
                    floating_point_destination,
                    operation(
                        regs.floating_point_register_double(floating_point_source),
                        regs.floating_point_register_double(floating_point_target),
                    ),
                )

        else:

            def step() -> None:
                regs.set_floating_point_register_single(
                    floating_point_destination,
                    operation(
                        regs.floating_point_register_single(floating_point_source),
                        regs.floating_point_register_single(floating_point_target),
                    ),
                )

        return step

    return factory


for _add_token, _subtract_token, _multiply_token, _divide_token, _double in (
    (Tok.ADD_S_OPCODE, Tok.SUB_S_OPCODE, Tok.MUL_S_OPCODE, Tok.DIV_S_OPCODE, False),
    (Tok.ADD_D_OPCODE, Tok.SUB_D_OPCODE, Tok.MUL_D_OPCODE, Tok.DIV_D_OPCODE, True),
):
    _FACTORIES[_add_token] = _floating_point_binary(lambda a, b: a + b, _double)
    _FACTORIES[_subtract_token] = _floating_point_binary(lambda a, b: a - b, _double)
    _FACTORIES[_multiply_token] = _floating_point_binary(lambda a, b: a * b, _double)
    _FACTORIES[_divide_token] = _floating_point_binary(machine.f_divide, _double)


def _floating_point_unary(
    operation: Callable[[float], float], double: bool
) -> Callable[[Instruction], Step]:
    """The factory of abs/neg/sqrt.s or .d: fd = op(fs)."""

    def factory(instruction: Instruction) -> Step:
        floating_point_destination, floating_point_source = (
            instruction.shift_amount,
            instruction.destination_register,
        )
        if double:

            def step() -> None:
                if floating_point_destination & 1:
                    raise console.run_error("Odd FP double register number\n")
                regs.set_floating_point_register_double(
                    floating_point_destination,
                    operation(
                        regs.floating_point_register_double(floating_point_source)
                    ),
                )

        else:

            def step() -> None:
                regs.set_floating_point_register_single(
                    floating_point_destination,
                    operation(
                        regs.floating_point_register_single(floating_point_source)
                    ),
                )

        return step

    return factory


for _absolute_token, _neg_token, _sqrt_token, _double in (
    (Tok.ABS_S_OPCODE, Tok.NEG_S_OPCODE, Tok.SQRT_S_OPCODE, False),
    (Tok.ABS_D_OPCODE, Tok.NEG_D_OPCODE, Tok.SQRT_D_OPCODE, True),
):
    _FACTORIES[_absolute_token] = _floating_point_unary(math.fabs, _double)
    _FACTORIES[_neg_token] = _floating_point_unary(lambda x: -x, _double)
    _FACTORIES[_sqrt_token] = _floating_point_unary(machine.f_sqrt, _double)


def _rounded(rounding: Callable[[float], float]) -> Callable[[float], float]:
    """rounding (ceil, floor) applied only to finite values, as C's does."""
    return lambda x: float(rounding(x)) if math.isfinite(x) else x


# Each conversion to a word: how it adjusts the value before truncating, and
# whether it reads a double (the .d form) or a single (the .s form).
_TO_WORD: dict[int, tuple[Callable[[float], float], Callable[[int], float]]] = {}
_to_word_adjust: Callable[[float], float]
for _double_form, _single_form, _to_word_adjust in (
    (Tok.CEIL_W_D_OPCODE, Tok.CEIL_W_S_OPCODE, _rounded(math.ceil)),
    (Tok.FLOOR_W_D_OPCODE, Tok.FLOOR_W_S_OPCODE, _rounded(math.floor)),
    # then truncated: spim's own rounding
    (Tok.ROUND_W_D_OPCODE, Tok.ROUND_W_S_OPCODE, lambda x: x + 0.5),
    (Tok.TRUNC_W_D_OPCODE, Tok.TRUNC_W_S_OPCODE, lambda x: x),
    (Tok.CVT_W_D_OPCODE, Tok.CVT_W_S_OPCODE, lambda x: x),
):
    _TO_WORD[_double_form] = (_to_word_adjust, regs.floating_point_register_double)
    _TO_WORD[_single_form] = (_to_word_adjust, regs.floating_point_register_single)


@_executes(
    Tok.CEIL_W_D_OPCODE,
    Tok.CEIL_W_S_OPCODE,
    Tok.FLOOR_W_D_OPCODE,
    Tok.FLOOR_W_S_OPCODE,
    Tok.ROUND_W_D_OPCODE,
    Tok.ROUND_W_S_OPCODE,
    Tok.TRUNC_W_D_OPCODE,
    Tok.TRUNC_W_S_OPCODE,
    Tok.CVT_W_D_OPCODE,
    Tok.CVT_W_S_OPCODE,
)
def _floating_point_to_word(instruction: Instruction) -> Step:
    """Convert fs to a 32-bit integer in fd (x86's INT_MIN when out of range)."""
    floating_point_destination, floating_point_source = (
        instruction.shift_amount,
        instruction.destination_register,
    )
    adjust: Callable[[float], float]
    read: Callable[[int], float]
    adjust, read = _TO_WORD[instruction.opcode]

    def step() -> None:
        floating_point_register[floating_point_destination] = machine.to_s32(
            adjust(read(floating_point_source))
        )

    return step


@_executes(
    Tok.CVT_D_S_OPCODE, Tok.CVT_D_W_OPCODE, Tok.CVT_S_D_OPCODE, Tok.CVT_S_W_OPCODE
)
def _floating_point_convert(instruction: Instruction) -> Step:
    """cvt between single, double and word."""
    floating_point_destination, floating_point_source = (
        instruction.shift_amount,
        instruction.destination_register,
    )
    opcode: int = instruction.opcode
    match opcode:
        case Tok.CVT_D_S_OPCODE:

            def step() -> None:
                regs.set_floating_point_register_double(
                    floating_point_destination,
                    regs.floating_point_register_single(floating_point_source),
                )
        case Tok.CVT_D_W_OPCODE:

            def step() -> None:
                regs.set_floating_point_register_double(
                    floating_point_destination,
                    float(floating_point_register[floating_point_source]),
                )
        case Tok.CVT_S_D_OPCODE:

            def step() -> None:
                regs.set_floating_point_register_single(
                    floating_point_destination,
                    regs.floating_point_register_double(floating_point_source),
                )
        case Tok.CVT_S_W_OPCODE:

            def step() -> None:
                regs.set_floating_point_register_single(
                    floating_point_destination,
                    float(floating_point_register[floating_point_source]),
                )
        case _:
            raise AssertionError(
                f"_fp_convert: opcode {opcode} is not a cvt; only they are registered"
            )

    return step


def _floating_point_compare(double: bool) -> Callable[[Instruction], Step]:
    """The factory of c.cond.s/.d: set FP condition code cc."""

    def factory(instruction: Instruction) -> Step:
        floating_point_source, floating_point_target, condition, condition_code = (
            instruction.destination_register,
            instruction.target_register,
            instruction.source_register,
            instruction.shift_amount >> 2,
        )
        read: Callable[[int], float] = (
            regs.floating_point_register_double
            if double
            else regs.floating_point_register_single
        )

        def step() -> None:
            a: float = read(floating_point_source)
            b: float = read(floating_point_target)
            if math.isnan(a) or math.isnan(b):
                if condition & instruction_module.CONDITION_INVALID:
                    _raise(ExceptionCode.FLOATING_POINT_EXCEPTION)
                    return
                regs.set_floating_point_condition_code(
                    condition_code, condition & instruction_module.CONDITION_UNORDERED
                )
            else:
                result: int = 0
                if condition & instruction_module.CONDITION_LESS_THAN and a < b:
                    result = 1
                if condition & instruction_module.CONDITION_EQUAL and a == b:
                    result = 1
                regs.set_floating_point_condition_code(condition_code, result)

        return step

    return factory


# The FP compares are the table's c.cond.fmt rows; the fmt field (bits 21-25)
# is 0x11 for a double, 0x10 for a single.
for _row in opcodes.ROWS:
    if _row.kind == OpType.FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
        _FACTORIES[_row.token] = _floating_point_compare(
            (_row.encoding >> 21) & 0x1F == 0x11
        )


@_executes(
    Tok.MOV_S_OPCODE,
    Tok.MOVF_S_OPCODE,
    Tok.MOVT_S_OPCODE,
    Tok.MOVN_S_OPCODE,
    Tok.MOVZ_S_OPCODE,
    Tok.MOV_D_OPCODE,
    Tok.MOVF_D_OPCODE,
    Tok.MOVT_D_OPCODE,
    Tok.MOVN_D_OPCODE,
    Tok.MOVZ_D_OPCODE,
)
def _floating_point_move(instruction: Instruction) -> Step:
    """mov.fmt, and its conditional forms (on a cc, or on a register)."""
    floating_point_destination, floating_point_source, target_register = (
        instruction.shift_amount,
        instruction.destination_register,
        instruction.target_register,
    )
    opcode: int = instruction.opcode
    double: bool = opcode in (
        Tok.MOV_D_OPCODE, Tok.MOVF_D_OPCODE, Tok.MOVT_D_OPCODE, Tok.MOVN_D_OPCODE,
        Tok.MOVZ_D_OPCODE,
    )  # fmt: skip

    def condition() -> bool:
        """Whether this move happens."""
        match opcode:
            case Tok.MOVF_S_OPCODE | Tok.MOVF_D_OPCODE:
                return regs.floating_point_condition_code(target_register >> 2) == 0
            case Tok.MOVT_S_OPCODE | Tok.MOVT_D_OPCODE:
                return regs.floating_point_condition_code(target_register >> 2) != 0
            case Tok.MOVN_S_OPCODE | Tok.MOVN_D_OPCODE:
                return general_purpose_registers[target_register] != 0
            case Tok.MOVZ_S_OPCODE | Tok.MOVZ_D_OPCODE:
                return general_purpose_registers[target_register] == 0
            case Tok.MOV_S_OPCODE | Tok.MOV_D_OPCODE:
                return True
            case _:
                raise AssertionError(
                    f"_fp_move: opcode {opcode} is not an FP move; only they are"
                    " registered"
                )

    def step() -> None:
        if condition():
            if double:
                regs.set_floating_point_register_double(
                    floating_point_destination,
                    regs.floating_point_register_double(floating_point_source),
                )
            else:
                floating_point_register[floating_point_destination] = (
                    floating_point_register[floating_point_source]
                )  # a single moves as raw bits

    return step


@_executes(Tok.MOVF_OPCODE, Tok.MOVT_OPCODE)
def _move_on_floating_point_condition(instruction: Instruction) -> Step:
    """movf/movt: rd = rs if FP condition code cc is false/true."""
    destination_register, source_register, condition_code = (
        instruction.destination_register,
        instruction.source_register,
        instruction.target_register >> 2,
    )
    wanted: int = 0 if instruction.opcode == Tok.MOVF_OPCODE else 1

    def step() -> None:
        if regs.floating_point_condition_code(condition_code) == wanted:
            general_purpose_registers[destination_register] = general_purpose_registers[
                source_register
            ]

    return step


@_executes(Tok.MFC1_OPCODE, Tok.MTC1_OPCODE)
def _move_floating_point_word(instruction: Instruction) -> Step:
    """mfc1/mtc1: copy a word's bits between rt and FP register fs."""
    target_register, floating_point_source = (
        instruction.target_register,
        instruction.destination_register,
    )
    if instruction.opcode == Tok.MFC1_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = floating_point_register[
                floating_point_source
            ]

    else:

        def step() -> None:
            floating_point_register[floating_point_source] = general_purpose_registers[
                target_register
            ]

    return step


@_executes(Tok.CFC1_OPCODE, Tok.CTC1_OPCODE)
def _floating_point_control(instruction: Instruction) -> Step:
    """cfc1/ctc1: move from/to an FPU control register (FIR, FCSR)."""
    target_register, number = (
        instruction.target_register,
        instruction.destination_register,
    )
    if instruction.opcode == Tok.CFC1_OPCODE:

        def step() -> None:
            general_purpose_registers[target_register] = cpu.coprocessor_registers[1][
                number
            ]

    else:

        def step() -> None:
            cpu.coprocessor_registers[1][number] = general_purpose_registers[
                target_register
            ]
            if (
                number == regs.FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER
                and general_purpose_registers[target_register]
                & M32
                & ~regs.FLOATING_POINT_CONTROL_STATUS_MASK
            ):
                _raise(ExceptionCode.FLOATING_POINT_EXCEPTION)  # an unsupported mode

    return step


@_executes(Tok.LWC1_OPCODE, Tok.LDC1_OPCODE)
def _load_floating_point(instruction: Instruction) -> Step:
    """lwc1/ldc1: load one or two words into FP registers."""
    floating_point_target, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    double: bool = instruction.opcode == Tok.LDC1_OPCODE

    def step() -> None:
        address: int = (general_purpose_registers[base] + offset) & M32
        if double:
            if address & 3:
                memory._bad_address(ExceptionCode.ADDRESS_ERROR_LOAD, address)
            _load(
                floating_point_register,
                floating_point_target,
                memory.read_word(address),
            )
            _load(
                floating_point_register,
                floating_point_target + 1,
                memory.read_word((address + 4) & M32),
            )
        else:
            _load(
                floating_point_register,
                floating_point_target,
                memory.read_word(address),
            )

    return step


@_executes(Tok.SWC1_OPCODE, Tok.SDC1_OPCODE)
def _store_floating_point(instruction: Instruction) -> Step:
    """swc1/sdc1: store FP register(s) as one or two words."""
    floating_point_target, base, offset = (
        instruction.target_register,
        instruction.source_register,
        instruction.immediate,
    )
    double: bool = instruction.opcode == Tok.SDC1_OPCODE

    def step() -> None:
        if double:
            low: int
            high: int
            low, high = machine.double_words(
                regs.floating_point_register_double(floating_point_target)
            )
            address: int = (general_purpose_registers[base] + offset) & M32
            if address & 3:
                memory._bad_address(ExceptionCode.ADDRESS_ERROR_LOAD, address)
            memory.write_word(address, low)
            memory.write_word((address + 4) & M32, high)
        else:
            memory.write_word(
                (general_purpose_registers[base] + offset) & M32,
                floating_point_register[floating_point_target],
            )

    return step
