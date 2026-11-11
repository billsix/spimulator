#!/usr/bin/env python3
"""Rewrite dispatch chains as ``match`` and guard runs as one if/elif/else chain.

The Python coding standard asks for two things this script does by text
surgery, using the AST only to find where each piece is (so comments and
layout survive):

- **A dispatch chain to ``match``.** An if/elif chain that compares one subject
  (``kind == OpType.RS_INST``, ``op in (Tok.A, Tok.B)``) becomes ``match
  subject:`` with ``case OpType.RS_INST:`` / ``case Tok.A | Tok.B:``. A link
  that tests something else keeps its test as a guard, ``case _ if test:``.
  ``else`` becomes ``case _:``; a chain without one gets a ``case _:`` that
  does nothing, with a comment. A run of separate early-exit ``if``s on one
  subject is a dispatch chain too, and the code after it is its ``case _``.
- **A guard run to one chain.** A run of separate ``if``s, each leaving the
  block, becomes ``if``/``elif``, and the code after the run becomes its
  ``else``.

A chain whose bodies hold a triple-quoted string is skipped and reported:
re-indenting it would change the string.

Usage::

    to_match.py match FILE:LINE [...]
    to_match.py elif FILE:LINE [...]

Each FILE:LINE (repo-relative) is the line of the chain's or run's first
``if``. Each file is processed bottom-up, re-parsed after every edit, so the
lines still to do stay valid.
"""

import ast
import collections
import pathlib
import re
import sys
from collections.abc import Sequence

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
EXITS: tuple[type[ast.stmt], ...] = (ast.Return, ast.Raise, ast.Continue, ast.Break)
NO_DEFAULT: str = "pass  # any other value: nothing to do, as before"


def chain_links(head: ast.If) -> list[ast.If]:
    """The ``if`` and each ``elif`` of a chain.

    Args:
        head: the chain's ``if``.

    Returns:
        list[ast.If]: the links in order.
    """
    match head.orelse:
        case [ast.If() as link] if link.col_offset == head.col_offset:
            return [head, *chain_links(link)]
        case _:
            return [head]


def compared(test: ast.expr, subject: str) -> list[ast.expr] | None:
    """The values a test compares the subject with, or None when it does not.

    Args:
        test: an ``if`` test.
        subject: the subject's source text.

    Returns:
        list[ast.expr] | None: the value expressions, one per alternative.
    """
    match test:
        case ast.Compare(left=left, ops=[ast.Eq()], comparators=[value]) if (
            ast.unparse(left) == subject
        ):
            return [value]
        case ast.Compare(
            left=left,
            ops=[ast.In()],
            comparators=[
                ast.Tuple(elts=items) | ast.List(elts=items) | ast.Set(elts=items)
            ],
        ) if ast.unparse(left) == subject:
            return list(items)
        case ast.BoolOp(op=ast.Or(), values=parts):
            found: list[list[ast.expr] | None] = [
                compared(part, subject) for part in parts
            ]
            return (
                [v for f in found if f is not None for v in f]
                if all(f is not None for f in found)
                else None
            )
        case _:
            return None


def is_pattern_value(node: ast.expr) -> bool:
    """Whether an expression can be written as a ``case`` value pattern.

    Args:
        node: the compared value.

    Returns:
        bool: True for a literal, a negative number, or a dotted name.
    """
    match node:
        case ast.Constant() | ast.Attribute():
            return True
        case ast.UnaryOp(op=ast.USub(), operand=ast.Constant()):
            return True
        case _:
            return False


def best_subject(tests: Sequence[ast.expr]) -> str | None:
    """The subject most tests compare, when at least three do.

    Args:
        tests: the chain's tests.

    Returns:
        str | None: the subject's source text.
    """
    counts: collections.Counter[str] = collections.Counter()
    for test in tests:
        for node in ast.walk(test):
            if isinstance(node, ast.Compare):
                counts[ast.unparse(node.left)] += 1
    for subject, _ in counts.most_common():
        if sum(compared(t, subject) is not None for t in tests) >= 3:
            return subject
    return None


def source(lines: Sequence[str], node: ast.AST) -> str:
    """A node's source text, on one line.

    Args:
        lines: the file's lines.
        node: an expression.

    Returns:
        str: its text with runs of whitespace collapsed.
    """
    text: str = ast.get_source_segment("\n".join(lines), node) or ast.unparse(node)
    return " ".join(text.split())


def trailing_comment(line: str) -> str:
    """The ``# ...`` comment at the end of a header line, with its leading spaces.

    Args:
        line: a header line (``elif x == 1:  # note``).

    Returns:
        str: ``"  # note"``, or an empty string.
    """
    match: re.Match[str] | None = re.search(r":(\s*#.*)$", line)
    return match[1] if match else ""


