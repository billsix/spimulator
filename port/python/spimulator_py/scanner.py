"""The lexical scanner: source bytes to tokens (the C engine's scanner.c).

The scanner reads one line at a time (like ``fgets`` with a 4096-byte
buffer) and hands out tokens through a three-token lookahead buffer:
:func:`peek`, :func:`peek2` and :func:`advance`.  The parser calls them.

Quirks of the C scanner that a program can see, kept on purpose:

* ``,`` is white space, so ``add $t0 $t1 $t2`` is accepted;
* ``;`` ends a statement like a newline;
* ``-`` followed by a digit starts a negative number (so ``8 -4`` is two
  numbers);
* integers are decimal or ``0x`` hex, and wrap silently to 32 bits;
* a name defined as a constant (``X = 5``) is replaced by its value as it
  is scanned, which depends on how far ahead the lookahead buffer has read.

Tokens are numbered as in opcodes.py (structural tokens from 256; a
punctuation character is its own character code).  Text is a ``str`` with
one character per byte.
"""

from dataclasses import dataclass, field
from typing import BinaryIO

from . import console, opcodes, options, symbol_table
from .opcodes import (
    TOK_EOF,
    TOK_FP,
    TOK_FP_REG,
    TOK_ID,
    TOK_INT,
    TOK_NL,
    TOK_REG,
    TOK_STR,
)

LINE_BUF_MAX: int = 4096

_DIGITS: frozenset[int] = frozenset(b"0123456789")
_HEX_DIGITS: frozenset[int] = frozenset(b"0123456789abcdefABCDEF")
_ALPHA: frozenset[int] = frozenset(
    b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
)
_ALNUM: frozenset[int] = _ALPHA | _DIGITS
_PUNCTUATION: frozenset[int] = frozenset(b"*/:()+->=")

REGISTER_NAMES: dict[str, int] = {
    "a0": 4, "a1": 5, "a2": 6, "a3": 7, "at": 1, "fp": 30, "gp": 28,
    "k0": 26, "k1": 27, "kt0": 26, "kt1": 27, "ra": 31, "s0": 16,
    "s1": 17, "s2": 18, "s3": 19, "s4": 20, "s5": 21, "s6": 22, "s7": 23,
    "s8": 30, "sp": 29, "t0": 8, "t1": 9, "t2": 10, "t3": 11, "t4": 12,
    "t5": 13, "t6": 14, "t7": 15, "t8": 24, "t9": 25, "v0": 2, "v1": 3,
    "zero": 0,
}  # fmt: skip


@dataclass
class Token:
    """A scanned token: its type and its value (int, float or str)."""

    type: int
    value: int | float | str = 0


def _c_atoi(text: str) -> int:
    """C's ``atoi``: optional sign, leading digits, else 0."""
    digits: str = ""
    body: str = text.lstrip(" \t\n\v\f\r")
    sign: int = 1
    if body[:1] in ("-", "+"):
        sign = -1 if body[0] == "-" else 1
        body = body[1:]
    for char in body:
        if not char.isascii() or not char.isdigit():
            break
        digits += char
    return sign * int(digits) if digits else 0


def register_name_to_number(name: str) -> int:
    """The number of a register name without the ``$`` (-1 if it is none).

    ``"8"``/``"31"`` are numbers, ``"f4"`` an FP register, ``"t0"`` a name.
    Like the C engine, ``$f`` numbers are not range-checked.

    Example:
        >>> register_name_to_number("t0"), register_name_to_number("f12")
        (8, 12)
        >>> register_name_to_number("32"), register_name_to_number("x")
        (32, -1)
    """
    first: str = name[:1]
    second: str = name[1:2]
    if "0" <= first <= "9" and (
        second == "" or ("0" <= second <= "9" and len(name) == 2)
    ):
        return _c_atoi(name)
    if first == "f" and "0" <= second <= "9":
        return _c_atoi(name[1:])
    return REGISTER_NAMES.get(name, -1)


@dataclass
class _ScannerState:
    """Everything the scanner remembers about one input source."""

    source: BinaryIO | None = None
    line_buf: bytes = b""
    line_pos: int = 0
    at_eof: bool = False
    line_no: int = 1
    current_line: bytes = b""  # the line of the first token, for messages
    current_line_no: int = 0
    current_line_saved: bool = False
    force_id_next: bool = False
    tokens: list[Token | None] = field(default_factory=lambda: [None, None, None])


_state: _ScannerState = _ScannerState()
_stack: list[_ScannerState] = []

line_no: int = 1
"""The line number of the input (the parser and messages read it)."""

scan_value: int | float | str = 0
"""The value of the last token :func:`advance` returned that has one."""


