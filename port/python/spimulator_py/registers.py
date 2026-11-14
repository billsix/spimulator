"""The processor's registers (the C engine's registers.h).

All of them live in one object, :data:`cpu`, so the executor can bind it to
a local name.  Every register holds a signed 32-bit value (see machine.py).

The floating-point registers: MIPS32 revision 1 has 32 single-precision
registers, and a double occupies an even/odd pair.  The C engine overlays
three arrays on the same 128 bytes (``float[32]``, ``double[16]``,
``int32_t[32]``).  Here the 32 words are the real storage (:attr:`CPU.fpr`)
and :func:`floating_point_register_single` / :func:`floating_point_register_double` read them as a float or a double, so a
value written one way and read another way behaves as it does in C.
"""

from . import console, machine

GENERAL_PURPOSE_REGISTER_COUNT: int = 32
FLOATING_POINT_GENERAL_REGISTER_LENGTH: int = 32

VALUE_REGISTER_0: int = 2
ARGUMENT_REGISTER_0: int = 4
ARGUMENT_REGISTER_1: int = 5
ARGUMENT_REGISTER_2: int = 6
ARGUMENT_REGISTER_3: int = 7
FLOATING_POINT_ARGUMENT_REGISTER_0: int = 12
STACK_POINTER_REGISTER: int = 29
RESULT_REGISTER: int = 2
FLOATING_POINT_RESULT_REGISTER: int = 0
GLOBAL_POINTER_REGISTER: int = 28

# Coprocessor 0 (exceptions) register numbers.
COPROCESSOR_0_BAD_VIRTUAL_ADDRESS: int = 8
COPROCESSOR_0_COUNT: int = 9
COPROCESSOR_0_COMPARE: int = 11
COPROCESSOR_0_STATUS: int = 12
COPROCESSOR_0_CAUSE: int = 13
COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER: int = 14
COPROCESSOR_0_CONFIG: int = 16

# Fields of the Status register.
STATUS_COPROCESSOR_USABLE: int = 0xF0000000
STATUS_INTERRUPT_MASK: int = 0x0000FF00
STATUS_USER_MODE: int = 0x00000010
STATUS_EXCEPTION_LEVEL: int = 0x00000002
STATUS_INTERRUPT_ENABLE: int = 0x00000001
STATUS_MASK: int = (
    STATUS_COPROCESSOR_USABLE
    | STATUS_USER_MODE
    | STATUS_INTERRUPT_MASK
    | STATUS_EXCEPTION_LEVEL
    | STATUS_INTERRUPT_ENABLE
)

# Fields of the Cause register.
CAUSE_BD: int = 0x80000000
CAUSE_INTERRUPTS_PENDING: int = 0x0000FF00
CAUSE_INTERRUPT_PENDING_7: int = 0x00008000
CAUSE_EXCEPTION_CODE: int = 0x0000007C
CAUSE_MASK: int = CAUSE_BD | CAUSE_INTERRUPTS_PENDING | CAUSE_EXCEPTION_CODE

CONFIG_MASK: int = 0x80000 | 0x60000 | 0x1C000 | 0x380

# Floating-point control registers (coprocessor 1's control registers).
FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER: int = 0
FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER: int = 31
FLOATING_POINT_IMPLEMENTATION_HAS_WORD: int = 0x0008000
FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE: int = 0x0001000
FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE: int = 0x0000800
FLOATING_POINT_CONTROL_STATUS_MASK: int = 0xFE800000
CONDITION_CODE_0_BIT: int = 23
CONDITION_CODE_1_BIT: int = 25

INTEGER_REGISTER_NAMES: list[str] = [
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
]  # fmt: skip
"""Register names as the disassembler prints them ($0 prints as $r0)."""


