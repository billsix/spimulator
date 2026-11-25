"""Inline single-use annotated assignments in Python files, where that cannot change behaviour.

Usage: ``python3 inline_single_use_python.py [--dry-run] [--skip REGEX] FILE.py ...``.

A binding ``name: T = value`` is inlined into the statement that follows it when:

* the name is stored once and loaded once in its function, and that load is in the
  following statement, in that statement's own expressions: not inside a lambda, a
  comprehension, a nested function or a nested statement (the body of an ``if``, a
  loop, a ``try`` or a ``with``), and not in the branch of a conditional expression or
  a later operand of ``and``/``or``, any of which would evaluate it later, repeatedly,
  only sometimes, or under a different exception handler; and not inside an f-string,
  where a substituted expression is hard to read;
* nothing with an effect is evaluated in that statement before the name: no call,
  ``await`` or ``yield`` finishes ahead of it in source order, so the value is computed
  at the same moment as before;
* the value is not a call the Python standard keeps as a statement (``subprocess.run``),
  and its source does not match ``--skip``;
* the value fits on one line of source.  A value that takes several lines (a table, a
  conditional, a long computation) keeps its name: substituted into the next statement
  it would bury that statement, and only a person can write the comment that should
  replace the name;
* the value's source has no comment inside it (the comment would be lost).

The value's source text replaces the name.  It is put in parentheses unless it is already
an atom, a call, a subscript or an attribute, or the name stood alone as a call argument,
a returned or assigned value, or an element of a display, where no parentheses are needed.  The script works from the bottom of each file up,
so earlier positions stay valid, and repeats until a pass changes nothing.  Run the
formatter afterwards, then the project's gate: this script does not prove equivalence.
"""

import argparse
import ast
import collections
import itertools
import pathlib
import re

KEPT_CALLS: re.Pattern[str] = re.compile(r"\bsubprocess\.run\(")

Lead = tuple[ast.AnnAssign, ast.stmt, ast.Name]


def leads_in(function: ast.FunctionDef | ast.AsyncFunctionDef) -> list[Lead]:
    """Find the bindings of one function that are safe to inline.

    Args:
        function: The function to search, nested blocks included.

    Returns:
        Each binding with the statement that follows it and the one load of its name.
    """

    def direct_load(statement: ast.stmt, name: str) -> ast.Name | None:
        """Find the load of a name on a statement's own level, if nothing runs before it.

        Args:
            statement: The statement that should read the name.
            name: The name to look for.

        Returns:
            The load, or None when it is deferred, repeated or preceded by an effect.
        """
        deferred: tuple[type[ast.AST], ...] = (
            ast.Lambda,
            ast.ListComp,
            ast.SetComp,
            ast.DictComp,
            ast.GeneratorExp,
            ast.FunctionDef,
            ast.AsyncFunctionDef,
            ast.ClassDef,
            ast.For,
            ast.While,
        )
        inside_deferred: set[int] = {
            id(inner)
            for node in ast.walk(statement)
            if isinstance(node, deferred) or (isinstance(node, ast.stmt) and node is not statement)
            for inner in ast.walk(node)
        }
        # The parts of an expression that run only sometimes.
        for node in ast.walk(statement):
            sometimes: list[ast.expr] = []
            if isinstance(node, ast.IfExp):
                sometimes = [node.body, node.orelse]
            elif isinstance(node, ast.BoolOp):
                sometimes = node.values[1:]
            elif isinstance(node, ast.JoinedStr):
                sometimes = list(node.values)
            inside_deferred.update(id(inner) for part in sometimes for inner in ast.walk(part))
        found: list[ast.Name] = [
            node
            for node in ast.walk(statement)
            if isinstance(node, ast.Name) and node.id == name and id(node) not in inside_deferred
        ]
        if len(found) != 1:
            return None
        load: ast.Name = found[0]
        position: tuple[int, int] = (load.lineno, load.col_offset)
        for node in ast.walk(statement):
            if isinstance(node, (ast.Call, ast.Await, ast.Yield, ast.YieldFrom, ast.NamedExpr)):
                ends: tuple[int, int] = (node.end_lineno or 0, node.end_col_offset or 0)
                if ends <= position:
                    return None
        return load

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
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            stores.update(dict.fromkeys(node.names, 2))
    found: list[Lead] = []
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
                ):
                    load: ast.Name | None = direct_load(following, statement.target.id)
                    if load is not None:
                        found.append((statement, following, load))
    return found


