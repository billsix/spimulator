#!/usr/bin/env python3
"""Add the Python port's run to every example's BUILD AND RUN header.

After the block's `spimulator` line(s) it inserts, for each of them, the same
command run by `spimulator-python`:

     *   the paired MIPS assembly, in spimulator:
     *     $ spimulator -f echo.asm hello world
     *
     *   the same assembly, in the Python port:
     *     $ spimulator-python -f echo.asm hello world

Idempotent: a block that already has a `spimulator-python` line is left
alone, so a second run changes nothing.  Prints each file it changes.
"""

import pathlib
import re

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
SRC: pathlib.Path = REPO / "examples" / "src"
MARK: str = "BUILD AND RUN"
COMMAND: str = " *     $ "
# `spimulator` as the program a command runs: at its start or after a pipe.
PROGRAM: re.Pattern[str] = re.compile(r"(^|\| )spimulator ")


def add_port_lines(text: str) -> str:
    """text with the port's lines added to its BUILD AND RUN block."""
    lines: list[str] = text.split("\n")
    start: int = next(i for i, line in enumerate(lines) if MARK in line)
    end: int = next(i for i in range(start, len(lines)) if lines[i].startswith(" */"))
    block: list[str] = lines[start:end]
    if any("spimulator-python" in line for line in block):
        return text
    runs: list[int] = [
        start + i
        for i, line in enumerate(block)
        if line.startswith(COMMAND) and PROGRAM.search(line[len(COMMAND) :])
    ]
    heading: str = (
        " *   the same assembly versions, in the Python port:"
        if len(runs) > 1
        else " *   the same assembly, in the Python port:"
    )
    ported: list[str] = [
        COMMAND + PROGRAM.sub(r"\1spimulator-python ", lines[i][len(COMMAND) :])
        for i in runs
    ]
    last: int = runs[-1]
    return "\n".join([*lines[: last + 1], " *", heading, *ported, *lines[last + 1 :]])


def main() -> None:
    """Rewrite every example .c that has a BUILD AND RUN block."""
    for path in sorted(SRC.rglob("*.c")):
        text: str = path.read_text()
        if MARK not in text:
            continue
        changed: str = add_port_lines(text)
        if changed != text:
            path.write_text(changed)
            print(path.relative_to(REPO))


if __name__ == "__main__":
    main()
