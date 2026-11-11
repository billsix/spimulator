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
        if not line:
            group += 1
            continue
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
    if len({row.symbol for row in opcodes}) != len(opcodes) or len(known) != len(types):
        raise ValueError("a token or a type tag is listed twice")
    for encoding, rows in itertools.groupby(
        sorted(opcodes, key=lambda row: row.encoding), key=lambda row: row.encoding
    ):
        group_rows: list[Opcode] = list(rows)
        marked: int = sum(row.decodes for row in group_rows)
        if (len(group_rows) > 1 and marked != 1) or (len(group_rows) == 1 and marked):
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


OPCODES_H_PREAMBLE: str = """/* SPIM S20 MIPS simulator.
   Master list of MIPS instructions, assembler pseudo-instructions,
   assembler directives, and spim REPL commands — the single source
   of truth that every keyword table in the rest of spim derives from.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text.

   ============================================================
   X-macro file.  If you haven't seen this pattern before:
   ============================================================

   Each line below is a call to a macro named OP() with four
   arguments — the textual mnemonic, the token symbol, an
   operand-shape type tag, and the binary encoding.  OP() is NOT
   defined in this file.  Each consumer defines its own OP()
   before #including this file, so each row expands into
   whatever shape that consumer wants.  Typical pattern:

       #define OP(name, sym, type, enc) sym,
       #include "opcodes.h"

   That expansion (used in tokens.h) turns each row into just
   the second column — so OP("add", TOK_ADD_OPCODE, ...) becomes
   "TOK_ADD_OPCODE," inside an enum body.  scanner.c picks the
   first three columns ({name, sym, type}) to build a keyword
   table.  instruction.c includes opcodes.h three times with three
   different OP() definitions to build three different lookup
   tables.

   The 380-row instruction list lives in one place and every
   dependent table updates together when a row is added.

   ============================================================
   Constraints this file MUST respect:
   ============================================================

   1. No header guard.  Each TU re-includes the file multiple
      times with different OP() definitions to build different
      tables.

   2. No typedefs, no preprocessor directives other than the
      OP() rows.  This file is #include'd INSIDE another enum
      body in tokens.h, so anything that isn't a
      comma-terminated expression is a syntax error.

   3. The type-tag identifiers in the third column
      (BRANCH_COPROC_COND_INST, RD_RS_RT_INST, etc.) are defined in
      opcode-types.h.  Consumers that need those names visible must
      include opcode-types.h at file scope before any OP() use.

   Adding a new instruction: insert one row in alphabetical
   order.  Don't add anything else to this file. */

/* Information on each keyword token that can be read by spim.	Must be
   sorted in alphabetical order. */

"""

OPCODE_TYPES_H_PREAMBLE: str = """/* SPIM S20 MIPS simulator.
   Operand-shape type tags used by opcodes.h's X-macro entries.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef OP_TYPES_H
#define OP_TYPES_H

#include <stdint.h>

/* Each OP() entry in opcodes.h carries one of these tags in its third
   argument.  Tables consuming opcodes.h (i_opcode_tbl in
   instruction.c, keyword_tbl in scanner.c, op_type_table in parser.c)
   key per-instruction handling on the value.  uint8_t-backed — values
   fit in 0..42.

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
   alone (syscall, nop) or `<op> imm` (break N).

   Lives in a separate header from opcodes.h because opcodes.h is
   included inside other enum bodies (notably tokens.h's TOK_* enum)
   where a typedef would be a syntax error.  opcodes.h itself is pure
   X-macro content. */
typedef enum op_type : uint8_t {
"""


def opcodes_h(table: Table) -> str:
    """``include/opcodes.h``: the X-macro rows the C engine's tables include.

    Args:
        table: the data.

    Returns:
        str: the file's text.
    """

    def row_line(row: Opcode) -> str:
        """One ``OP(...)`` row, with its comment.

        Args:
            row: the row.

        Returns:
            str: the line, before formatting.
        """
        comment: str = f" /* {row.comment} */" if row.comment else ""
        return f'OP("{row.name}", {row.symbol}, {row.type_name}, {row.encoding_text}){comment}'

    body: str = "\n".join(grouped(table.opcodes, row_line)) + "\n"
    return clang_format(OPCODES_H_PREAMBLE + body, "include/opcodes.h")


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


OUTPUTS: Mapping[str, Callable[[Table], str]] = {
    "include/opcodes.h": opcodes_h,
    "include/opcode-types.h": opcode_types_h,
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
        if not options.check:
            target.write_text(wanted)
            print(f"wrote {path}")
            continue
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
                f"... {len(diff) - DIFF_LINES_SHOWN} more diff lines", file=sys.stderr
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
