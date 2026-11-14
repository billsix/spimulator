#!/usr/bin/env python3
"""Derive the rename table for the C core from the glossary.

Reads ``data/names.tsv``, ``data/macros.tsv``, ``data/glossary.tsv`` and the hand-written
``data/renames.manual.tsv`` (``old  new``; ``new`` equal to ``old`` means "keep"), and writes:

- ``data/renames.tsv``: ``old  new  kinds  source  location`` for every name that changes;
  ``location`` is ``*`` (every declaration) or the ``path:line`` of the one declaration. ``source`` is
  ``auto`` (each word spelled out from the glossary, or a keyword token's ``TOK`` prefix) or
  ``manual``.
- ``data/renames.needed.tsv``: the names the automatic rule cannot decide, because they mix
  upper and lower case, or contain a word with more than one meaning (glossary spelling "per
  name", "(1) ..." or one of AMBIGUOUS). Each needs a row in ``renames.manual.tsv``.

It exits 1 if a name still needs a manual row, or a new name is a C keyword, collides with a
non-local name already declared in the core, or is shared by renames that may not share it.
Locals in different functions may share a spelling; the ``-Wshadow`` build checks their scopes.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/rename_map.py
"""

import collections
import pathlib
import re
import sys

DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")
# A generated assembler-keyword token: the keyword is the assembly language's own spelling, so
# only the TOK prefix is spelled out.
KEYWORD_TOKEN: re.Pattern[str] = re.compile(
    r"^TOK_(\w+?)_(OPCODE|PSEUDO_OP|DIRECTIVE)$"
)
# Variables, parameters and struct members: a member lives in its own namespace, a same-scope
# duplicate fails to compile, and -Wshadow reports a local that hides an outer name.
LOCAL_KINDS: frozenset[str] = frozenset({"VarDecl", "ParmVarDecl", "FieldDecl"})
SPELLED_CATEGORIES: frozenset[str] = frozenset({"spell-out", "architecture"})
# Words whose glossary spelling is a single phrase, but which also occur with another meaning
# (an English word, an assembler register name, or a CP0 field mnemonic).
AMBIGUOUS: frozenset[str] = frozenset(
    {"at", "be", "im", "ip", "ie", "cu", "um", "ar", "mt", "el", "es", "sp", "gp", "fd"}
    | {
        "hi",
        "lo",
        "res",
        "fres",
        "fa",
        "tr",
        "ov",
        "ri",
        "sys",
        "bp",
        "fpe",
        "dbe",
        "ibe",
    }
    | {"tlbl", "tlbs", "mod", "int", "id", "in", "sh", "cp"}
)
# Names fixed by something outside the core: the C runtime, POSIX, or the build.
EXTERNAL: frozenset[str] = frozenset({"main", "argc", "argv", "environ", "fd_set"})
C_KEYWORDS: frozenset[str] = frozenset(
    [
        "alignas",
        "alignof",
        "auto",
        "bool",
        "break",
        "case",
        "char",
        "const",
        "constexpr",
        "continue",
        "default",
        "do",
        "double",
        "else",
        "enum",
        "extern",
        "false",
        "float",
        "for",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "nullptr",
        "register",
        "restrict",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "struct",
        "switch",
        "thread_local",
        "true",
        "typedef",
        "typeof",
        "typeof_unqual",
        "union",
        "unsigned",
        "void",
        "volatile",
        "while",
    ]
)


def read_rows(path: pathlib.Path) -> list[list[str]]:
    """The tab-separated rows of a data file, without blank or ``#`` lines.

    Args:
        path: the file.

    Returns:
        list[list[str]]: its rows.
    """
    return [
        line.split("\t")
        for line in path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    ]


def spellings() -> tuple[dict[str, str], set[str]]:
    """The glossary's word spellings, and the words it marks as decided per name.

    Returns:
        tuple[dict[str, str], set[str]]: word to spelled-out phrase, and the per-name words.
    """
    spelled: dict[str, str] = {}
    per_name: set[str] = set()
    row: list[str]
    for row in read_rows(DATA / "glossary.tsv")[1:]:
        word: str = row[0]
        spelling: str = row[2]
        category: str = row[3]
        if "/" in category or "per name" in spelling or spelling.startswith("("):
            per_name.add(word)
        elif category in SPELLED_CATEGORIES and spelling != "keep":
            spelled[word] = spelling
        else:
            pass  # kept: keyword, familiar, external, math, not-abbreviation
    return spelled, per_name | AMBIGUOUS


