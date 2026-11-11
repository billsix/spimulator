#!/usr/bin/env python3
"""List the const candidates in src/*.c, read from clang's JSON AST.

clang-tidy's ``misc-const-correctness`` checks C++ only and reports nothing on a
``.c`` file, so this script finds the candidates the C coding standard's
``const`` rules name. Each is one of four categories:

- ``local-scalar``: a non-const, initialized, non-array local that is never
  assigned, incremented or address-taken, so it can be ``const T x = ...``.
- ``local-pointer``: the same for a pointer local that is never repointed, so it
  can be ``T *const p``.
- ``param-pointee``: a pointer parameter never written through (``*p =``,
  ``p->f =``, ``p[i] =``, ``++``/``--`` through it) and never passed to a call, so
  it can be ``const T *p``.
- ``param-cascade``: as ``param-pointee``, but passed to a call; it can be const
  only once the callee's parameter is, so each needs a look by hand.

Function-scope statics, arrays, and the setjmp functions (``main``,
``top_level``) are skipped. The counts are an upper bound: the compiler decides.

Usage::

    const_candidates.py BUILDDIR [FILE ...]

BUILDDIR holds ``compile_commands.json``. The candidates go to stdout, one per
line, and the totals per category to stderr.
"""

import collections
import json
import pathlib
import shlex
import subprocess
import sys
from collections.abc import Iterator, Mapping
from typing import Any

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
SETJMP_FUNCTIONS: frozenset[str] = frozenset({"main", "top_level"})
CATEGORIES: tuple[str, ...] = (
    "local-scalar",
    "local-pointer",
    "param-pointee",
    "param-cascade",
)
# Flags whose next argument is a file name the AST dump does not want.
FLAGS_WITH_A_VALUE: frozenset[str] = frozenset({"-o", "-MQ", "-MF", "-MT"})
# Flags dropped outright: compile-only, dependency output, and -Werror (a
# warning must not stop the dump).
DROPPED_FLAGS: frozenset[str] = frozenset(
    {"-c", "-MD", "-Werror", "-fdiagnostics-color=always"}
)
TRANSPARENT_KINDS: frozenset[str] = frozenset({"ImplicitCastExpr", "ParenExpr"})
ACCESS_KINDS: frozenset[str] = frozenset(
    {"ImplicitCastExpr", "ParenExpr", "MemberExpr", "ArraySubscriptExpr"}
)

type Node = dict[str, Any]


def clang_arguments(entry: Mapping[str, Any]) -> list[str]:
    """The compile command of one compile_commands.json entry, made into AST-dump flags.

    Args:
        entry: one entry of ``compile_commands.json``.

    Returns:
        list[str]: the compiler's arguments without the compiler, a leading
        wrapper such as ``ccache``, the output and dependency-file flags, and
        ``-Werror``.
    """
    words: list[str] = (
        shlex.split(entry["command"])
        if "command" in entry
        else list(entry["arguments"])
    )
    # The compiler is the first word ending in cc, clang or gcc; anything before
    # it is a wrapper.
    compiler_index: int = next(
        i for i, word in enumerate(words) if word.endswith(("cc", "clang", "gcc"))
    )
    kept: list[str] = []
    skip_next: bool = False
    for word in words[compiler_index + 1 :]:
        if skip_next:
            skip_next = False
        elif word in FLAGS_WITH_A_VALUE:
            skip_next = True
        elif word not in DROPPED_FLAGS:
            kept.append(word)
    return kept


def ast_for(entry: Mapping[str, Any]) -> Node:
    """The JSON AST clang builds for one translation unit.

    Args:
        entry: one entry of ``compile_commands.json``.

    Returns:
        Node: the ``TranslationUnitDecl`` node.
    """
    done: subprocess.CompletedProcess[bytes] = subprocess.run(
        [
            "clang",
            *clang_arguments(entry),
            "-fsyntax-only",
            "-Xclang",
            "-ast-dump=json",
        ],
        cwd=entry["directory"],
        capture_output=True,
        check=True,
    )
    return json.loads(done.stdout)


def walk(node: Node) -> Iterator[Node]:
    """Every node of a subtree, the root first.

    Args:
        node: the root of the subtree.

    Yields:
        Node: each node, in depth-first order.
    """
    yield node
    for child in node.get("inner", []):
        yield from walk(child)


def referenced_id(node: Node) -> str | None:
    """The id of the declaration an expression names, through casts and parentheses.

    Args:
        node: an expression node.

    Returns:
        str | None: the declaration's id when the expression is a plain name,
        else None.
    """
    while node.get("kind") in TRANSPARENT_KINDS and node.get("inner"):
        node = node["inner"][0]
    return (
        node.get("referencedDecl", {}).get("id")
        if node.get("kind") == "DeclRefExpr"
        else None
    )


