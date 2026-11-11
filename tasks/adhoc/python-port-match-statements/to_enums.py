#!/usr/bin/env python3
"""Turn the Python port's string-keyed token and tag lookups into enum members.

Edits ``port/python/spimulator_py/*.py``:

- ``TOK["X"]``, a local alias ``t["X"]``, and ``opcodes.TOKEN["X"]`` become
  ``Tok.X``; ``KIND["X"]`` and ``opcodes.KIND["X"]`` become ``OpType.X``. The
  type checker then sees every name (``Tok`` and ``OpType`` are the generated
  ``IntEnum`` classes in ``opcode_table.py``).
- A lookup by a computed name, ``TOK[f"{name}_OPCODE"]``, becomes
  ``Tok[f"{name}_OPCODE"]``: an Enum class looks members up by name the same
  way, with the same ``KeyError`` for a bad one.
- The ``TOK = opcodes.TOKEN`` module aliases and ``t = TOK`` local aliases go,
  and each edited module imports what it now uses from ``.opcode_table``.
- instruction.py's module-level tag aliases (``RS_INST: int =
  KIND["RS_INST"]``) go, and each use becomes ``OpType.RS_INST``.

Collections of token names written as strings are left for a hand edit; the
script prints the remaining ``TOK``/``KIND`` uses.

Usage::

    to_enums.py
"""

import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PACKAGE: pathlib.Path = REPO / "port" / "python" / "spimulator_py"
LITERAL_TOKEN: re.Pattern[str] = re.compile(r'\b(?:opcodes\.TOKEN|TOK|t)\["(\w+)"\]')
LITERAL_KIND: re.Pattern[str] = re.compile(r'\b(?:opcodes\.KIND|KIND)\["(\w+)"\]')
DYNAMIC_TOKEN: re.Pattern[str] = re.compile(r"\bTOK\[(?=f?\")")
ALIASES: tuple[re.Pattern[str], ...] = (
    re.compile(r"^TOK: dict\[str, int\] = opcodes\.TOKEN\n", re.MULTILINE),
    re.compile(r"^    t: dict\[str, int\] = TOK\n", re.MULTILINE),
)
TAG_ALIAS: re.Pattern[str] = re.compile(
    r'^([A-Z_]+_INST|[A-Z_]+_TYPE_INST): int = KIND\["\1"\]\n', re.MULTILINE
)


def convert(text: str, module: str) -> str:
    """The converted text of one module.

    Args:
        text: the module's source.
        module: its file name.

    Returns:
        str: the source with the lookups rewritten.
    """
    if module == "instruction.py":
        tags: list[str] = TAG_ALIAS.findall(text)
        text = TAG_ALIAS.sub("", text)
        for tag in tags:
            text = re.sub(rf"(?<![\w.\"]){tag}(?![\w\"])", f"OpType.{tag}", text)
    text = LITERAL_TOKEN.sub(r"Tok.\1", text)
    text = LITERAL_KIND.sub(r"OpType.\1", text)
    text = DYNAMIC_TOKEN.sub("Tok[", text)
    for alias in ALIASES:
        text = alias.sub("", text)
    used: list[str] = [
        name for name in ("OpType", "Tok") if re.search(rf"\b{name}\b", text)
    ]
    if (
        used
        and module != "opcode_table.py"
        and not re.search(r"^from \.opcode_table import", text, re.MULTILINE)
    ):
        anchor: re.Match[str] | None = re.search(
            r"^from \. import [^\n]*\n|^from \.\w+ import", text, re.MULTILINE
        )
        position: int = anchor.start() if anchor else 0
        text = (
            text[:position]
            + f"from .opcode_table import {', '.join(used)}\n"
            + text[position:]
        )
    return text


def main() -> int:
    """Convert every module; print what is left.

    Returns:
        int: the exit status, 0.
    """
    for path in sorted(PACKAGE.glob("*.py")):
        if path.name in ("opcode_table.py", "opcodes.py"):
            continue
        original: str = path.read_text()
        converted: str = convert(original, path.name)
        if converted != original:
            path.write_text(converted)
            print(f"converted {path.name}")
    for path in sorted(PACKAGE.glob("*.py")):
        for number, line in enumerate(path.read_text().split("\n"), start=1):
            if re.search(r"\b(TOK|KIND|TOKEN)\b", line) and path.name not in (
                "opcodes.py",
                "opcode_table.py",
            ):
                print(f"left: {path.name}:{number}: {line.strip()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