def inline_once(source: str, skip: re.Pattern[str] | None, report: list[str], name: str) -> str:
    """Inline every safe binding that one parse of the source finds.

    Args:
        source: The file's text.
        skip: Values whose source matches this stay bindings; None skips nothing.
        report: Receives one line per inline.
        name: The file's name, for the report.

    Returns:
        The edited text, equal to the input when nothing qualified.
    """
    lines: list[str] = source.splitlines(keepends=True)
    starts: list[int] = [0]
    for line in lines:
        starts.append(starts[-1] + len(line.encode()))
    data: bytes = source.encode()

    def offset(line: int, column: int) -> int:
        """Turn an ast position into a byte offset into the file.

        Args:
            line: The 1-based line number.
            column: The byte column within that line.

        Returns:
            The offset from the start of the file.
        """
        return starts[line - 1] + column

    chosen: list[Lead] = []
    taken: set[int] = set()
    for function in ast.walk(ast.parse(source)):
        if not isinstance(function, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        for statement, following, load in leads_in(function):
            assert statement.value is not None
            value: str = ast.get_source_segment(source, statement.value) or ""
            if "#" in value or KEPT_CALLS.search(value) or (skip and skip.search(value)):
                continue
            if statement.value.end_lineno != statement.value.lineno:
                continue
            # A statement edited as a "following" must not also be inlined in this pass.
            if id(statement) in taken or id(following) in taken:
                continue
            taken.update((id(statement), id(following)))
            chosen.append((statement, following, load))
    # Loads that are a whole call argument, returned or assigned value, or display element.
    stands_alone: set[int] = set()
    for _, following, _ in chosen:
        for node in ast.walk(following):
            whole: list[ast.expr | None] = []
            match node:
                case ast.Call():
                    whole = [*node.args, *(keyword.value for keyword in node.keywords)]
                case ast.Return() | ast.Assign() | ast.AnnAssign() | ast.Expr():
                    whole = [node.value]
                case ast.List() | ast.Set():
                    whole = list(node.elts)
                case _:
                    pass
            stands_alone.update(id(part) for part in whole if isinstance(part, ast.Name))
    for statement, _, load in sorted(chosen, key=lambda lead: lead[0].lineno, reverse=True):
        assert statement.value is not None
        value = ast.get_source_segment(source, statement.value) or ""
        atom: bool = (
            isinstance(
                statement.value,
                (
                    ast.Name,
                    ast.Constant,
                    ast.Call,
                    ast.Subscript,
                    ast.Attribute,
                    ast.List,
                    ast.Dict,
                    ast.JoinedStr,
                    ast.ListComp,
                ),
            )
            or id(load) in stands_alone
        )
        replacement: bytes = (value if atom else f"({value})").encode()
        load_start: int = offset(load.lineno, load.col_offset)
        load_end: int = offset(load.end_lineno or load.lineno, load.end_col_offset or 0)
        data = data[:load_start] + replacement + data[load_end:]
        # Remove the binding's own lines, from the start of its first to the end of its last.
        first: int = starts[statement.lineno - 1]
        last: int = starts[statement.end_lineno or statement.lineno]
        data = data[:first] + data[last:]
        assert isinstance(statement.target, ast.Name)
        report.append(f"inlined {name}:{statement.lineno}: {statement.target.id}")
    return data.decode()


def main() -> None:
    """Inline the safe single-use bindings of every file named on the command line."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="report, change nothing")
    parser.add_argument("--skip", help="regular expression; a matching value stays a binding")
    parser.add_argument("files", nargs="+", type=pathlib.Path)
    arguments: argparse.Namespace = parser.parse_args()
    skip: re.Pattern[str] | None = re.compile(arguments.skip) if arguments.skip else None
    for path in arguments.files:
        source: str = path.read_text()
        report: list[str] = []
        while True:
            edited: str = inline_once(source, skip, report, str(path))
            if edited == source or arguments.dry_run:
                break
            ast.parse(edited)
            source = edited
        if not arguments.dry_run:
            path.write_text(source)
        if report:
            print("\n".join(report))


if __name__ == "__main__":
    main()
