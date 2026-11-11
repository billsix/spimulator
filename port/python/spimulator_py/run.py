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

from . import console, instruction, machine, memory, opcodes, options, syscall, traps
from . import memory_layout as layout
from . import registers as regs
from .inst import ImmExpr, Instruction
from .machine import M32
from .opcode_table import OpType, Tok
from .registers import cpu
from .traps import ExcCode

type Step = Callable[[], bool | None]
"""An instruction's compiled form: run it once."""

SIGN: int = 0x80000000

gpr: list[int] = cpu.gpr
fpr: list[int] = cpu.fpr

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


def _start_cp0_timer() -> None:
    """Start the 10 ms timer unless it is already running."""
    global _timer_deadline
    now: float = time.monotonic()
    if _timer_deadline is None or now >= _timer_deadline:
        _timer_deadline = now + layout.TIMER_TICK_MS / 1000


def _poll_cp0_timer() -> None:
    """Count one tick if the timer expired, raising interrupt 7 on Compare."""
    global _timer_deadline
    now: float = time.monotonic()
    if _timer_deadline is not None and now >= _timer_deadline:
        cp0: list[int] = cpu.cpr[0]
        cp0[regs.CP0_COUNT] = _wrap(cp0[regs.CP0_COUNT] + 1)
        if cp0[regs.CP0_COUNT] == cp0[regs.CP0_COMPARE]:
            memory._raise_interrupt(7)
        _timer_deadline = now + layout.TIMER_TICK_MS / 1000


# --- the main loop ------------------------------------------------------------------


def run_spim(initial_pc: int, steps_to_run: int, display: bool) -> bool:
    """Run from initial_pc for up to steps_to_run instructions.

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
        cpu.pc = initial_pc
        mapped: bool = not options.bare_machine and options.mapped_io
        chunk: int = layout.IO_INTERVAL if mapped else steps_to_run
        _start_cp0_timer()
        delayed_loads: bool = options.delayed_loads
        text_seg: list[Instruction | None] = memory.text_seg
        text_top: int = memory.text_top
        text_bot: int = layout.TEXT_BOT
        poll: int = 0
        while steps_to_run > 0:
            step_size: int = min(chunk, steps_to_run)
            steps_to_run -= step_size
            if mapped:
                memory.check_memory_mapped_io()
            cp0: list[int] = cpu.cpr[0]
            status: int = cp0[regs.CP0_STATUS]
            if (
                status & regs.STATUS_IE
                and not status & regs.STATUS_EXL
                and cp0[regs.CP0_CAUSE] & regs.CAUSE_IP & status & regs.STATUS_IM
            ):
                # Take the interrupt before the next instruction, so EPC
                # points at the instruction to return to.
                traps.raise_exception(ExcCode.INT)
                syscall.handle_exception()
            for _ in range(step_size):
                gpr[0] = 0  # $0 is always zero
                poll -= 1
                if poll < 0:
                    poll = 63
                    _poll_cp0_timer()
                pc: int = cpu.pc
                inst: Instruction | None
                if text_bot <= pc < text_top and not pc & 3:
                    inst = text_seg[(pc - text_bot) >> 2]
                else:
                    traps.exception_occurred = False
                    inst = memory.read_inst(pc)
                    if traps.exception_occurred:  # in fetching the instruction
                        traps.exception_occurred = False
                        syscall.handle_exception()
                        continue
                if inst is None:
                    raise console.run_error(
                        f"Attempt to execute non-instruction at 0x{pc:08x}\n"
                    )
                expr: ImmExpr | None = inst.expr
                if (
                    expr is not None
                    and expr.symbol is not None
                    and expr.symbol.addr == 0
                ):
                    raise console.run_error(
                        f"Instruction references undefined symbol at 0x{pc:08x}\n"
                        f"  {instruction.inst_to_string(pc)}"
                    )
                if delayed_loads:
                    if _delayed_load_2 is not None:
                        registers, number, value = _delayed_load_2
                        registers[number] = value
                    _delayed_load_2 = _delayed_load_1
                    _delayed_load_1 = None
                step: Step | None = inst.exec_cache
                if step is None:
                    step = _compile(inst)
                    inst.exec_cache = step
                traps.exception_occurred = False
                stop: bool | None = step()
                if stop is not None:
                    return stop
                cpu.pc = (cpu.pc + 4) & M32
                if display:
                    instruction.print_inst(cpu.pc)
                if traps.exception_occurred:
                    syscall.handle_exception()
        return True
    finally:
        _display = saved_display


def _compile(inst: Instruction) -> Step:
    """The step function of inst (decoded once, then cached on it).

    Raises:
        SystemExit: the opcode cannot be executed (the C engine's fatal
            "Unknown instruction type").
    """
    factory: Callable[[Instruction], Step] | None = _FACTORIES.get(inst.opcode)
    if factory is None:
        opcode: int = machine.s16(inst.opcode)

        def unknown() -> None:
            raise console.fatal_error(f"Unknown instruction type: {opcode}\n")

        return unknown
    return factory(inst)


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


def _raise(code: ExcCode) -> None:
    """Raise a MIPS exception; the main loop handles it after this step."""
    traps.raise_exception(code)


# --- branches and loads ---------------------------------------------------------------


def _jump(target: int) -> None:
    """Jump to target, first running the delay-slot instruction if delayed."""
    if options.delayed_branches:
        traps.running_in_delay_slot = True
        run_spim((cpu.pc + 4) & M32, 1, _display)
        traps.running_in_delay_slot = False
    cpu.pc = (target - 4) & M32  # the main loop adds 4


def _branch(taken: bool, offset: int, nullify: bool) -> None:
    """A conditional branch of offset bytes from its own address.

    A branch-likely (nullify) that is not taken skips its delay slot.
    """
    if taken:
        target: int = cpu.pc + offset
        if options.delayed_branches:
            target += 4
        _jump(target)
    elif nullify:
        cpu.pc = (cpu.pc + 4) & M32
    else:
        pass  # not taken and not nullified: the delay slot runs as the next instruction


def _load(registers: list[int], number: int, value: int) -> None:
    """Write a loaded value now, or one instruction later with delayed loads."""
    global _delayed_load_1
    if options.delayed_loads:
        _delayed_load_1 = (registers, number, value)
    else:
        registers[number] = value


def _coprocessor_unusable(inst: Instruction) -> Step:
    """Coprocessor 2 does not exist: raise CpU."""

    def step() -> None:
        _raise(ExcCode.CPU)

    return step


for _token in (
    Tok.BC2F_OPCODE, Tok.BC2FL_OPCODE, Tok.BC2T_OPCODE, Tok.BC2TL_OPCODE,
    Tok.CFC2_OPCODE, Tok.COP2_OPCODE, Tok.CTC2_OPCODE, Tok.LDC2_OPCODE,
    Tok.LWC2_OPCODE, Tok.MFC2_OPCODE, Tok.MTC2_OPCODE, Tok.SDC2_OPCODE,
    Tok.SWC2_OPCODE,
):  # fmt: skip
    _FACTORIES[_token] = _coprocessor_unusable


def _reserved(inst: Instruction) -> Step:
    """Not a MIPS32 instruction (rfe) or the TLB, which is not simulated: RI."""

    def step() -> None:
        _raise(ExcCode.RI)

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
def _no_operation(inst: Instruction) -> Step:
    """Cache and memory-ordering details are not simulated."""

    def step() -> None:
        return None

    return step


# --- integer arithmetic and logic ----------------------------------------------------


@_executes(Tok.ADD_OPCODE)
def _add(inst: Instruction) -> Step:
    """add: rd = rs + rt, trapping on signed overflow."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt

    def step() -> None:
        total: int = gpr[rs] + gpr[rt]
        if not -SIGN <= total < SIGN:
            _raise(ExcCode.OV)
        else:
            gpr[rd] = total

    return step