def init(source: BinaryIO | None) -> None:
    """Start scanning source from its first line (C ``scanner_init``)."""
    global _state, line_no
    _state = _ScannerState(source=source)
    line_no = 1


def start_line() -> None:
    """Take a fresh snapshot of the next line for error messages."""
    _state.current_line_saved = False


def force_identifier() -> None:
    """Scan the next name as an identifier even if it is a keyword."""
    _state.force_id_next = True


def push_source(source: BinaryIO) -> None:
    """Save the scanner's state and start on source (REPL command lines)."""
    global _state, line_no
    if len(_stack) >= 8:
        raise console.fatal_error(
            "scanner_push_source: nested scanner stack overflow\n"
        )
    _state.line_no = line_no
    _stack.append(_state)
    init(source)


def pop_source() -> None:
    """Return to the state saved by :func:`push_source` (none: do nothing)."""
    global _state, line_no
    if _stack:
        _state = _stack.pop()
        line_no = _state.line_no


# --- characters --------------------------------------------------------------


def _peek_char() -> int:
    """The current byte, or 0 at the end of the line buffer."""
    pos: int = _state.line_pos
    return _state.line_buf[pos] if pos < len(_state.line_buf) else 0


def _peek_char2() -> int:
    """The byte after the current one, or 0."""
    pos: int = _state.line_pos + 1
    return _state.line_buf[pos] if pos < len(_state.line_buf) else 0


def _next_char() -> int:
    """Consume and return the current byte (0 at the end of the buffer)."""
    char: int = _peek_char()
    if _state.line_pos < len(_state.line_buf):
        _state.line_pos += 1
    return char


def _fill_line_buf() -> None:
    """Read the next line, as ``fgets`` into a 4096-byte buffer would."""
    if _state.at_eof:
        return
    line: bytes = _state.source.readline(LINE_BUF_MAX - 1) if _state.source else b""
    if not line:
        _state.at_eof = True
        _state.line_buf = b""
        _state.line_pos = 0
        return
    # strlen() stops at a NUL byte; whatever followed it on the line is lost.
    _state.line_buf = line.split(b"\0", 1)[0]
    _state.line_pos = 0


def _maybe_snapshot_line() -> None:
    """Remember the current line the first time a token is found on it."""
    if not _state.current_line_saved:
        _state.current_line = _state.line_buf.removesuffix(b"\n")
        _state.current_line_no = line_no
        _state.current_line_saved = True


# --- token scanners ------------------------------------------------------------


def _fp_literal_end() -> int:
    """Where a float literal ``D+.D+[eE[+-]D*]`` starting here ends (or here)."""
    buf: bytes = _state.line_buf
    start: int = _state.line_pos
    pos: int = start
    end: int = len(buf)
    if pos >= end or buf[pos] not in _DIGITS:
        return start
    while pos < end and buf[pos] in _DIGITS:
        pos += 1
    if pos >= end or buf[pos] != ord("."):  # `,` separates operands
        return start
    pos += 1
    if pos >= end or buf[pos] not in _DIGITS:
        return start
    while pos < end and buf[pos] in _DIGITS:
        pos += 1
    if pos < end and buf[pos] in b"eE":
        pos += 1
        if pos < end and buf[pos] in b"+-":
            pos += 1
        while pos < end and buf[pos] in _DIGITS:
            pos += 1
    return pos


def _scan_fp(sign: int, end: int) -> Token:
    """A float literal ending at end, through C's ``atof`` (63 bytes max)."""
    from . import machine

    text: bytes = _state.line_buf[_state.line_pos : end][:63]
    _state.line_pos = end
    return Token(TOK_FP, sign * machine.c_atof(text))


def _scan_int(sign: int) -> Token:
    """A decimal or ``0x`` hex integer, wrapped to 32 bits."""
    from . import machine

    value: int = 0
    if _peek_char() == ord("0") and _peek_char2() in b"xX":
        _next_char()
        _next_char()
        while _peek_char() in _HEX_DIGITS:
            value = ((value << 4) | int(chr(_next_char()), 16)) & 0xFFFFFFFF
    else:
        while _peek_char() in _DIGITS:
            value = (value * 10 + _next_char() - ord("0")) & 0xFFFFFFFF
    return Token(TOK_INT, machine.s32(sign * value))


def _hex_value(char: int) -> int:
    """The value of a hex digit byte."""
    return int(chr(char), 16)


