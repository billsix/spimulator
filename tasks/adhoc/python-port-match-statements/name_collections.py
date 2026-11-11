#!/usr/bin/env python3
"""Turn the Python port's collections of token-name strings into ``Tok`` members.

The second step after to_enums.py, for the collections it leaves:

- ``_tokens("A_OPCODE", "B_OPCODE")`` (instruction.py) becomes
  ``frozenset({Tok.A_OPCODE, Tok.B_OPCODE})``, and the helper goes.
- ``frozenset(TOK[name] for name in ("A_OPCODE", ...))`` and
  ``tuple(TOK[n] for n in (...))`` (parser.py) become
  ``frozenset((Tok.A_OPCODE, ...))`` and ``(Tok.A_OPCODE, ...)``.
- pseudo_op.py's two ``dict[str, str]`` tables of names become
  ``dict[int, int]`` tables of members, and the comprehensions that looked
  the names up go.

Only a string literal that is exactly the name of a ``Tok`` member, inside
one of those constructs, is rewritten.

Usage::

    name_collections.py
"""

import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PACKAGE: pathlib.Path = REPO / "port" / "python" / "spimulator_py"
MEMBERS: frozenset[str] = frozenset(
    re.findall(
        r"^    (\w+) = \d+$",
        (PACKAGE / "opcode_table.py").read_text().split("class Tok(")[1],
        re.MULTILINE,
    )
)


def members(text: str) -> str:
    """Rewrite every quoted token name in a stretch of source as a member.

    Args:
        text: source text.

    Returns:
        str: the text with ``"X"`` replaced by ``Tok.X`` where X is a member.
    """
    return re.sub(
        r'"(\w+)"', lambda m: f"Tok.{m[1]}" if m[1] in MEMBERS else m[0], text
    )


def closing(text: str, open_index: int) -> int:
    """The index of the parenthesis that closes the one at open_index.

    Args:
        text: source text.
        open_index: the index of a ``(``.

    Returns:
        int: the index of its matching ``)``.
    """
    depth: int = 0
    for index in range(open_index, len(text)):
        match text[index]:
            case "(":
                depth += 1
            case ")":
                depth -= 1
                if depth == 0:
                    return index
            case _:
                pass  # any other character leaves the depth unchanged
    raise ValueError("unbalanced parentheses")


def instruction_py(text: str) -> str:
    """Replace every ``_tokens(...)`` call with a frozenset literal and drop the helper.

    Args:
        text: instruction.py's source.

    Returns:
        str: the converted source.
    """
    helper: re.Match[str] | None = re.search(
        r"\ndef _tokens\(\*names: str\).*?\n    return frozenset\(TOK\[name\] for name in names\)\n",
        text,
        re.DOTALL,
    )
    if helper is not None:
        text = text[: helper.start()] + "\n" + text[helper.end() :]
    while (call := re.search(r"(?<![\w.])_tokens\(", text)) is not None:
        open_index: int = call.end() - 1
        close: int = closing(text, open_index)
        text = (
            text[: call.start()]
            + "frozenset({"
            + members(text[open_index + 1 : close])
            + "})"
            + text[close + 1 :]
        )
    return text.replace("from .opcodes import KIND\n", "")


def parser_py(text: str) -> str:
    """Replace the ``TOK[name] for name in (...)`` collections with member tuples.

    Args:
        text: parser.py's source.

    Returns:
        str: the converted source.
    """
    pattern: re.Pattern[str] = re.compile(
        r"(?P<head>frozenset|tuple)\(\n(?P<indent>\s+)TOK\[(?P<var>\w+)\]\n\s+for (?P=var) in "
    )
    while (match := pattern.search(text)) is not None:
        open_index: int = text.index("(", match.end() - 1)
        close: int = closing(text, open_index)
        inner: str = members(text[open_index : close + 1])
        outer_close: int = closing(text, text.index("(", match.start()))
        replacement: str = (
            f"frozenset({inner})" if match["head"] == "frozenset" else inner
        )
        text = text[: match.start()] + replacement + text[outer_close + 1 :]
    return re.sub(r"\n    KIND,", "", text)


def pseudo_op_py(text: str) -> str:
    """Make the two name tables member tables and drop the lookups built on them.

    Args:
        text: pseudo_op.py's source.

    Returns:
        str: the converted source.
    """
    for table in ("_IMM_TO_OP", "_OP_TO_IMM"):
        start: int = text.index(f"{table}: dict[str, str] = {{")
        end: int = text.index("}\n", start) + 2
        text = (
            text[:start]
            + members(text[start:end]).replace("dict[str, str]", "dict[int, int]")
            + text[end:]
        )
        text = re.sub(
            rf"{table}_TOKENS: dict\[int, int\] = \{{TOK\[a\]: TOK\[b\] for a, b in {table}\.items\(\)\}}\n",
            "",
            text,
        )
        text = re.sub(rf"\b{table}_TOKENS\b", table, text)
    return text


def main() -> int:
    """Convert the three modules.

    Returns:
        int: the exit status, 0.
    """
    for name, convert in (
        ("instruction.py", instruction_py),
        ("parser.py", parser_py),
        ("pseudo_op.py", pseudo_op_py),
    ):
        path: pathlib.Path = PACKAGE / name
        before: str = path.read_text()
        after: str = convert(before)
        if after != before:
            path.write_text(after)
            print(f"converted {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
