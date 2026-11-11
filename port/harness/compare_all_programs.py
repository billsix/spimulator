#!/usr/bin/env python3
"""Every assembly program in the repo, through the C engine and a port: compare.

A wider net than corpus.txt.  Every .s/.asm file under tests/, examples/ and
pgu/ is assembled six ways (-dump, -full_dump, -print-ast, -print-ast-json,
-show-expansion, -listing) and run once with empty stdin.  For each case the
stdout, the stderr (without the `Loaded:` line), the exit status and every
file the engine wrote (text.asm, data.asm, the listing) are compared, with the
same rule as compare_engines.py: a difference only inside close floating-point
numbers is a WARN, anything else a FAIL.

Written by the Python port's work (tasks/archive/2026/11/10/engine-port-python.md, where it found
three differences no other test showed) and promoted here for every port.

Usage:
    compare_all_programs.py --reference SPIMULATOR --engine PORT
                            [--jobs N] [--timeout S] [FILTER]

FILTER keeps only cases whose label contains it.  Exit 1 if any case FAILs.
"""

import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile
from collections.abc import Iterator

from compare_engines import REPO, compare_text, resolve_engine

EXCEPTIONS: pathlib.Path = REPO / "src" / "exceptions.s"

# Cases left out, with the reason.  Without -mapped_io these two programs
# spin until the engine's 2**31 - 1 step limit; the debug C build takes about
# 14 minutes to get there (found by the Rust port, which takes 43 s), so the
# C side always times out and the comparison would test nothing.  Their
# assembly is still compared in the other modes.
SKIPPED: dict[str, str] = {
    **{
        f"tests/{program} {mode}": "runs to the 2**31 - 1 step limit without -mapped_io"
        for program in ("timer.s", "tt.io.s")
        for mode in ("run", "listing")
    },
    # yes(1): prints "y" until killed, so both engines would only time out
    # (a C-side timeout fails the case).
    **{
        f"examples/src/intro/yes/yes.asm {mode}": "prints y forever, like yes(1)"
        for mode in ("run", "listing")
    },
}


def sources() -> list[pathlib.Path]:
    """Every assembly file the engines are compared on."""
    patterns: tuple[str, ...] = (
        "tests/*.s",
        "examples/**/*.asm",
        "examples/**/*.s",
        "pgu/**/*.s",
        "pgu/**/*.asm",
    )
    found: dict[str, list[pathlib.Path]] = {
        pattern: [p for p in REPO.glob(pattern) if "builddir" not in p.parts]
        for pattern in patterns
    }
    # A tracked-source pattern that matches nothing means a tree was not copied
    # (the image build copies only some of the repo): fail rather than compare
    # less.  examples/**/*.s are the native listings the examples build writes,
    # so they exist only after it.
    required: tuple[str, ...] = (
        "tests/*.s",
        "examples/**/*.asm",
        "pgu/**/*.s",
        "pgu/**/*.asm",
    )
    empty: list[str] = [p for p in required if not found[p]]
    if empty:
        raise SystemExit(f"compare_all_programs: no files match {', '.join(empty)}")
    return sorted(p for paths in found.values() for p in paths)


def cases() -> list[tuple[str, list[str]]]:
    """(label, arguments) for every command line; LISTING is the listing path."""
    result: list[tuple[str, list[str]]] = []
    ef: list[str] = ["-exception_file", str(EXCEPTIONS)]
    for source in sources():
        name: str = str(source.relative_to(REPO))
        handler: list[str] = ["-bare", "-noexception"] if ".bare." in name else ef
        result += [
            (f"{name} dump", [*handler, "-dump", "-f", str(source)]),
            (f"{name} full_dump", [*handler, "-full_dump", "-f", str(source)]),
            (f"{name} print-ast", ["-noexception", "-print-ast", "-f", str(source)]),
            (f"{name} json", ["-noexception", "-print-ast-json", "-f", str(source)]),
            (
                f"{name} expansion",
                ["-noexception", "-show-expansion", "-f", str(source)],
            ),
            (f"{name} listing", [*ef, "-listing", "LISTING", "-f", str(source)]),
            (f"{name} run", [*handler, "-f", str(source)]),
        ]
    return result


