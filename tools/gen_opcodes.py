#!/usr/bin/env python3
"""Generate every opcode table in the repo from include/opcodes.tsv.

``include/opcodes.tsv`` lists every MIPS instruction, assembler
pseudo-instruction, assembler directive and REPL command spimulator knows,
and ``include/opcode-types.tsv`` the operand-shape tags its rows carry. This
tool writes, from those two files, every table that needs them; the list is
``OUTPUTS`` below. The generated files are committed, so building needs no
generator; the image build and a meson test run this tool with ``--check``,
which fails when a committed file differs from what the data files produce.

Usage::

    tools/gen_opcodes.py           # rewrite every generated file
    tools/gen_opcodes.py --check   # exit 1 if any is out of date

Developers run it through ``make generate``. C output is formatted with
clang-format and the repo's ``.clang-format``; the other languages are
written in their final form.
"""

import argparse
import dataclasses
import difflib
import itertools
import json
import pathlib
import subprocess
import sys
from collections.abc import Callable, Iterator, Mapping, Sequence

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[1]
OPCODES_TSV: pathlib.Path = REPO / "include" / "opcodes.tsv"
TYPES_TSV: pathlib.Path = REPO / "include" / "opcode-types.tsv"
# tokens.h numbers eight structural tokens from 256, then one per row.
FIRST_KEYWORD_TOKEN: int = 264
# Each comment in opcode-types.h is padded so the closing */ lines up.
TYPE_COMMENT_WIDTH: int = 26
DIFF_LINES_SHOWN: int = 40


@dataclasses.dataclass(frozen=True, slots=True)
class Opcode:
    """One row of opcodes.tsv.

    Attributes:
        name: the mnemonic as written in assembly (``add``, ``.word``).
        symbol: the C token name (``TOK_ADD_OPCODE``).
        token: the token number, from the row's position.
        type_name: the operand-shape tag (``RD_RS_RT_INST``).
        encoding_text: the fixed bits as the C source writes them
            (``0x00000020``, ``-1``).
        decodes: True on the row a shared encoding decodes to.
        comment: the row's note, or an empty string.
        group: the number of the blank-line-separated group it is in.
    """

    name: str
    symbol: str
    token: int
    type_name: str
    encoding_text: str
    decodes: bool
    comment: str
    group: int

    @property
    def encoding(self) -> int:
        """The fixed bits as a signed 32-bit value (-1 for no encoding).

        Returns:
            int: the value an ``int32_t`` holding the encoding has.
        """
        bits: int = int(self.encoding_text, 0) & 0xFFFFFFFF
        return bits - (1 << 32) if bits & 0x80000000 else bits


@dataclasses.dataclass(frozen=True, slots=True)
class OpType:
    """One row of opcode-types.tsv.

    Attributes:
        name: the tag (``RD_RS_RT_INST``).
        value: its number.
        comment: an example of the operand list, or an empty string.
        group: the number of the blank-line-separated group it is in.
    """

    name: str
    value: int
    comment: str
    group: int


@dataclasses.dataclass(frozen=True, slots=True)
class Table:
    """Both data files, read and checked.

    Attributes:
        opcodes: every row of opcodes.tsv, in file order.
        types: every row of opcode-types.tsv, in file order.
    """

    opcodes: tuple[Opcode, ...]
    types: tuple[OpType, ...]

    def decode_rows(self) -> list[Opcode]:
        """One row per distinct encoding, sorted by encoding: the decode table.

        Where several rows share an encoding, the one marked in the decode
        column is chosen.

        Returns:
            list[Opcode]: the rows a binary word can decode to.
        """
        return sorted(
            (
                row
                for row in self.opcodes
                if row.decodes
                or sum(other.encoding == row.encoding for other in self.opcodes) == 1
            ),
            key=lambda row: row.encoding,
        )


def data_lines(path: pathlib.Path) -> Iterator[tuple[int, list[str]]]:
    """The rows of a data file as (group, fields), skipping ``#`` comment lines.

    Args:
        path: a ``.tsv`` data file.

    Yields:
        tuple[int, list[str]]: the group number and the tab-separated fields
        of each row; an empty line starts the next group.
    """
    group: int = 0
    for line in path.read_text().split("\n"):
        if line.startswith("#"):
            continue
        elif not line:
            group += 1
            continue
        else:
            yield group, line.split("\t")


