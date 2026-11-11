"""Rewrite run.py's @_executes("NAME", ...) decorators to Tok.NAME_OPCODE tokens."""

import pathlib
import re
import sys

path: pathlib.Path = pathlib.Path(sys.argv[1])
text: str = path.read_text()
pattern: re.Pattern[str] = re.compile(r'@_executes\(("[^)]*?)\)(  # fmt: skip)?', re.DOTALL)


def tokens(match: re.Match[str]) -> str:
    """The decorator with each quoted mnemonic replaced by its token."""
    names: list[str] = re.findall(r'"([A-Z0-9_]+)"', match.group(1))
    return "@_executes(" + ", ".join(f"Tok.{n}_OPCODE" for n in names) + ")"


new: str
count: int
new, count = pattern.subn(tokens, text)
path.write_text(new)
print(f"{count} decorators rewritten")
