#!/usr/bin/env python3
"""List every name declared in the C core, from clang's JSON AST.

Reads each translation unit of a compile database (any compiler may configure the build; only
the include, define and standard flags are passed to clang) and prints one tab-separated row per
declaration whose location is under ``src/`` or ``include/``::

    kind  name  path  line

``kind`` is the clang node kind (``VarDecl``, ``ParmVarDecl``, ``FunctionDecl``, ``FieldDecl``,
``RecordDecl``, ``TypedefDecl``, ``EnumDecl``, ``EnumConstantDecl``). Macros are not in the AST;
``discover.sh`` lists them with ``git grep``.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/list_names.py BUILDDIR > names.tsv
"""

import json
import pathlib
import shlex
import subprocess
import sys
import typing

Json: typing.TypeAlias = typing.Any

KINDS: frozenset[str] = frozenset(
    {
        "VarDecl",
        "ParmVarDecl",
        "FunctionDecl",
        "FieldDecl",
        "RecordDecl",
        "TypedefDecl",
        "EnumDecl",
        "EnumConstantDecl",
    }
)
SCOPES: tuple[str, ...] = ("src", "include")
JOINED_FLAGS: tuple[str, ...] = (
    "-I",
    "-D",
    "-U",
    "-std=",
    "-isystem",
    "-iquote",
    "-include",
)
SPLIT_FLAGS: frozenset[str] = frozenset(
    {"-I", "-D", "-U", "-isystem", "-iquote", "-include"}
)


def parse_flags(arguments: list[str]) -> list[str]:
    """Keep the flags of a compile command that change what clang parses.

    Args:
        arguments: the command's arguments, without the compiler itself.

    Returns:
        list[str]: the include, define and standard flags, in order.
    """
    kept: list[str] = []
    take_next: bool = False
    argument: str
    for argument in arguments:
        if take_next:
            kept.append(argument)
            take_next = False
        elif argument in SPLIT_FLAGS:
            kept.append(argument)
            take_next = True
        elif argument.startswith(JOINED_FLAGS):
            kept.append(argument)
        else:
            pass  # warnings, optimization, -c, -o and its value
    return kept


def ast_of(entry: dict[str, Json]) -> Json:
    """The JSON AST of one compile-database entry.

    Args:
        entry: a ``compile_commands.json`` entry.

    Returns:
        Json: clang's ``-ast-dump=json`` of that translation unit.
    """
    arguments: list[str] = (
        entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    )
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        [
            "clang",
            *parse_flags(arguments[1:]),
            "-fsyntax-only",
            "-Xclang",
            "-ast-dump=json",
            entry["file"],
        ],
        cwd=entry["directory"],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(completed.stdout)


def declarations(tree: Json) -> list[tuple[str, str, str, int]]:
    """Every named declaration in one AST, as (kind, name, file, line).

    clang's JSON writes a location's ``file`` and ``line`` only when they differ from the
    previous location printed, so the walk carries both forward in dump order.

    Args:
        tree: a translation unit's JSON AST.

    Returns:
        list[tuple[str, str, str, int]]: kind, name, file and line of each declaration.
    """
    found: list[tuple[str, str, str, int]] = []
    current: dict[str, Json] = {"file": "", "line": 0}

    def visit(node: Json) -> None:
        match node:
            case {"offset": int(), **fields}:
                if "file" in fields:
                    current["file"] = fields["file"]
                if "line" in fields:
                    current["line"] = fields["line"]
                location: str
                for location in ("spellingLoc", "expansionLoc"):
                    if location in fields:
                        visit(fields[location])
            case {"kind": str(kind), "loc": dict(loc), **fields}:
                visit(loc)
                if (
                    kind in KINDS
                    and fields.get("name")
                    and not fields.get("isImplicit")
                ):
                    found.append(
                        (kind, fields["name"], current["file"], current["line"])
                    )
                key: str
                for key in ("range", "inner"):
                    if key in fields:
                        visit(fields[key])
            case dict():
                value: Json
                for value in node.values():
                    visit(value)
            case list():
                item: Json
                for item in node:
                    visit(item)
            case _:
                pass

    visit(tree)
    return found


def main(builddir: pathlib.Path) -> int:
    """Print the core's declarations, one row each, sorted and de-duplicated.

    Args:
        builddir: a build directory holding ``compile_commands.json``.

    Returns:
        int: 0.
    """
    root: pathlib.Path = pathlib.Path.cwd().resolve()
    entries: list[dict[str, Json]] = json.loads(
        (builddir / "compile_commands.json").read_text()
    )
    rows: set[tuple[str, str, str, int]] = set()
    entry: dict[str, Json]
    for entry in entries:
        kind: str
        name: str
        file_name: str
        line: int
        for kind, name, file_name, line in declarations(ast_of(entry)):
            path: pathlib.Path = (
                pathlib.Path(entry["directory"]) / file_name
            ).resolve()
            if path.is_relative_to(root) and path.relative_to(root).parts[0] in SCOPES:
                rows.add((kind, name, str(path.relative_to(root)), line))
    row: tuple[str, str, str, int]
    for row in sorted(rows, key=lambda r: (r[2], r[3], r[1])):
        sys.stdout.write("\t".join(map(str, row)) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(pathlib.Path(sys.argv[1])))
