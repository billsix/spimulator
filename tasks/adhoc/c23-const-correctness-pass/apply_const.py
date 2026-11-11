#!/usr/bin/env python3
"""Add ``const`` to the candidates const_candidates.py finds, using the compiler as the judge.

Each round scans the core with const_candidates.py, inserts ``const`` at
every candidate of the chosen categories, and builds the ``spimulator``
target (``-Werror``). Every compile error is traced back to the candidates
it names, and those candidates are put back as they were and not tried
again. The build is repeated until it is clean. The scan then runs again,
because a ``const`` added in one round can unlock another: a parameter that
was passed to a now-``const`` callee. The rounds stop when a scan finds
nothing new to try.

A ``const`` can only make a compile fail, never change behavior: writing a
``const`` object or dropping a qualifier is an error under ``-Werror``. So a
clean build is the proof that each insertion is sound.

Usage::

    apply_const.py BUILDDIR CATEGORY [CATEGORY ...]

BUILDDIR is a meson build of the repo (it needs ``compile_commands.json``).
The categories are those of const_candidates.py: ``local-scalar``,
``local-pointer``, ``param-pointee`` and ``param-cascade`` (the two parameter
categories are applied the same way). The candidates that were rejected go to
``data/rejected-<category>.txt`` next to this script, one per line with the
compiler's message.
"""

import collections
import dataclasses
import json
import pathlib
import re
import subprocess
import sys
from collections.abc import Iterator, Mapping, Sequence
from typing import Any

from const_candidates import REPO, Node, ast_for, scan_function, walk

HERE: pathlib.Path = pathlib.Path(__file__).resolve().parent
# A parameter type that cannot take a pointee const: stdio takes a plain
# FILE *, and T ** does not convert to const T ** in C.
SKIPPED_PARAMETER_TYPES: re.Pattern[str] = re.compile(r"\bFILE\b|\*\s*\*|\(\*")
ERROR_LINE: re.Pattern[str] = re.compile(
    r"^(?P<file>[^:\s]+\.[ch]):(?P<line>\d+):(?P<col>\d+): error: (?P<message>.*)$"
)
IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_]\w*")
MAX_BUILDS: int = 60


@dataclasses.dataclass(frozen=True, slots=True)
class Insertion:
    """One ``const`` to insert: where, and the candidate it belongs to.

    Attributes:
        path: the source file.
        offset: the byte offset the text goes in front of.
        text: what to insert (``"const "``).
        key: the candidate (``file:function:name``); every insertion of one
            candidate (a parameter and its prototypes) shares the key.
        function: the function the candidate is in.
        name: the variable or parameter.
        category: the const_candidates.py category.
    """

    path: pathlib.Path
    offset: int
    text: str
    key: str
    function: str
    name: str
    category: str


def mark_files(value: Any, current: list[str]) -> None:
    """Record the source file of every location in a clang JSON AST.

    clang prints a location's file only when it differs from the previous
    location printed, so the file is carried along in print order and stored
    as ``_file`` on each location dict. A macro location (with
    ``spellingLoc``/``expansionLoc`` instead of an offset) is left unmarked.

    Args:
        value: a node, list or location of the AST, walked in print order.
        current: a one-element list holding the file of the last location.
    """
    match value:
        case dict():
            if "offset" in value and "file" in value:
                current[0] = value["file"]
            if "offset" in value:
                value["_file"] = current[0]
            for child in value.values():
                mark_files(child, current)
        case list():
            for child in value:
                mark_files(child, current)
        case _:
            pass  # a number or string carries no location


def source_path(location: Mapping[str, Any], directory: str) -> pathlib.Path | None:
    """The file a marked location is in, or None for a macro location.

    Args:
        location: a location dict marked by mark_files.
        directory: the compile command's directory, for relative paths.

    Returns:
        pathlib.Path | None: the resolved file.
    """
    return (
        pathlib.Path(directory, location["_file"]).resolve()
        if "_file" in location and "offset" in location
        else None
    )