def _scan_escape_char(text: bytes, pos: int) -> tuple[int, int]:
    """Decode the char-literal escape after a backslash at text[pos].

    Returns:
        tuple[int, int]: the character's value and the position after it.
    """
    first: int = text[pos] if pos < len(text) else 0
    pos += 1
    simple: dict[int, int] = {
        ord("a"): 7, ord("b"): 8, ord("f"): 12, ord("n"): 10, ord("r"): 13,
        ord("t"): 9, ord("\\"): 92, ord('"'): 34, ord("'"): 39,
    }  # fmt: skip
    if first in simple:
        return simple[first], pos
    if first in b"xX":
        high: int = text[pos] if pos < len(text) else 0
        low: int = text[pos + 1] if pos + 1 < len(text) else 0
        value: int = 0
        if high in _HEX_DIGITS:
            value = _hex_value(high) << 4
        else:
            console.error("Bad character in \\X construct in char literal\n")
        if low in _HEX_DIGITS:
            value |= _hex_value(low)
        else:
            console.error("Bad character in \\X construct in char literal\n")
        return value, pos + 2
    console.error(f"Bad escape character \\{chr(first)} in char literal\n")
    return 0, pos


def _decode_string(src: bytes) -> bytes:
    """The bytes a string literal's body stands for.

    Only ``\\n \\t \\"``, three-digit octal ``\\NNN`` (first digit 0-3) and
    ``\\XHH`` are escapes; any other backslash is kept as written.
    """
    out: bytearray = bytearray()
    i: int = 0
    end: int = len(src)
    while i < end:
        if src[i] != ord("\\") or i + 1 >= end:
            out.append(src[i])
            i += 1
            continue
        escape: int = src[i + 1]
        match chr(escape):
            case "n":
                out.append(10)
                i += 2
            case "t":
                out.append(9)
                i += 2
            case '"':
                out.append(34)
                i += 2
            case "0" | "1" | "2" | "3":
                if i + 3 >= end:
                    out.append(src[i])
                    i += 1
                    continue
                value: int = (escape - ord("0")) << 6
                for digit, shift in ((src[i + 2], 3), (src[i + 3], 0)):
                    if ord("0") <= digit <= ord("7"):
                        value += (digit - ord("0")) << shift
                    else:
                        console.error("Bad character in \\ooo construct in string\n")
                out.append(value & 0xFF)
                i += 4
            case "X":
                if i + 3 >= end:
                    out.append(src[i])
                    i += 1
                    continue
                value = 0
                if src[i + 2] in _HEX_DIGITS:
                    value = _hex_value(src[i + 2])
                else:
                    console.error("Bad character in \\X construct in string\n")
                value <<= 4
                if src[i + 3] in _HEX_DIGITS:
                    value |= _hex_value(src[i + 3])
                else:
                    console.error("Bad character in \\X construct in string\n")
                out.append(value & 0xFF)
                i += 4
            case _:
                out.append(src[i])  # keep the backslash; the next byte follows
                i += 1
    return bytes(out)


def _keyword_or_label(name: str, force_id: bool, allow_keyword: bool) -> Token:
    """A name: a keyword token, a constant's value, or an identifier."""
    if allow_keyword and not force_id:
        token: int | None = opcodes.keyword_token(name)
        allow_pseudo: bool = not options.bare_machine and options.accept_pseudo_insts
        if token is not None and (
            allow_pseudo or opcodes.ROW_BY_TOKEN[token].kind != opcodes.PSEUDO_OP
        ):
            return Token(token, token)
    label: symbol_table.Label | None = symbol_table.label_is_defined(name)
    if label is not None and label.const_flag:
        from . import machine

        return Token(TOK_INT, machine.s32(label.addr))
    return Token(TOK_ID, name)


def _scan_identifier(force_id: bool) -> Token:
    """An identifier or keyword (``[_.A-Za-z][_.A-Za-z0-9]*``, cut to 255)."""
    start: int = _state.line_pos
    while (
        _peek_char() == ord("_") or _peek_char() == ord(".") or _peek_char() in _ALNUM
    ):
        _next_char()
    name: str = console.from_bytes(_state.line_buf[start : _state.line_pos][:255])
    return _keyword_or_label(name, force_id, allow_keyword=True)


def _scan_register() -> Token:
    """A ``$`` register: a GPR, an FP register, or else a name."""
    _next_char()  # the $
    start: int = _state.line_pos
    while _peek_char() in b"_.$" or _peek_char() in _ALNUM:
        _next_char()
    name: str = console.from_bytes(_state.line_buf[start : _state.line_pos][:63])
    number: int = register_name_to_number(name)
    if number != -1 and name[:1] == "f" and name[1:2] != "p":
        return Token(TOK_FP_REG, number)
    if 0 <= number < 32:
        return Token(TOK_REG, number)
    return _keyword_or_label(name, force_id=True, allow_keyword=False)


