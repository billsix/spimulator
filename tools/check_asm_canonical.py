#!/usr/bin/env python3
"""Verify the #CANONICAL block in each example .asm against a real spim run.

The stack-visible teaching style (tasks/reference/mips-asm-teaching-style.md)
annotates each demo with hand-traced values in a SNAPSHOT diagram.  Those
values cannot be checked by the stdout golden tests, so a wrong one would rot
silently.  This checker anchors every annotated demo to a REAL run: each file
carries a machine-readable #CANONICAL block naming one canonical invocation
(args and/or a stdin file) and its exact expected stdout; the checker runs the
demo under spim and fails on any mismatch.  It verifies the canonical run's
final output (the anchor the hand-traced snapshot values are derived from), not
the intermediate register/slot values themselves.

Block grammar (comment lines; leading '#' then spaces are stripped):

    #CANONICAL (...free text...)
    #  args: 5 7            # whitespace-split argv; omit / blank for none
    #  stdin: path/rel/to/examples/src   # optional
    #  stdout:
    #  | first expected line
    #  | second expected line

Usage:
    tools/check_asm_canonical.py FILE.asm [FILE.asm ...]
    tools/check_asm_canonical.py --all          # every *.asm with a block

Environment:
    SPIM   path to the spimulator binary (default: <repo>/builddir/spimulator)

Exit status: 0 if every checked block matches, 1 otherwise.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT: Path = Path(__file__).resolve().parents[1]
SRC_DIR: Path = REPO_ROOT / "examples" / "src"


def spim_path() -> Path:
    """The spim binary to run — $SPIM if set, else the in-tree build."""
    env: str | None = os.environ.get("SPIM")
    return Path(env) if env else REPO_ROOT / "builddir" / "spimulator"


def strip_comment(line: str) -> str | None:
    """Return a comment line's content (after '# '), or None if not a comment."""
    stripped: str = line.lstrip()
    if not stripped.startswith("#"):
        return None
    return stripped[1:].lstrip()


def parse_canonical(path: Path) -> dict[str, object] | None:
    """Parse the one #CANONICAL block in `path`, or None if it has none."""
    lines: list[str] = path.read_text().splitlines()
    start: int = -1
    for index, line in enumerate(lines):
        content: str | None = strip_comment(line)
        if content is not None and content.startswith("CANONICAL"):
            start = index
            break
    if start < 0:
        return None

    args: list[str] = []
    stdin_file: str | None = None
    expected: list[str] = []
    in_stdout: bool = False
    for line in lines[start + 1:]:
        content: str | None = strip_comment(line)
        if content is None:
            break  # the block ends at the first non-comment line
        if content.startswith("|"):
            expected.append(content[1:].lstrip() if content[1:].startswith(" ") else content[1:])
            continue
        if content.startswith("args:"):
            args = content[len("args:"):].split()
            in_stdout = False
        elif content.startswith("stdin:"):
            stdin_file = content[len("stdin:"):].strip()
            in_stdout = False
        elif content.startswith("stdout:"):
            in_stdout = True
        elif in_stdout:
            break
    return {"args": args, "stdin_file": stdin_file, "expected": expected}


def run_demo(asm: Path, args: list[str], stdin_file: str | None) -> str:
    """Run `spim -f asm args` (optional stdin), return stdout (stderr dropped)."""
    cmd: list[str] = [str(spim_path()), "-f", str(asm), *args]
    stdin_bytes: bytes = b""
    if stdin_file:
        stdin_bytes = (SRC_DIR / stdin_file).read_bytes()
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        cmd, input=stdin_bytes, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        check=False,
    )
    return completed.stdout.decode()


def check_file(asm: Path) -> bool | None:
    """Check one file. True=pass, False=fail, None=no #CANONICAL block."""
    block: dict[str, object] | None = parse_canonical(asm)
    if block is None:
        return None
    args: list[str] = block["args"]  # type: ignore[assignment]
    stdin_file: str | None = block["stdin_file"]  # type: ignore[assignment]
    expected_lines: list[str] = block["expected"]  # type: ignore[assignment]
    actual: str = run_demo(asm, args, stdin_file)
    actual_lines: list[str] = actual.splitlines()
    rel: str = asm.relative_to(REPO_ROOT).as_posix()
    if actual_lines == expected_lines:
        print(f"PASS  {rel}")
        return True
    print(f"FAIL  {rel}", file=sys.stderr)
    print(f"  expected: {expected_lines!r}", file=sys.stderr)
    print(f"  actual:   {actual_lines!r}", file=sys.stderr)
    return False


def main(argv: list[str]) -> int:
    """Check the files named on the command line (or --all)."""
    if not argv:
        print(__doc__)
        return 2
    files: list[Path]
    if argv == ["--all"]:
        files = sorted(SRC_DIR.rglob("*.asm"))
    else:
        files = [Path(a).resolve() for a in argv]

    status: int = 0
    checked: int = 0
    for asm in files:
        result: bool | None = check_file(asm)
        if result is None:
            continue
        checked += 1
        if not result:
            status = 1
    if checked == 0:
        print("no #CANONICAL blocks found in the given files", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