def base_id(node: Node) -> str | None:
    """The id of the name at the base of ``*p``, ``p->f``, ``s.f`` or ``p[i]``.

    Args:
        node: an expression node, usually the left side of an assignment.

    Returns:
        str | None: the base declaration's id, or None when the base is not a
        plain name.
    """
    while True:
        match node.get("kind"):
            case kind if kind in ACCESS_KINDS and node.get("inner"):
                node = node["inner"][0]
            case "UnaryOperator" if node.get("opcode") == "*":
                node = node["inner"][0]
            case _:
                return referenced_id(node)


def is_const(qual_type: str) -> bool:
    """Whether a clang qualType string is already const at the top or pointee level.

    Args:
        qual_type: the type as clang prints it, e.g. ``const char *``.

    Returns:
        bool: True when the type starts or ends with ``const``.
    """
    return qual_type.startswith("const ") or qual_type.rstrip().endswith("const")


def scan_function(function: Node) -> list[tuple[str, Node]]:
    """The const candidates in one function definition.

    Args:
        function: a ``FunctionDecl`` node with a body.

    Returns:
        list[tuple[str, Node]]: (category, declaration) for each candidate.
    """
    if function["name"] in SETJMP_FUNCTIONS:
        return []
    parameters: dict[str, Node] = {
        p["id"]: p for p in function.get("inner", []) if p.get("kind") == "ParmVarDecl"
    }
    locals_by_id: dict[str, Node] = {}
    modified: set[str | None] = set()
    written_through: set[str | None] = set()
    passed_to_a_call: set[str | None] = set()
    for node in walk(function):
        match node.get("kind"), node.get("opcode"):
            case "VarDecl", _ if (
                node.get("storageClass") != "static"
                and node.get("init")
                and "[" not in node["type"]["qualType"]
                and not is_const(node["type"]["qualType"])
            ):
                locals_by_id[node["id"]] = node
            case ("BinaryOperator", "=") | ("CompoundAssignOperator", _):
                target: Node = node["inner"][0]
                modified.add(referenced_id(target))
                # s.f = / a[i] = / *p = : the base is written (a struct) or
                # written through (a pointer); both rule it out, conservatively.
                # A plain `p = q` repoints p and leaves its pointee alone.
                if referenced_id(target) is None:
                    written_through.add(base_id(target))
                    modified.add(base_id(target))
            case "UnaryOperator", "++" | "--":
                operand: Node = node["inner"][0]
                modified.add(referenced_id(operand))
                modified.add(base_id(operand))
                written_through.add(base_id(operand))
            case "UnaryOperator", "&":
                modified.add(referenced_id(node["inner"][0]))
            case "CallExpr", _:
                passed_to_a_call.update(
                    referenced_id(argument) for argument in node.get("inner", [])[1:]
                )
            case _:
                pass  # no other node binds, writes or passes a name
    local_candidates: list[tuple[str, Node]] = [
        ("local-pointer" if "*" in v["type"]["qualType"] else "local-scalar", v)
        for vid, v in locals_by_id.items()
        if vid not in modified
    ]
    parameter_candidates: list[tuple[str, Node]] = [
        ("param-cascade" if pid in passed_to_a_call else "param-pointee", p)
        for pid, p in parameters.items()
        if "*" in p["type"]["qualType"]
        and "(*)" not in p["type"]["qualType"]
        and not is_const(p["type"]["qualType"])
        and pid not in written_through
    ]
    return local_candidates + parameter_candidates


def main() -> int:
    """Print every candidate and the totals per category.

    Returns:
        int: the exit status, 0.
    """
    build: pathlib.Path = pathlib.Path(sys.argv[1])
    wanted: set[pathlib.Path] = {(REPO / f).resolve() for f in sys.argv[2:]}
    entries: list[dict[str, Any]] = [
        e
        for e in json.loads((build / "compile_commands.json").read_text())
        if pathlib.Path(e["directory"], e["file"]).resolve().parent == REPO / "src"
        and (not wanted or pathlib.Path(e["directory"], e["file"]).resolve() in wanted)
    ]
    counts: collections.Counter[str] = collections.Counter()
    for entry in entries:
        name: str = pathlib.Path(entry["file"]).name
        # clang's JSON gives a node's file only when it differs from the
        # previous node's, so the current file is carried along.
        current_file: str | None = None
        for node in ast_for(entry).get("inner", []):
            current_file = node.get("loc", {}).get("file") or current_file
            is_definition: bool = node.get("kind") == "FunctionDecl" and any(
                child.get("kind") == "CompoundStmt" for child in node.get("inner", [])
            )
            if not is_definition or (
                current_file and pathlib.Path(current_file).name != name
            ):
                continue
            for category, declaration in scan_function(node):
                counts[category] += 1
                print(
                    f"{name}:{declaration.get('loc', {}).get('line', '?')}: {category}:"
                    f" {node['name']}: {declaration.get('name')}"
                    f" ({declaration['type']['qualType']})"
                )
    for category in CATEGORIES:
        print(f"{category}: {counts[category]}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