def read_table() -> Table:
    """Read both data files and check the rules the generated code relies on.

    Returns:
        Table: the checked rows.

    Raises:
        ValueError: when a row is malformed, the names are not in strictly
            increasing byte order (the scanner searches them with bsearch),
            a name, token or tag repeats, a row names an unknown tag, or a
            shared encoding does not have exactly one row marked to decode.
    """
    types: list[OpType] = []
    for group, fields in data_lines(TYPES_TSV):
        match fields:
            case [name, value, comment]:
                types.append(OpType(name, int(value), comment, group))
            case _:
                raise ValueError(f"{TYPES_TSV.name}: bad row {fields!r}")
    known: set[str] = {t.name for t in types}
    opcodes: list[Opcode] = []
    for group, fields in data_lines(OPCODES_TSV):
        match fields:
            case [name, symbol, type_name, encoding_text, decode, comment] if (
                decode in ("", "*")
            ):
                if type_name not in known:
                    raise ValueError(
                        f"{OPCODES_TSV.name}: {name}: unknown type {type_name}"
                    )
                opcodes.append(
                    Opcode(
                        name,
                        symbol,
                        FIRST_KEYWORD_TOKEN + len(opcodes),
                        type_name,
                        encoding_text,
                        decode == "*",
                        comment,
                        group,
                    )
                )
            case _:
                raise ValueError(f"{OPCODES_TSV.name}: bad row {fields!r}")
    names: list[bytes] = [row.name.encode() for row in opcodes]
    if any(a >= b for a, b in itertools.pairwise(names)):
        raise ValueError(
            f"{OPCODES_TSV.name}: names are not in strictly increasing order"
        )
    elif len({row.symbol for row in opcodes}) != len(opcodes) or len(known) != len(
        types
    ):
        raise ValueError("a token or a type tag is listed twice")
    else:
        for encoding, rows in itertools.groupby(
            sorted(opcodes, key=lambda row: row.encoding), key=lambda row: row.encoding
        ):
            group_rows: list[Opcode] = list(rows)
            marked: int = sum(row.decodes for row in group_rows)
            if (len(group_rows) > 1 and marked != 1) or (
                len(group_rows) == 1 and marked
            ):
                raise ValueError(
                    f"{OPCODES_TSV.name}: encoding {encoding:#x} needs exactly one row marked"
                    f" '*' in the decode column when rows share it, and none otherwise"
                )
        return Table(tuple(opcodes), tuple(types))


def clang_format(text: str, path: str) -> str:
    """Format C text with clang-format and the repo's style, as if it were at path.

    Args:
        text: the C source.
        path: the repo-relative path it will be written to.

    Returns:
        str: the formatted source.
    """
    done: subprocess.CompletedProcess[str] = subprocess.run(
        ["clang-format", f"--assume-filename={path}"],
        input=text,
        capture_output=True,
        text=True,
        cwd=REPO,
        check=True,
    )
    return done.stdout


def grouped[T: (Opcode, OpType)](
    rows: Sequence[T], render: Callable[[T], str]
) -> list[str]:
    """Render rows one per line, with a blank line between groups.

    Args:
        rows: the rows, in file order.
        render: the text of one row.

    Returns:
        list[str]: the lines.
    """
    return [
        line
        for index, row in enumerate(rows)
        for line in ([""] if index and row.group != rows[index - 1].group else [])
        + [render(row)]
    ]


OPCODE_TYPES_H_PREAMBLE: str = """/* SPIM S20 MIPS simulator.
   Operand-shape type tags of the opcode table.
   GENERATED by tools/gen_opcodes.py from include/opcode-types.tsv -- do
   not edit; change the .tsv and run `make generate`.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef OP_TYPES_H
#define OP_TYPES_H

#include <stdint.h>

/* Each row of include/opcodes.tsv carries one of these tags in its type
   column.  The tables generated from it (opcode-tables.h) key
   per-instruction handling on the value.  uint8_t-backed — values fit in
   0..42.

   Naming scheme: each tag spells the operand list exactly as the
   student writes it in assembly, left to right, using the MIPS
   register-field names (rd/rs/rt, fd/fs/ft/fr for FP):

       RD_RS_RT_INST     ->  add   rd, rs, rt
       RT_RS_IMM_INST    ->  addiu rt, rs, imm16
       RT_ADDR_INST      ->  lw    rt, address
       RD_RT_SHAMT_INST  ->  sll   rd, rt, shamt
       BRANCH_RS_RT_LABEL_INST -> beq rs, rt, label

   COPREG is a coprocessor register operand (mfc0/mtc1-family).
   MOVC/FP_MOVC are the conditional moves on the FP condition code;
   J covers `<op> label` or `<op> reg` jumps; NOARG covers `<op>`
   alone (syscall, nop) or `<op> imm` (break N). */
typedef enum op_type : uint8_t {
"""