def spell_out(name: str, spelled: dict[str, str]) -> str:
    """Spell out each glossary word of a single-case name, keeping its case and separators.

    Args:
        name: an all-lower or all-upper identifier (digits and ``_`` allowed).
        spelled: word to spelled-out phrase.

    Returns:
        str: the new name.
    """
    upper: bool = name.upper() == name and name.lower() != name

    def replace(match: re.Match[str]) -> str:
        word: str = match.group(0)
        phrase: str = spelled.get(word.lower(), word.lower())
        return phrase.upper() if upper else phrase

    return re.sub(r"[A-Za-z]+", replace, name)


def shareable(kind_sets: list[set[str]]) -> bool:
    """Whether several names may all take one new spelling.

    Args:
        kind_sets: the declaration kinds of each name.

    Returns:
        bool: True when every name is a local, or they are one struct tag and one typedef.
    """
    every: set[str] = set().union(*kind_sets)
    return every <= LOCAL_KINDS or (
        len(kind_sets) == 2 and every <= {"RecordDecl", "TypedefDecl"}
    )


def main() -> int:
    """Write ``renames.tsv`` and ``renames.needed.tsv``.

    Returns:
        int: 0 when every name is decided and nothing collides, else 1.
    """
    spelled: dict[str, str]
    per_name: set[str]
    spelled, per_name = spellings()
    kinds_of: dict[str, set[str]] = collections.defaultdict(set)
    row: list[str]
    for row in read_rows(DATA / "names.tsv") + read_rows(DATA / "macros.tsv"):
        if not (row[0] == "MacroDefinition" and row[1].endswith("_H")):
            kinds_of[row[1]].add(row[0])
    manual_rows: list[list[str]] = read_rows(DATA / "renames.manual.tsv")
    manual: dict[str, str] = {row[0]: row[1] for row in manual_rows if len(row) == 2}
    # Rows that rename one declaration only: (old, new, path:line).
    located: list[tuple[str, str, str]] = [
        (row[0], row[1], row[2]) for row in manual_rows if len(row) == 3
    ]
    located_names: set[str] = {old for old, _, _ in located}
    renames: dict[str, tuple[str, str]] = {}
    needed: list[str] = []
    old: str
    new: str
    name: str
    for name in sorted(kinds_of):
        words: list[str] = [word.lower() for word in WORD.findall(name)]
        mixed_case: bool = name.lower() != name and name.upper() != name
        if name in EXTERNAL or name.startswith("_"):
            continue  # the runtime's, or a macro's internal temporary
        elif name in located_names and name not in manual:
            continue  # only its located declarations change
        elif KEYWORD_TOKEN.match(name):
            renames[name] = ("TOKEN_" + name.removeprefix("TOK_"), "auto")
        elif name in manual:
            if manual[name] != name:
                renames[name] = (manual[name], "manual")
        elif mixed_case or any(word in per_name for word in words):
            if any(word in spelled or word in per_name for word in words):
                needed.append(name)
        else:
            new = spell_out(name, spelled)
            if new != name:
                renames[name] = (new, "auto")
    status: int = 0
    olds_of_new: dict[str, list[str]] = collections.defaultdict(list)
    for old, (new, _) in renames.items():
        olds_of_new[new].append(old)
    source: str
    for old, (new, source) in sorted(renames.items()):
        if new in C_KEYWORDS:
            print(f"keyword: {old} -> {new}", file=sys.stderr)
            status = 1
        elif (
            new in kinds_of
            and new not in renames
            and not (kinds_of[old] | kinds_of[new]) <= LOCAL_KINDS
        ):
            print(f"collides with an existing name: {old} -> {new}", file=sys.stderr)
            status = 1
        elif len(olds_of_new[new]) > 1 and not shareable(
            [kinds_of[other] for other in olds_of_new[new]]
        ):
            print(f"two names become {new}: {old}", file=sys.stderr)
            status = 1
        else:
            pass
    (DATA / "renames.tsv").write_text(
        "".join(
            f"{old}\t{new}\t{','.join(sorted(kinds_of[old]))}\t{source}\t*\n"
            for old, (new, source) in sorted(renames.items())
        )
        + "".join(
            f"{old}\t{new}\t{','.join(sorted(kinds_of[old]))}\tmanual\t{location}\n"
            for old, new, location in sorted(located)
        )
    )
    (DATA / "renames.needed.tsv").write_text(
        "".join(f"{name}\t{','.join(sorted(kinds_of[name]))}\n" for name in needed)
    )
    if needed:
        print(f"{len(needed)} names need a manual row", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
