#!/usr/bin/env python3
"""Rename the ports' references to the names ``tools/gen_opcodes.py`` generates for them.

The generator writes each port an opcode table and an operand-shape type whose names come from
the C names in ``include/opcodes.tsv`` and ``include/opcode-types.tsv``. When those C names are
spelled out, the generated port files change with them, and the ports' hand-written code must
follow. Per port, the generated spellings are:

- Haskell (``port/haskell``): the C token names (``TOKEN_ADD_OPCODE``) and tags as they are.
- Rust (``port/rust``): each tag in CamelCase (``OpType::TargetRegisterAddressInstruction``),
  and the keyword tokens as constants named like the C tokens (``TOKEN_ADD_OPCODE``).
- Python (``port/python``): the tags as they are (``OpType.TARGET_REGISTER_ADDRESS_INSTRUCTION``);
  its ``Tok`` members drop the ``TOKEN_`` prefix, which the rename does not touch.

Only names that occur in a generated file of that port are renamed, as whole identifiers, in
every other tracked file under the port's directory. The ports' own names (their scanner tokens,
fields and functions) are the port follow-up tasks' work.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/rename_port_generated_refs.py
"""

import pathlib
import re
import subprocess
import sys

DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_][A-Za-z0-9_']*")
GENERATED: dict[str, tuple[str, ...]] = {
    "port/haskell": ("port/haskell/src/Opcodes.hs", "port/haskell/src/OpcodeTypes.hs"),
    "port/rust": ("port/rust/src/opcodes_table.rs", "port/rust/src/opcode_types.rs"),
    "port/python": ("port/python/spimulator_py/opcode_table.py",),
}


def rust_variant(name: str) -> str:
    """A C enumerator name as the generator's Rust enum variant.

    Args:
        name: ``TARGET_REGISTER_ADDRESS_INSTRUCTION``-style name.

    Returns:
        str: ``TargetRegisterAddressInstruction``.
    """
    return "".join(part.capitalize() for part in name.split("_"))


def c_renames() -> dict[str, str]:
    """Old to new C names for the tokens and tags the generator emits.

    Returns:
        dict[str, str]: the renames whose old name is a token or an operand-shape tag.
    """
    table: dict[str, str] = {}
    line: str
    for line in (DATA / "renames.tsv").read_text().splitlines():
        fields: list[str] = line.split("\t")
        if fields[4] == "*" and "EnumConstantDecl" in fields[2]:
            table[fields[0]] = fields[1]
    return table


def main() -> int:
    """Rename the references in each port.

    Returns:
        int: 0.
    """
    renames: dict[str, str] = c_renames()
    port: str
    generated: tuple[str, ...]
    for port, generated in GENERATED.items():
        spelled: dict[str, str] = dict(renames)
        if port == "port/rust":
            spelled |= {
                rust_variant(old): rust_variant(new) for old, new in renames.items()
            }
        names_in_generated: set[str] = {
            name
            for path in generated
            for name in IDENTIFIER.findall(pathlib.Path(path).read_text())
        }
        # A name the generated files now spell the new way, so the old spelling is what the
        # hand-written code still has.
        table: dict[str, str] = {
            old: new for old, new in spelled.items() if new in names_in_generated
        }
        files: list[str] = subprocess.run(
            ["git", "ls-files", port], capture_output=True, text=True, check=True
        ).stdout.split()
        changed: int = 0
        path_name: str
        for path_name in files:
            path: pathlib.Path = pathlib.Path(path_name)
            if path_name in generated or path.suffix not in {".hs", ".rs", ".py"}:
                continue
            text: str = path.read_text()
            renamed: str = IDENTIFIER.sub(
                lambda match, table=table: table.get(match.group(0), match.group(0)),
                text,
            )
            if renamed != text:
                path.write_text(renamed)
                changed += 1
        print(f"{port}: {len(table)} names, {changed} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
