"""The opcode table and the lookups the C engine builds on it.

The repo keeps one list of every instruction, pseudo-instruction and
assembler directive, include/opcodes.tsv.  tools/gen_opcodes.py generates
the C engine's tables from it, and this port's: ``opcode_table.py``, which
this module turns into the lookups the rest of the port uses.  So the port
can never disagree with the C engine about a mnemonic, its operand shape, or
its encoding.

Token numbers matter: ``-print-ast`` prints them (``INST_R op=268``), so they
are numbered exactly as include/tokens.h numbers them: the eight structural
tokens from 256, then one per row in file order.
"""

from dataclasses import dataclass

from .opcode_table import DECODE, OpType
from .opcode_table import ROWS as _TABLE_ROWS

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
    """One row of the opcode table (include/opcodes.tsv).

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


ROWS: list[OpcodeRow] = [
    OpcodeRow(name, f"TOK_{token.name}", int(token), int(kind), encoding)
    for name, token, kind, encoding in _TABLE_ROWS
]

KIND: dict[str, int] = {tag.name: int(tag) for tag in OpType}
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


# The C engine's keyword_tbl, searched by name (the names are unique).
_KEYWORDS: dict[str, int] = {row.name: row.token for row in ROWS}

# The C engine's a_opcode_tbl: one token per encoding (opcodes.tsv's decode
# column settles the encodings several rows share).
_BY_ENCODING: dict[int, int] = {encoding: int(token) for encoding, token in DECODE}


def keyword_token(name: str) -> int | None:
    """The token of a keyword (mnemonic or directive), or None.

    Example:
        >>> keyword_token("add") == TOKEN["ADD_OPCODE"], keyword_token("foo")
        (True, None)
    """
    return _KEYWORDS.get(name)


def token_for_encoding(a_opcode: int) -> int | None:
    """The token whose fixed bits are a_opcode, or None (C a_opcode_tbl).

    Example:
        >>> name_of(token_for_encoding(0x4600000f) or 0)
        'floor.w.s'
    """
    return _BY_ENCODING.get(a_opcode)
