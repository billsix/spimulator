"""List every positional (non-designated) struct or union initializer in the C tree.

For each translation unit in a clang ``compile_commands.json``, dump clang's JSON AST
and find each ``InitListExpr`` of a struct or union type that is written in this
repository.  The source text right after its ``{`` tells the spelling apart: ``.``
starts a designated initializer, ``}`` is the empty ``{}`` zero-initializer, and
anything else is positional.  Prints one tab-separated row per positional
initializer: path, line, type, and the source line.

clang-tidy's ``modernize-use-designated-initializers`` would be the natural oracle, but
in clang-tidy 23 it reports nothing for C translation units, so this reads the AST
instead.

Usage::

    CC=clang meson setup <builddir> && ninja -C <builddir>
    python3 tasks/adhoc/c23-designated-initializers/discover.py <builddir>
"""

import json
import pathlib
import shlex
import subprocess
import sys
import typing

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]

Json: typing.TypeAlias = typing.Any


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
    # Drop the output and the compile step; keep every flag that shapes parsing.
    kept: list[str] = []
    skip_next: bool = False
    argument: str
    for argument in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        match argument:
            case "-o" | "-MF" | "-MQ" | "-MT":
                skip_next = True
            case "-c" | "-MD" | "-MMD":
                pass
            case _:
                kept.append(argument)
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ["clang", *kept, "-fsyntax-only", "-Xclang", "-ast-dump=json"],
        cwd=entry["directory"],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(completed.stdout)


def is_record(type_info: dict[str, str]) -> bool:
    """Whether an AST type is a struct or union (not an array of them).

    Args:
        type_info: the node's ``type`` object.

    Returns:
        bool: True for a struct or union type.
    """
    spelled: str = type_info.get("desugaredQualType", type_info.get("qualType", ""))
    return spelled.startswith(("struct ", "union ")) and not spelled.endswith("]")


def positional_initializers(tree: Json) -> list[tuple[str, int, str]]:
    """The positional record initializers in one AST, as (file, offset, type).

    clang's JSON writes a location's ``file`` only when it differs from the previous
    location printed, so the walk keeps the current file as it goes, in dump order.

    Args:
        tree: a translation unit's JSON AST.

    Returns:
        list[tuple[str, int, str]]: file, byte offset of the ``{``, and type name.
    """
    found: list[tuple[str, int, str]] = []
    current_file: list[str] = [""]

    def visit(node: Json) -> None:
        match node:
            case {"offset": int(), **fields}:
                if "file" in fields:
                    current_file[0] = fields["file"]
                spelling: Json = fields.get("spellingLoc")
                expansion: Json = fields.get("expansionLoc")
                if spelling is not None:
                    visit(spelling)
                if expansion is not None:
                    visit(expansion)
            case {"kind": "InitListExpr", "range": {"begin": begin}, "type": type_info}:
                visit(begin)
                begin_file: str = current_file[0]
                if "offset" in begin and is_record(type_info):
                    found.append((begin_file, begin["offset"], type_info["qualType"]))
                key: str
                for key in ("range", "inner"):
                    if key in node:
                        visit(node[key])
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


def spelling_after_brace(source: bytes, offset: int) -> str:
    """Classify an initializer by the first token after its ``{``.

    Args:
        source: the file's bytes.
        offset: the byte offset of the initializer's start.

    Returns:
        str: ``"designated"``, ``"empty"``, ``"positional"``, or ``"elided"`` when
        the start is not a ``{`` (brace elision: an implicit sub-list).
    """
    if source[offset : offset + 1] != b"{":
        return "elided"
    rest: bytes = source[offset + 1 :].lstrip()
    while rest.startswith((b"/*", b"//")):
        end: int = rest.find(b"*/") + 2 if rest.startswith(b"/*") else rest.find(b"\n")
        rest = rest[end:].lstrip()
    match rest[:1]:
        case b"." | b"[":
            return "designated"
        case b"}":
            return "empty"
        case _:
            return "positional"


def main(build_dir: str) -> None:
    """Print every positional record initializer written in the repository.

    Args:
        build_dir: a meson build directory configured with clang.
    """
    entries: list[dict[str, Json]] = json.loads(
        (pathlib.Path(build_dir) / "compile_commands.json").read_text()
    )
    seen: set[tuple[str, int]] = set()
    rows: list[tuple[str, int, str, str]] = []
    entry: dict[str, Json]
    for entry in entries:
        file_name: str
        offset: int
        type_name: str
        for file_name, offset, type_name in positional_initializers(ast_of(entry)):
            path: pathlib.Path = (
                pathlib.Path(entry["directory"]) / file_name
            ).resolve()
            if not path.is_relative_to(REPO) or (str(path), offset) in seen:
                continue
            seen.add((str(path), offset))
            source: bytes = path.read_bytes()
            if spelling_after_brace(source, offset) != "positional":
                continue
            line: int = source.count(b"\n", 0, offset) + 1
            text: str = source.splitlines()[line - 1].decode().strip()
            rows.append((str(path.relative_to(REPO)), line, type_name, text))
    row: tuple[str, int, str, str]
    for row in sorted(rows):
        sys.stdout.write("\t".join(map(str, row)) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