@_executes(Tok.ADDI_OPCODE)
def _addi(inst: Instruction) -> Step:
    """addi: rt = rs + imm, trapping on signed overflow."""
    rt, rs, imm = inst.rt, inst.rs, inst.imm

    def step() -> None:
        total: int = gpr[rs] + imm
        if not -SIGN <= total < SIGN:
            _raise(ExcCode.OV)
        else:
            gpr[rt] = total

    return step


@_executes(Tok.ADDIU_OPCODE)
def _addiu(inst: Instruction) -> Step:
    """addiu: rt = rs + imm, wrapping (no trap, despite the "u")."""
    rt, rs, imm = inst.rt, inst.rs, inst.imm

    def step() -> None:
        gpr[rt] = ((gpr[rs] + imm + SIGN) & M32) - SIGN

    return step


@_executes(Tok.ADDU_OPCODE)
def _addu(inst: Instruction) -> Step:
    """addu: rd = rs + rt, wrapping."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt

    def step() -> None:
        gpr[rd] = ((gpr[rs] + gpr[rt] + SIGN) & M32) - SIGN

    return step


@_executes(Tok.SUB_OPCODE)
def _sub(inst: Instruction) -> Step:
    """sub: rd = rs - rt, trapping on signed overflow."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt

    def step() -> None:
        difference: int = gpr[rs] - gpr[rt]
        if not -SIGN <= difference < SIGN:
            _raise(ExcCode.OV)
        else:
            gpr[rd] = difference

    return step