def _scan_string() -> Token:
    """A ``"..."`` string literal (a missing closing quote ends at the line)."""
    _next_char()  # the opening "
    start: int = _state.line_pos
    while _peek_char() != 0 and _peek_char() != ord('"'):
        if _peek_char() == ord("\\") and _peek_char2() != 0:
            _next_char()
        _next_char()
    body: bytes = _state.line_buf[start : _state.line_pos]
    if _peek_char() == ord('"'):
        _next_char()
    return Token(TOK_STR, console.from_bytes(_decode_string(body)))


def _scan_char() -> Token:
    """A ``'c'`` character literal, as an integer token."""
    _next_char()  # the opening '
    value: int
    if _peek_char() == ord("\\"):
        _next_char()
        value, _state.line_pos = _scan_escape_char(_state.line_buf, _state.line_pos)
    else:
        value = _next_char()
    if _peek_char() == ord("'"):
        _next_char()
    return Token(TOK_INT, value)


def _scan_one_token() -> Token:
    """Skip white space and comments, then scan one token."""
    global line_no
    force_id: bool = _state.force_id_next
    _state.force_id_next = False
    while True:
        while _state.line_pos < len(_state.line_buf):
            char: int = _peek_char()
            if char in b" \t\r,":
                _next_char()
            elif char == ord("#"):
                while _peek_char() != 0 and _peek_char() != ord("\n"):
                    _next_char()
            else:
                break
        else:
            if _state.at_eof:
                return Token(TOK_EOF)
            _fill_line_buf()
            if _state.at_eof and not _state.line_buf:
                return Token(TOK_EOF)
            _state.current_line_saved = False
            continue
        break
    char = _peek_char()
    if char == ord("\n"):
        _next_char()
        line_no += 1
        return Token(TOK_NL)
    if char == ord(";"):
        _next_char()
        return Token(TOK_NL)
    _maybe_snapshot_line()
    match char:
        case _ if char == ord("-") and _peek_char2() in _DIGITS:
            _next_char()
            end: int = _fp_literal_end()
            return _scan_fp(-1, end) if end > _state.line_pos else _scan_int(-1)
        case _ if char in _DIGITS:
            end = _fp_literal_end()
            return _scan_fp(1, end) if end > _state.line_pos else _scan_int(1)
        case _ if char == ord("_") or char == ord(".") or char in _ALPHA:
            return _scan_identifier(force_id)
        case _ if char == ord("$"):
            return _scan_register()
        case _ if char == ord('"'):
            return _scan_string()
        case _ if char == ord("'"):
            return _scan_char()
        case _ if char == ord("?"):
            _next_char()
            return Token(TOK_ID, "?")
        case _ if char in _PUNCTUATION:
            _next_char()
            return Token(char, char)
        case _:
            _next_char()
            console.error(
                f"Unknown character in source: '{chr(char)}' (0x{char:02x})\n"
            )
            return Token(TOK_NL)  # recover as if the line ended


# --- the lookahead buffer ------------------------------------------------------


def _ensure_filled(count: int) -> None:
    """Scan until the first count lookahead slots hold tokens."""
    tokens: list[Token | None] = _state.tokens
    for i in range(count):
        if tokens[i] is None:
            tokens[i] = _scan_one_token()


def peek() -> int:
    """The type of the next token, without consuming it."""
    if _state.tokens[0] is None:
        _ensure_filled(1)
    token: Token | None = _state.tokens[0]
    assert token is not None
    return token.type


def peek2() -> int:
    """The type of the token after the next one."""
    _ensure_filled(2)
    token: Token | None = _state.tokens[1]
    assert token is not None
    return token.type


_VALUED: frozenset[int] = frozenset(
    (TOK_INT, TOK_ID, TOK_REG, TOK_FP_REG, TOK_STR, TOK_FP)
)


def advance() -> int:
    """Consume the next token and return its type; its value is in scan_value."""
    global scan_value
    _ensure_filled(1)
    token: Token | None = _state.tokens[0]
    assert token is not None
    if token.type in _VALUED:
        scan_value = token.value
    _state.tokens = [_state.tokens[1], _state.tokens[2], None]
    return token.type


# --- the current line, for messages and listings --------------------------------


def erroneous_line() -> str:
    """The current source line, indented, for a parse-error message."""
    return f"\t  {console.from_bytes(_state.current_line)}\n"


def source_line() -> str | None:
    """The current source line as ``"NNN: text"``, or None if there is none."""
    if not _state.current_line_saved:
        return None
    return f"{_state.current_line_no}: {console.from_bytes(_state.current_line)}"
