#!/usr/bin/env python3
"""Import the Python port's ``instruction`` module as ``instruction_module``.

Run before ``port_names.py apply python``. The C core's ``inst`` became ``instruction``, and the
port's locals named ``inst`` follow it; in a file that also imports the ``instruction`` module
the two would collide. So in every such file each code token ``instruction`` (they are all the
module) becomes ``instruction_module``, and the import says ``instruction as
instruction_module``. Both import forms are handled: ``from . import a, instruction, b`` and the
parenthesized one over several lines, at any indentation.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-python-port/alias_instruction_module.py
"""

import importlib.util
import pathlib
import re
import sys

HERE: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-python-port")
IMPORT: re.Pattern[str] = re.compile(
    r"^[ \t]*from \. import (?:\([^)]*\binstruction\b[^)]*\)|[^\n(]*\binstruction\b[^\n]*)",
    re.MULTILINE,
)


def main() -> int:
    """Alias the module in every file that imports it.

    Returns:
        int: 0.
    """
    spec = importlib.util.spec_from_file_location("port_names", HERE / "port_names.py")
    assert spec is not None and spec.loader is not None
    port_names = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(port_names)
    path: pathlib.Path
    for path in sorted(pathlib.Path("port/python/spimulator_py").glob("*.py")):
        text: str = path.read_text()
        if IMPORT.search(text) is None:
            continue
        pieces: list[str] = []
        cursor: int = 0
        start: int
        end: int
        in_comment: bool
        for start, end, in_comment in port_names.spans("python", text):
            if text[start:end] == "instruction" and not in_comment:
                pieces.append(text[cursor:start])
                pieces.append("instruction_module")
                cursor = end
        pieces.append(text[cursor:])
        text = "".join(pieces)
        text = re.sub(
            r"(\bfrom \. import [^\n]*?|\bfrom \. import \([^)]*?)\binstruction_module\b",
            r"\1instruction as instruction_module",
            text,
        )
        path.write_text(text)
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