def opcode_types_h(table: Table) -> str:
    """``include/opcode-types.h``: the C ``op_type`` enum.

    Args:
        table: the data.

    Returns:
        str: the file's text.
    """

    def entry_line(entry: OpType) -> str:
        """One enumerator, with its padded comment.

        Args:
            entry: the tag.

        Returns:
            str: the line, before formatting.
        """
        comment: str = (
            f" /* {entry.comment.ljust(TYPE_COMMENT_WIDTH)} */" if entry.comment else ""
        )
        return f"  {entry.name} = {entry.value},{comment}"

    body: str = "\n".join(grouped(table.types, entry_line))
    return clang_format(
        OPCODE_TYPES_H_PREAMBLE + body + "\n} op_type;\n\n#endif /* OP_TYPES_H */\n",
        "include/opcode-types.h",
    )


def haskell_encoding(row: Opcode) -> str:
    """A row's encoding as a Haskell ``Int32`` expression.

    The C source writes encodings as unsigned hex, which wrap as an int32_t.

    Args:
        row: the row.

    Returns:
        str: ``(-1)`` or ``(fromIntegral (0x... :: Integer))``.
    """
    return (
        "(-1)"
        if row.encoding_text == "-1"
        else f"(fromIntegral ({row.encoding_text} :: Integer))"
    )


def haskell_opcodes(table: Table) -> str:
    """``port/haskell/src/Opcodes.hs``: the Haskell port's opcode table.

    Args:
        table: the data.

    Returns:
        str: the module's text.
    """
    rows: tuple[Opcode, ...] = table.opcodes
    decode: list[Opcode] = table.decode_rows()

    def row_text(row: Opcode) -> str:
        """One ``OpRow`` value.

        Args:
            row: the row.

        Returns:
            str: the Haskell expression.
        """
        return (
            f'OpRow "{row.name}" {row.symbol} {row.type_name} {haskell_encoding(row)}'
        )

    lines: list[str] = [
        "-- | GENERATED by tools/gen_opcodes.py from include/opcodes.tsv -- do not edit.",
        "--",
        "-- Regenerate (from the repo root): make generate",
        "--",
        "-- Every keyword the C engine knows -- instructions, pseudo-instructions",
        "-- and assembler directives -- in the order of opcodes.tsv.  The C engine",
        "-- numbers these tokens from 264 upward in this order (tokens.h); the",
        "-- numbers appear in the -print-ast output, so 'opcodeToken' keeps them.",
        "module Opcodes",
        "  ( Opcode (..)",
        "  , OpRow (..)",
        "  , opcodeRows",
        "  , opcodeDecodeRows",
        "  , opcodeToken",
        "  , firstOpcodeToken",
        "  ) where",
        "",
        "import Data.Int (Int32)",
        "import OpcodeTypes (OpType (..))",
        "",
        "-- | One constructor per row of opcodes.tsv, named like the C token.",
        "data Opcode",
        *[("  = " if i == 0 else "  | ") + row.symbol for i, row in enumerate(rows)],
        "  deriving (Eq, Ord, Show, Enum, Bounded)",
        "",
        "-- | A row of the table: the mnemonic, its token, its operand shape,",
        "-- and its binary encoding (-1 for directives and pseudo-ops).",
        "data OpRow = OpRow",
        "  { rowName :: String",
        "  , rowOpcode :: Opcode",
        "  , rowType :: OpType",
        "  , rowEncoding :: Int32",
        "  }",
        "",
        "-- | The token number of the first row (tokens.h: 256 + 8 structural tokens).",
        "firstOpcodeToken :: Int",
        f"firstOpcodeToken = {FIRST_KEYWORD_TOKEN}",
        "",
        "-- | The C engine's integer token for an opcode.",
        "opcodeToken :: Opcode -> Int",
        "opcodeToken op = firstOpcodeToken + fromEnum op",
        "",
        "-- | All rows, in the order of opcodes.tsv (which is alphabetical).",
        "opcodeRows :: [OpRow]",
        "opcodeRows =",
        *[("  [ " if i == 0 else "  , ") + row_text(row) for i, row in enumerate(rows)],
        "  ]",
        "",
        "-- | The decode table: one row per binary encoding, sorted by encoding as",
        "-- a signed 32-bit value.  Where several rows share an encoding, it holds",
        "-- the one opcodes.tsv's decode column marks.",
        "opcodeDecodeRows :: [OpRow]",
        "opcodeDecodeRows =",
        *[
            ("  [ " if i == 0 else "  , ") + row_text(row)
            for i, row in enumerate(decode)
        ],
        "  ]",
    ]
    return "\n".join(lines) + "\n"


