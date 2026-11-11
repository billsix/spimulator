#!/usr/bin/env python3
"""List the file-scope ``static`` objects in src/*.c that are never written.

A ``static`` object is visible only in its own file, so if nothing in that
file assigns it, increments it, writes into it (an element or member), takes
its address, or passes it (an array decays to a pointer) to a call, it can be
``const``. The compiler decides in the end; this lists what to try.

Usage::

    file_scope_candidates.py BUILDDIR

One line per candidate on stdout: ``file: name (type)``.
"""

import json
import pathlib
import sys
from typing import Any

from const_candidates import REPO, Node, ast_for, base_id, is_const, referenced_id, walk


def written(tree: Node) -> set[str | None]:
    """The ids of every declaration a translation unit writes or lets escape.

    Args:
        tree: the translation unit's AST.

    Returns:
        set[str | None]: ids assigned, incremented, written through (as the
        base of a member, element or dereference), address-taken, or passed
        to a call.
    """
    found: set[str | None] = set()
    for node in walk(tree):
        match node.get("kind"), node.get("opcode"):
            case ("BinaryOperator", "=") | ("CompoundAssignOperator", _):
                found.update(
                    {referenced_id(node["inner"][0]), base_id(node["inner"][0])}
                )
            case "UnaryOperator", "++" | "--" | "&":
                found.update(
                    {referenced_id(node["inner"][0]), base_id(node["inner"][0])}
                )
            case "CallExpr", _:
                found.update(
                    base_id(argument) for argument in node.get("inner", [])[1:]
                )
            case _:
                pass  # reads do not matter
    return found


def main() -> int:
    """Print every candidate.

    Returns:
        int: the exit status, 0.
    """
    build: pathlib.Path = pathlib.Path(sys.argv[1])
    entries: list[dict[str, Any]] = [
        e
        for e in json.loads((build / "compile_commands.json").read_text())
        if pathlib.Path(e["directory"], e["file"]).resolve().parent == REPO / "src"
    ]
    for entry in entries:
        name: str = pathlib.Path(entry["file"]).name
        tree: Node = ast_for(entry)
        changed: set[str | None] = written(tree)
        current_file: str | None = None
        for node in tree.get("inner", []):
            current_file = node.get("loc", {}).get("file") or current_file
            in_this_file: bool = (
                current_file is not None and pathlib.Path(current_file).name == name
            )
            if (
                in_this_file
                and node.get("kind") == "VarDecl"
                and node.get("storageClass") == "static"
                and not is_const(node["type"]["qualType"])
                and node["id"] not in changed
            ):
                print(f"{name}: {node['name']} ({node['type']['qualType']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
