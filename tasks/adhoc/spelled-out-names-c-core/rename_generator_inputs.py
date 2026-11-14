#!/usr/bin/env python3
"""Rename the C names in the opcode generator and its two data files.

``include/opcodes.tsv`` (the token and type columns, never the mnemonic),
``include/opcode-types.tsv`` (the tag column) and ``tools/gen_opcodes.py`` (the C it emits, and
the C names its comments cite) are renamed through ``data/renames.tsv``. In the Python file only names that cannot be
Python's own are renamed: those that contain ``_`` or upper case, so ``len``, ``op`` and ``str``
stay. opcode-types.tsv also gains a fourth column, the tree-sitter grammar's category name for
the tag, filled with the category the generator used to derive from the old tag spelling
(``RD_RS_RT_INST`` gave ``RD_RS_RT``), so ``tree-sitter/grammar.js`` and its generated parser are
unchanged.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/rename_generator_inputs.py
"""

import pathlib
import re
import sys

DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def renames() -> dict[str, str]:
    """Old to new, from the rows of ``renames.tsv`` that rename every declaration.

    Returns:
        dict[str, str]: the renames.
    """
    table: dict[str, str] = {}
    line: str
    for line in (DATA / "renames.tsv").read_text().splitlines():
        fields: list[str] = line.split("\t")
        if fields[4] == "*":
            table[fields[0]] = fields[1]
    return table


def grammar_category(tag: str) -> str:
    """The tree-sitter category the generator derived from a tag's old spelling.

    Args:
        tag: an old operand-shape tag.

    Returns:
        str: the category name.
    """
    match tag:
        case "PSEUDO_OP":
            return "PSEUDO"
        case name if name.endswith("_TYPE_INST"):
            return name.removesuffix("_TYPE_INST")
        case name:
            return name.removesuffix("_INST")


def rename_tokens(text: str, table: dict[str, str], python: bool) -> str:
    """Rename every identifier of ``text`` found in ``table``.

    Args:
        text: the file.
        table: old to new.
        python: rename only names with ``_`` or upper case (the file is Python).

    Returns:
        str: the renamed text.
    """

    def replace(match: re.Match[str]) -> str:
        name: str = match.group(0)
        if name not in table or python and "_" not in name and name.lower() == name:
            return name
        else:
            return table[name]

    return IDENTIFIER.sub(replace, text)


def main() -> int:
    """Rename the generator and its data files in place.

    Returns:
        int: 0.
    """
    table: dict[str, str] = renames()
    types_path: pathlib.Path = pathlib.Path("include/opcode-types.tsv")
    lines: list[str] = []
    line: str
    for line in types_path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            lines.append(line)
        else:
            fields: list[str] = line.split("\t")
            category: str = grammar_category(fields[0])
            fields[0] = table.get(fields[0], fields[0])
            lines.append("\t".join([*fields, category]))
    types_path.write_text("\n".join(lines) + "\n")
    opcodes_path: pathlib.Path = pathlib.Path("include/opcodes.tsv")
    opcode_lines: list[str] = []
    for line in opcodes_path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            opcode_lines.append(line)
        else:
            # mnemonic, token, type, ...: only the token and type columns are C names; the
            # mnemonic is what the student types.
            fields = line.split("\t")
            fields[1] = table.get(fields[1], fields[1])
            fields[2] = table.get(fields[2], fields[2])
            opcode_lines.append("\t".join(fields))
    opcodes_path.write_text("\n".join(opcode_lines) + "\n")
    generator_path: pathlib.Path = pathlib.Path("tools/gen_opcodes.py")
    generator_path.write_text(rename_tokens(generator_path.read_text(), table, True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
