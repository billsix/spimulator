"""Rewrite runs of early-exit `if`s in C source as one `if` / `else if` / `else` chain.

Usage, from anywhere:

    python3 tasks/adhoc/c-switch-and-else-if-chains/guards_to_else_if.py FILE:LINE [FILE:LINE ...]

Each FILE:LINE names the first `if` of a run, as `data/guard_runs.tsv` lists it (paths relative
to the repository root). The run is every `if` that follows it in the same block, at the same
indentation, with only blank lines or comments in between, that has no `else` and whose body ends
by leaving the block (return, continue, break, goto, or a no-return call); the code after the last one, up to the
end of the block, becomes the chain's `else`. Comments between two guards move to the top of the
next branch. A run whose guards and tail are all single statements keeps the unbraced form;
otherwise every branch is braced. Runs are rewritten bottom-up within each file, so a run nested in
another's tail is rewritten first. The tool checks its own work: every non-blank line of the
original run, apart from `if`/`}` punctuation it rewrites, must survive. Run `clang-format`
afterwards.

It is text surgery, so it handles only the shapes this task's runs have: a guard is
`if (COND) STATEMENT;` on one line, or `if (COND) {` (COND may span lines) through the matching
`}` on its own line.
"""

import pathlib
import sys
from dataclasses import dataclass

REPO_ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]


@dataclass(frozen=True, slots=True, kw_only=True)
class Guard:
    """One `if` of a run.

    Attributes:
        leading: Comment lines that came before it (after the previous guard).
        header: The `if (COND)` lines, without the trailing `{`, or the whole line when unbraced.
        body: The body's lines (empty when unbraced).
        braced: Whether the guard had braces.
    """

    leading: list[str]
    header: list[str]
    body: list[str]
    braced: bool


def indent_of(line: str) -> int:
    """Count a line's leading spaces.

    Args:
        line: A source line.

    Returns:
        The number of leading spaces.
    """
    return len(line) - len(line.lstrip(" "))


def is_comment_or_blank(line: str) -> bool:
    """Say whether a line holds only a comment (or comment continuation) or nothing.

    Args:
        line: A source line.

    Returns:
        True for blank lines and `/* ... */`, ` * ...` or `// ...` lines.
    """
    stripped: str = line.strip()
    return stripped == "" or stripped.startswith(("/*", "*", "//"))


# Calls that never return, so a body ending in one leaves the block.
NORETURN_CALLS: tuple[str, ...] = ("fatal_error(", "run_error(", "exit(", "abort(")


def leaves(statement: str) -> bool:
    """Say whether a statement always leaves the enclosing block.

    Args:
        statement: One line of code: the last statement of a guard's body.

    Returns:
        True for return, continue, break, goto, or a call to a function that never returns.
    """
    stripped: str = statement.strip()
    return stripped.startswith(
        ("return", "continue;", "break;", "goto ", *NORETURN_CALLS)
    )


def read_guard(lines: list[str], index: int, indent: int) -> tuple[Guard | None, int]:
    """Read the guard starting at `index`, after any leading comment lines.

    Args:
        lines: The file's lines.
        index: Where to start reading.
        indent: The run's indentation.

    Returns:
        The guard and the index after it, or None and the index of the first line that is not
        part of a guard (the tail).
    """
    leading: list[str] = []
    position: int = index
    while position < len(lines) and is_comment_or_blank(lines[position]):
        # A block comment's later lines need not start with `*`: take it whole.
        opens_block: bool = lines[position].strip().startswith("/*")
        while opens_block and "*/" not in lines[position]:
            leading.append(lines[position])
            position += 1
        leading.append(lines[position])
        position += 1
    line: str = lines[position]
    if indent_of(line) != indent or not line.strip().startswith("if ("):
        return None, index
    header: list[str] = [line]
    while not header[-1].rstrip().endswith("{") and not header[-1].rstrip().endswith(
        ";"
    ):
        position += 1
        header.append(lines[position])
    if header[-1].rstrip().endswith(";"):
        if not leaves(header[-1].split(")")[-1]):
            return None, index
        else:
            return Guard(
                leading=leading, header=header, body=[], braced=False
            ), position + 1
    header[-1] = header[-1].rstrip()[:-1].rstrip()
    body: list[str] = []
    position += 1
    depth: int = 1
    while True:
        depth += lines[position].count("{") - lines[position].count("}")
        if depth == 0:
            break
        else:
            body.append(lines[position])
            position += 1
    # A guard ends at its own `}`: an `} else` means this `if` already has an else branch.
    last_code: list[str] = [line for line in body if not is_comment_or_blank(line)]
    if (
        lines[position] != " " * indent + "}"
        or not last_code
        or not leaves(last_code[-1])
    ):
        return None, index
    else:
        return Guard(
            leading=leading, header=header, body=body, braced=True
        ), position + 1


def is_one_chain(tail: list[str], indent: int) -> bool:
    """Say whether a tail is exactly one braced `if` / `else if` / `else` chain.

    Args:
        tail: The tail's lines, with blank lines trimmed from both ends.
        indent: The run's indentation.

    Returns:
        True if the tail's lines at the run's indentation are the chain's `if (` line, its
        `} else` lines, and its closing `}` as the last line, and nothing else.
    """
    pad: str = " " * indent
    at_level: list[str] = [
        line for line in tail if line.strip() and indent_of(line) == indent
    ]
    return (
        len(at_level) >= 2
        and at_level[0] == tail[0]
        and tail[0].startswith(pad + "if (")
        and at_level[-1] == tail[-1] == pad + "}"
        and all(line.startswith(pad + "} else") for line in at_level[1:-1])
    )