@_executes(Tok.SUBU_OPCODE)
def _subu(inst: Instruction) -> Step:
    """subu: rd = rs - rt, wrapping."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt

    def step() -> None:
        gpr[rd] = ((gpr[rs] - gpr[rt] + SIGN) & M32) - SIGN

    return step


def _logic(operation: Callable[[int, int], int]) -> Callable[[Instruction], Step]:
    """The factory of a three-register logical instruction."""

    def factory(inst: Instruction) -> Step:
        rd, rs, rt = inst.rd, inst.rs, inst.rt

        def step() -> None:
            gpr[rd] = operation(gpr[rs], gpr[rt])

        return step

    return factory


_FACTORIES[Tok.AND_OPCODE] = _logic(lambda a, b: a & b)
_FACTORIES[Tok.OR_OPCODE] = _logic(lambda a, b: a | b)
_FACTORIES[Tok.XOR_OPCODE] = _logic(lambda a, b: a ^ b)
_FACTORIES[Tok.NOR_OPCODE] = _logic(lambda a, b: ~(a | b))
_FACTORIES[Tok.SLT_OPCODE] = _logic(lambda a, b: 1 if a < b else 0)
_FACTORIES[Tok.SLTU_OPCODE] = _logic(lambda a, b: 1 if a & M32 < b & M32 else 0)


@_executes(Tok.ANDI_OPCODE, Tok.ORI_OPCODE, Tok.XORI_OPCODE)
def _logic_immediate(inst: Instruction) -> Step:
    """andi/ori/xori: the immediate is zero-extended."""
    rt, rs, imm = inst.rt, inst.rs, inst.imm & 0xFFFF
    opcode: int = inst.opcode

    if opcode == Tok.ANDI_OPCODE:

        def step() -> None:
            gpr[rt] = gpr[rs] & imm

    elif opcode == Tok.ORI_OPCODE:

        def step() -> None:
            gpr[rt] = gpr[rs] | imm

    elif opcode == Tok.XORI_OPCODE:

        def step() -> None:
            gpr[rt] = gpr[rs] ^ imm

    else:
        raise AssertionError(
            f"_logic_immediate: opcode {opcode} is not andi/ori/xori; only they are"
            " registered"
        )

    return step


@_executes(Tok.SLTI_OPCODE)
def _slti(inst: Instruction) -> Step:
    """slti: rt = 1 if rs < imm (signed)."""
    rt, rs, imm = inst.rt, inst.rs, inst.imm

    def step() -> None:
        gpr[rt] = 1 if gpr[rs] < imm else 0

    return step


@_executes(Tok.SLTIU_OPCODE)
def _sltiu(inst: Instruction) -> Step:
    """sltiu: the immediate is sign-extended, then compared unsigned."""
    rt, rs, imm = inst.rt, inst.rs, inst.imm & M32

    def step() -> None:
        gpr[rt] = 1 if gpr[rs] & M32 < imm else 0

    return step


@_executes(Tok.LUI_OPCODE)
def _lui(inst: Instruction) -> Step:
    """lui: rt = imm << 16."""
    rt, value = inst.rt, machine.s32(inst.imm << 16)

    def step() -> None:
        gpr[rt] = value

    return step


@_executes(Tok.CLO_OPCODE, Tok.CLZ_OPCODE)
def _count_leading(inst: Instruction) -> Step:
    """clo/clz: count the leading ones/zeros of rs."""
    rd, rs = inst.rd, inst.rs
    count: Callable[[int], int] = (
        machine.leading_ones if inst.opcode == Tok.CLO_OPCODE else machine.leading_zeros
    )

    def step() -> None:
        gpr[rd] = count(gpr[rs])

    return step


@_executes(Tok.MOVN_OPCODE, Tok.MOVZ_OPCODE)
def _move_conditional(inst: Instruction) -> Step:
    """movn/movz: rd = rs if rt is nonzero/zero."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt
    if inst.opcode == Tok.MOVN_OPCODE:

        def step() -> None:
            if gpr[rt] != 0:
                gpr[rd] = gpr[rs]

    else:

        def step() -> None:
            if gpr[rt] == 0:
                gpr[rd] = gpr[rs]

    return step


# --- shifts ----------------------------------------------------------------------------


@_executes(Tok.SLL_OPCODE, Tok.SRL_OPCODE, Tok.SRA_OPCODE)
def _shift(inst: Instruction) -> Step:
    """sll/srl/sra by the constant shamt (srl is logical, sra arithmetic)."""
    rd, rt, shamt = inst.rd, inst.rt, inst.shamt
    opcode: int = inst.opcode
    if shamt >= 32:  # cannot be encoded; the C engine copies rt

        def step() -> None:
            gpr[rd] = gpr[rt]

    elif opcode == Tok.SLL_OPCODE:

        def step() -> None:
            gpr[rd] = (((gpr[rt] << shamt) + SIGN) & M32) - SIGN

    elif opcode == Tok.SRL_OPCODE:

        def step() -> None:
            gpr[rd] = (((gpr[rt] & M32) >> shamt) + SIGN & M32) - SIGN

    else:

        def step() -> None:
            gpr[rd] = gpr[rt] >> shamt  # Python's >> is arithmetic

    return step


@_executes(Tok.SLLV_OPCODE, Tok.SRLV_OPCODE, Tok.SRAV_OPCODE)
def _shift_variable(inst: Instruction) -> Step:
    """sllv/srlv/srav by the low 5 bits of rs."""
    rd, rt, rs = inst.rd, inst.rt, inst.rs
    opcode: int = inst.opcode
    if opcode == Tok.SLLV_OPCODE:

        def step() -> None:
            gpr[rd] = (((gpr[rt] << (gpr[rs] & 31)) + SIGN) & M32) - SIGN

    elif opcode == Tok.SRLV_OPCODE:

        def step() -> None:
            gpr[rd] = (((gpr[rt] & M32) >> (gpr[rs] & 31)) + SIGN & M32) - SIGN

    else:

        def step() -> None:
            gpr[rd] = gpr[rt] >> (gpr[rs] & 31)

    return step


