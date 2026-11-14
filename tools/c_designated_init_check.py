#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# SHARED, VENDORED TOOL -- keep every copy byte-identical.
# Canonical: runClaudeInContainer tasks/reference/c_designated_init_check.py
#   (github.com/billsix/runClaudeInContainer). Edit the canonical, then re-copy
#   it over every vendored copy (runCrushInContainer mirrors it).
# Vendored into a C project's tools/ so the project's own CI is self-contained
#   (a public repo can't rely on a mounted ~/.claude).
# Checks the "Use designated initializers" rule of c-coding-standard.md.
# ---------------------------------------------------------------------------
"""List the struct and union initializers written positionally instead of designated.

The rule (c-coding-standard.md, "Structs, constants, and the preprocessor"): initialize a struct
or union with designated initializers, ``{.x = 10, .y = 20}``, not ``{10, 20}``. The tool reads
clang's JSON AST of every translation unit in a compile database and finds each struct or union
``InitListExpr`` written in the given source paths. The first token after its ``{`` tells the
spelling: ``.`` (or ``[``) is designated, ``}`` is the empty ``{}`` zero-initializer, anything else
is positional and is reported. A positional row inside an array of structs is one site each.

clang-tidy's ``modernize-use-designated-initializers`` would be the natural tool, but as of
clang-tidy 23 it reports nothing for C translation units; this reads the AST instead.

**Accepting a site.** Put ``designated-init-check: ok`` in a comment, with the reason, on the line
where the initializer's ``{`` is or on the line just above it::

    /* designated-init-check: ok -- the field order is the wire format being taught */

**What it can't see.** Code that the build's ``#if``/``#ifdef`` leaves out, and translation units
missing from the compile database. Headers are covered when a parsed ``.c`` file includes them.

Usage, from the project root (any compiler may configure the build: only its include, define and
standard flags reach clang, which must be installed)::

    python3 tools/c_designated_init_check.py BUILDDIR [SOURCE ...]

``SOURCE`` paths (files or directories, relative to the project root) limit the report; with
none, every file under the project root is reported. Prints one tab-separated row per site
(path, line, type, the source line) and exits 1 if there are any, else 0.
"""

import argparse
import json
import pathlib
import shlex
import subprocess
import sys
import typing

Json: typing.TypeAlias = typing.Any

ACCEPT_MARKER: str = "designated-init-check: ok"


# Flags that change what clang parses, written joined (``-Ifoo``) or, for SPLIT_FLAGS, with the
# value as a separate argument. Every other flag (warnings, optimization, output) is dropped, so a
# gcc-only warning flag in a gcc-configured build cannot stop clang.
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


def is_record(type_info: dict[str, str]) -> bool:
    """Whether an AST type is a struct or union (not an array of them).

    Args:
        type_info: the node's ``type`` object.

    Returns:
        bool: True for a struct or union type.
    """
    spelled: str = type_info.get("desugaredQualType", type_info.get("qualType", ""))
    return spelled.startswith(("struct ", "union ")) and not spelled.endswith("]")


def record_initializers(tree: Json) -> list[tuple[str, int, str]]:
    """Every struct or union initializer in one AST, as (file, offset, type).

    clang's JSON writes a location's ``file`` only when it differs from the previous
    location printed, so the walk keeps the current file as it goes, in dump order.

    Args:
        tree: a translation unit's JSON AST.

    Returns:
        list[tuple[str, int, str]]: file, byte offset of the start, and type name.
    """
    found: list[tuple[str, int, str]] = []
    current_file: list[str] = [""]

    def visit(node: Json) -> None:
        match node:
            case {"offset": int(), **fields}:
                if "file" in fields:
                    current_file[0] = fields["file"]
                location: str
                for location in ("spellingLoc", "expansionLoc"):
                    if location in fields:
                        visit(fields[location])
            case {"kind": "InitListExpr", "range": {"begin": begin}, "type": type_info}:
                visit(begin)
                if "offset" in begin and is_record(type_info):
                    found.append(
                        (current_file[0], begin["offset"], type_info["qualType"])
                    )
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


def accepted(lines: list[bytes], line: int) -> bool:
    """Whether the site's line, or the one above, carries the accept marker.

    Args:
        lines: the file's lines.
        line: the 1-based line of the initializer.

    Returns:
        bool: True if ``designated-init-check: ok`` is on either line.
    """
    marker: bytes = ACCEPT_MARKER.encode()
    return any(
        marker in lines[number - 1] for number in (line, line - 1) if number >= 1
    )


def main(arguments: argparse.Namespace) -> int:
    """Print every positional record initializer in the chosen sources.

    Args:
        arguments: the parsed command line.

    Returns:
        int: 1 if any site was reported, else 0.
    """
    root: pathlib.Path = pathlib.Path.cwd().resolve()
    scopes: list[pathlib.Path] = [
        (root / source).resolve() for source in arguments.sources
    ] or [root]
    entries: list[dict[str, Json]] = json.loads(
        (pathlib.Path(arguments.builddir) / "compile_commands.json").read_text()
    )
    seen: set[tuple[pathlib.Path, int]] = set()
    rows: list[tuple[str, int, str, str]] = []
    entry: dict[str, Json]
    for entry in entries:
        file_name: str
        offset: int
        type_name: str
        for file_name, offset, type_name in record_initializers(ast_of(entry)):
            path: pathlib.Path = (
                pathlib.Path(entry["directory"]) / file_name
            ).resolve()
            if (path, offset) in seen or not any(
                path == scope or path.is_relative_to(scope) for scope in scopes
            ):
                continue
            seen.add((path, offset))
            source: bytes = path.read_bytes()
            if spelling_after_brace(source, offset) != "positional":
                continue
            lines: list[bytes] = source.splitlines()
            line: int = source.count(b"\n", 0, offset) + 1
            if accepted(lines, line):
                continue
            shown: str = (
                str(path.relative_to(root)) if path.is_relative_to(root) else str(path)
            )
            rows.append((shown, line, type_name, lines[line - 1].decode().strip()))
    row: tuple[str, int, str, str]
    for row in sorted(rows):
        sys.stdout.write("\t".join(map(str, row)) + "\n")
    return 1 if rows else 0


def parse_arguments() -> argparse.Namespace:
    """Parse the command line.

    Returns:
        argparse.Namespace: ``builddir`` and ``sources``.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="List positional (non-designated) struct/union initializers."
    )
    parser.add_argument(
        "builddir", help="a build dir with compile_commands.json (clang)"
    )
    parser.add_argument("sources", nargs="*", help="limit the report to these paths")
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(main(parse_arguments()))
