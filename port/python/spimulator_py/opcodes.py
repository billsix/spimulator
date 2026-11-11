"""The opcode table, read from the C engine's include/opcodes.h at import time.

The C engine keeps one master list of every instruction, pseudo-instruction
and assembler directive in include/opcodes.h, as rows of an "X-macro"::

    OP("add", TOK_ADD_OPCODE, RD_RS_RT_INST, 0x00000020)

Each C file that needs a table defines ``OP`` to pick the columns it wants
and includes the file.  This module does the same thing in Python: it reads
the rows with a regular expression, so the port can never disagree with the
C engine about a mnemonic, its operand shape, or its encoding.

Token numbers matter: ``-print-ast`` prints them (``INST_R op=268``), so they
are numbered exactly as include/tokens.h numbers them: the eight structural
tokens from 256, then one per opcodes.h row in file order.
"""

import pathlib
import re
from dataclasses import dataclass

INCLUDE_DIR: pathlib.Path = pathlib.Path(__file__).resolve().parents[3] / "include"

# The structural tokens of include/tokens.h.  The scanner returns a single
# punctuation character ('+', '(', ...) as its own character code, which is
# below 256, so these never collide with it.
TOK_EOF: int = 256
TOK_NL: int = 257
TOK_INT: int = 258
TOK_ID: int = 259
TOK_REG: int = 260
TOK_FP_REG: int = 261
TOK_STR: int = 262
TOK_FP: int = 263


@dataclass(frozen=True)
class OpcodeRow:
    """One row of opcodes.h.

    Attributes:
        name: the mnemonic as written in assembly ("add", ".word", "li").
        symbol: the C token name ("TOK_ADD_OPCODE").
        token: the token number the scanner returns for the mnemonic.
        kind: the operand-shape tag from opcode-types.h (e.g. 26 for
            RD_RS_RT_INST); ASM_DIR for directives, PSEUDO_OP for
            pseudo-instructions.
        encoding: the instruction's fixed bits as a signed 32-bit value, or
            -1 for a directive or pseudo-instruction.
    """

    name: str
    symbol: str
    token: int
    kind: int
    encoding: int


def _read_op_types() -> dict[str, int]:
    """The ``NAME = value`` pairs of the op_type enum in opcode-types.h."""
    text: str = (INCLUDE_DIR / "opcode-types.h").read_text()
    return {name: int(value) for name, value in re.findall(r"(\w+) = (\d+),", text)}


def _read_rows() -> list[OpcodeRow]:
    """Every OP(...) row of opcodes.h, in file order."""
    text: str = (INCLUDE_DIR / "opcodes.h").read_text()
    # Drop comments first: the header's prose shows an example OP() row.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    types: dict[str, int] = _read_op_types()
    pattern: re.Pattern[str] = re.compile(
        r'OP\(\s*"([^"]+)",\s*(\w+),\s*(\w+),\s*(-?\w+)\s*\)'
    )
    rows: list[OpcodeRow] = []
    for number, (name, symbol, kind, encoding) in enumerate(
        pattern.findall(text), start=TOK_FP + 1
    ):
        bits: int = int(encoding, 0) & 0xFFFFFFFF
        signed: int = bits - (1 << 32) if bits & 0x80000000 else bits
        rows.append(OpcodeRow(name, symbol, number, types[kind], signed))
    return rows


ROWS: list[OpcodeRow] = _read_rows()

KIND: dict[str, int] = _read_op_types()
"""Operand-shape tags by name, e.g. ``KIND["RD_RS_RT_INST"] == 26``."""

ASM_DIR: int = KIND["ASM_DIR"]
PSEUDO_OP: int = KIND["PSEUDO_OP"]

TOKEN: dict[str, int] = {row.symbol.removeprefix("TOK_"): row.token for row in ROWS}
"""Token numbers by C name without the ``TOK_`` prefix:
``TOKEN["ADD_OPCODE"]`` is the token of ``add``."""

ROW_BY_TOKEN: dict[int, OpcodeRow] = {row.token: row for row in ROWS}
"""The row of each token (the C engine's name_tbl, searched by token)."""


def name_of(token: int) -> str:
    """The mnemonic of an opcode token, or "<unknown>" (C ``op_token_name``).

    Example:
        >>> name_of(TOKEN["ADDU_OPCODE"])
        'addu'
    """
    row: OpcodeRow | None = ROW_BY_TOKEN.get(token)
    return row.name if row is not None else "<unknown>"


def _bsearch[T: (str, int)](table: list[tuple[T, int]], key: T) -> int | None:
    """glibc's ``bsearch`` over a table sorted on its first column.

    When several rows share a key, which one is found depends on the order
    the halving visits them, so this is written exactly as glibc's loop:
    the C engine's disassembler picks one of two instructions that share an
    encoding this way (``prefx`` over ``floor.w.s``, for example).

    Returns:
        int | None: the second column of the row found, or None.
    """
    low: int = 0
    high: int = len(table)
    while low < high:
        middle: int = (low + high) // 2
        probe: T = table[middle][0]
        if key < probe:
            high = middle
        elif key > probe:
            low = middle + 1
        else:
            return table[middle][1]
    return None


# The C engine's keyword_tbl: searched by name with bsearch over the rows in
# file order (opcodes.h is kept sorted).
_KEYWORDS: list[tuple[str, int]] = [(row.name, row.token) for row in ROWS]

# The C engine's a_opcode_tbl: (encoding, token) sorted by encoding with
# glibc's qsort, which is a stable merge sort here, as Python's sort is.
_BY_ENCODING: list[tuple[int, int]] = sorted(
    ((row.encoding, row.token) for row in ROWS), key=lambda pair: pair[0]
)


def keyword_token(name: str) -> int | None:
    """The token of a keyword (mnemonic or directive), or None.

    Example:
        >>> keyword_token("add") == TOKEN["ADD_OPCODE"], keyword_token("foo")
        (True, None)
    """
    return _bsearch(_KEYWORDS, name)


def token_for_encoding(a_opcode: int) -> int | None:
    """The token whose fixed bits are a_opcode, or None (C a_opcode_tbl).

    Example:
        >>> name_of(token_for_encoding(0x4600000f) or 0)
        'prefx'
    """
    return _bsearch(_BY_ENCODING, a_opcode)
