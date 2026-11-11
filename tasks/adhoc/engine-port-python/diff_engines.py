#!/usr/bin/env python3
"""Run many command lines through the C engine and the Python port; diff them.

A wider net than port/harness/corpus.txt, used while porting: every .s file
under tests/, examples/ and pgu/ assembled with -dump, -print-ast,
-print-ast-json, -show-expansion and -listing, and (with --run) executed.
Each case compares stdout, stderr (the Loaded: line removed), the exit
status, and any file the engine wrote (text.asm, data.asm, the listing).

Usage:
    diff_engines.py --reference C_SPIMULATOR [--run] [--timeout S] [FILTER]

FILTER keeps only cases whose label contains it.  Exit 1 if any differ.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

REPO: pathlib.Path = pathlib.Path(__file__).resolve().parents[3]
PORT: pathlib.Path = REPO / "port" / "python" / "spimulator-python"
EXCEPTIONS: pathlib.Path = REPO / "src" / "exceptions.s"


def sources() -> list[pathlib.Path]:
    """Every assembly file the assembler should be compared on."""
    found: list[pathlib.Path] = sorted(
        p
        for pattern in ("tests/*.s", "examples/**/*.asm", "examples/**/*.s", "pgu/**/*.s")
        for p in REPO.glob(pattern)
        if "builddir" not in p.parts
    )
    return found


def cases(run: bool) -> list[tuple[str, list[str]]]:
    """(label, arguments) for each command line; LISTING is the listing path."""
    result: list[tuple[str, list[str]]] = []
    ef: list[str] = ["-exception_file", str(EXCEPTIONS)]
    for source in sources():
        name: str = str(source.relative_to(REPO))
        bare: list[str] = ["-bare", "-noexception"] if ".bare." in name else ef
        result += [
            (f"{name} dump", [*bare, "-dump", "-f", str(source)]),
            (f"{name} full_dump", [*bare, "-full_dump", "-f", str(source)]),
            (f"{name} print-ast", ["-noexception", "-print-ast", "-f", str(source)]),
            (f"{name} json", ["-noexception", "-print-ast-json", "-f", str(source)]),
            (f"{name} expansion", ["-noexception", "-show-expansion", "-f", str(source)]),
            (f"{name} listing", [*ef, "-listing", "LISTING", "-f", str(source)]),
        ]
        if run:
            result.append((f"{name} run", [*bare, "-f", str(source)]))
    return result


def execute(engine: str, args: list[str], timeout: float) -> tuple[str, ...]:
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
            )
        except subprocess.TimeoutExpired:
            return ("timeout",)
        stderr: str = "\n".join(
            line
            for line in done.stderr.decode("latin-1").split("\n")
            if not line.startswith("Loaded: ")
        )
        files: list[str] = [
            f"{p.name}:\n{p.read_text('latin-1')}" for p in sorted(pathlib.Path(scratch).iterdir())
        ]
        return (done.stdout.decode("latin-1"), stderr, str(done.returncode), *files)


def main() -> int:
    """Compare every case; print the differing ones."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("filter", nargs="?", default="")
    options: argparse.Namespace = parser.parse_args()
    failed: int = 0
    total: int = 0
    for label, args in cases(options.run):
        if options.filter not in label:
            continue
        total += 1
        want: tuple[str, ...] = execute(options.reference, args, options.timeout)
        got: tuple[str, ...] = execute(str(PORT), args, options.timeout)
        if want != got:
            failed += 1
            print(f"DIFF {label}")
            for index, (w, g) in enumerate(zip(want, got)):
                if w != g:
                    w_lines: list[str] = w.split("\n")
                    g_lines: list[str] = g.split("\n")
                    for n, (wl, gl) in enumerate(zip(w_lines, g_lines)):
                        if wl != gl:
                            print(f"    part {index} line {n + 1}:\n      C:  {wl!r}\n      py: {gl!r}")
                            break
                    else:
                        print(f"    part {index}: {len(w_lines)} vs {len(g_lines)} lines")
                    break
            else:
                print(f"    {len(want)} vs {len(got)} parts")
    print(f"{total - failed} same, {failed} different")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
