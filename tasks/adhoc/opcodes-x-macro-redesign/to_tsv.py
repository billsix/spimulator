#!/usr/bin/env python3
"""Convert include/opcodes.h and include/opcode-types.h into the opcode data files.

A one-shot conversion for the move to tools/gen_opcodes.py: it reads the two
hand-written headers and writes ``include/opcodes.tsv`` and
``include/opcode-types.tsv``, keeping every row's order, group break and
trailing comment, so that the generator can write the headers back byte for
byte. The ``decode`` column marks, for each encoding several rows share, the
row the C engine's qsort + bsearch decode it to; that answer is read from
``port/rust/src/opcodes_table.rs``, which ``port/rust/tools/gen_opcodes.c``
computed by running the C engine's own sort and search.

Usage::

    to_tsv.py
"""

import collections
import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
ROW: re.Pattern[str] = re.compile(
    r'^OP\("(?P<name>[^"]+)",\s*(?P<token>\w+),\s*(?P<type>\w+),\s*(?P<encoding>-?\w+)\)'
    r"\s*(?:/\*\s*(?P<comment>.*?)\s*\*/)?\s*$"
)
TYPE_ENTRY: re.Pattern[str] = re.compile(
    r"^\s*(?P<name>\w+) = (?P<value>\d+),\s*(?:/\*\s*(?P<comment>.*?)\s*\*/)?\s*$"
)
OPCODES_HEADER_LINES: int = 56


def decode_winners() -> set[str]:
    """The names the C engine decodes each shared encoding to, from the Rust table.

    Returns:
        set[str]: one mnemonic per encoding that several rows share.
    """
    text: str = (REPO / "port/rust/src/opcodes_table.rs").read_text()
    rows: list[tuple[str, int, int]] = [
        (name, int(token), int(encoding))
        for name, token, encoding in re.findall(
            r'name: "([^"]+)", token: (\d+), op_type: OpType::\w+, encoding: (-?\d+)',
            text,
        )
    ]
    name_by_token: dict[int, str] = {token: name for name, token, _ in rows}
    decode: dict[int, int] = {
        int(encoding): int(token)
        for encoding, token in re.findall(
            r"^\s+\((-?\d+), (\d+)\),", text, re.MULTILINE
        )
    }
    counts: collections.Counter[int] = collections.Counter(e for _, _, e in rows)
    return {name_by_token[decode[e]] for e, n in counts.items() if n > 1}


def opcode_lines() -> list[str]:
    """The opcode rows as TSV lines, with an empty line at each group break.

    Returns:
        list[str]: the data lines of ``opcodes.tsv`` (no header).
    """
    lines: list[str] = (
        (REPO / "include/opcodes.h").read_text().split("\n")[OPCODES_HEADER_LINES:]
    )
    winners: set[str] = decode_winners()
    out: list[str] = []
    pending: str = ""
    for line in lines:
        if pending:
            pending += " " + line.strip()
        elif line.startswith("OP("):
            pending = line
        else:
            if line.strip():
                raise ValueError(f"unexpected line in opcodes.h: {line!r}")
            if out and out[-1] != "":
                out.append("")
            continue
        if ")" not in pending:
            continue
        match: re.Match[str] | None = ROW.match(pending)
        if match is None:
            raise ValueError(f"unparsed row: {pending!r}")
        pending = ""
        out.append(
            "\t".join(
                [
                    match["name"],
                    match["token"],
                    match["type"],
                    match["encoding"],
                    "*" if match["name"] in winners else "",
                    match["comment"] or "",
                ]
            )
        )
    while out and out[-1] == "":
        out.pop()
    return out


def type_lines() -> list[str]:
    """The operand-shape tags as TSV lines, with an empty line at each group break.

    Returns:
        list[str]: the data lines of ``opcode-types.tsv`` (no header).
    """
    text: str = (REPO / "include/opcode-types.h").read_text()
    body: str = text[text.index("typedef enum op_type : uint8_t {\n") :].split("\n", 1)[
        1
    ]
    body = body[: body.index("} op_type;")]
    out: list[str] = []
    for line in body.split("\n"):
        match: re.Match[str] | None = TYPE_ENTRY.match(line)
        if match is not None:
            out.append(
                "\t".join([match["name"], match["value"], match["comment"] or ""])
            )
        elif not line.strip():
            if out and out[-1] != "":
                out.append("")
        else:
            raise ValueError(f"unexpected line in opcode-types.h: {line!r}")
    while out and out[-1] == "":
        out.pop()
    return out


OPCODES_PREAMBLE: str = """\
# The opcode table: every MIPS instruction, assembler pseudo-instruction,
# assembler directive and spim REPL command, one row each.
#
# This file is the source.  tools/gen_opcodes.py generates from it the C
# engine's tables and every port's (the list is in that tool's docstring);
# the generated files are committed.  After an edit, run `make generate` and
# commit this file with what it rewrote.  The image build fails if a
# committed generated file is out of date.
#
# Columns, separated by one tab:
#   name      the mnemonic as written in assembly
#   token     the C token name; tokens are numbered in this file's order
#   type      the operand-shape tag, from opcode-types.tsv
#   encoding  the instruction's fixed bits (-1 for directives and
#             pseudo-instructions), written as it appears in the C source
#   decode    "*" on the one row a binary word decodes to, when several
#             rows share its encoding; empty otherwise
#   comment   an optional note, kept as a C comment on the row
#
# Rows stay in alphabetical order (the scanner searches them by name).  An
# empty line starts a new group, kept as a blank line in the C output.
"""

TYPES_PREAMBLE: str = """\
# The operand-shape tags each row of opcodes.tsv carries in its type column.
#
# This file is a source for tools/gen_opcodes.py (see opcodes.tsv).
#
# Columns, separated by one tab:
#   name     the tag; it spells the operand list as the student writes it
#   value    its number (uint8_t, 0..42)
#   comment  an example, kept as a C comment
#
# An empty line starts a new group, kept as a blank line in the C output.
"""


def main() -> int:
    """Write both data files.

    Returns:
        int: the exit status, 0.
    """
    (REPO / "include/opcodes.tsv").write_text(
        OPCODES_PREAMBLE + "\n".join(opcode_lines()) + "\n"
    )
    (REPO / "include/opcode-types.tsv").write_text(
        TYPES_PREAMBLE + "\n".join(type_lines()) + "\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