def moved(leading: list[str], pad: str) -> list[str]:
    """Re-indent comment lines that move into a branch, keeping their relative indentation.

    Args:
        leading: The comment (and blank) lines from between two guards.
        pad: The run's indentation; the branch body is two spaces deeper.

    Returns:
        The non-blank lines, re-indented.
    """
    kept: list[str] = [line for line in leading if line.strip()]
    if not kept:
        return []
    else:
        base: int = indent_of(kept[0])
        return [
            pad + "  " + " " * max(indent_of(line) - base, 0) + line.strip()
            for line in kept
        ]


def convert(lines: list[str], start: int) -> list[str]:
    """Rewrite the run whose first `if` is at `start` (0-based).

    Args:
        lines: The file's lines.
        start: Index of the run's first `if`.

    Returns:
        The file's new lines.
    """
    indent: int = indent_of(lines[start])
    pad: str = " " * indent
    guards: list[Guard] = []
    position: int = start
    while True:
        guard: Guard | None
        guard, after = read_guard(lines, position, indent)
        if guard is None:
            break
        else:
            guards.append(guard)
            position = after
    # The tail runs to the line that closes the enclosing block.
    tail_end: int = position
    while not (
        lines[tail_end].strip().startswith("}") and indent_of(lines[tail_end]) < indent
    ):
        tail_end += 1
    tail: list[str] = lines[position:tail_end]
    while tail and tail[-1].strip() == "":
        tail.pop()
    while tail and tail[0].strip() == "":
        tail.pop(0)
    assert len(guards) >= 2, f"line {start + 1}: not a run"
    code_tail: list[str] = [line for line in tail if not is_comment_or_blank(line)]
    unbraced: bool = all(not guard.braced for guard in guards) and len(code_tail) == 1
    out: list[str] = []
    for number, guard in enumerate(guards):
        keyword: str = "if" if number == 0 else "else if"
        first: str = guard.header[0].replace("if (", f"{keyword} (", 1)
        if number > 0 and not unbraced:
            first = pad + "} " + first.lstrip()
        if not guard.braced:
            # `if (COND) STATEMENT;`: split the statement from the condition.
            text: str = first.rstrip()
            depth: int = 0
            split: int = -1
            for offset, character in enumerate(text):
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
                    if depth == 0 and split < 0 and offset > text.index("("):
                        split = offset + 1
            condition: str = text[:split]
            statement: str = text[split:].strip()
            out.append(condition if unbraced else condition + " {")
            out.extend(moved(guard.leading, pad))
            out.append(pad + "  " + statement)
        else:
            out.append(first if len(guard.header) > 1 else first + " {")
            if len(guard.header) > 1:
                out.extend(guard.header[1:-1])
                out.append(guard.header[-1] + " {")
            # Blank lines that separated the guards are dropped; the comment moves in.
            out.extend(moved(guard.leading, pad))
            out.extend(guard.body)
    if is_one_chain(tail, indent) and not unbraced:
        # `} else {` + `if (...) {...} else {...}` reads as one longer chain: splice it.
        out.append(pad + "} else " + tail[0].lstrip())
        out.extend(tail[1:-1])
        out.append(tail[-1])
    else:
        out.append(pad + "else" if unbraced else pad + "} else {")
        out.extend(("  " + line) if line.strip() else line for line in tail)
        if not unbraced:
            out.append(pad + "}")
    return lines[:start] + out + lines[tail_end:]


def check(old: list[str], new: list[str]) -> None:
    """Fail if a non-punctuation line of the old text is missing from the new.

    Args:
        old: The region's original lines.
        new: The region's rewritten lines.
    """

    def kept(lines: list[str]) -> list[str]:
        """Normalize lines for comparison.

        Args:
            lines: Source lines.

        Returns:
            The stripped lines, minus braces and `if`/`else` headers.
        """
        return [
            line.strip()
            for line in lines
            if line.strip() not in ("", "}", "{", "} else {", "else")
            and not line.strip().startswith(("if (", "} else if (", "else if ("))
        ]

    new_kept: set[str] = set(kept(new))
    missing: list[str] = [line for line in kept(old) if line not in new_kept]
    # A single-line guard's statement is split off its condition, so look for it as a suffix.
    really_missing: list[str] = [
        line for line in missing if not any(line.endswith(other) for other in new_kept)
    ]
    assert not really_missing, f"lines lost: {really_missing}"


def main(sites: list[str]) -> None:
    """Rewrite each named run.

    Args:
        sites: FILE:LINE strings.
    """
    by_file: dict[str, list[int]] = {}
    for site in sites:
        path, line = site.rsplit(":", 1)
        by_file.setdefault(path, []).append(int(line))
    for path, starts in by_file.items():
        source: pathlib.Path = REPO_ROOT / path
        lines: list[str] = source.read_text().split("\n")
        for start in sorted(starts, reverse=True):
            before: list[str] = lines
            lines = convert(lines, start - 1)
            check(before[start - 1 :], lines[start - 1 :])
        source.write_text("\n".join(lines))
        print(f"{path}: {len(starts)} runs")


if __name__ == "__main__":
    main(sys.argv[1:])
