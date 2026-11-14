#!/usr/bin/env python3
"""Rename the C identifiers that the open docs cite in Markdown code spans.

A renamed identifier is rewritten only inside backticks, where a doc names code, so prose that
uses an architecture term ("the FIR register") is left as written. Only renames of every
declaration (``*`` rows) whose old name is clearly an identifier (it contains ``_``, or is upper
case of three letters or more) apply. Archived tasks, review records and the ad-hoc scripts are
history and are not touched, nor are the spelled-out-names task docs, which discuss the old
names on purpose.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/rename_doc_references.py
"""

import pathlib
import re
import subprocess
import sys

DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
CODE_SPAN: re.Pattern[str] = re.compile(r"`[^`\n]+`")
IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SKIPPED_PREFIXES: tuple[str, ...] = (
    "tasks/archive/",
    "tasks/review/",
    "tasks/adhoc/",
    "tasks/spelled-out-names-",
)


def renames() -> dict[str, str]:
    """Old to new, for renames of every declaration whose old name is clearly code.

    Returns:
        dict[str, str]: the renames.
    """
    table: dict[str, str] = {}
    line: str
    for line in (DATA / "renames.tsv").read_text().splitlines():
        fields: list[str] = line.split("\t")
        old: str = fields[0]
        if fields[4] == "*" and ("_" in old or (old.upper() == old and len(old) >= 3)):
            table[old] = fields[1]
    return table


def main() -> int:
    """Rewrite the code spans of every open Markdown doc.

    Returns:
        int: 0.
    """
    table: dict[str, str] = renames()
    files: list[str] = subprocess.run(
        ["git", "ls-files", "*.md"], capture_output=True, text=True, check=True
    ).stdout.split()

    def rename_span(match: re.Match[str]) -> str:
        return IDENTIFIER.sub(
            lambda word: table.get(word.group(0), word.group(0)), match.group(0)
        )

    name: str
    for name in files:
        if name.startswith(SKIPPED_PREFIXES):
            continue
        path: pathlib.Path = pathlib.Path(name)
        text: str = path.read_text()
        renamed: str = CODE_SPAN.sub(rename_span, text)
        if renamed != text:
            path.write_text(renamed)
            print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
