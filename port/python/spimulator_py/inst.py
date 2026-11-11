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
class ImmExpr:
    """An immediate written as ``symbol +/- offset`` (C ``imm_expr``).

    Attributes:
        offset: the constant part.
        symbol: the label, or None for a plain constant.
        bits: 0 for the whole value, > 0 for its upper 16 bits (``lui``),
            < 0 for its lower 16 bits.
        pc_relative: the value is a branch displacement from the PC.
    """

    offset: int = 0
    symbol: "Label | None" = None
    bits: int = 0
    pc_relative: bool = False

    def copy(self) -> "ImmExpr":
        """A shallow copy (the label is shared, as in C)."""
        return ImmExpr(self.offset, self.symbol, self.bits, self.pc_relative)


@dataclass
class AddrExpr:
    """An address written as ``imm(register)`` (C ``addr_expr``)."""

    reg_no: int
    imm: ImmExpr


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
        "rs",
        "rt",
        "rd",
        "shamt",
        "imm",
        "target",
        "encoding",
        "expr",
        "source",
        "exec_cache",
    )

    def __init__(self, opcode: int = 0) -> None:
        self.opcode: int = opcode
        self.rs: int = 0
        self.rt: int = 0
        self.rd: int = 0
        self.shamt: int = 0
        self.imm: int = 0
        self.target: int = 0
        self.encoding: int = 0
        self.expr: ImmExpr | None = None
        self.source: str | None = None
        self.exec_cache: Callable[[], bool | None] | None = None

    def _bytes_changed(self) -> None:
        """Recompute the overlapping views (imm, target) from the four bytes."""
        self.imm = machine.s16(self.rd | self.shamt << 8)
        self.target = self.rs | self.rt << 8 | self.rd << 16 | self.shamt << 24
        self.exec_cache = None

    def set_rs(self, value: int) -> None:
        """Set the rs byte (also the FP condition, ``COND``)."""
        self.rs = value & 0xFF
        self._bytes_changed()

    def set_rt(self, value: int) -> None:
        """Set the rt byte (also FT)."""
        self.rt = value & 0xFF
        self._bytes_changed()

    def set_rd(self, value: int) -> None:
        """Set the rd byte (also FS)."""
        self.rd = value & 0xFF
        self._bytes_changed()

    def set_shamt(self, value: int) -> None:
        """Set the shamt byte (also FD)."""
        self.shamt = value & 0xFF
        self._bytes_changed()

    def set_imm(self, value: int) -> None:
        """Set the 16-bit immediate (bytes rd and shamt)."""
        value &= 0xFFFF
        self.rd = value & 0xFF
        self.shamt = value >> 8
        self._bytes_changed()

    def set_target(self, value: int) -> None:
        """Set the 32-bit J-type target (all four bytes)."""
        value &= 0xFFFFFFFF
        self.rs = value & 0xFF
        self.rt = (value >> 8) & 0xFF
        self.rd = (value >> 16) & 0xFF
        self.shamt = value >> 24
        self._bytes_changed()

    def set_encoding(self, value: int) -> None:
        """Set the machine word (stored signed, as C's int32_t)."""
        self.encoding = machine.s32(value)

    def copy(self) -> "Instruction":
        """A copy with its own expression (C ``copy_inst``)."""
        other: Instruction = Instruction(self.opcode)
        other.rs, other.rt, other.rd, other.shamt = (
            self.rs,
            self.rt,
            self.rd,
            self.shamt,
        )
        other._bytes_changed()
        other.encoding = self.encoding
        other.expr = self.expr.copy() if self.expr is not None else None
        other.source = self.source
        return other