def insertions_for(
    entry: Mapping[str, Any], categories: frozenset[str], rejected: set[str]
) -> list[Insertion]:
    """The const insertions for one translation unit's candidates.

    Args:
        entry: the compile_commands.json entry of one ``src/*.c`` file.
        categories: the categories to apply.
        rejected: candidate keys the compiler already refused.

    Returns:
        list[Insertion]: every insertion, prototypes included.
    """
    tree: Node = ast_for(entry)
    mark_files(tree, [entry["file"]])
    main_file: pathlib.Path = pathlib.Path(entry["directory"], entry["file"]).resolve()
    directory: str = entry["directory"]
    declarations_by_name: dict[str, list[Node]] = collections.defaultdict(list)
    for node in tree.get("inner", []):
        if node.get("kind") == "FunctionDecl":
            declarations_by_name[node["name"]].append(node)
    found: list[Insertion] = []
    for function in (
        node
        for node in tree.get("inner", [])
        if node.get("kind") == "FunctionDecl"
        and source_path(node.get("loc", {}), directory) == main_file
        and any(child.get("kind") == "CompoundStmt" for child in node.get("inner", []))
    ):
        candidates: list[tuple[str, Node]] = [
            (category, declaration)
            for category, declaration in scan_function(function)
            if category in categories
        ]
        candidate_ids: set[str] = {declaration["id"] for _, declaration in candidates}
        # A declaration statement declaring several variables gets one const
        # for all of them, so it is a candidate only if every one is.
        groups: dict[int, list[Node]] = collections.defaultdict(list)
        for node in walk(function):
            if node.get("kind") == "VarDecl" and "offset" in node.get("range", {}).get(
                "begin", {}
            ):
                groups[node["range"]["begin"]["offset"]].append(node)
        for category, declaration in candidates:
            key: str = f"{main_file.name}:{function['name']}:{declaration.get('name')}"
            qual_type: str = declaration["type"]["qualType"]
            if key in rejected:
                continue
            match category:
                case "local-scalar":
                    begin: Mapping[str, Any] = declaration["range"]["begin"]
                    group: list[Node] = groups.get(begin.get("offset", -1), [])
                    if source_path(begin, directory) == main_file and all(
                        v["id"] in candidate_ids for v in group
                    ):
                        found.append(
                            Insertion(
                                main_file,
                                begin["offset"],
                                "const ",
                                key,
                                function["name"],
                                declaration["name"],
                                category,
                            )
                        )
                case "local-pointer":
                    location: Mapping[str, Any] = declaration["loc"]
                    if source_path(location, directory) == main_file:
                        found.append(
                            Insertion(
                                main_file,
                                location["offset"],
                                "const ",
                                key,
                                function["name"],
                                declaration["name"],
                                category,
                            )
                        )
                case "param-pointee" | "param-cascade" if (
                    not SKIPPED_PARAMETER_TYPES.search(qual_type)
                ):
                    index: int = [
                        p["id"]
                        for p in function["inner"]
                        if p.get("kind") == "ParmVarDecl"
                    ].index(declaration["id"])
                    for redeclaration in declarations_by_name[function["name"]]:
                        parameters: list[Node] = [
                            p
                            for p in redeclaration.get("inner", [])
                            if p.get("kind") == "ParmVarDecl"
                        ]
                        begin_at: Mapping[str, Any] = (
                            parameters[index]["range"]["begin"]
                            if index < len(parameters)
                            else {}
                        )
                        path: pathlib.Path | None = source_path(begin_at, directory)
                        if path is not None and (
                            path.parent in (REPO / "src", REPO / "include")
                        ):
                            found.append(
                                Insertion(
                                    path,
                                    begin_at["offset"],
                                    "const ",
                                    key,
                                    function["name"],
                                    declaration.get("name", ""),
                                    category,
                                )
                            )
                case _:
                    pass  # a skipped parameter type, or a category not asked for
    return found


def apply(insertions: Sequence[Insertion]) -> None:
    """Insert each text at its offset, last offset first so the others stay valid.

    Args:
        insertions: the insertions; duplicates (a header seen from several
            translation units) are applied once.
    """
    by_path: dict[pathlib.Path, set[tuple[int, str]]] = collections.defaultdict(set)
    for insertion in insertions:
        by_path[insertion.path].add((insertion.offset, insertion.text))
    for path, edits in by_path.items():
        data: bytes = path.read_bytes()
        for offset, text in sorted(edits, reverse=True):
            if not data[offset:].startswith(b"const "):
                data = data[:offset] + text.encode() + data[offset:]
        path.write_bytes(data)


