#!/usr/bin/env python3
"""Add a finished engine port's run to every example's BUILD AND RUN header.

Each teaching .c under examples/src has a BUILD AND RUN block (the format is
in tools/check_example_commands.py).  For a port named NAME, this inserts,
after the block's last engine lines, the block's `spimulator` command(s) run
by `spimulator-NAME`:

     *   the paired MIPS assembly, in spimulator:
     *     $ spimulator -f echo.asm hello world
     *
     *   the same assembly, in the Python port:
     *     $ spimulator-python -f echo.asm hello world

so finished ports appear in the order they were added.  The last step of each
port's task (tasks/engine-ports.md).  Idempotent: a block that already has a
`spimulator-NAME` line is left alone, so a second run changes nothing.  Prints
each file it changes.

Usage:
    tools/add_port_lines.py NAME LABEL     e.g.  tools/add_port_lines.py haskell "GHC Haskell"

First written for the Python port (tasks/archive/2026/11/10/engine-port-python.md) and made
general for every port.
"""

import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[1]
SRC: pathlib.Path = REPO / "examples" / "src"
MARK: str = "BUILD AND RUN"
COMMAND: str = " *     $ "
# `spimulator` (the C engine) as the program a command runs: at its start or
# after a pipe.
C_ENGINE: re.Pattern[str] = re.compile(r"(^|\| )spimulator ")
# Any engine: spimulator or spimulator-<port>.
ANY_ENGINE: re.Pattern[str] = re.compile(r"(^|\| )spimulator(-[a-z]+)? ")


def add_port_lines(text: str, name: str, label: str) -> str:
    """text with the NAME port's lines added to its BUILD AND RUN block."""
    lines: list[str] = text.split("\n")
    start: int = next(i for i, line in enumerate(lines) if MARK in line)
    end: int = next(i for i in range(start, len(lines)) if lines[i].startswith(" */"))
    block: list[str] = lines[start:end]
    if any(f"spimulator-{name} " in line for line in block):
        return text
    commands: list[int] = [
        start + i for i, line in enumerate(block) if line.startswith(COMMAND)
    ]
    c_runs: list[int] = [
        i for i in commands if C_ENGINE.search(lines[i][len(COMMAND) :])
    ]
    engine_runs: list[int] = [
        i for i in commands if ANY_ENGINE.search(lines[i][len(COMMAND) :])
    ]
    heading: str = (
        f" *   the same assembly versions, in the {label} port:"
        if len(c_runs) > 1
        else f" *   the same assembly, in the {label} port:"
    )
    ported: list[str] = [
        COMMAND + C_ENGINE.sub(rf"\1spimulator-{name} ", lines[i][len(COMMAND) :])
        for i in c_runs
    ]
    last: int = engine_runs[-1]
    return "\n".join([*lines[: last + 1], " *", heading, *ported, *lines[last + 1 :]])


def main() -> int:
    """Rewrite every example .c that has a BUILD AND RUN block."""
    if len(sys.argv) != 3:
        print(__doc__.split("Usage:")[1].split("\n\n")[0], file=sys.stderr)
        return 2
    name: str = sys.argv[1]
    label: str = sys.argv[2]
    for path in sorted(SRC.rglob("*.c")):
        text: str = path.read_text()
        if MARK not in text:
            continue
        changed: str = add_port_lines(text, name, label)
        if changed != text:
            path.write_text(changed)
            print(path.relative_to(REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
