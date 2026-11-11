#!/usr/bin/env python3
"""Write the inferred types binding-types reports into the Haskell port's sources.

Reads the tab-separated report of ``port/haskell/tools/BindingTypes.hs`` (see
its header) and edits ``port/haskell/src/*.hs``:

- ``do``: ``x <- e`` becomes ``x :: T <- e``; a tuple or parenthesized
  pattern takes the signature after it, and any other pattern is wrapped:
  ``(Just x :: Maybe T) <- e``.
- ``local``: a signature line ``name :: T`` goes above the binding, at its
  column. When the name shares its line with ``let`` or ``where``, the line is
  split so the signature stays inside the block.
- ``pattern``: one signature line per bound variable, placed the same way.

Edits are applied bottom-up within each file, so the reported positions of
the edits still to come stay valid. A report line whose position does not
hold its name is skipped and printed, for a hand edit.

Usage::

    annotate.py REPORT
"""

import collections
import dataclasses
import pathlib
import re
import sys

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PORT: pathlib.Path = REPO / "port" / "haskell"
VARIABLE: re.Pattern[str] = re.compile(r"[a-z_][\w']*")


@dataclasses.dataclass(frozen=True, slots=True)
class Report:
    """One line of the binding-types report.

    Attributes:
        kind: ``do``, ``local`` or ``pattern``.
        file: the source file, relative to port/haskell.
        line: the 1-based start line.
        column: the 1-based start column.
        end_line: the 1-based end line.
        end_column: one past the last column.
        names: the bound names.
        types: one type per name (a single type for ``do`` and ``local``).
    """

    kind: str
    file: str
    line: int
    column: int
    end_line: int
    end_column: int
    names: tuple[str, ...]
    types: tuple[str, ...]


def read_report(path: pathlib.Path) -> list[Report]:
    """Parse the report.

    Args:
        path: the report file.

    Returns:
        list[Report]: one entry per line.
    """
    reports: list[Report] = []
    for line in path.read_text().splitlines():
        kind, file, start, column, end, end_column, names, types = line.split("\t")
        reports.append(
            Report(
                kind,
                file,
                int(start),
                int(column),
                int(end),
                int(end_column),
                tuple(names.split()),
                tuple(types.split(";;")),
            )
        )
    return reports


def signature_name(name: str) -> str:
    """A name as it appears in a signature: an operator is parenthesized.

    Args:
        name: the bound name.

    Returns:
        str: ``name`` or ``(op)``.
    """
    return name if VARIABLE.fullmatch(name) else f"({name})"


def signature_lines(lines: list[str], report: Report) -> list[str] | None:
    """The replacement for a binding's first line: signatures, then the binding.

    Args:
        lines: the file's lines.
        report: a ``local`` or ``pattern`` report.

    Returns:
        list[str] | None: the new lines, or None when the binding is not at
        the reported position.
    """
    text: str = lines[report.line - 1]
    prefix: str = text[: report.column - 1]
    rest: str = text[report.column - 1 :]
    indent: str = " " * (report.column - 1)
    first: str = report.names[0] if report.kind == "local" else "("
    if not (rest.startswith(first) or (report.kind == "pattern" and rest[:1] in "(!~")):
        return None
    signatures: list[str] = [
        f"{signature_name(n)} :: {t}"
        for n, t in zip(report.names, report.types, strict=True)
    ]
    if prefix.strip():
        # `let x = ...` / `where x = ...`: the first signature takes the
        # binding's place after the keyword; the binding moves to its own line.
        return [
            prefix + signatures[0],
            *[indent + s for s in signatures[1:]],
            indent + rest,
        ]
    return [indent + s for s in signatures] + [text]


def annotate_do(lines: list[str], report: Report) -> bool:
    """Add the type to a do-bind's pattern, in place.

    Args:
        lines: the file's lines.
        report: a ``do`` report.

    Returns:
        bool: False when the pattern spans lines, which is left for a hand edit.
    """
    if report.line != report.end_line:
        return False
    text: str = lines[report.line - 1]
    start: int = report.column - 1
    end: int = report.end_column - 1
    pattern: str = text[start:end]
    if VARIABLE.fullmatch(pattern) or (
        pattern.startswith("(") and pattern.endswith(")")
    ):
        lines[report.line - 1] = text[:end] + f" :: {report.types[0]}" + text[end:]
    else:
        lines[report.line - 1] = (
            text[:start] + f"({pattern} :: {report.types[0]})" + text[end:]
        )
    return True


def main() -> int:
    """Apply every report; print the ones left for a hand edit.

    Returns:
        int: the exit status, 0.
    """
    by_file: dict[str, list[Report]] = collections.defaultdict(list)
    for report in read_report(pathlib.Path(sys.argv[1])):
        by_file[report.file].append(report)
    skipped: list[Report] = []
    for file, reports in by_file.items():
        path: pathlib.Path = PORT / file
        lines: list[str] = path.read_text().split("\n")
        for report in sorted(reports, key=lambda r: (r.line, r.column), reverse=True):
            match report.kind:
                case "do":
                    if not annotate_do(lines, report):
                        skipped.append(report)
                case "local" | "pattern":
                    replacement: list[str] | None = signature_lines(lines, report)
                    if replacement is None:
                        skipped.append(report)
                    else:
                        lines[report.line - 1 : report.line] = replacement
                case _:
                    raise ValueError(f"unknown kind {report.kind!r}")
        path.write_text("\n".join(lines))
    for report in skipped:
        print(
            f"skipped {report.kind} {report.file}:{report.line}:{report.column} {' '.join(report.names)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
