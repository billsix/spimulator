#!/usr/bin/env python3
"""Mark the words that occur only inside assembler-keyword token names.

A token such as ``TOK_ADDIU_OPCODE``, ``TOK_LI_PSEUDO_OP`` or ``TOK_ASCIIZ_DIRECTIVE`` is
generated from ``include/opcodes.tsv`` and spells the keyword a user types in assembly. The
keyword part is the assembly language's own vocabulary, so it is exempt; the ``TOK`` and
``OPCODE``/``PSEUDO_OP``/``DIRECTIVE`` parts are not. This reads ``words.tsv`` and the names
tables and prints ``words.tsv`` with one more column, ``keyword_only`` (``yes`` when every name
containing the word is such a token and the word is not one of the fixed parts).

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/classify_words.py words.tsv names.tsv macros.tsv
"""

import pathlib
import re
import sys

KEYWORD_TOKEN: re.Pattern[str] = re.compile(
    r"^TOK_(\w+?)_(OPCODE|PSEUDO_OP|DIRECTIVE)$"
)
FIXED_PARTS: frozenset[str] = frozenset({"tok", "opcode", "pseudo", "op", "directive"})
WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")


def main(words_table: pathlib.Path, name_tables: list[pathlib.Path]) -> int:
    """Print ``words.tsv`` with the ``keyword_only`` column appended.

    Args:
        words_table: the ``words.tsv`` path.
        name_tables: the ``names.tsv`` and ``macros.tsv`` paths.

    Returns:
        int: 0.
    """
    outside_tokens: set[str] = set()
    table: pathlib.Path
    for table in name_tables:
        line: str
        for line in table.read_text().splitlines():
            name: str = line.split("\t")[1]
            match: re.Match[str] | None = KEYWORD_TOKEN.match(name)
            parts: str = name if match is None else ""
            outside_tokens.update(word.lower() for word in WORD.findall(parts))
    for line in words_table.read_text().splitlines():
        word: str = line.split("\t")[0]
        keyword_only: bool = word not in outside_tokens and word not in FIXED_PARTS
        sys.stdout.write(f"{line}\t{'yes' if keyword_only else 'no'}\n")
    return 0


if __name__ == "__main__":
    sys.exit(
        main(pathlib.Path(sys.argv[1]), [pathlib.Path(path) for path in sys.argv[2:]])
    )
