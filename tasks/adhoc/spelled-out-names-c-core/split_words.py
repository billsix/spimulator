#!/usr/bin/env python3
"""Split the core's names into words, and say which words a dictionary does not know.

Reads ``names.tsv`` and ``macros.tsv`` (rows ``kind  name  path  line``) and prints one
tab-separated row per distinct lower-cased word::

    word  in_dictionary  name_count  use_count  kinds  example_names

``in_dictionary`` is ``yes`` when hunspell's ``en_US`` dictionary accepts the word. ``name_count``
is how many distinct names contain it; ``use_count`` is how many times those names appear as
identifiers in ``src/`` and ``include/``. Rows are sorted unknown-first, then by ``use_count``.
Include guards (``NAME_H``) are dropped.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/split_words.py names.tsv macros.tsv
"""

import collections
import pathlib
import re
import subprocess
import sys

IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# A word is a run of capitals not followed by a lower-case letter (an acronym), a capitalised
# or lower-case run, or a run of digits.
WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")
EXAMPLES: int = 6


def words_of(name: str) -> list[str]:
    """The lower-cased words of one identifier, split on ``_``, case changes and digits.

    Args:
        name: an identifier.

    Returns:
        list[str]: its words, in order.
    """
    return [word.lower() for word in WORD.findall(name)]


def dictionary_words(words: list[str]) -> set[str]:
    """The words hunspell's ``en_US`` dictionary accepts.

    Args:
        words: the words to check.

    Returns:
        set[str]: the accepted ones.
    """
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ["hunspell", "-d", "en_US", "-l"],
        input="\n".join(words) + "\n",
        capture_output=True,
        text=True,
        check=True,
    )
    return set(words) - set(completed.stdout.split())


def main(tables: list[pathlib.Path]) -> int:
    """Print the word table.

    Args:
        tables: the ``names.tsv`` and ``macros.tsv`` paths.

    Returns:
        int: 0.
    """
    kinds_of_name: dict[str, set[str]] = collections.defaultdict(set)
    table: pathlib.Path
    for table in tables:
        line: str
        for line in table.read_text().splitlines():
            kind: str
            name: str
            kind, name, *_ = line.split("\t")
            if not (kind == "MacroDefinition" and name.endswith("_H")):
                kinds_of_name[name].add(
                    kind.removesuffix("Decl").removesuffix("Definition")
                )
    sources: list[str] = subprocess.run(
        ["git", "ls-files", "src", "include"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    uses: collections.Counter[str] = collections.Counter(
        identifier
        for source in sources
        for identifier in IDENTIFIER.findall(pathlib.Path(source).read_text())
    )
    names_of_word: dict[str, set[str]] = collections.defaultdict(set)
    for name in kinds_of_name:
        word: str
        for word in words_of(name):
            names_of_word[word].add(name)
    known: set[str] = dictionary_words(sorted(names_of_word))
    rows: list[tuple[str, str, int, int, str, str]] = []
    for word, names in names_of_word.items():
        by_use: list[str] = sorted(names, key=lambda n: (-uses[n], n))
        rows.append(
            (
                word,
                "yes" if word in known else "no",
                len(names),
                sum(uses[n] for n in names),
                ",".join(sorted({k for n in names for k in kinds_of_name[n]})),
                " ".join(by_use[:EXAMPLES]),
            )
        )
    row: tuple[str, str, int, int, str, str]
    for row in sorted(rows, key=lambda r: (r[1] == "yes", -r[3], r[0])):
        sys.stdout.write("\t".join(map(str, row)) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main([pathlib.Path(argument) for argument in sys.argv[1:]]))
