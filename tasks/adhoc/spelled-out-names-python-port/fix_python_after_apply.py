#!/usr/bin/env python3
"""The Python port's renames that a whole-port token pass cannot make.

Run after ``port_names.py apply python``. Each fix is a name whose spelling means two things
in the port, or a name that lives in a string:

- ``from .inst import``: ``inst`` is also a module (``inst.py``), so the module path goes back.
- ``console.py``: its ``fd`` is a POSIX file descriptor, not the floating-point destination field.
- The ``ExceptionCode`` members share spellings with other names (``CPU`` is also the CPU
  class), so they are renamed here, with the manual's mnemonic in a comment.
- The CPU object's ``hi``/``lo`` are the HI and LO registers (decision 2 of the C task), and its
  ``fpr`` holds all 32 floating-point registers, so it is plural.
- ``__slots__`` lists attribute names as strings, and the CPU class docstring lists them too.
- A forward-reference annotation names a renamed class in a string (``"ImmExpr"``).
- ``Kind`` member names are printed by ``-print-ast``; they print the C engine's spelling.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-python-port/fix_python_after_apply.py
"""

import pathlib
import re
import sys

PORT: pathlib.Path = pathlib.Path("port/python/spimulator_py")
NAMES: pathlib.Path = pathlib.Path(
    "tasks/adhoc/spelled-out-names-python-port/data/python.names.tsv"
)
MEMBERS: dict[str, tuple[str, str]] = {
    "INT": ("INTERRUPT", "Int"),
    "MOD": ("TRANSLATION_LOOKASIDE_BUFFER_MODIFIED", "Mod"),
    "TLBL": ("TRANSLATION_LOOKASIDE_BUFFER_LOAD", "TLBL"),
    "TLBS": ("TRANSLATION_LOOKASIDE_BUFFER_STORE", "TLBS"),
    "ADEL": ("ADDRESS_ERROR_LOAD", "AdEL"),
    "ADES": ("ADDRESS_ERROR_STORE", "AdES"),
    "IBE": ("INSTRUCTION_BUS_ERROR", "IBE"),
    "DBE": ("DATA_BUS_ERROR", "DBE"),
    "SYS": ("SYSTEM_CALL", "Sys"),
    "BP": ("BREAKPOINT", "Bp"),
    "RI": ("RESERVED_INSTRUCTION", "RI"),
    "CPU": ("COPROCESSOR_UNUSABLE", "CpU"),
    "OV": ("ARITHMETIC_OVERFLOW", "Ov"),
    "TR": ("TRAP", "Tr"),
    "FPE": ("FLOATING_POINT_EXCEPTION", "FPE"),
}
CPU_ATTRIBUTES: dict[str, str] = {
    "high": "multiply_divide_high",
    "low": "multiply_divide_low",
    "floating_point_register": "floating_point_registers",
}
CPU_DOCSTRING_OLD: str = """        gpr: the 32 general registers ($0 is forced to 0 before each step).
        hi: the HI register (multiply/divide results).
        lo: the LO register.
        pc: the program counter (unsigned).
        cpr: coprocessor registers, ``cpr[0]`` is CP0 (Status, Cause, EPC,
            ...), ``cpr[1]`` holds the FPU control registers (FIR, FCSR).
        ccr: coprocessor control registers (``cfc0``/``ctc0``).
        fpr: the 32 floating-point registers, as raw words."""
CPU_DOCSTRING_NEW: str = """        general_purpose_registers: the 32 GPRs ($0 is forced to 0 before each
            step).
        multiply_divide_high: the HI register (multiply/divide results).
        multiply_divide_low: the LO register.
        program_counter: the PC (unsigned).
        coprocessor_registers: ``coprocessor_registers[0]`` is CP0 (Status,
            Cause, EPC, ...), ``coprocessor_registers[1]`` holds the FPU control
            registers (FIR, FCSR).
        coprocessor_control_registers: the coprocessor control registers
            (``cfc0``/``ctc0``).
        floating_point_registers: the 32 FPRs, as raw words."""