def indented(lines: Sequence[str], extra: int) -> list[str]:
    """Lines shifted right by extra spaces (blank lines stay blank).

    Args:
        lines: the lines.
        extra: the number of spaces.

    Returns:
        list[str]: the shifted lines.
    """
    return [(" " * extra + line) if line.strip() else line for line in lines]


def body_range(link: ast.If) -> tuple[int, int]:
    """The 0-based [start, end) line range of an ``if`` link's body.

    Args:
        link: the link.

    Returns:
        tuple[int, int]: the range.
    """
    return link.body[0].lineno - 1, link.body[-1].end_lineno or link.body[-1].lineno


def split_leading_comments(lines: list[str], start: int, end: int, indent: int) -> int:
    """Where the lines that move to the next case begin, in [start, end).

    After a link's body, the blank lines, the comment lines at the chain's
    indent (they describe the next case) and an ``else:`` line move with the
    next case; deeper-indented lines stay in the body.

    Args:
        lines: the file's lines.
        start: the range start.
        end: the range end.
        indent: the chain's indent.

    Returns:
        int: the index where the moving lines begin (end if none).
    """
    pad: str = " " * indent
    cut: int = end
    while cut > start and (
        not lines[cut - 1].strip()
        or (lines[cut - 1].startswith(pad + "#"))
        or re.match(rf"{pad}else:\s*(#.*)?$", lines[cut - 1])
    ):
        cut -= 1
    return cut


def has_triple_quote(lines: Sequence[str]) -> bool:
    """Whether any line holds a triple-quoted string delimiter.

    Args:
        lines: the lines.

    Returns:
        bool: True when re-indenting could change a string.
    """
    return any('"""' in line or "'''" in line for line in lines)


def enclosing_block(tree: ast.Module, line: int) -> tuple[list[ast.stmt], int]:
    """The statement list holding the ``if`` that starts on a line, and its index.

    Args:
        tree: the module.
        line: the 1-based line of the ``if``.

    Returns:
        tuple[list[ast.stmt], int]: the block and the index of the ``if``.
    """
    for node in ast.walk(tree):
        for field in ("body", "orelse", "finalbody", "handlers"):
            block: object = getattr(node, field, None)
            if isinstance(block, list):
                for index, statement in enumerate(block):
                    if isinstance(statement, ast.If) and statement.lineno == line:
                        return block, index
    raise ValueError(f"no if statement starts on line {line}")


def run_of(block: Sequence[ast.stmt], index: int) -> list[ast.If]:
    """The guard run that starts at block[index].

    Args:
        block: the statement list.
        index: the run's first ``if``.

    Returns:
        list[ast.If]: the consecutive ``if``s with no ``else`` that end in an exit.
    """
    run: list[ast.If] = []
    for statement in block[index:]:
        if (
            isinstance(statement, ast.If)
            and not statement.orelse
            and isinstance(statement.body[-1], EXITS)
        ):
            run.append(statement)
        else:
            break
    return run


def to_match(
    lines: list[str],
    links: Sequence[ast.If],
    tail_range: tuple[int, int] | None,
    subject: str,
    tail_comment: str = "",
) -> list[str] | None:
    """The ``match`` that replaces a chain (or a dispatch run and the code after it).

    Each link's body runs from the line after its header (comments included)
    to the next link; comment lines at the chain's own indent just before the
    next link describe it, so they move above its ``case``.

    Args:
        lines: the file's lines.
        links: the ``if``/``elif`` links, or the run's ``if``s.
        tail_range: the 0-based [start, end) lines that become ``case _``: an
            ``else`` body (from the line after ``else:``) or the code after a
            run; None when there is neither.
        subject: the subject's source text.
        tail_comment: the trailing comment of the ``else:`` line, kept on
            ``case _:``.

    Returns:
        list[str] | None: the new lines for the chain's whole range, or None
        when the chain must be left for a hand edit.
    """
    indent: int = links[0].col_offset
    pad: str = " " * indent
    out: list[str] = [f"{pad}match {subject}:"]
    lead: list[str] = []
    for number, link in enumerate(links):
        if link.body[0].lineno == link.lineno:
            return None  # a one-line `if x: y`; left for a hand edit
        values: list[ast.expr] | None = compared(link.test, subject)
        pattern: str = (
            " | ".join(source(lines, v) for v in values)
            if values is not None and all(is_pattern_value(v) for v in values)
            else f"_ if {source(lines, link.test)}"
        )
        # The header ends on the first line, from the test's last line on, that
        # ends in a colon (a parenthesized test closes with `):` a line later).
        header_end: int = next(
            k + 1
            for k in range((link.test.end_lineno or link.lineno) - 1, len(lines))
            if re.search(r":\s*(#.*)?$", lines[k])
        )
        boundary: int = (
            links[number + 1].lineno - 1
            if number + 1 < len(links)
            else (
                tail_range[0]
                if tail_range
                else (link.body[-1].end_lineno or link.body[-1].lineno)
            )
        )
        cut: int = split_leading_comments(lines, header_end, boundary, indent)
        body: list[str] = lines[header_end:cut]
        if has_triple_quote(body):
            return None
        out.extend(indented(lead, 4))
        out.append(f"{pad}    case {pattern}:{trailing_comment(lines[header_end - 1])}")
        out.extend(indented(body, 4))
        lead = [
            line
            for line in lines[cut:boundary]
            if line.strip() and not re.match(rf"{pad}else:\s*(#.*)?$", line)
        ]
    out.extend(indented(lead, 4))
    out.append(f"{pad}    case _:{tail_comment}")
    if tail_range is not None:
        tail_lines: list[str] = lines[tail_range[0] : tail_range[1]]
        if has_triple_quote(tail_lines):
            return None
        shift: int = (
            indent + 8 - min(len(t) - len(t.lstrip()) for t in tail_lines if t.strip())
        )
        out.extend(indented(tail_lines, shift))
    else:
        out.append(f"{pad}        {NO_DEFAULT}")
    return out


