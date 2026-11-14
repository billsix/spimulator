"""Assemble-time events, for the ``-listing`` trace (assembler-event.c).

The assembler announces what it does (a label defined, an instruction or a
datum stored, a forward reference made or patched) through these functions.
When an observer is installed (spim.py installs one for ``-listing FILE``) it
receives an :class:`Event`; otherwise each function returns at once.
"""

from collections.abc import Callable
from dataclasses import dataclass
from enum import Enum, auto

from . import scanner


class Kind(Enum):
    """What happened."""

    LABEL_DEFINITION = auto()
    TEXT_INSTRUCTION = auto()
    DATA_BYTE = auto()
    DATA_HALF = auto()
    DATA_WORD = auto()
    DATA_DOUBLE = auto()
    DATA_STRING = auto()
    FORWARD_REF = auto()
    FORWARD_RESOLVED = auto()


@dataclass
class Event:
    """One assemble-time event.

    Attributes:
        kind: what happened.
        source_line: the scanner's line number at the time.
        addr: the address the event touches.
        name: the label or symbol involved, if any.
        flag: LABEL_DEFINITION: the label is global; DATA_STRING: it is
            NUL-terminated.
        value: the stored integer, string length or resolved value.
        double: DATA_DOUBLE's value.
    """

    kind: Kind
    source_line: int
    address: int
    name: str = ""
    flag: bool = False
    value: int = 0
    double: float = 0.0


observer: Callable[[Event], None] | None = None
"""The installed observer, or None."""


def _fire(
    kind: Kind,
    address: int,
    name: str = "",
    flag: bool = False,
    value: int = 0,
    double: float = 0.0,
) -> None:
    """Hand an event to the observer (the caller checked there is one)."""
    if observer is None:
        raise AssertionError("_fire: observer is None")
    observer(Event(kind, scanner.line_no, address, name, flag, value, double))


def label_definition(address: int, name: str, is_global: bool) -> None:
    """A label became defined at addr."""
    if observer is not None:
        _fire(Kind.LABEL_DEFINITION, address, name=name, flag=is_global)


def text_instruction(address: int, encoding: int) -> None:
    """An instruction was stored at addr."""
    if observer is not None:
        _fire(Kind.TEXT_INSTRUCTION, address, value=encoding)


def data_byte(address: int, value: int) -> None:
    """A ``.byte`` was stored."""
    if observer is not None:
        _fire(Kind.DATA_BYTE, address, value=value)


def data_half(address: int, value: int) -> None:
    """A ``.half`` was stored."""
    if observer is not None:
        _fire(Kind.DATA_HALF, address, value=value)


def data_word(address: int, value: int) -> None:
    """A ``.word`` was stored."""
    if observer is not None:
        _fire(Kind.DATA_WORD, address, value=value)


def data_double(address: int, value: float) -> None:
    """A ``.double`` was stored."""
    if observer is not None:
        _fire(Kind.DATA_DOUBLE, address, double=value)


def data_string(address: int, length: int, null_terminated: bool) -> None:
    """A ``.ascii``/``.asciiz`` string was stored."""
    if observer is not None:
        _fire(Kind.DATA_STRING, address, value=length, flag=null_terminated)


def forward_ref(address: int, symbol: str) -> None:
    """The instruction or datum at addr uses the undefined symbol."""
    if observer is not None:
        _fire(Kind.FORWARD_REF, address, name=symbol)


def forward_resolved(address: int, symbol: str, value: int) -> None:
    """The forward reference at addr was patched with symbol's value."""
    if observer is not None:
        _fire(Kind.FORWARD_RESOLVED, address, name=symbol, value=value)
