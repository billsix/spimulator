#!/usr/bin/env python3
"""Find runs of separate ``if`` statements that are one decision.

The Python coding standard's "Mutually exclusive branches are one
if/elif/else chain" asks for a run of early-exit guards in front of a result
to be written as one ``if``/``elif``/``else`` chain. This lists each run of two
or more consecutive ``if`` statements in one block where no ``if`` has an
``else`` and each one's body ends by leaving the block (``return``, ``raise``,
``continue`` or ``break``). A single guard is not listed: the standard leaves
it as it is.

Usage::

    elif_chain_candidates.py [PATH ...]

The default paths are the repo's maintained Python. One line per run goes to
stdout (file:line, the number of ifs, the exit kinds, and ``+ result`` when
statements follow the run and become its ``else``), and the total to stderr.
"""

import ast
import pathlib
import sys
from collections.abc import Iterator, Sequence

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_PATHS: tuple[str, ...] = (
    "port/python",
    "port/harness",
    "tools",
    "tree-sitter/scripts",
)
MIN_RUN: int = 2


def exit_kind(statement: ast.stmt) -> str | None:
    """How an ``if`` without an ``else`` leaves its block, if its body always does.

    Args:
        statement: a statement of a block.

    Returns:
        str | None: ``return``, ``raise``, ``continue`` or ``break`` when the
        statement is an ``if`` with no ``else`` whose last statement is one of
        those, else None.
    """
    match statement:
        case ast.If(orelse=[], body=[*_, ast.Return()]):
            return "return"
        case ast.If(orelse=[], body=[*_, ast.Raise()]):
            return "raise"
        case ast.If(orelse=[], body=[*_, ast.Continue()]):
            return "continue"
        case ast.If(orelse=[], body=[*_, ast.Break()]):
            return "break"
        case _:
            return None


def runs_in(block: Sequence[ast.stmt]) -> Iterator[tuple[int, int, set[str], bool]]:
    """The guard runs of one block of statements.

    Args:
        block: the statements of one body.

    Yields:
        tuple[int, int, set[str], bool]: the first if's line, the number of
        ifs, their exit kinds, and whether statements follow the run.
    """
    start: int = 0
    while start < len(block):
        end: int = start
        while end < len(block) and exit_kind(block[end]) is not None:
            end += 1
        if end - start >= MIN_RUN:
            kinds: set[str] = {exit_kind(s) or "" for s in block[start:end]}
            yield block[start].lineno, end - start, kinds, end < len(block)
        start = max(end, start + 1)


def blocks(tree: ast.AST) -> Iterator[list[ast.stmt]]:
    """Every block of statements in a tree.

    Args:
        tree: a parsed module.

    Yields:
        list[ast.stmt]: each ``body``, ``orelse``, ``finalbody`` and handler
        body.
    """
    for node in ast.walk(tree):
        for field in ("body", "orelse", "finalbody"):
            statements: object = getattr(node, field, None)
            if (
                isinstance(statements, list)
                and statements
                and isinstance(statements[0], ast.stmt)
            ):
                yield statements


def main() -> int:
    """Print every run and the total.

    Returns:
        int: the exit status, 0.
    """
    files: list[pathlib.Path] = sorted(
        f
        for p in (sys.argv[1:] or DEFAULT_PATHS)
        for f in ((REPO / p).rglob("*.py") if (REPO / p).is_dir() else [REPO / p])
    )
    total: int = 0
    for f in files:
        for block in blocks(ast.parse(f.read_text(), str(f))):
            for line, count, kinds, has_result in runs_in(block):
                total += 1
                print(
                    f"{f.relative_to(REPO)}:{line}: {count} ifs ({','.join(sorted(kinds))})"
                    f"{' + result' if has_result else ''}"
                )
    print(f"{total} runs", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