def convert_match(path: pathlib.Path, line: int) -> bool:
    """Rewrite the chain or dispatch run starting on a line as a ``match``.

    Args:
        path: the file.
        line: the 1-based line of its first ``if``.

    Returns:
        bool: False when it was left for a hand edit.
    """
    text: str = path.read_text()
    lines: list[str] = text.split("\n")
    tree: ast.Module = ast.parse(text)
    block, index = enclosing_block(tree, line)
    statement: ast.stmt = block[index]
    if not isinstance(statement, ast.If):
        raise TypeError(f"line {line} does not start an if statement")
    head: ast.If = statement
    links: list[ast.If] = chain_links(head)
    rest: list[ast.stmt] = []
    else_body: list[ast.stmt] = list(links[-1].orelse)
    if len(links) == 1:  # a guard run
        links = run_of(block, index)
        rest = list(block[index + len(links) :])
        else_body = []
    subject: str | None = best_subject([link.test for link in links])
    if subject is None:
        return False
    tail_range: tuple[int, int] | None = None
    else_line: int = 0
    end: int = links[-1].end_lineno or links[-1].lineno
    if else_body:
        last_body_end: int = links[-1].body[-1].end_lineno or links[-1].body[-1].lineno
        else_line: int = next(
            k
            for k in range(last_body_end, else_body[0].lineno)
            if re.match(r"\s*else:\s*(#.*)?$", lines[k])
        )
        tail_range = (else_line + 1, else_body[-1].end_lineno or else_body[-1].lineno)
        end = tail_range[1]
    elif rest:
        tail_range = (
            links[-1].end_lineno or links[-1].lineno,
            rest[-1].end_lineno or rest[-1].lineno,
        )
        end = tail_range[1]
    new: list[str] | None = to_match(
        lines,
        links,
        tail_range,
        subject,
        trailing_comment(lines[else_line]) if else_body else "",
    )
    if new is None:
        return False
    lines[head.lineno - 1 : end] = new
    path.write_text("\n".join(lines))
    return True


def convert_elif(path: pathlib.Path, line: int) -> bool:
    """Rewrite the guard run starting on a line as one if/elif/else chain.

    Args:
        path: the file.
        line: the 1-based line of its first ``if``.

    Returns:
        bool: False when it was left for a hand edit.
    """
    text: str = path.read_text()
    lines: list[str] = text.split("\n")
    block, index = enclosing_block(ast.parse(text), line)
    run: list[ast.If] = run_of(block, index)
    if len(run) < 2:
        return False  # no longer a run (an inner run was converted); rescan
    rest: list[ast.stmt] = list(block[index + len(run) :])
    pad: str = " " * run[0].col_offset
    if rest:
        rest_start: int = run[-1].end_lineno or run[-1].lineno
        rest_end: int = rest[-1].end_lineno or rest[-1].lineno
        tail: list[str] = lines[rest_start:rest_end]
        if has_triple_quote(tail):
            return False
        lines[rest_start:rest_end] = [f"{pad}else:", *indented([t for t in tail], 4)]
    for statement in reversed(run[1:]):
        first: str = lines[statement.lineno - 1]
        if not first.startswith(pad + "if "):
            return False
        lines[statement.lineno - 1] = pad + "elif " + first[len(pad) + 3 :]
    path.write_text("\n".join(lines))
    return True


def main() -> int:
    """Convert every FILE:LINE given; report the ones left for a hand edit.

    Returns:
        int: the exit status, 0.
    """
    mode: str = sys.argv[1]
    by_file: dict[str, list[int]] = collections.defaultdict(list)
    for spec in sys.argv[2:]:
        file, line = spec.rsplit(":", 1)
        by_file[file].append(int(line))
    for file, numbers in by_file.items():
        for line in sorted(numbers, reverse=True):
            done: bool = (convert_match if mode == "match" else convert_elif)(
                REPO / file, line
            )
            print(f"{'converted' if done else 'LEFT'} {mode} {file}:{line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
