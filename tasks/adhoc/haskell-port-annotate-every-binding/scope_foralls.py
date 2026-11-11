#!/usr/bin/env python3
"""Bring outer type variables into scope for the local signatures that use them.

A local signature that mentions the enclosing function's type variable needs
that variable bound by an explicit ``forall`` in the enclosing (top-level)
signature: ScopedTypeVariables, part of GHC2021, scopes a signature's type
variables over the body only then. Without it GHC reports a "rigid type
variable" mismatch naming the top-level signature as
``NAME :: forall A B. ...`` at ``FILE:LINE:COL``. This script reads GHC's
error output and adds that ``forall A B.`` to each named top-level signature
in the source (column 1 only, so a local signature is never changed).

Usage::

    scope_foralls.py GHC_LOG
"""

import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PORT: pathlib.Path = REPO / "port" / "haskell"
BOUND_BY: re.Pattern[str] = re.compile(
    r"the type signature for:\s+(?P<name>\S+) :: forall (?P<vars>[^.]+)\.[^\n]*\n\s+at (?P<file>src/\S+\.hs):(?P<line>\d+):1-"
)


def main() -> int:
    """Add the foralls; print each signature changed.

    Returns:
        int: the exit status, 0.
    """
    log: str = pathlib.Path(sys.argv[1]).read_text()
    targets: set[tuple[str, int, str, str]] = {
        (m["file"], int(m["line"]), m["name"], m["vars"].strip()) for m in BOUND_BY.finditer(log)
    }
    for file, line, name, variables in sorted(targets):
        path: pathlib.Path = PORT / file
        lines: list[str] = path.read_text().split("\n")
        text: str = lines[line - 1]
        head: str = f"{name} :: "
        if text.startswith(head) and not text[len(head) :].startswith("forall "):
            lines[line - 1] = f"{head}forall {variables}. {text[len(head) :]}"
            path.write_text("\n".join(lines))
            print(f"{file}:{line}: forall {variables}. for {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
