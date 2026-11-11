#!/usr/bin/env python3
"""Add the class constraints GHC asks for to the new local signatures.

The ``.hie`` type of a polymorphic local binding has no class context
(``go :: t -> t`` for a ``go`` that needs ``Integral t``), so the signature
written from it is too general and GHC reports ``No instance for 'C a'`` with
``add (C a) to the context of the type signature for: NAME :: ... at
FILE:LINE:COL``. This script reads GHC's error output and adds each such
constraint to that signature, which then states the binding's inferred type.

Usage::

    add_constraints.py GHC_LOG
"""

import collections
import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PORT: pathlib.Path = REPO / "port" / "haskell"
BLOCK: re.Pattern[str] = re.compile(
    r"^(?P<file>src/\S+\.hs):(?P<line>\d+):\d+: error:.*?(?=^src/\S+\.hs:\d+:\d+: error:|\Z)",
    re.MULTILINE | re.DOTALL,
)
ADVICE: re.Pattern[str] = re.compile(
    r"add \((?P<constraint>[^\n]+?)\) to the context of\s+the type signature for:\s+(?P<name>\S+) :: "
)


def main() -> int:
    """Add the constraints; print each signature changed.

    Returns:
        int: the exit status, 0.
    """
    log: str = pathlib.Path(sys.argv[1]).read_text()
    wanted: dict[tuple[str, int, str], set[str]] = collections.defaultdict(set)
    for block in BLOCK.finditer(log):
        for advice in ADVICE.finditer(block[0]):
            # GHC may name a class by its defining module (GHC.Internal.Bits.Bits);
            # the source names it as imported, unqualified.
            constraint: str = re.sub(
                r"\b(?:[A-Z][\w']*\.)+(?=[A-Z])", "", advice["constraint"]
            )
            wanted[(block["file"], int(block["line"]), advice["name"])].add(constraint)
    for (file, error_line, name), constraints in sorted(wanted.items(), reverse=True):
        path: pathlib.Path = PORT / file
        lines: list[str] = path.read_text().split("\n")
        head: str = f"{name} :: "
        # The signature is the nearest line above the error that starts one.
        found: list[int] = [
            i
            for i in range(error_line - 1, -1, -1)
            if re.match(rf"\s*(?:let\s+|where\s+)?{re.escape(head)}", lines[i])
        ]
        if not found:
            print(
                f"{file}:{error_line}: no signature for {name} above; left for a hand edit"
            )
            continue
        line: int = found[0] + 1
        text: str = lines[line - 1]
        start: int = text.index(head)
        body: str = text[start + len(head) :]
        existing: re.Match[str] | None = re.match(
            r"(?:forall [^.]+\.\s*)?(\(([^)]*)\)|[^=]+?) =>\s*", body
        )
        if existing is not None:
            old: str = existing[2] if existing[2] is not None else existing[1]
            body = (
                f"({old}, {', '.join(sorted(constraints))}) => {body[existing.end() :]}"
            )
        elif len(constraints) == 1:
            body = f"{next(iter(constraints))} => {body}"
        else:
            body = f"({', '.join(sorted(constraints))}) => {body}"
        lines[line - 1] = text[:start] + head + body
        path.write_text("\n".join(lines))
        print(f"{file}:{line}: {name} :: {body}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