# --- multiply and divide ----------------------------------------------------------------


def _set_hi_lo(product: int) -> None:
    """Split a 64-bit product (any sign) into HI and LO."""
    cpu.lo = _wrap(product)
    cpu.hi = _wrap(product >> 32)


@_executes(Tok.MULT_OPCODE, Tok.MULTU_OPCODE, Tok.MUL_OPCODE)
def _multiply(inst: Instruction) -> Step:
    """mult/multu: HI:LO = rs * rt; mul also puts LO in rd (and clobbers HI)."""
    rd, rs, rt = inst.rd, inst.rs, inst.rt
    opcode: int = inst.opcode
    if opcode == Tok.MULTU_OPCODE:

        def step() -> None:
            _set_hi_lo((gpr[rs] & M32) * (gpr[rt] & M32))

    elif opcode == Tok.MULT_OPCODE:

        def step() -> None:
            _set_hi_lo(gpr[rs] * gpr[rt])

    else:

        def step() -> None:
            _set_hi_lo(gpr[rs] * gpr[rt])
            gpr[rd] = cpu.lo

    return step


@_executes(Tok.MADD_OPCODE, Tok.MADDU_OPCODE, Tok.MSUB_OPCODE, Tok.MSUBU_OPCODE)
def _multiply_accumulate(inst: Instruction) -> Step:
    """madd/msub(u): HI:LO += or -= rs * rt, as 64-bit arithmetic."""
    rs, rt = inst.rs, inst.rt
    opcode: int = inst.opcode
    is_unsigned: bool = opcode in (Tok.MADDU_OPCODE, Tok.MSUBU_OPCODE)
    sign: int = 1 if opcode in (Tok.MADD_OPCODE, Tok.MADDU_OPCODE) else -1

    def step() -> None:
        a: int = gpr[rs] & M32 if is_unsigned else gpr[rs]
        b: int = gpr[rt] & M32 if is_unsigned else gpr[rt]
        accumulator: int = ((cpu.hi & M32) << 32) | (cpu.lo & M32)
        _set_hi_lo(accumulator + sign * a * b)

    return step


@_executes(Tok.DIV_OPCODE)
def _div(inst: Instruction) -> Step:
    """div: LO = rs / rt, HI = rs % rt, truncating toward zero.

    Dividing by zero or INT_MIN by -1 leaves HI and LO unchanged.
    """
    rs, rt = inst.rs, inst.rt

    def step() -> None:
        divisor: int = gpr[rt]
        dividend: int = gpr[rs]
        if divisor != 0 and not (dividend == -SIGN and divisor == -1):
            cpu.lo = machine.c_div(dividend, divisor)
            cpu.hi = machine.c_rem(dividend, divisor)

    return step


