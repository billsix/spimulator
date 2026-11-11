"""The processor's registers (the C engine's registers.h).

All of them live in one object, :data:`cpu`, so the executor can bind it to
a local name.  Every register holds a signed 32-bit value (see machine.py).

The floating-point registers: MIPS32 revision 1 has 32 single-precision
registers, and a double occupies an even/odd pair.  The C engine overlays
three arrays on the same 128 bytes (``float[32]``, ``double[16]``,
``int32_t[32]``).  Here the 32 words are the real storage (:attr:`CPU.fpr`)
and :func:`fpr_s` / :func:`fpr_d` read them as a float or a double, so a
value written one way and read another way behaves as it does in C.
"""

from . import console, machine

R_LENGTH: int = 32
FGR_LENGTH: int = 32

REG_V0: int = 2
REG_A0: int = 4
REG_A1: int = 5
REG_A2: int = 6
REG_A3: int = 7
REG_FA0: int = 12
REG_SP: int = 29
REG_RES: int = 2
REG_FRES: int = 0
REG_GP: int = 28

# Coprocessor 0 (exceptions) register numbers.
CP0_BADVADDR: int = 8
CP0_COUNT: int = 9
CP0_COMPARE: int = 11
CP0_STATUS: int = 12
CP0_CAUSE: int = 13
CP0_EPC: int = 14
CP0_CONFIG: int = 16

# Fields of the Status register.
STATUS_CU: int = 0xF0000000
STATUS_IM: int = 0x0000FF00
STATUS_UM: int = 0x00000010
STATUS_EXL: int = 0x00000002
STATUS_IE: int = 0x00000001
STATUS_MASK: int = STATUS_CU | STATUS_UM | STATUS_IM | STATUS_EXL | STATUS_IE

# Fields of the Cause register.
CAUSE_BD: int = 0x80000000
CAUSE_IP: int = 0x0000FF00
CAUSE_IP7: int = 0x00008000
CAUSE_EXCCODE: int = 0x0000007C
CAUSE_MASK: int = CAUSE_BD | CAUSE_IP | CAUSE_EXCCODE

CONFIG_MASK: int = 0x80000 | 0x60000 | 0x1C000 | 0x380

# Floating-point control registers (coprocessor 1's control registers).
FIR_REG: int = 0
FCSR_REG: int = 31
FIR_W: int = 0x0008000
FIR_D: int = 0x0001000
FIR_S: int = 0x0000800
FCSR_MASK: int = 0xFE800000
CC0_BIT: int = 23
CC1_BIT: int = 25

INT_REG_NAMES: list[str] = [
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
]  # fmt: skip
"""Register names as the disassembler prints them ($0 prints as $r0)."""


class CPU:
    """The register file.

    Attributes:
        gpr: the 32 general registers ($0 is forced to 0 before each step).
        hi: the HI register (multiply/divide results).
        lo: the LO register.
        pc: the program counter (unsigned).
        cpr: coprocessor registers, ``cpr[0]`` is CP0 (Status, Cause, EPC,
            ...), ``cpr[1]`` holds the FPU control registers (FIR, FCSR).
        ccr: coprocessor control registers (``cfc0``/``ctc0``).
        fpr: the 32 floating-point registers, as raw words.
    """

    __slots__ = ("gpr", "hi", "lo", "pc", "cpr", "ccr", "fpr")

    def __init__(self) -> None:
        self.gpr: list[int] = [0] * R_LENGTH
        self.hi: int = 0
        self.lo: int = 0
        self.pc: int = 0
        self.cpr: list[list[int]] = [[0] * 32 for _ in range(4)]
        self.ccr: list[list[int]] = [[0] * 32 for _ in range(4)]
        self.fpr: list[int] = [0] * (FGR_LENGTH + 1)  # +1: see fpr_d's pair


cpu: CPU = CPU()
"""The one register file."""


def fpr_s(reg: int) -> float:
    """FP register reg read as a single-precision float (C ``FPR_S``)."""
    return machine.bits_float(cpu.fpr[reg])


def set_fpr_s(reg: int, value: float) -> None:
    """Store value, rounded to single precision, in FP register reg."""
    cpu.fpr[reg] = machine.float_bits(value)


def fpr_d(reg: int) -> float:
    """The even/odd FP register pair starting at reg as a double (``FPR_D``).

    Raises:
        RunError: reg is odd, which C reports as a run-time error.
    """
    if reg & 1:
        raise console.run_error("Odd FP double register number\n")
    return machine.words_double(cpu.fpr[reg], cpu.fpr[reg + 1])


def set_fpr_d(reg: int, value: float) -> None:
    """Store a double in the FP register pair starting at reg (``SET_FPR_D``).

    Raises:
        RunError: reg is odd.
    """
    if reg & 1:
        raise console.run_error("Odd FP double register number\n")
    cpu.fpr[reg], cpu.fpr[reg + 1] = machine.double_words(value)


def cc_mask(n: int) -> int:
    """The FCSR bit of FP condition code n (cc 0 is bit 23, 1..7 bits 25..31)."""
    return (1 << CC0_BIT) if n == 0 or n > 7 else (1 << (CC1_BIT + n - 1))


def fcc(n: int) -> int:
    """FP condition code n, 0 or 1."""
    return 0 if machine.u32(cpu.cpr[1][FCSR_REG]) & cc_mask(n) == 0 else 1


def set_fcc(n: int, value: int) -> None:
    """Set FP condition code n to value (0 clears, anything else sets)."""
    fcsr: int = machine.u32(cpu.cpr[1][FCSR_REG])
    fcsr = fcsr & ~cc_mask(n) if value == 0 else fcsr | cc_mask(n)
    cpu.cpr[1][FCSR_REG] = machine.s32(fcsr)


def exc_code() -> int:
    """The ExcCode field of the Cause register (C ``CP0_ExCode``)."""
    return (machine.u32(cpu.cpr[0][CP0_CAUSE]) & CAUSE_EXCCODE) >> 2