def printed_kind_names(text: str, renamed: dict[str, str]) -> str:
    """Print each AST kind with the C engine's spelling, not the renamed member's name.

    ``-print-ast`` and ``-print-ast-json`` print a node's kind; the C engine prints fixed strings
    (``src/ast.c``) that the parity tests compare, so the renamed ``Kind`` members get a table of
    their printed names.

    Args:
        text: ``ast_nodes.py``.
        renamed: old name to new name.

    Returns:
        str: the file with the table, and the printers reading it.
    """
    start: int = text.index("class Kind(Enum):")
    end: int = text.index("\n\n\n", start)
    members: list[str] = re.findall(
        r"^    ([A-Z_]+) = auto\(\)", text[start:end], re.MULTILINE
    )
    old_of: dict[str, str] = {new: old for old, new in renamed.items()}
    rows: list[str] = [
        f'    Kind.{member}: "{old_of[member]}",'
        for member in members
        if member in old_of
    ]
    table: str = (
        "\n\n\n# The name each kind prints as in -print-ast and -print-ast-json: the C engine's\n"
        "# spelling (src/ast.c), which the parity tests compare. A kind not listed prints its\n"
        "# own name.\n"
        "_PRINTED_NAME: dict[Kind, str] = {\n" + "\n".join(rows) + "\n}\n\n\n"
        "def printed_name(kind: Kind) -> str:\n"
        '    """The name a kind prints as, as the C engine prints it."""\n'
        "    return _PRINTED_NAME.get(kind, kind.name)"
    )
    text = text[:end] + table + text[end:]
    return text.replace("{node.kind.name}", "{printed_name(node.kind)}")


def main() -> int:
    """Make every fix.

    Returns:
        int: 0.
    """
    renamed: dict[str, str] = {
        line.split("\t")[0]: line.split("\t")[1]
        for line in NAMES.read_text().splitlines()
    }
    path: pathlib.Path
    for path in sorted(PORT.glob("*.py")):
        text: str = path.read_text()
        # A forward reference to a renamed class is a string ("ImmExpr").
        text = re.sub(
            r'"([A-Z][A-Za-z0-9]*)"',
            lambda match: '"' + renamed.get(match.group(1), match.group(1)) + '"',
            text,
        )
        text = re.sub(
            r"^from \.instruction import", "from .inst import", text, flags=re.MULTILINE
        )
        text = re.sub(
            r"\bExceptionCode\.([A-Z]+)\b",
            lambda match: (
                "ExceptionCode." + MEMBERS.get(match.group(1), (match.group(1), ""))[0]
            ),
            text,
        )
        old: str
        new: str
        for old, new in CPU_ATTRIBUTES.items():
            text = re.sub(rf"\b(cpu|self)\.{old}\b", rf"\1.{new}", text)
        if path.name == "console.py":
            text = re.sub(r"\bfloating_point_destination\b", "fd", text)
        if path.name == "traps.py":
            old_member: str
            spelled: str
            manual: str
            for old_member, (spelled, manual) in MEMBERS.items():
                text, count = re.subn(
                    rf"^(\s+){old_member} = (\d+)(  # (.*))?$",
                    lambda match, spelled=spelled, manual=manual: (
                        f"{match.group(1)}{spelled} = {match.group(2)}  # {manual}"
                        + (f": {match.group(4)}" if match.group(4) else "")
                    ),
                    text,
                    count=1,
                    flags=re.MULTILINE,
                )
                assert count == 1, old_member
            text = text.replace(
                '"""The ExceptionCode values of the MIPS32 Cause register."""',
                '"""The values of the MIPS32 Cause register\'s ExcCode field."""',
            )
        if "__slots__ = (" in text:
            start: int = text.index("__slots__ = (")
            end: int = text.index(")", start)
            slots: str = re.sub(
                r'"([A-Za-z_]\w*)"',
                lambda match: (
                    '"'
                    + CPU_ATTRIBUTES.get(
                        renamed.get(match.group(1), match.group(1)),
                        renamed.get(match.group(1), match.group(1)),
                    )
                    + '"'
                ),
                text[start:end],
            )
            text = text[:start] + slots + text[end:]
        text = text.replace(CPU_DOCSTRING_OLD, CPU_DOCSTRING_NEW)
        if path.name == "ast_nodes.py":
            text = printed_kind_names(text, renamed)
        path.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
