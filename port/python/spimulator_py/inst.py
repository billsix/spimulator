"""The data structures of one assembled instruction (instruction.h).

An :class:`Instruction` is what one word of the text segment holds: the
opcode token, the register fields, the immediate, its 32-bit encoding, the
symbolic expression it was assembled from (if any), and the source line.

C stores the fields in overlapping unions, in this byte order::

    byte  0    1    2      3
          rs   rt   rd     shamt       (R-type)
          rs   rt   imm (16 bits)      (I-type: imm overlaps rd and shamt)
          target (all 32 bits)         (J-type: overlaps everything)

and the engine depends on the overlap (for example, an FP compare stores its
condition in the rs byte and reads FS from the rd byte).  So this class
keeps the four bytes and recomputes the overlapping views whenever one is
set.  Reading ``inst.rs`` or ``inst.imm`` is a plain attribute read, which
keeps the executor fast; writing goes through the ``set_*`` methods.
"""

from collections.abc import Callable
from dataclasses import dataclass
from typing import TYPE_CHECKING

from . import machine

if TYPE_CHECKING:
    from .symbol_table import Label


@dataclass
class ImmediateExpression:
    """An immediate written as ``symbol +/- offset`` (C ``imm_expr``).

    Attributes:
        offset: the constant part.
        symbol: the label, or None for a plain constant.
        bits: 0 for the whole value, > 0 for its upper 16 bits (``lui``),
            < 0 for its lower 16 bits.
        program_counter_relative: the value is a branch displacement from the PC.
    """

    offset: int = 0
    symbol: "Label | None" = None
    bits: int = 0
    program_counter_relative: bool = False

    def copy(self) -> "ImmediateExpression":
        """A shallow copy (the label is shared, as in C)."""
        return ImmediateExpression(
            self.offset, self.symbol, self.bits, self.program_counter_relative
        )


@dataclass
class AddressExpression:
    """An address written as ``imm(register)`` (C ``addr_expr``)."""

    register_no: int
    immediate: ImmediateExpression


class Instruction:
    """One instruction (C ``mips_instruction``).

    Attributes:
        opcode: the opcode token (a ``Tok`` member, or its int value).
        rs, rt, rd, shamt: the register bytes (each 0..255, as C's uint8_t).
        imm: the signed 16-bit immediate (bytes rd and shamt).
        target: the 32-bit J-type target (all four bytes).
        encoding: the 32-bit machine word (0 until it is encoded).
        expr: the immediate's symbolic expression, if any.
        source: the source line ("NNN: text") it was assembled from, if any.
        exec_cache: the executor's decoded form, built on first execution.
    """

    __slots__ = (
        "opcode",
        "source_register",
        "target_register",
        "destination_register",
        "shift_amount",
        "immediate",
        "target",
        "encoding",
        "expression",
        "source",
        "exec_cache",
    )

    def __init__(self, opcode: int = 0) -> None:
        self.opcode: int = opcode
        self.source_register: int = 0
        self.target_register: int = 0
        self.destination_register: int = 0
        self.shift_amount: int = 0
        self.immediate: int = 0
        self.target: int = 0
        self.encoding: int = 0
        self.expression: ImmediateExpression | None = None
        self.source: str | None = None
        self.exec_cache: Callable[[], bool | None] | None = None

    def _bytes_changed(self) -> None:
        """Recompute the overlapping views (imm, target) from the four bytes."""
        self.immediate = machine.s16(self.destination_register | self.shift_amount << 8)
        self.target = (
            self.source_register
            | self.target_register << 8
            | self.destination_register << 16
            | self.shift_amount << 24
        )
        self.exec_cache = None

    def set_source_register(self, value: int) -> None:
        """Set the rs byte (also the FP condition, ``COND``)."""
        self.source_register = value & 0xFF
        self._bytes_changed()

    def set_target_register(self, value: int) -> None:
        """Set the rt byte (also FT)."""
        self.target_register = value & 0xFF
        self._bytes_changed()

    def set_destination_register(self, value: int) -> None:
        """Set the rd byte (also FS)."""
        self.destination_register = value & 0xFF
        self._bytes_changed()

    def set_shift_amount(self, value: int) -> None:
        """Set the shamt byte (also FD)."""
        self.shift_amount = value & 0xFF
        self._bytes_changed()

    def set_immediate(self, value: int) -> None:
        """Set the 16-bit immediate (bytes rd and shamt)."""
        value &= 0xFFFF
        self.destination_register = value & 0xFF
        self.shift_amount = value >> 8
        self._bytes_changed()

    def set_target(self, value: int) -> None:
        """Set the 32-bit J-type target (all four bytes)."""
        value &= 0xFFFFFFFF
        self.source_register = value & 0xFF
        self.target_register = (value >> 8) & 0xFF
        self.destination_register = (value >> 16) & 0xFF
        self.shift_amount = value >> 24
        self._bytes_changed()

    def set_encoding(self, value: int) -> None:
        """Set the machine word (stored signed, as C's int32_t)."""
        self.encoding = machine.s32(value)

    def copy(self) -> "Instruction":
        """A copy with its own expression (C ``copy_inst``)."""
        other: Instruction = Instruction(self.opcode)
        (
            other.source_register,
            other.target_register,
            other.destination_register,
            other.shift_amount,
        ) = (
            self.source_register,
            self.target_register,
            self.destination_register,
            self.shift_amount,
        )
        other._bytes_changed()
        other.encoding = self.encoding
        other.expression = (
            self.expression.copy() if self.expression is not None else None
        )
        other.source = self.source
        return other