def haskell_opcode_types(table: Table) -> str:
    """``port/haskell/src/OpcodeTypes.hs``: the operand-shape tags as a Haskell type.

    Args:
        table: the data.

    Returns:
        str: the module's text.
    """
    lines: list[str] = [
        "-- | GENERATED by tools/gen_opcodes.py from include/opcode-types.tsv -- do not edit.",
        "--",
        "-- Operand-shape tags for the rows of the opcode table (C: include/opcode-types.h).",
        "-- Each tag spells the operand list as the student writes it, left to right:",
        "-- 'RD_RS_RT_INST' is @add rd, rs, rt@, 'RT_ADDR_INST' is @lw rt, address@.",
        "-- The constructor names are the C enumerator names, so the two sources can",
        "-- be read side by side.",
        "module OpcodeTypes (OpType (..)) where",
        "",
        "-- | The operand shape of one keyword.",
        "data OpType",
        *[
            ("  = " if i == 0 else "  | ")
            + entry.name
            + (f" -- ^ @{entry.comment}@" if entry.comment else "")
            for i, entry in enumerate(table.types)
        ],
        "  deriving (Eq, Show)",
    ]
    return "\n".join(lines) + "\n"


def rust_variant(name: str) -> str:
    """A C enumerator name as a Rust enum variant: ``RD_RS_RT_INST`` is ``RdRsRtInst``.

    Args:
        name: the C name.

    Returns:
        str: the CamelCase name.
    """
    return "".join(part.capitalize() for part in name.split("_"))


def rust_opcodes_table(table: Table) -> str:
    """``port/rust/src/opcodes_table.rs``: the Rust port's opcode table.

    Args:
        table: the data.

    Returns:
        str: the module's text.
    """
    rows: tuple[Opcode, ...] = table.opcodes
    decode: list[Opcode] = table.decode_rows()
    lines: list[str] = [
        "// GENERATED by tools/gen_opcodes.py from include/opcodes.tsv.",
        "// Do not edit by hand: change opcodes.tsv and run `make generate`.",
        "",
        "//! The opcode table: every instruction, pseudo-instruction and",
        "//! directive the assembler knows, in the order of `include/opcodes.tsv`.",
        "",
        "use crate::opcode_types::OpType;",
        "use crate::opcodes::OpcodeRow;",
        "",
        "/// Every row of `include/opcodes.tsv`: mnemonic, token, operand shape and",
        "/// binary encoding (`-1` for directives and pseudo-instructions).  The",
        "/// token of row `i` is `FIRST_KEYWORD_TOKEN + i`, as in `tokens.h`.",
        "#[rustfmt::skip]",
        f"pub const OPCODE_ROWS: [OpcodeRow; {len(rows)}] = [",
        *[
            f'    OpcodeRow {{ name: "{row.name}", token: {row.token}, op_type: OpType::'
            f"{rust_variant(row.type_name)}, encoding: {row.encoding} }},"
            for row in rows
        ],
        "];",
        "",
        *[f"pub const {row.symbol}: i32 = {row.token};" for row in rows],
        "",
        "/// Binary encoding -> token, one entry per encoding, sorted by encoding.",
        "/// Where several rows share an encoding, the entry is the row",
        "/// `include/opcodes.tsv`'s decode column marks.",
        "#[rustfmt::skip]",
        f"pub const DECODE_TABLE: [(i32, i32); {len(decode)}] = [",
        *[f"    ({row.encoding}, {row.token}), // {row.name}" for row in decode],
        "];",
    ]
    return "\n".join(lines) + "\n"


