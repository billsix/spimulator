#!/usr/bin/env python3
"""Drop the ``_inst``/``Inst`` suffix from the instruction-emit functions, in C and the ports.

Item B3 of tasks/codebase-cleanup-plan.md: the suffix did not mark anything (``store_word`` also
emits an instruction and never had it). Each name is replaced as a whole word in the tracked
sources that use it; running the script a second time changes nothing.

Usage:
    tasks/adhoc/codebase-cleanup-plan/drop_inst_suffix.py
"""

import pathlib
import re
import subprocess

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
RENAMES: dict[str, str] = {
    # C, Rust, and the C names the Haskell comments cite.
    "r_type_inst": "r_type",
    "i_type_inst": "i_type",
    "i_type_inst_free": "i_type_free",
    "i_type_inst_full_word": "i_type_full_word",
    "j_type_inst": "j_type",
    "make_r_type_inst": "make_r_type",
    "r_co_type_inst": "r_co_type",
    "r_cond_type_inst": "r_cond_type",
    "r_sh_type_inst": "r_sh_type",
    # Python.
    "_make_r_type_inst": "_make_r_type",
    "_i_type_inst_full_word": "_i_type_full_word",
    # Haskell.
    "rTypeInst": "rType",
    "iTypeInst": "iType",
    "iTypeInstFullWord": "iTypeFullWord",
    "jTypeInst": "jType",
    "rCoTypeInst": "rCoType",
    "rCondTypeInst": "rCondType",
    "rShTypeInst": "rShType",
}
PATTERN: re.Pattern[str] = re.compile(r"\b(" + "|".join(map(re.escape, RENAMES)) + r")\b")
# Sources only: generated tables, task records and reviews describe history and stay.
PATHSPECS: tuple[str, ...] = ("src", "include", "port", "tasks/reference")


def tracked_files() -> list[pathlib.Path]:
    """The tracked files under PATHSPECS that mention an old name.

    Returns:
        list[pathlib.Path]: each file's absolute path.
    """
    listed: subprocess.CompletedProcess[str] = subprocess.run(
        ["git", "grep", "-lwE", "|".join(RENAMES), "--", *PATHSPECS],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=False,  # exit 1 means no file matches, which is fine
    )
    return [REPO / name for name in listed.stdout.splitlines()]


def main() -> None:
    """Rename in every file and print each file changed, with its count."""
    for path in tracked_files():
        text: str = path.read_text()
        renamed: str
        count: int
        renamed, count = PATTERN.subn(lambda match: RENAMES[match.group(1)], text)
        if count:
            path.write_text(renamed)
            print(f"{path.relative_to(REPO)}: {count}")


if __name__ == "__main__":
    main()
