#!/usr/bin/env python3
"""Prepare the three chains whose values a ``case`` could not spell.

Run before ``to_match.py``:

- spim.py: the REPL command numbers (``_EXIT, _READ, ... = range(1, 8)``)
  become ``class Command(enum.IntEnum)``, numbered as the C engine's spim.c
  enum (UNKNOWN_CMD = 0, EXIT_CMD, ...); the structural tokens it imported as
  bare names (``TOK_REG``) are written ``opcodes.TOK_REG``, a dotted name a
  ``case`` can use.
- scanner.py: the escape chain compares ``chr(escape)`` with one-character
  strings instead of ``escape`` with ``ord(...)`` calls, which a pattern cannot
  hold.

Usage::

    pre_edits.py
"""

import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PACKAGE: pathlib.Path = REPO / "port" / "python" / "spimulator_py"
COMMANDS: tuple[str, ...] = (
    "UNKNOWN", "EXIT", "READ", "RUN", "STEP", "PRINT", "PRINT_SYM", "PRINT_ALL_REGS",
    "REINITIALIZE", "ASM", "REDO", "NOP", "HELP", "CONTINUE", "SET_BKPT", "DELETE_BKPT",
    "LIST_BKPT", "DUMPNATIVE", "DUMP", "DISASM", "ARGS",
)  # fmt: skip
STRUCTURAL: tuple[str, ...] = (
    "TOK_EOF",
    "TOK_FP_REG",
    "TOK_ID",
    "TOK_INT",
    "TOK_NL",
    "TOK_REG",
    "TOK_STR",
)


def replace_once(text: str, old: str, new: str) -> str:
    """Replace a substring that must occur exactly once.

    Args:
        text: the source.
        old: the text to find.
        new: its replacement.

    Returns:
        str: the edited source.

    Raises:
        ValueError: when old does not occur exactly once.
    """
    if text.count(old) != 1:
        raise ValueError(f"expected one occurrence of {old!r}")
    return text.replace(old, new)


def spim_py(text: str) -> str:
    """The Command enum and the dotted structural tokens.

    Args:
        text: spim.py's source.

    Returns:
        str: the edited source.
    """
    text = replace_once(
        text,
        "_EXIT, _READ, _RUN, _STEP, _PRINT, _PRINT_SYM, _PRINT_ALL_REGS = range(1, 8)\n"
        "_REINITIALIZE, _ASM, _REDO, _NOP, _HELP, _CONTINUE, _SET_BKPT = range(8, 15)\n"
        "_DELETE_BKPT, _LIST_BKPT, _DUMPNATIVE, _DUMP, _DISASM, _ARGS = range(15, 21)\n"
        "_UNKNOWN: int = 0\n",
        "class Command(enum.IntEnum):\n"
        '    """A REPL command, numbered as the C engine\'s spim.c numbers them.\n\n'
        "    The values mirror the C enum so the two sources can be read side by side;\n"
        '    nothing outside the program sees them.\n    """\n\n'
        + "".join(f"    {name} = {number}\n" for number, name in enumerate(COMMANDS)),
    )
    for name in COMMANDS:
        text = re.sub(rf"(?<![\w.])_{name}(?!\w)", f"Command.{name}", text)
    for token in STRUCTURAL:
        text = re.sub(rf"(?<![\w.]){token}(?!\w)", f"opcodes.{token}", text)
    text = replace_once(
        text,
        "from .opcodes import " + ", ".join(f"opcodes.{t}" for t in STRUCTURAL) + "\n",
        "",
    )
    text = replace_once(text, "import io\n", "import enum\nimport io\n")
    return replace_once(
        text, "    memory,\n    options,\n", "    memory,\n    opcodes,\n    options,\n"
    )


def scanner_py(text: str) -> str:
    """The escape chain on chr(escape).

    Args:
        text: scanner.py's source.

    Returns:
        str: the edited source.
    """
    for old, new in (
        ('        if escape == ord("n"):', '        if chr(escape) == "n":'),
        ('        elif escape == ord("t"):', '        elif chr(escape) == "t":'),
        ("        elif escape == ord('\"'):", "        elif chr(escape) == '\"':"),
        (
            '        elif escape in b"0123":',
            '        elif chr(escape) in ("0", "1", "2", "3"):',
        ),
        ('        elif escape == ord("X"):', '        elif chr(escape) == "X":'),
    ):
        text = replace_once(text, old, new)
    return text


def main() -> int:
    """Edit both modules.

    Returns:
        int: the exit status, 0.
    """
    for name, edit in (("spim.py", spim_py), ("scanner.py", scanner_py)):
        path: pathlib.Path = PACKAGE / name
        path.write_text(edit(path.read_text()))
        print(f"edited {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