def rust_opcode_types(table: Table) -> str:
    """``port/rust/src/opcode_types.rs``: the operand-shape tags as a Rust enum.

    Args:
        table: the data.

    Returns:
        str: the module's text.
    """
    lines: list[str] = [
        "// GENERATED by tools/gen_opcodes.py from include/opcode-types.tsv.",
        "// Do not edit by hand: change opcode-types.tsv and run `make generate`.",
        "",
        "//! Operand-shape tags of the opcode table (`include/opcode-types.h`).",
        "//!",
        "//! Every row of the opcode table carries one of these tags.  The parser",
        "//! uses it to know which operands to read, the encoder and decoder to know",
        "//! where each field lives in the 32-bit word, and the disassembler to know",
        "//! how to print the instruction.  Each name spells the operand list as the",
        "//! student writes it: `RdRsRtInst` is `add rd, rs, rt`.",
        "",
        "/// The operand shape of an opcode-table row.",
        "#[derive(Clone, Copy, Debug, PartialEq, Eq)]",
        "pub enum OpType {",
        *[
            line
            for entry in table.types
            for line in ([f"    /// `{entry.comment}`."] if entry.comment else [])
            + [f"    {rust_variant(entry.name)},"]
        ],
        "}",
    ]
    return "\n".join(lines) + "\n"


def python_opcode_table(table: Table) -> str:
    """``port/python/spimulator_py/opcode_table.py``: the Python port's opcode table.

    Args:
        table: the data.

    Returns:
        str: the module's text, as ruff formats it.
    """
    lines: list[str] = [
        '"""The opcode table: GENERATED by tools/gen_opcodes.py -- do not edit.',
        "",
        "Generated from include/opcodes.tsv and include/opcode-types.tsv; regenerate",
        "from the repo root with ``make generate``. ``Tok`` numbers the keyword tokens",
        "as include/tokens.h does (the eight structural tokens come first, from 256).",
        '"""',
        "",
        "import enum",
        "",
        "",
        "class OpType(enum.IntEnum):",
        '    """The operand-shape tags (C: include/opcode-types.h)."""',
        "",
        *[
            f"    {entry.name} = {entry.value}"
            + (f"  # {entry.comment}" if entry.comment else "")
            for entry in table.types
        ],
        "",
        "",
        "class Tok(enum.IntEnum):",
        '    """The keyword tokens, named like the C tokens without ``TOK_``."""',
        "",
        *[
            f"    {row.symbol.removeprefix('TOK_')} = {row.token}"
            for row in table.opcodes
        ],
        "",
        "",
        "ROWS: tuple[tuple[str, Tok, OpType, int], ...] = (",
        *[
            f'    ("{row.name}", Tok.{row.symbol.removeprefix("TOK_")}, OpType.{row.type_name}, {row.encoding}),'
            for row in table.opcodes
        ],
        ")",
        '"""Every row, in file order: mnemonic, token, operand shape, encoding (-1: none)."""',
        "",
        "DECODE: tuple[tuple[int, Tok], ...] = (",
        *[
            f"    ({row.encoding}, Tok.{row.symbol.removeprefix('TOK_')}),"
            for row in table.decode_rows()
        ],
        ")",
        '"""Encoding -> token, one entry per encoding, sorted; ties as the decode column marks."""',
    ]
    return "\n".join(lines) + "\n"


def tree_sitter_keyword_lists(table: Table) -> str:
    """``tree-sitter/scripts/keyword-lists.js``: the grammar's keywords, by category.

    Each operand-shape tag is a category (``RD_RS_RT_INST`` becomes
    ``RD_RS_RT``, ``PSEUDO_OP`` becomes ``PSEUDO``), listed in name order with
    its mnemonics in name order; ``grammar.js`` builds its keyword rules from
    them.

    Args:
        table: the data.

    Returns:
        str: the module's text.
    """

    def category(type_name: str) -> str:
        """The grammar's name for an operand-shape tag.

        Args:
            type_name: the tag.

        Returns:
            str: the category name.
        """
        match type_name:
            case "PSEUDO_OP":
                return "PSEUDO"
            case name if name.endswith("_TYPE_INST"):
                return name.removesuffix("_TYPE_INST")
            case name:
                return name.removesuffix("_INST")

    used: list[str] = sorted({row.type_name for row in table.opcodes})
    lines: list[str] = [
        "/* Generated from include/opcodes.tsv by tools/gen_opcodes.py",
        " * (make generate) — do not edit by hand. */",
        "",
        "module.exports = {",
        *[
            f"  {category(type_name)}: ["
            + ", ".join(
                json.dumps(row.name)
                for row in sorted(table.opcodes, key=lambda r: r.name)
                if row.type_name == type_name
            )
            + "],"
            for type_name in used
        ],
        "};",
    ]
    return "\n".join(lines) + "\n"


