"""List the single-use local bindings of the Python port that the next statement reads.

Usage: ``python3 list_single_use_python.py FILE.py ...``.  Read-only.

For every function it finds each annotated assignment ``name: T = value`` whose name is
stored once and loaded once in that function (nested functions included in the count),
where the load is in the statement that directly follows.  It prints the assignment and
the line of the following statement.  Each line is a lead for "inline a binding used
once", to be read: the value may be read before a later statement changes its source,
or be a call made for its effect.
"""

import ast
import collections
import itertools
import pathlib
import sys


def leads_in(
    function: ast.FunctionDef | ast.AsyncFunctionDef,
) -> list[tuple[ast.AnnAssign, ast.stmt]]:
    """Find the single-use annotated assignments of one function.

    Args:
        function: The function to search, nested blocks included.

    Returns:
        Each such assignment with the statement that follows it and reads its name.
    """
    stores: collections.Counter[str] = collections.Counter()
    loads: collections.Counter[str] = collections.Counter()
    for node in ast.walk(function):
        if isinstance(node, ast.Name):
            match node.ctx:
                case ast.Store() | ast.Del():
                    stores[node.id] += 1
                case ast.Load():
                    loads[node.id] += 1
                case _:
                    raise ValueError(node.ctx)
    found: list[tuple[ast.AnnAssign, ast.stmt]] = []
    for node in ast.walk(function):
        for field in ("body", "orelse", "finalbody"):
            block: object = getattr(node, field, None)
            if not isinstance(block, list):
                continue
            for statement, following in itertools.pairwise(block):
                if (
                    isinstance(statement, ast.AnnAssign)
                    and isinstance(statement.target, ast.Name)
                    and statement.value is not None
                    and stores[statement.target.id] == 1
                    and loads[statement.target.id] == 1
                    and any(
                        isinstance(inner, ast.Name) and inner.id == statement.target.id
                        for inner in ast.walk(following)
                    )
                ):
                    found.append((statement, following))
    return found


def main() -> None:
    """Print the leads of every file named on the command line."""
    for name in sys.argv[1:]:
        source: str = pathlib.Path(name).read_text()
        lines: list[str] = source.splitlines()
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                for statement, following in leads_in(node):
                    print(f"{name}:{statement.lineno}: {lines[statement.lineno - 1].strip()}")
                    print(f"    then {lines[following.lineno - 1].strip()}")


if __name__ == "__main__":
    main()