def build_errors(build: pathlib.Path) -> list[tuple[pathlib.Path, int, int, str]]:
    """Build the spimulator target and return its errors.

    Args:
        build: the meson build directory.

    Returns:
        list[tuple[pathlib.Path, int, int, str]]: (file, line, column,
        message) per error; empty when the build succeeded.
    """
    done: subprocess.CompletedProcess[str] = subprocess.run(
        ["meson", "compile", "-C", str(build), "spimulator"],
        capture_output=True,
        text=True,
        check=False,  # a failed build is the expected, informative case
    )
    found: list[tuple[pathlib.Path, int, int, str]] = [
        ((build / m["file"]).resolve(), int(m["line"]), int(m["col"]), m["message"])
        for m in (
            ERROR_LINE.match(line) for line in (done.stdout + done.stderr).splitlines()
        )
        if m is not None
    ]
    if done.returncode != 0 and not found:
        raise RuntimeError(
            f"the build failed without a parsable error:\n{done.stdout[-3000:]}{done.stderr[-3000:]}"
        )
    return found


def enclosing_function(path: pathlib.Path, line: int) -> str | None:
    """The name of the function whose body holds a line, read from the source text.

    Args:
        path: the source file.
        line: a 1-based line number.

    Returns:
        str | None: the function name, from the nearest definition line above
        (a line starting in column 1 with ``name(``), or None.
    """
    lines: list[str] = path.read_text(errors="replace").splitlines()[:line]
    definitions: Iterator[re.Match[str]] = (
        m
        for m in (
            re.match(r"^(?:[\w\[\]: ]+[\s*])?(\w+)\(", text) for text in reversed(lines)
        )
        if m is not None
    )
    return next((m.group(1) for m in definitions), None)


def blamed(
    error: tuple[pathlib.Path, int, int, str], tried: Sequence[Insertion]
) -> set[str]:
    """The candidate keys a compile error is caused by.

    The identifiers on the error's line are matched, in order, against the
    candidates of the function the line is in (a parameter or local named
    there), then against the functions the line names (a function whose type
    changed, used as a callback or declared in a prototype). When neither
    matches, every candidate of the enclosing function is blamed.

    Args:
        error: (file, line, column, message) from build_errors.
        tried: the insertions applied in this round.

    Returns:
        set[str]: the keys to put back.
    """
    path, line, _, _ = error
    text: str = path.read_text(errors="replace").splitlines()[line - 1]
    names: set[str] = set(IDENTIFIER.findall(text))
    function: str | None = enclosing_function(path, line)
    return (
        {i.key for i in tried if i.function == function and i.name in names}
        or {i.key for i in tried if i.function in names}
        or {i.key for i in tried if i.function == function}
    )


def main() -> int:
    """Run rounds until a scan finds nothing new; report what was kept.

    Returns:
        int: the exit status, 0.
    """
    build: pathlib.Path = pathlib.Path(sys.argv[1]).resolve()
    categories: frozenset[str] = frozenset(sys.argv[2:])
    entries: list[dict[str, Any]] = [
        e
        for e in json.loads((build / "compile_commands.json").read_text())
        if pathlib.Path(e["directory"], e["file"]).resolve().parent == REPO / "src"
    ]
    rejected: dict[str, str] = {}
    category_of: dict[str, str] = {}
    kept: set[str] = set()
    builds: int = 0
    while True:
        # Each translation unit is scanned against the tree as it now is.
        tried: list[Insertion] = [
            i
            for entry in entries
            for i in insertions_for(entry, categories, set(rejected))
        ]
        tried = [i for i in tried if i.key not in kept]
        if not tried:
            break
        category_of.update((i.key, i.category) for i in tried)
        originals: dict[pathlib.Path, bytes] = {
            i.path: i.path.read_bytes() for i in tried
        }
        apply(tried)
        while (errors := build_errors(build)) and builds < MAX_BUILDS:
            builds += 1
            for error in errors:
                for key in blamed(error, tried):
                    rejected.setdefault(key, error[3])
            for path, data in originals.items():
                path.write_bytes(data)
            tried = [i for i in tried if i.key not in rejected]
            apply(tried)
        if errors:
            raise RuntimeError(f"still failing after {MAX_BUILDS} builds: {errors[:3]}")
        kept.update(i.key for i in tried)
        print(
            f"round: {len({i.key for i in tried})} kept, {len(rejected)} rejected so far",
            file=sys.stderr,
        )
    for category in sorted(categories):
        (HERE / "data" / f"rejected-{category}.txt").write_text(
            "".join(
                f"{key}: {message}\n"
                for key, message in sorted(rejected.items())
                if category_of[key] == category
            )
        )
    print(f"kept {len(kept)}, rejected {len(rejected)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