C_LICENSE: str = """   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */
"""


def tokens_h(table: Table) -> str:
    """``include/tokens.h``: the C engine's ``TOK_*`` token enum.

    Args:
        table: the data.

    Returns:
        str: the file's text.
    """
    text: str = (
        "/* SPIM S20 MIPS simulator.\n"
        "   TOK_* token enum shared by the scanner, parser, and runtime.\n"
        "   GENERATED by tools/gen_opcodes.py from include/opcodes.tsv -- do not\n"
        "   edit; change the .tsv and run `make generate`.\n"
        "\n"
        "   Eight structural/literal tokens come first, from 256, so they don't\n"
        "   collide with single-character punctuation tokens like '+', ',', '('\n"
        "   which the scanner returns as their own ASCII value.  Then one keyword\n"
        "   token per row of opcodes.tsv, in its order.  The numbers appear in\n"
        "   -print-ast output (INST_R op=268), and the ports number their tokens\n"
        "   the same way.\n" + C_LICENSE + "\n"
        "#ifndef TOKENS_H\n#define TOKENS_H\n\n#include <stdint.h>\n\nenum : int32_t {\n"
        "  /* Structural / literal tokens. */\n"
        "  TOK_EOF = 256,\n  TOK_NL,\n  TOK_INT,\n  TOK_ID,\n  TOK_REG,\n  TOK_FP_REG,\n  TOK_STR,\n  TOK_FP,\n\n"
        "  /* One keyword token per row of opcodes.tsv. */\n"
        + "".join(f"  {row.symbol},\n" for row in table.opcodes)
        + "};\n\n#endif /* TOKENS_H */\n"
    )
    return clang_format(text, "include/tokens.h")


def opcode_tables_h(table: Table) -> str:
    """``include/opcode-tables.h``: the declarations of the C engine's opcode tables.

    Args:
        table: the data.

    Returns:
        str: the file's text.
    """
    text: str = (
        "/* SPIM S20 MIPS simulator.\n"
        "   The opcode tables: every instruction, pseudo-instruction, directive\n"
        "   and REPL command, as the scanner, parser, assembler and disassembler\n"
        "   look them up (with map_string_to_name_val_val and\n"
        "   map_int_to_name_val_val, which bsearch them).\n"
        "   GENERATED by tools/gen_opcodes.py from include/opcodes.tsv -- do not\n"
        "   edit; change the .tsv and run `make generate`.\n" + C_LICENSE + "\n"
        "#ifndef OPCODE_TABLES_H\n#define OPCODE_TABLES_H\n\n"
        '#include "spim-utils.h" /* name_val_val */\n\n'
        "/* The number of rows of opcodes.tsv. */\n"
        f"constexpr int OPCODE_COUNT = {len(table.opcodes)};\n\n"
        "/* The number of distinct encodings: the rows of opcode_decode_tbl. */\n"
        f"constexpr int OPCODE_DECODE_COUNT = {len(table.decode_rows())};\n\n"
        "/* {name, token, op_type} per row, in opcodes.tsv's order, which is sorted\n"
        "   both by name (the scanner's keyword search) and by token (a token's\n"
        "   mnemonic and operand shape). */\n"
        "extern const name_val_val opcode_tbl[OPCODE_COUNT];\n\n"
        "/* {name, token, encoding}: the fixed bits the assembler emits for a\n"
        "   token, sorted by token. */\n"
        "extern const name_val_val opcode_encoding_tbl[OPCODE_COUNT];\n\n"
        "/* {name, encoding, token}: the token a binary encoding decodes to, one\n"
        "   row per encoding, sorted by encoding.  Where several rows share an\n"
        "   encoding, opcodes.tsv's decode column picks the row. */\n"
        "extern const name_val_val opcode_decode_tbl[OPCODE_DECODE_COUNT];\n\n"
        "#endif /* OPCODE_TABLES_H */\n"
    )
    return clang_format(text, "include/opcode-tables.h")


