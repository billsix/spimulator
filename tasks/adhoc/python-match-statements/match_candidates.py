#!/usr/bin/env python3
"""Find the if/elif chains a ``match`` statement could replace.

A chain is reported when at least ``MIN_BRANCHES`` of its ``if``/``elif`` tests
compare one subject expression with ``==`` or ``in``, or test it with ``is``
or ``isinstance``. Each compared value is classified by what a ``case`` can
spell:

- ``value``: a literal (``case 10:``) or a dotted name (``case Tok.ADD:``),
  usable in a pattern as written.
- ``lookup``: a subscript (``t['LI_PSEUDO_OP']``) or a bare name
  (``_READ``). A pattern cannot use either (a bare name in a ``case`` captures
  instead of comparing), so the constants must first become dotted names,
  such as members of an enum or attributes of a namespace class.

Usage::

    match_candidates.py [PATH ...]

The default paths are the repo's maintained Python. One line per chain goes to
stdout (file:line, matching branches over all branches, the subject, the
value classes, and ``else`` when the chain has a final ``else``), and the
total to stderr.
"""

import ast
import pathlib
import sys
from collections.abc import Iterator

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_PATHS: tuple[str, ...] = (
    "port/python",
    "port/harness",
    "tools",
    "tree-sitter/scripts",
)
MIN_BRANCHES: int = 3


def value_class(node: ast.expr) -> str:
    """Whether a compared value can be written in a ``case`` pattern as it is.

    Args:
        node: the right-hand side of a comparison.

    Returns:
        str: ``value`` for a literal or dotted name, ``lookup`` for anything
        else.
    """
    match node:
        case ast.Constant() | ast.Attribute():
            return "value"
        case ast.UnaryOp(op=ast.USub(), operand=ast.Constant()):
            return "value"
        case _:
            return "lookup"


def comparison(test: ast.expr) -> tuple[str, str] | None:
    """The subject and value class of one ``if`` test, when it is a comparison a case can take.

    Args:
        test: the test of an ``if`` or ``elif``.

    Returns:
        tuple[str, str] | None: (subject source, value class), or None when
        the test is not a comparison of one subject.
    """
    match test:
        case ast.Compare(left=left, ops=[ast.Eq()], comparators=[right]):
            return ast.unparse(left), value_class(right)
        case ast.Compare(
            left=left,
            ops=[ast.In()],
            comparators=[
                ast.Tuple(elts=items) | ast.List(elts=items) | ast.Set(elts=items)
            ],
        ):
            return ast.unparse(left), (
                "value"
                if all(value_class(item) == "value" for item in items)
                else "lookup"
            )
        case ast.Compare(left=left, ops=[ast.Is()], comparators=[ast.Constant()]):
            return ast.unparse(left), "value"
        case ast.Call(func=ast.Name(id="isinstance"), args=[subject, _]):
            return ast.unparse(subject), "value"
        case ast.BoolOp(op=ast.Or(), values=values):
            parts: list[tuple[str, str] | None] = [comparison(v) for v in values]
            subjects: set[str] = {p[0] for p in parts if p is not None}
            return (
                (
                    subjects.pop(),
                    "value" if all(p[1] == "value" for p in parts if p) else "lookup",
                )
                if None not in parts and len(subjects) == 1
                else None
            )
        case _:
            return None


def chain_links(head: ast.If) -> list[ast.If]:
    """The ``if`` and each ``elif`` of the chain that starts at ``head``.

    Args:
        head: the chain's ``if``.

    Returns:
        list[ast.If]: the links in order; an ``elif`` is an ``If`` that is the
        only statement of the previous link's ``orelse``.
    """
    match head.orelse:
        case [ast.If() as next_link]:
            return [head, *chain_links(next_link)]
        case _:
            return [head]


def chains(tree: ast.Module) -> Iterator[tuple[int, int, int, str, set[str], bool]]:
    """Every if/elif chain in a module with ``MIN_BRANCHES`` comparisons of one subject.

    Args:
        tree: a parsed module.

    Yields:
        tuple[int, int, int, str, set[str], bool]: the chain's line, its
        number of links, the number comparing the subject, the subject's
        source, the value classes seen, and whether it ends in an ``else``.
    """
    inside_a_chain: set[int] = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.If) or id(node) in inside_a_chain:
            continue
        links: list[ast.If] = chain_links(node)
        inside_a_chain.update(id(link) for link in links)
        classes_by_subject: dict[str, list[str]] = {}
        for found in (comparison(link.test) for link in links):
            if found is not None:
                classes_by_subject.setdefault(found[0], []).append(found[1])
        for subject, classes in classes_by_subject.items():
            if len(classes) >= MIN_BRANCHES:
                yield (
                    node.lineno,
                    len(links),
                    len(classes),
                    subject,
                    set(classes),
                    bool(links[-1].orelse),
                )


def main() -> int:
    """Print every candidate chain and the total.

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
        for line, links, matching, subject, classes, has_else in chains(
            ast.parse(f.read_text(), str(f))
        ):
            total += 1
            print(
                f"{f.relative_to(REPO)}:{line}: {matching}/{links} on {subject}"
                f" ({','.join(sorted(classes))}){' else' if has_else else ''}"
            )
    print(f"{total} chains", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