@_executes(Tok.DIVU_OPCODE)
def _divu(inst: Instruction) -> Step:
    """divu: unsigned division; by zero leaves HI and LO unchanged."""
    rs, rt = inst.rs, inst.rt

    def step() -> None:
        divisor: int = gpr[rt] & M32
        if divisor != 0:
            dividend: int = gpr[rs] & M32
            cpu.lo = _wrap(dividend // divisor)
            cpu.hi = _wrap(dividend % divisor)

    return step


@_executes(Tok.MFHI_OPCODE, Tok.MFLO_OPCODE)
def _move_from_hi_lo(inst: Instruction) -> Step:
    """mfhi/mflo: rd = HI or LO."""
    rd: int = inst.rd
    if inst.opcode == Tok.MFHI_OPCODE:

        def step() -> None:
            gpr[rd] = cpu.hi

    else:

        def step() -> None:
            gpr[rd] = cpu.lo

    return step


@_executes(Tok.MTHI_OPCODE, Tok.MTLO_OPCODE)
def _move_to_hi_lo(inst: Instruction) -> Step:
    """mthi/mtlo: HI or LO = rs."""
    rs: int = inst.rs
    if inst.opcode == Tok.MTHI_OPCODE:

        def step() -> None:
            cpu.hi = gpr[rs]

    else:

        def step() -> None:
            cpu.lo = gpr[rs]

    return step


# --- branches and jumps -----------------------------------------------------------------


def _branch_offset(inst: Instruction) -> int:
    """The branch's displacement in bytes (the 16-bit field times 4)."""
    return inst.imm << 2


@_executes(Tok.BEQ_OPCODE, Tok.BEQL_OPCODE, Tok.BNE_OPCODE, Tok.BNEL_OPCODE)
def _branch_compare(inst: Instruction) -> Step:
    """beq/bne (and their branch-likely forms): compare rs with rt."""
    rs, rt, offset = inst.rs, inst.rt, _branch_offset(inst)
    opcode: int = inst.opcode
    nullify: bool = opcode in (Tok.BEQL_OPCODE, Tok.BNEL_OPCODE)
    if opcode in (Tok.BEQ_OPCODE, Tok.BEQL_OPCODE):

        def step() -> None:
            _branch(gpr[rs] == gpr[rt], offset, nullify)

    else:

        def step() -> None:
            _branch(gpr[rs] != gpr[rt], offset, nullify)

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
def _branch_sign(inst: Instruction) -> Step:
    """The branches on the sign of rs; the -AL forms link $ra first."""
    rs, offset = inst.rs, _branch_offset(inst)
    link: bool = inst.opcode in _LINKING_SIGN_BRANCHES
    nullify: bool = instruction.opcode_is_nullified_branch(inst.opcode)
    test: Callable[[int], bool] = _SIGN_TESTS[inst.opcode]

    def step() -> None:
        if link:
            # Set before the test, so `bgezal $ra, ...` tests the new $ra.
            gpr[31] = _wrap(cpu.pc + (8 if options.delayed_branches else 4))
        _branch(test(gpr[rs]), offset, nullify)

    return step


@_executes(Tok.BC1F_OPCODE, Tok.BC1FL_OPCODE, Tok.BC1T_OPCODE, Tok.BC1TL_OPCODE)
def _branch_fp_condition(inst: Instruction) -> Step:
    """bc1f/bc1t: branch on FP condition code cc (rt holds cc, nd, tf)."""
    offset: int = _branch_offset(inst)
    cc, nullify, on_true = inst.rt >> 2, bool((inst.rt >> 1) & 1), inst.rt & 1

    def step() -> None:
        _branch(regs.fcc(cc) == on_true, offset, nullify)

    return step


@_executes(Tok.J_OPCODE, Tok.JAL_OPCODE)
def _jump_absolute(inst: Instruction) -> Step:
    """j/jal: jump within the current 256 MB region; jal links $ra."""
    target: int = (inst.target << 2) & M32
    link: bool = inst.opcode == Tok.JAL_OPCODE

    def step() -> None:
        if link:
            gpr[31] = _wrap(cpu.pc + (8 if options.delayed_branches else 4))
        _jump((cpu.pc & 0xF0000000) | target)

    return step


# The hazard-barrier forms (.hb) run as the plain ones: the simulator has no pipeline
# hazards for a barrier to clear.
@_executes(Tok.JR_OPCODE, Tok.JR_HB_OPCODE)
def _jr(inst: Instruction) -> Step:
    """jr: jump to the address in rs."""
    rs: int = inst.rs

    def step() -> None:
        _jump(gpr[rs] & M32)

    return step


@_executes(Tok.JALR_OPCODE, Tok.JALR_HB_OPCODE)
def _jalr(inst: Instruction) -> Step:
    """jalr: rd = return address, jump to rs (read before rd is written)."""
    rd, rs = inst.rd, inst.rs

    def step() -> None:
        target: int = gpr[rs] & M32
        gpr[rd] = _wrap(cpu.pc + (8 if options.delayed_branches else 4))
        _jump(target)

    return step


@_executes(Tok.ERET_OPCODE)
def _eret(inst: Instruction) -> Step:
    """eret: clear EXL and return to EPC (with a delay slot if delayed)."""

    def step() -> None:
        cp0: list[int] = cpu.cpr[0]
        cp0[regs.CP0_STATUS] = _wrap(cp0[regs.CP0_STATUS] & ~regs.STATUS_EXL)
        _jump(cp0[regs.CP0_EPC] & M32)

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
def _load_integer(inst: Instruction) -> Step:
    """The loads; a faulting load still writes 0 to rt."""
    rt, base, offset = inst.rt, inst.rs, inst.imm
    opcode: int = inst.opcode
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
        _load(gpr, rt, read((gpr[base] + offset) & M32) & mask)

    return step


@_executes(Tok.LWL_OPCODE, Tok.LWR_OPCODE)
def _load_unaligned(inst: Instruction) -> Step:
    """lwl/lwr: merge part of an aligned word into rt (little-endian)."""
    rt, base, offset = inst.rt, inst.rs, inst.imm
    is_left: bool = inst.opcode == Tok.LWL_OPCODE

    def step() -> None:
        addr: int = (gpr[base] + offset) & M32
        byte: int = addr & 3
        reg: int = gpr[rt] & M32
        word: int = memory.read_word(addr & 0xFFFFFFFC) & M32
        if not traps.exception_occurred:
            if is_left:
                keep: int = 8 * (3 - byte)  # bits of rt that survive
                word = ((word << keep) & M32) | (reg & ((1 << keep) - 1))
            else:
                drop: int = 8 * byte
                word = (reg & ~(M32 >> drop) & M32) | (word >> drop)
        _load(gpr, rt, _wrap(word))

    return step


@_executes(Tok.SB_OPCODE, Tok.SH_OPCODE, Tok.SW_OPCODE, Tok.SC_OPCODE)
def _store_integer(inst: Instruction) -> Step:
    """The stores (sc is a plain store: one processor, so it always succeeds)."""
    rt, base, offset = inst.rt, inst.rs, inst.imm
    write: Callable[[int, int], None]
    match inst.opcode:
        case Tok.SB_OPCODE:
            write = memory.write_byte
        case Tok.SH_OPCODE:
            write = memory.write_half
        case Tok.SW_OPCODE | Tok.SC_OPCODE:
            write = memory.write_word
        case _:
            raise AssertionError(
                f"_store_integer: opcode {inst.opcode} is not a store; only they are"
                " registered"
            )

    def step() -> None:
        write((gpr[base] + offset) & M32, gpr[rt])

    return step


@_executes(Tok.SWL_OPCODE, Tok.SWR_OPCODE)
def _store_unaligned(inst: Instruction) -> Step:
    """swl/swr: store part of rt into an aligned word (little-endian)."""
    rt, base, offset = inst.rt, inst.rs, inst.imm
    is_left: bool = inst.opcode == Tok.SWL_OPCODE

    def step() -> None:
        addr: int = (gpr[base] + offset) & M32
        byte: int = addr & 3
        reg: int = gpr[rt] & M32
        aligned: int = addr & 0xFFFFFFFC
        data: int = memory.read_word(aligned) & M32
        if is_left:
            shift: int = 8 * (3 - byte)
            data = (data & ~(M32 >> shift) & M32) | (reg >> shift)
        else:
            shift = 8 * byte
            data = ((reg << shift) & M32) | (data & ((1 << shift) - 1))
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
def _trap_register(inst: Instruction) -> Step:
    """Trap if the comparison of rs with rt holds."""
    rs, rt = inst.rs, inst.rt
    test: Callable[[int, int], bool] = _TRAP_TESTS[inst.opcode]

    def step() -> None:
        if test(gpr[rs], gpr[rt]):
            _raise(ExcCode.TR)

    return step


@_executes(
    Tok.TEQI_OPCODE,
    Tok.TNEI_OPCODE,
    Tok.TGEI_OPCODE,
    Tok.TLTI_OPCODE,
    Tok.TGEIU_OPCODE,
    Tok.TLTIU_OPCODE,
)
def _trap_immediate(inst: Instruction) -> Step:
    """Trap if the comparison of rs with the sign-extended immediate holds."""
    rs, imm = inst.rs, inst.imm
    test: Callable[[int, int], bool] = _TRAP_TESTS[inst.opcode]

    def step() -> None:
        if test(gpr[rs], imm):
            _raise(ExcCode.TR)

    return step


@_executes(Tok.BREAK_OPCODE)
def _break(inst: Instruction) -> Step:
    """break: a Bp exception; ``break 1`` is a debugger breakpoint (stop)."""
    is_debugger: bool = inst.rd == 1

    def step() -> bool | None:
        _raise(ExcCode.BP)
        return True if is_debugger else None

    return step


@_executes(Tok.SYSCALL_OPCODE)
def _syscall(inst: Instruction) -> Step:
    """syscall: the service number is in $v0 (syscall.py)."""

    def step() -> bool | None:
        return None if syscall.do_syscall() else False

    return step


# --- coprocessor 0 ----------------------------------------------------------------------------


@_executes(Tok.MFC0_OPCODE)
def _mfc0(inst: Instruction) -> Step:
    """mfc0: rt = a CP0 register (its number is in the rd byte)."""
    rt, number = inst.rt, inst.rd

    def step() -> None:
        gpr[rt] = cpu.cpr[0][number]

    return step


@_executes(Tok.MTC0_OPCODE)
def _mtc0(inst: Instruction) -> Step:
    """mtc0: a CP0 register = rt, keeping only its writable bits."""
    rt, number = inst.rt, inst.rd

    def step() -> None:
        cp0: list[int] = cpu.cpr[0]
        cp0[number] = gpr[rt]
        match number:
            case regs.CP0_COMPARE:
                cp0[regs.CP0_CAUSE] = _wrap(cp0[regs.CP0_CAUSE] & ~regs.CAUSE_IP7)
            case regs.CP0_STATUS:
                cp0[number] = _wrap(
                    (cp0[number] & regs.STATUS_MASK)
                    | (regs.STATUS_CU & 0x30000000)
                    | regs.STATUS_UM
                )
            case regs.CP0_CAUSE:
                cp0[number] = _wrap(cp0[number] & regs.CAUSE_MASK)
            case regs.CP0_CONFIG:
                cp0[number] = _wrap(cp0[number] & regs.CONFIG_MASK)
            case _:
                pass  # other CP0 registers keep the whole value written

    return step


@_executes(Tok.CFC0_OPCODE, Tok.CTC0_OPCODE)
def _coprocessor0_control(inst: Instruction) -> Step:
    """cfc0/ctc0: move from/to a CP0 control register."""
    rt, number = inst.rt, inst.rd
    if inst.opcode == Tok.CFC0_OPCODE:

        def step() -> None:
            gpr[rt] = cpu.ccr[0][number]

    else:

        def step() -> None:
            cpu.ccr[0][number] = gpr[rt]

    return step


# --- the floating-point unit ---------------------------------------------------------------


def _fp_binary(
    operation: Callable[[float, float], float], double: bool
) -> Callable[[Instruction], Step]:
    """The factory of add/sub/mul/div.s or .d: fd = fs op ft."""

    def factory(inst: Instruction) -> Step:
        fd, fs, ft = inst.shamt, inst.rd, inst.rt
        if double:

            def step() -> None:
                if fd & 1:
                    raise console.run_error("Odd FP double register number\n")
                regs.set_fpr_d(fd, operation(regs.fpr_d(fs), regs.fpr_d(ft)))

        else:

            def step() -> None:
                regs.set_fpr_s(fd, operation(regs.fpr_s(fs), regs.fpr_s(ft)))

        return step

    return factory


for _add_token, _sub_token, _mul_token, _div_token, _double in (
    (Tok.ADD_S_OPCODE, Tok.SUB_S_OPCODE, Tok.MUL_S_OPCODE, Tok.DIV_S_OPCODE, False),
    (Tok.ADD_D_OPCODE, Tok.SUB_D_OPCODE, Tok.MUL_D_OPCODE, Tok.DIV_D_OPCODE, True),
):
    _FACTORIES[_add_token] = _fp_binary(lambda a, b: a + b, _double)
    _FACTORIES[_sub_token] = _fp_binary(lambda a, b: a - b, _double)
    _FACTORIES[_mul_token] = _fp_binary(lambda a, b: a * b, _double)
    _FACTORIES[_div_token] = _fp_binary(machine.f_div, _double)


def _fp_unary(
    operation: Callable[[float], float], double: bool
) -> Callable[[Instruction], Step]:
    """The factory of abs/neg/sqrt.s or .d: fd = op(fs)."""

    def factory(inst: Instruction) -> Step:
        fd, fs = inst.shamt, inst.rd
        if double:

            def step() -> None:
                if fd & 1:
                    raise console.run_error("Odd FP double register number\n")
                regs.set_fpr_d(fd, operation(regs.fpr_d(fs)))

        else:

            def step() -> None:
                regs.set_fpr_s(fd, operation(regs.fpr_s(fs)))

        return step

    return factory


for _abs_token, _neg_token, _sqrt_token, _double in (
    (Tok.ABS_S_OPCODE, Tok.NEG_S_OPCODE, Tok.SQRT_S_OPCODE, False),
    (Tok.ABS_D_OPCODE, Tok.NEG_D_OPCODE, Tok.SQRT_D_OPCODE, True),
):
    _FACTORIES[_abs_token] = _fp_unary(math.fabs, _double)
    _FACTORIES[_neg_token] = _fp_unary(lambda x: -x, _double)
    _FACTORIES[_sqrt_token] = _fp_unary(machine.f_sqrt, _double)


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
    _TO_WORD[_double_form] = (_to_word_adjust, regs.fpr_d)
    _TO_WORD[_single_form] = (_to_word_adjust, regs.fpr_s)


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
def _fp_to_word(inst: Instruction) -> Step:
    """Convert fs to a 32-bit integer in fd (x86's INT_MIN when out of range)."""
    fd, fs = inst.shamt, inst.rd
    adjust: Callable[[float], float]
    read: Callable[[int], float]
    adjust, read = _TO_WORD[inst.opcode]

    def step() -> None:
        fpr[fd] = machine.to_s32(adjust(read(fs)))

    return step


@_executes(
    Tok.CVT_D_S_OPCODE, Tok.CVT_D_W_OPCODE, Tok.CVT_S_D_OPCODE, Tok.CVT_S_W_OPCODE
)
def _fp_convert(inst: Instruction) -> Step:
    """cvt between single, double and word."""
    fd, fs = inst.shamt, inst.rd
    opcode: int = inst.opcode
    match opcode:
        case Tok.CVT_D_S_OPCODE:

            def step() -> None:
                regs.set_fpr_d(fd, regs.fpr_s(fs))
        case Tok.CVT_D_W_OPCODE:

            def step() -> None:
                regs.set_fpr_d(fd, float(fpr[fs]))
        case Tok.CVT_S_D_OPCODE:

            def step() -> None:
                regs.set_fpr_s(fd, regs.fpr_d(fs))
        case Tok.CVT_S_W_OPCODE:

            def step() -> None:
                regs.set_fpr_s(fd, float(fpr[fs]))
        case _:
            raise AssertionError(
                f"_fp_convert: opcode {opcode} is not a cvt; only they are registered"
            )

    return step


def _fp_compare(double: bool) -> Callable[[Instruction], Step]:
    """The factory of c.cond.s/.d: set FP condition code cc."""

    def factory(inst: Instruction) -> Step:
        fs, ft, cond, cc = inst.rd, inst.rt, inst.rs, inst.shamt >> 2
        read: Callable[[int], float] = regs.fpr_d if double else regs.fpr_s

        def step() -> None:
            a: float = read(fs)
            b: float = read(ft)
            if math.isnan(a) or math.isnan(b):
                if cond & instruction.COND_IN:
                    _raise(ExcCode.FPE)
                    return
                regs.set_fcc(cc, cond & instruction.COND_UN)
            else:
                result: int = 0
                if cond & instruction.COND_LT and a < b:
                    result = 1
                if cond & instruction.COND_EQ and a == b:
                    result = 1
                regs.set_fcc(cc, result)

        return step

    return factory


# The FP compares are the table's c.cond.fmt rows; the fmt field (bits 21-25)
# is 0x11 for a double, 0x10 for a single.
for _row in opcodes.ROWS:
    if _row.kind == OpType.FP_CMP_FS_FT_INST:
        _FACTORIES[_row.token] = _fp_compare((_row.encoding >> 21) & 0x1F == 0x11)


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
def _fp_move(inst: Instruction) -> Step:
    """mov.fmt, and its conditional forms (on a cc, or on a register)."""
    fd, fs, rt = inst.shamt, inst.rd, inst.rt
    opcode: int = inst.opcode
    double: bool = opcode in (
        Tok.MOV_D_OPCODE, Tok.MOVF_D_OPCODE, Tok.MOVT_D_OPCODE, Tok.MOVN_D_OPCODE,
        Tok.MOVZ_D_OPCODE,
    )  # fmt: skip

    def condition() -> bool:
        """Whether this move happens."""
        match opcode:
            case Tok.MOVF_S_OPCODE | Tok.MOVF_D_OPCODE:
                return regs.fcc(rt >> 2) == 0
            case Tok.MOVT_S_OPCODE | Tok.MOVT_D_OPCODE:
                return regs.fcc(rt >> 2) != 0
            case Tok.MOVN_S_OPCODE | Tok.MOVN_D_OPCODE:
                return gpr[rt] != 0
            case Tok.MOVZ_S_OPCODE | Tok.MOVZ_D_OPCODE:
                return gpr[rt] == 0
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
                regs.set_fpr_d(fd, regs.fpr_d(fs))
            else:
                fpr[fd] = fpr[fs]  # a single moves as raw bits

    return step


@_executes(Tok.MOVF_OPCODE, Tok.MOVT_OPCODE)
def _move_on_fp_condition(inst: Instruction) -> Step:
    """movf/movt: rd = rs if FP condition code cc is false/true."""
    rd, rs, cc = inst.rd, inst.rs, inst.rt >> 2
    wanted: int = 0 if inst.opcode == Tok.MOVF_OPCODE else 1

    def step() -> None:
        if regs.fcc(cc) == wanted:
            gpr[rd] = gpr[rs]

    return step


@_executes(Tok.MFC1_OPCODE, Tok.MTC1_OPCODE)
def _move_fp_word(inst: Instruction) -> Step:
    """mfc1/mtc1: copy a word's bits between rt and FP register fs."""
    rt, fs = inst.rt, inst.rd
    if inst.opcode == Tok.MFC1_OPCODE:

        def step() -> None:
            gpr[rt] = fpr[fs]

    else:

        def step() -> None:
            fpr[fs] = gpr[rt]

    return step


@_executes(Tok.CFC1_OPCODE, Tok.CTC1_OPCODE)
def _fp_control(inst: Instruction) -> Step:
    """cfc1/ctc1: move from/to an FPU control register (FIR, FCSR)."""
    rt, number = inst.rt, inst.rd
    if inst.opcode == Tok.CFC1_OPCODE:

        def step() -> None:
            gpr[rt] = cpu.cpr[1][number]

    else:

        def step() -> None:
            cpu.cpr[1][number] = gpr[rt]
            if number == regs.FCSR_REG and gpr[rt] & M32 & ~regs.FCSR_MASK:
                _raise(ExcCode.FPE)  # an unsupported mode

    return step


@_executes(Tok.LWC1_OPCODE, Tok.LDC1_OPCODE)
def _load_fp(inst: Instruction) -> Step:
    """lwc1/ldc1: load one or two words into FP registers."""
    ft, base, offset = inst.rt, inst.rs, inst.imm
    double: bool = inst.opcode == Tok.LDC1_OPCODE

    def step() -> None:
        addr: int = (gpr[base] + offset) & M32
        if double:
            if addr & 3:
                memory._bad_address(ExcCode.ADEL, addr)
            _load(fpr, ft, memory.read_word(addr))
            _load(fpr, ft + 1, memory.read_word((addr + 4) & M32))
        else:
            _load(fpr, ft, memory.read_word(addr))

    return step


@_executes(Tok.SWC1_OPCODE, Tok.SDC1_OPCODE)
def _store_fp(inst: Instruction) -> Step:
    """swc1/sdc1: store FP register(s) as one or two words."""
    ft, base, offset = inst.rt, inst.rs, inst.imm
    double: bool = inst.opcode == Tok.SDC1_OPCODE

    def step() -> None:
        if double:
            low: int
            high: int
            low, high = machine.double_words(regs.fpr_d(ft))
            addr: int = (gpr[base] + offset) & M32
            if addr & 3:
                memory._bad_address(ExcCode.ADEL, addr)
            memory.write_word(addr, low)
            memory.write_word((addr + 4) & M32, high)
        else:
            memory.write_word((gpr[base] + offset) & M32, fpr[ft])

    return step
