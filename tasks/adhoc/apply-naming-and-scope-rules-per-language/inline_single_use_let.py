"""Inline single-use ``let`` bindings in the Rust port, one at a time, keeping only those that compile.

Usage: ``python3 inline_single_use_let.py [--dry-run] FILE.rs ...`` (paths relative to
the repository root or absolute).

A binding is a candidate when all of these hold:

* it is one line, ``let NAME: TYPE = EXPR;``, not ``mut``, and EXPR is not a bare literal
  and does not end in a call whose result type the annotation chooses (``into``,
  ``collect``, ``parse``, ``default``, ``sum``, ``product``);
* NAME occurs exactly once after it inside its block, on the next line of code, outside
  a string literal;
* nothing is evaluated on that line before NAME except plain names: the text in front of
  it holds no closing parenthesis, at most one opening one, and no closure, loop header
  or macro; so moving EXPR into NAME's place cannot change the order of side effects;
* EXPR does not read the token stream (``parse_*``, ``advance``, ``value_*``,
  ``scan_one_token``): the C engine keeps those calls as named statements in source
  order, and the port mirrors it; nor is EXPR an ``unsafe`` block;
* NAME is not the scrutinee of a ``match`` or the start of an index, where the
  substituted text would need parentheses that the compiler then reports as unused;
* the line with EXPR substituted is at most 100 columns wide.

Each candidate is applied alone and ``cargo check --all-targets`` is run; a candidate
that does not compile (a borrow that needed the binding, an inference that needed the
annotation) is put back.  The script prints every decision.  A second run finds the
kept inlines gone and retries the ones that failed, which fail again; an inline can
also expose one more candidate (a binding whose use moved onto the next line), so run
it until it reports no inline.  Run ``cargo fmt`` afterwards.
"""

import argparse
import pathlib
import re
import subprocess

REPO_ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]

MANIFEST: pathlib.Path = REPO_ROOT / "port" / "rust" / "Cargo.toml"

LET: re.Pattern[str] = re.compile(r"^(\s*)let (\w+): [^=]+? = (.+);\s*$")

TYPE_FROM_ANNOTATION: re.Pattern[str] = re.compile(
    r"\.(?:into|collect|parse|sum|product)\(\)$|::default\(\)$|^[-\d.'\"\w]+$"
)

READS_TOKENS: re.Pattern[str] = re.compile(
    r"\bself\.(?:parse_\w+|advance|value_\w+|scan_one_token)\(|^unsafe\b"
)

SAFE_PREFIX: re.Pattern[str] = re.compile(r"^[^()|!]*(?:\([\w\s,&*.:\[\]]*)?$")


def candidate_at(lines: list[str], index: int) -> tuple[int, str] | None:
    """Decide whether the ``let`` on one line can be inlined into the next line of code.

    Args:
        lines: The file's lines.
        index: The index of the line to test.

    Returns:
        The index of the line that uses the binding and that line with the expression
        substituted, or None when the line is not a candidate.
    """
    found: re.Match[str] | None = LET.match(lines[index])
    if found is None or TYPE_FROM_ANNOTATION.search(found.group(3)):
        return None
    if READS_TOKENS.search(found.group(3)):
        return None
    indent: str = found.group(1)
    name: str = found.group(2)
    expression: str = found.group(3)
    word: re.Pattern[str] = re.compile(rf"\b{name}\b")
    # The block's remaining lines: up to the first line indented less than the `let`.
    rest: list[int] = []
    for later in range(index + 1, len(lines)):
        if lines[later].strip() and not lines[later].startswith(indent):
            break
        rest.append(later)
    code: list[int] = [
        later
        for later in rest
        if lines[later].strip() and not lines[later].strip().startswith("//")
    ]
    if not code or sum(len(word.findall(lines[later])) for later in rest) != 1:
        return None
    use: int = code[0]
    at: re.Match[str] | None = word.search(lines[use])
    if at is None:
        return None
    prefix: str = lines[use][: at.start()]
    if prefix.count('"') % 2 or not SAFE_PREFIX.match(prefix):
        return None
    if prefix.rstrip().endswith(("match", "[")):
        return None
    if re.match(r"\s*(?:for|while|loop)\b", prefix) or prefix.rstrip().endswith(("&", "&mut")):
        return None
    needs_parentheses: bool = bool(
        re.search(r"\s(?:as|\+|-|\*|/|\||&|<<|>>|==|<|>)\s|^if\b|^match\b", expression)
    )
    after: str = lines[use][at.end() :]
    bare_slot: bool = prefix.rstrip().endswith(("(", ",", "=", "return")) and after.lstrip()[
        :1
    ] in (
        ",",
        ")",
        ";",
    )
    replacement: str = f"({expression})" if needs_parentheses and not bare_slot else expression
    merged: str = prefix + replacement + after
    return (use, merged) if len(merged) <= 100 else None


def compiles() -> bool:
    """Report whether the Rust port, tests included, still compiles.

    Returns:
        True when ``cargo check --all-targets`` exits 0.
    """
    return (
        subprocess.run(
            [
                "cargo",
                "check",
                "--offline",
                "--quiet",
                "--all-targets",
                "--manifest-path",
                MANIFEST,
            ],
            capture_output=True,
            check=False,
        ).returncode
        == 0
    )


def main() -> None:
    """Try every candidate in the named files and keep the ones that compile."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="list candidates, change nothing")
    parser.add_argument("files", nargs="+", type=pathlib.Path)
    arguments: argparse.Namespace = parser.parse_args()
    for given in arguments.files:
        path: pathlib.Path = given if given.is_absolute() else REPO_ROOT / given
        index: int = 0
        while True:
            lines: list[str] = path.read_text().splitlines()
            if index >= len(lines):
                break
            candidate: tuple[int, str] | None = candidate_at(lines, index)
            if candidate is None:
                index += 1
                continue
            use, merged = candidate
            where: str = f"{path.relative_to(REPO_ROOT)}:{index + 1}"
            if arguments.dry_run:
                print(f"candidate {where}: {lines[index].strip()}\n    -> {merged.strip()}")
                index += 1
                continue
            edited: list[str] = lines[:index] + lines[index + 1 : use] + [merged] + lines[use + 1 :]
            path.write_text("\n".join(edited) + "\n")
            if compiles():
                print(f"inlined {where}: {lines[index].strip()}\n    -> {merged.strip()}")
            else:
                path.write_text("\n".join(lines) + "\n")
                print(f"kept    {where}: {lines[index].strip()} (does not compile inlined)")
                index += 1


if __name__ == "__main__":
    main()