class CPU:
    """The register file.

    Attributes:
        general_purpose_registers: the 32 GPRs ($0 is forced to 0 before each
            step).
        multiply_divide_high: the HI register (multiply/divide results).
        multiply_divide_low: the LO register.
        program_counter: the PC (unsigned).
        coprocessor_registers: ``coprocessor_registers[0]`` is CP0 (Status,
            Cause, EPC, ...), ``coprocessor_registers[1]`` holds the FPU control
            registers (FIR, FCSR).
        coprocessor_control_registers: the coprocessor control registers
            (``cfc0``/``ctc0``).
        floating_point_registers: the 32 FPRs, as raw words.
    """

    __slots__ = (
        "general_purpose_registers",
        "multiply_divide_high",
        "multiply_divide_low",
        "program_counter",
        "coprocessor_registers",
        "coprocessor_control_registers",
        "floating_point_registers",
    )

    def __init__(self) -> None:
        self.general_purpose_registers: list[int] = [0] * GENERAL_PURPOSE_REGISTER_COUNT
        self.multiply_divide_high: int = 0
        self.multiply_divide_low: int = 0
        self.program_counter: int = 0
        self.coprocessor_registers: list[list[int]] = [[0] * 32 for _ in range(4)]
        self.coprocessor_control_registers: list[list[int]] = [
            [0] * 32 for _ in range(4)
        ]
        self.floating_point_registers: list[int] = [0] * (
            FLOATING_POINT_GENERAL_REGISTER_LENGTH + 1
        )  # +1: see floating_point_register_double's pair


cpu: CPU = CPU()
"""The one register file."""


def floating_point_register_single(register_number: int) -> float:
    """FP register reg read as a single-precision float (C ``FPR_S``)."""
    return machine.bits_float(cpu.floating_point_registers[register_number])


def set_floating_point_register_single(register_number: int, value: float) -> None:
    """Store value, rounded to single precision, in FP register reg."""
    cpu.floating_point_registers[register_number] = machine.float_bits(value)


def floating_point_register_double(register_number: int) -> float:
    """The even/odd FP register pair starting at reg as a double (``FPR_D``).

    Raises:
        RunError: reg is odd, which C reports as a run-time error.
    """
    if register_number & 1:
        raise console.run_error("Odd FP double register number\n")
    return machine.words_double(
        cpu.floating_point_registers[register_number],
        cpu.floating_point_registers[register_number + 1],
    )


def set_floating_point_register_double(register_number: int, value: float) -> None:
    """Store a double in the FP register pair starting at reg (``SET_FPR_D``).

    Raises:
        RunError: reg is odd.
    """
    if register_number & 1:
        raise console.run_error("Odd FP double register number\n")
    (
        cpu.floating_point_registers[register_number],
        cpu.floating_point_registers[register_number + 1],
    ) = machine.double_words(value)


def condition_code_mask(n: int) -> int:
    """The FCSR bit of FP condition code n (cc 0 is bit 23, 1..7 bits 25..31)."""
    return (
        (1 << CONDITION_CODE_0_BIT)
        if n == 0 or n > 7
        else (1 << (CONDITION_CODE_1_BIT + n - 1))
    )


def floating_point_condition_code(n: int) -> int:
    """FP condition code n, 0 or 1."""
    return (
        0
        if machine.u32(
            cpu.coprocessor_registers[1][FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER]
        )
        & condition_code_mask(n)
        == 0
        else 1
    )


def set_floating_point_condition_code(n: int, value: int) -> None:
    """Set FP condition code n to value (0 clears, anything else sets)."""
    floating_point_control_status_register: int = machine.u32(
        cpu.coprocessor_registers[1][FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER]
    )
    floating_point_control_status_register = (
        floating_point_control_status_register & ~condition_code_mask(n)
        if value == 0
        else floating_point_control_status_register | condition_code_mask(n)
    )
    cpu.coprocessor_registers[1][FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER] = (
        machine.s32(floating_point_control_status_register)
    )


def exception_code() -> int:
    """The ExceptionCode field of the Cause register (C ``CP0_ExCode``)."""
    return (
        machine.u32(cpu.coprocessor_registers[0][COPROCESSOR_0_CAUSE])
        & CAUSE_EXCEPTION_CODE
    ) >> 2
