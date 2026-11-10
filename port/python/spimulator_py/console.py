"""Input and output: the program's console and the simulator's messages.

The C engine writes the MIPS program's output to stdout, and its own
messages (errors, exception reports, the REPL's answers) to stderr, flushing
after every write.  This port writes straight to the file descriptors with
``os.write``, which keeps the same order between the two streams.

Text inside the port is ``str`` holding one character per byte (decoded as
Latin-1), so a source line or a MIPS string with any bytes in it comes out
exactly as it went in.  :func:`to_bytes` / :func:`from_bytes` convert.

Errors: ``run_error`` in C prints a message and ``longjmp``s back to the
top level; here it raises :class:`RunError`.  ``fatal_error`` prints and
exits with status 255 (C's ``exit(-1)``).
"""

import os
import select
from typing import Any

from . import options

STDIN: int = 0
STDOUT: int = 1
STDERR: int = 2


class RunError(Exception):
    """A run-time error that abandons the current command (C ``run_error``)."""


class CrashError(BaseException):
    """Where the C engine dies of SIGSEGV, the port exits the same way.

    Raised for the C engine bugs listed in tasks/engine-port-python.md under
    "C engine bugs found"; only spim.main catches it, to exit with 139.
    """


def to_bytes(text: str) -> bytes:
    """The bytes that a one-character-per-byte string stands for."""
    return text.encode("latin-1")


def from_bytes(data: bytes) -> str:
    """A string with one character per byte of data."""
    return data.decode("latin-1")


def write_fd(fd: int, data: bytes) -> None:
    """Write all of data to the file descriptor fd.

    A write to a closed pipe kills the process with SIGPIPE, as in C (spim.main
    restores the signal's default action).
    """
    view: memoryview = memoryview(data)
    while view:
        written: int = os.write(fd, view)
        view = view[written:]


def output(text: str) -> None:
    """Write the program's output to stdout (C ``write_output(console_out)``)."""
    write_fd(STDOUT, to_bytes(text))


def message(text: str) -> None:
    """Write a simulator message to stderr (C ``write_output(message_out)``)."""
    write_fd(STDERR, to_bytes(text))


def error(text: str) -> None:
    """Print an error message on stderr (C ``error``)."""
    write_fd(STDERR, to_bytes(text))


def run_error(text: str) -> RunError:
    """Print text on stderr and return the RunError to raise (C ``run_error``).

    Callers write ``raise console.run_error(...)`` so the control flow is
    visible at the call site.
    """
    console_to_spim()
    error(text)
    return RunError(text)


def fatal_error(text: str) -> SystemExit:
    """Print text on stderr and return the exit to raise (C ``fatal_error``)."""
    error(text)
    return SystemExit(255)


def read_byte() -> bytes:
    """One byte from stdin, or b"" at end of file (C ``read(0, &c, 1)``)."""
    try:
        return os.read(STDIN, 1)
    except OSError:
        return b""


def read_input(size: int) -> bytes:
    """Read a line of at most size - 1 bytes from stdin (C ``read_input``).

    Like ``fgets``: stops after a newline, at end of file, or when the
    buffer is full; the caller adds the terminating NUL.  Reads one byte at a
    time, so nothing past the line is taken from stdin.

    Returns:
        bytes: the bytes read (empty at end of file).
    """
    data: bytearray = bytearray()
    while size > 1:
        byte: bytes = read_byte()
        if not byte:
            break
        data += byte
        size -= 1
        if byte == b"\n":
            break
    return bytes(data)


def read_int_input() -> int | None:
    """Read one decimal integer the way scanf's ``%ld`` does (syscall 5).

    Skips white space, takes an optional sign and the digits; the byte that
    ends the number is consumed too (an fd has no pushback).

    Returns:
        int | None: the value (unbounded; the caller truncates it), or None
        at end of file or when the next byte does not start a number.
    """
    byte: bytes = read_byte()
    while byte in (b" ", b"\t", b"\n", b"\r"):
        byte = read_byte()
    sign: int = 1
    if byte in (b"-", b"+"):
        sign = -1 if byte == b"-" else 1
        byte = read_byte()
    if not (byte and byte.isdigit()):
        return None
    value: int = 0
    while byte and byte.isdigit():
        value = value * 10 + int(byte)
        byte = read_byte()
    return sign * value


# --- the memory-mapped console --------------------------------------------

_saved_terminal: list[Any] | None = None


def console_to_program() -> None:
    """Put the terminal in raw-ish mode for a memory-mapped-IO program."""
    global _saved_terminal
    if options.mapped_io and _saved_terminal is None and os.isatty(STDIN):
        import termios

        _saved_terminal = termios.tcgetattr(STDIN)
        params: list[Any] = termios.tcgetattr(STDIN)
        iflag: int = params[0]
        iflag &= ~(
            termios.ISTRIP
            | termios.INLCR
            | termios.ICRNL
            | termios.IGNCR
            | termios.IXON
            | termios.IXOFF
            | termios.INPCK
            | termios.BRKINT
            | termios.PARMRK
        )
        params[0] = iflag | termios.IGNBRK | termios.IGNPAR | termios.ICRNL
        params[1] = termios.OPOST | termios.ONLCR
        cflag: int = params[2]
        params[2] = (cflag & ~termios.PARENB) | termios.CREAD | termios.CS8
        params[3] = 0
        cc: list[bytes | int] = params[6]
        cc[termios.VMIN] = 1
        cc[termios.VTIME] = 1
        termios.tcsetattr(STDIN, termios.TCSANOW, params)


def console_to_spim() -> None:
    """Give the terminal back to the simulator (undo console_to_program)."""
    global _saved_terminal
    if _saved_terminal is not None:
        import termios

        termios.tcsetattr(STDIN, termios.TCSANOW, _saved_terminal)
    _saved_terminal = None


def console_input_available() -> bool:
    """True if a byte can be read from stdin without waiting (mapped IO)."""
    if not options.mapped_io:
        return False
    try:
        readable: list[int] = select.select([STDIN], [], [], 0)[0]
    except (OSError, ValueError):
        return False
    return bool(readable)


def put_console_char(char: int) -> None:
    """Write one byte to stdout (the memory-mapped transmitter)."""
    write_fd(STDOUT, bytes([char & 0xFF]))