def execute(engine: str, args: list[str], timeout: float) -> dict[str, str]:
    """Run one engine in a scratch directory; return everything it produced."""
    with tempfile.TemporaryDirectory() as scratch:
        listing: str = str(pathlib.Path(scratch) / "listing.txt")
        argv: list[str] = [listing if a == "LISTING" else a for a in args]
        try:
            done: subprocess.CompletedProcess[bytes] = subprocess.run(
                [engine, *argv],
                stdin=subprocess.DEVNULL,
                capture_output=True,
                cwd=scratch,
                env={},
                timeout=timeout,
                check=False,  # the exit status is part of what is compared
            )
        except subprocess.TimeoutExpired:
            return {
                "stdout": "",
                "stderr": f"<timed out after {timeout} s>",
                "status": "-1",
            }
        stderr: str = "\n".join(
            line
            for line in done.stderr.decode("latin-1").split("\n")
            if not line.startswith("Loaded: ")
        )
        produced: dict[str, str] = {
            "stdout": done.stdout.decode("latin-1"),
            "stderr": stderr,
            "status": str(done.returncode),
        }
        for written in sorted(pathlib.Path(scratch).iterdir()):
            produced[written.name] = written.read_text("latin-1")
        return produced


def compare_case(
    reference: str, engine: str, label: str, args: list[str], timeout: float
) -> tuple[str, str, list[str]]:
    """(label, PASS | WARN | FAIL, notes) for one command line."""
    want: dict[str, str] = execute(reference, args, timeout)
    got: dict[str, str] = execute(engine, args, timeout)
    if want["status"] == "-1" and want["stderr"].startswith("<timed out"):
        # Both engines timing out would compare equal; a hung reference proves nothing.
        return label, "FAIL", [f"the C engine timed out after {timeout} s"]
    verdicts: list[str] = []
    notes: list[str] = []
    for part in sorted(set(want) | set(got)):
        verdict, part_notes = compare_text(
            part, want.get(part, "<missing>"), got.get(part, "<missing>")
        )
        verdicts.append(verdict)
        notes += part_notes
    worst: str = (
        "FAIL" if "FAIL" in verdicts else "WARN" if "WARN" in verdicts else "PASS"
    )
    return label, worst, notes


def main() -> int:
    """Compare every case; print the ones that are not PASS."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0]
    )
    parser.add_argument("--reference", required=True, help="the C spimulator")
    parser.add_argument("--engine", required=True, help="the port's executable")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("filter", nargs="?", default="")
    options: argparse.Namespace = parser.parse_args()
    reference: str = resolve_engine(options.reference)
    engine: str = resolve_engine(options.engine)
    selected: list[tuple[str, list[str]]] = [
        c for c in cases() if options.filter in c[0] and c[0] not in SKIPPED
    ]
    if not selected:
        print(f"no case matches {options.filter!r}")
        return 1
    counts: dict[str, int] = {"PASS": 0, "WARN": 0, "FAIL": 0}
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
        results: Iterator[tuple[str, str, list[str]]] = pool.map(
            lambda case: compare_case(
                reference, engine, case[0], case[1], options.timeout
            ),
            selected,
        )
        for label, verdict, notes in results:
            counts[verdict] += 1
            if verdict != "PASS":
                print(f"{verdict} {label}")
                for note in notes[:5]:
                    print(f"    {note}")
    print(
        f"{counts['PASS']} passed, {counts['WARN']} warned, {counts['FAIL']} failed"
        f" ({len(SKIPPED)} skipped by name, see SKIPPED)"
    )
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