def opcode_tables_c(table: Table) -> str:
    """``src/opcode-tables.c``: the C engine's opcode tables.

    Args:
        table: the data.

    Returns:
        str: the file's text.
    """

    def c_encoding(row: Opcode) -> str:
        """A row's encoding as a C ``int`` expression, as the C source writes it.

        Args:
            row: the row.

        Returns:
            str: ``-1`` or ``(int)0x...``.
        """
        return "-1" if row.encoding_text == "-1" else f"(int){row.encoding_text}"

    text: str = (
        "/* SPIM S20 MIPS simulator.\n"
        "   The opcode tables (declared and described in opcode-tables.h).\n"
        "   GENERATED by tools/gen_opcodes.py from include/opcodes.tsv -- do not\n"
        "   edit; change the .tsv and run `make generate`.\n" + C_LICENSE + "\n"
        '#include "opcode-tables.h"\n#include "opcode-types.h"\n#include "tokens.h"\n\n'
        "const name_val_val opcode_tbl[OPCODE_COUNT] = {\n"
        + "".join(
            f'    {{"{row.name}", {row.symbol}, {row.type_name}}},\n'
            for row in table.opcodes
        )
        + "};\n\nconst name_val_val opcode_encoding_tbl[OPCODE_COUNT] = {\n"
        + "".join(
            f'    {{"{row.name}", {row.symbol}, {c_encoding(row)}}},\n'
            for row in table.opcodes
        )
        + "};\n\nconst name_val_val opcode_decode_tbl[OPCODE_DECODE_COUNT] = {\n"
        + "".join(
            f'    {{"{row.name}", {c_encoding(row)}, {row.symbol}}},\n'
            for row in table.decode_rows()
        )
        + "};\n"
    )
    return clang_format(text, "src/opcode-tables.c")


OUTPUTS: Mapping[str, Callable[[Table], str]] = {
    "include/tokens.h": tokens_h,
    "include/opcode-types.h": opcode_types_h,
    "include/opcode-tables.h": opcode_tables_h,
    "src/opcode-tables.c": opcode_tables_c,
    "port/haskell/src/Opcodes.hs": haskell_opcodes,
    "port/haskell/src/OpcodeTypes.hs": haskell_opcode_types,
    "port/rust/src/opcodes_table.rs": rust_opcodes_table,
    "port/rust/src/opcode_types.rs": rust_opcode_types,
    "port/python/spimulator_py/opcode_table.py": python_opcode_table,
    "tree-sitter/scripts/keyword-lists.js": tree_sitter_keyword_lists,
}


def main() -> int:
    """Write every generated file, or with --check report the stale ones.

    Returns:
        int: 0, or 1 when --check found a file that differs.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0]
    )
    parser.add_argument(
        "--check", action="store_true", help="compare instead of writing"
    )
    options: argparse.Namespace = parser.parse_args()
    table: Table = read_table()
    stale: list[str] = []
    for path, render in OUTPUTS.items():
        wanted: str = render(table)
        target: pathlib.Path = REPO / path
        current: str = target.read_text() if target.exists() else ""
        if wanted == current:
            continue
        elif not options.check:
            target.write_text(wanted)
            print(f"wrote {path}")
            continue
        else:
            stale.append(path)
            diff: list[str] = list(
                difflib.unified_diff(
                    current.splitlines(),
                    wanted.splitlines(),
                    f"{path} (committed)",
                    f"{path} (generated)",
                    lineterm="",
                )
            )
            print(f"stale: {path}", file=sys.stderr)
            print("\n".join(diff[:DIFF_LINES_SHOWN]), file=sys.stderr)
            if len(diff) > DIFF_LINES_SHOWN:
                print(
                    f"... {len(diff) - DIFF_LINES_SHOWN} more diff lines",
                    file=sys.stderr,
                )
    if stale:
        print(
            f"{len(stale)} generated file(s) differ from include/opcodes.tsv and"
            " include/opcode-types.tsv. Run `make generate` and commit the result;"
            " never edit a generated file by hand.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
