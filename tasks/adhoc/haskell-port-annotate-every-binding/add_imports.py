#!/usr/bin/env python3
"""Add the type names a new signature needs to the imports GHC suggests.

A written type can name a type the module never had to mention, so it is not
in scope. GHC's error then suggests ``Add 'X' to the import list in the import
of 'M' (at FILE:LINE:...)``. This script reads GHC's error output and adds each
suggested name to that import line's explicit list. Where GHC suggests nothing
("Not in scope: type constructor or class 'X'" alone), a name in ``HOMES`` is
added to the module's import of its home module, or to a new import line after
the last one.

Usage::

    add_imports.py GHC_LOG
"""

import collections
import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PORT: pathlib.Path = REPO / "port" / "haskell"
SUGGESTION: re.Pattern[str] = re.compile(
    r"Add ‘(?P<name>[^’]+)’ to the import list in the import of\s+‘(?P<module>[^’]+)’\s+\(at (?P<file>src/\S+\.hs):(?P<line>\d+):"
)

NOT_IN_SCOPE: re.Pattern[str] = re.compile(
    r"^(?P<file>src/\S+\.hs):\d+:\d+: error: \[GHC-76037\]\n\s+Not in scope: type constructor or class \u2018(?P<name>[^\u2019]+)\u2019\n(?!\s+\u2022 Add|\s+Suggested fix(?:es)?:\n\s+\u2022 Add)",
    re.MULTILINE,
)
# Where the types the port's signatures name are defined.
HOMES: dict[str, str] = {
    "Bits": "Data.Bits",
    "CInt": "Foreign.C.Types",
    "CSize": "Foreign.C.Types",
    "PrintfArg": "Text.Printf",
    "PrintfType": "Text.Printf",
    "IORef": "Data.IORef",
    "InstRef": "Types",
    "IOArray": "Data.Array.IO",
    "IOUArray": "Data.Array.IO",
    "Int8": "Data.Int",
    "Int16": "Data.Int",
    "Int32": "Data.Int",
    "Int64": "Data.Int",
    "Word8": "Data.Word",
    "Word16": "Data.Word",
    "Word32": "Data.Word",
    "Word64": "Data.Word",
}


def add_import(file: str, name: str, module: str) -> None:
    """Import a name from a module, merging into an existing explicit import.

    Args:
        file: the source file, relative to port/haskell.
        name: the type name.
        module: its home module.
    """
    path: pathlib.Path = PORT / file
    lines: list[str] = path.read_text().split("\n")
    imports: list[int] = [
        i for i, line in enumerate(lines) if line.startswith("import ")
    ]
    same: list[int] = [
        i
        for i in imports
        if re.match(rf"import {re.escape(module)} \(", lines[i])
        and lines[i].endswith(")")
    ]
    if same:
        text: str = lines[same[0]]
        if re.search(rf"[(,]\s*{re.escape(name)}\s*[,)]", text):
            return
        lines[same[0]] = text[:-1] + f", {name})"
    else:
        lines.insert(imports[-1] + 1, f"import {module} ({name})")
    path.write_text("\n".join(lines))
    print(f"{file}: imports {name} from {module}")


QUALIFIED: re.Pattern[str] = re.compile(
    r"^(?P<file>src/\S+\.hs):(?P<line>\d+):(?P<column>\d+): error: \[GHC-76037\]\n\s+Not in scope: type constructor or class \u2018(?P<name>[^\u2019]+)\u2019\n\s+Suggested fix:\n\s+Perhaps use \u2018(?P<qualified>[A-Z][\w.]*\.(?P=name))\u2019",
    re.MULTILINE,
)


def qualify(file: str, line: int, column: int, name: str, qualified: str) -> None:
    """Write a type name the way the module imports it, at the error's position.

    Args:
        file: the source file, relative to port/haskell.
        line: the 1-based line of the error.
        column: the 1-based column where the name starts.
        name: the unqualified name.
        qualified: the name as the module can refer to it (``B.ByteString``).
    """
    path: pathlib.Path = PORT / file
    lines: list[str] = path.read_text().split("\n")
    text: str = lines[line - 1]
    if text[column - 1 : column - 1 + len(name)] == name:
        lines[line - 1] = (
            text[: column - 1] + qualified + text[column - 1 + len(name) :]
        )
        path.write_text("\n".join(lines))
        print(f"{file}:{line}: writes {qualified}")


def main() -> int:
    """Apply every suggestion; print each import changed.

    Returns:
        int: the exit status, 0.
    """
    log: str = pathlib.Path(sys.argv[1]).read_text()
    wanted: dict[tuple[str, int], set[str]] = collections.defaultdict(set)
    for match in SUGGESTION.finditer(log):
        wanted[(match["file"], int(match["line"]))].add(match["name"])
    for (file, line), names in sorted(wanted.items()):
        path: pathlib.Path = PORT / file
        lines: list[str] = path.read_text().split("\n")
        text: str = lines[line - 1]
        close: int = text.rfind(")")
        if not text.startswith("import ") or close < 0:
            print(
                f"{file}:{line}: not a one-line import list, left for a hand edit: {sorted(names)}"
            )
            continue
        present: str = text[text.index("(") + 1 : close]
        missing: list[str] = sorted(
            n
            for n in names
            if not re.search(rf"(?<![\w']){re.escape(n)}(?![\w'])", present)
        )
        if missing:
            lines[line - 1] = (
                text[:close] + "".join(f", {n}" for n in missing) + text[close:]
            )
            path.write_text("\n".join(lines))
            print(f"{file}:{line}: imports {', '.join(missing)}")
    for file, name in sorted(
        {(m["file"], m["name"]) for m in NOT_IN_SCOPE.finditer(log)}
    ):
        if name in HOMES:
            add_import(file, name, HOMES[name])
        else:
            print(
                f"{file}: {name} is not in scope and has no known home; left for a hand edit"
            )
    for match in QUALIFIED.finditer(log):
        qualify(
            match["file"],
            int(match["line"]),
            int(match["column"]),
            match["name"],
            match["qualified"],
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
