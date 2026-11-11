"""Mark each C finding with what the Python port's same-named function now does.

Run after `scan_conditionals.py`:

    python3 tasks/adhoc/c-switch-and-else-if-chains/crossref_python.py

For every row of `data/if_chains.tsv` and `data/guard_runs.tsv`, it looks up the C function's
name in the Python port (`port/python/spimulator_py/`, trying `name` then `_name`) and counts
the `match` statements and `elif` links in that Python function. The result is
`data/crossref_python.tsv`: the C row's location, function and shape, then the Python
function's location and its counts, or "no counterpart". It is a pointer for a reader, not a
proof that the two sites correspond: one C function can hold several chains.
"""

import ast
import pathlib

HERE: pathlib.Path = pathlib.Path(__file__).resolve().parent
REPO_ROOT: pathlib.Path = HERE.parents[2]
PORT: pathlib.Path = REPO_ROOT / "port" / "python" / "spimulator_py"


def python_functions() -> dict[str, tuple[str, int, int]]:
    """Index the port's functions by name.

    Returns:
        For each function name, its `path:line`, its `match` count and its `elif` count.
    """
    index: dict[str, tuple[str, int, int]] = {}
    for source in sorted(PORT.glob("*.py")):
        tree: ast.Module = ast.parse(source.read_text())
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef):
                matches: int = sum(
                    isinstance(inner, ast.Match) for inner in ast.walk(node)
                )
                # An `elif` is an If whose orelse is exactly one If; a nested `else: if` also counts,
                # which is close enough for a pointer.
                elifs: int = sum(
                    isinstance(inner, ast.If)
                    and len(inner.orelse) == 1
                    and isinstance(inner.orelse[0], ast.If)
                    for inner in ast.walk(node)
                )
                where: str = f"{source.relative_to(REPO_ROOT)}:{node.lineno}"
                index.setdefault(node.name, (where, matches, elifs))
    return index


def main() -> None:
    """Write `data/crossref_python.tsv`."""
    index: dict[str, tuple[str, int, int]] = python_functions()
    rows: list[str] = ["c_where\tc_function\tc_kind\tshape\tpython_where\tmatch\telif"]
    for worklog in ("if_chains", "guard_runs"):
        lines: list[str] = (
            (HERE / "data" / f"{worklog}.tsv").read_text().splitlines()[1:]
        )
        for line in lines:
            fields: list[str] = line.split("\t")
            found: tuple[str, int, int] | None = index.get(
                fields[1], index.get(f"_{fields[1]}")
            )
            python_part: str = (
                "no counterpart\t\t"
                if found is None
                else f"{found[0]}\t{found[1]}\t{found[2]}"
            )
            rows.append(
                f"{fields[0]}\t{fields[1]}\t{worklog}\t{fields[2]}\t{python_part}"
            )
    (HERE / "data" / "crossref_python.tsv").write_text("\n".join(rows) + "\n")
    print(
        f"{len(rows) - 1} rows; no counterpart: {sum('no counterpart' in r for r in rows)}"
    )


if __name__ == "__main__":
    main()
