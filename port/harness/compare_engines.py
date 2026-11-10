#!/usr/bin/env python3
"""Run each case in corpus.txt through the C engine and a port; compare them.

The C engine is the standard (tasks/engine-ports.md).  For every case the
port must produce the same stdout, the same stderr and the same exit status.
One difference is forgiven: a line that differs only inside printed
floating-point numbers, when each pair of numbers is equal or very close
(within one unit in the last printed digit, or a relative 1e-15), or both
NaN.  That is reported as WARN and does not fail the run, because how a float
is turned into text does not gate a port.  Any other difference is FAIL.

The `Loaded: <exception file>` line is left out of stderr: it names the file
path, which is the only part of the output that depends on where the engines
are run from.

Usage:
    compare_engines.py --reference SPIMULATOR --engine PORT
                       [--exception-file FILE] [--timeout SECONDS] [CASE ...]

With no CASE names, every case runs.  Exit status: 0 when no case FAILs, 1
otherwise.
"""

import argparse
import math
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass

HERE: pathlib.Path = pathlib.Path(__file__).resolve().parent
REPO: pathlib.Path = HERE.parents[1]
TESTS: pathlib.Path = REPO / "tests"

# A number as printed: optional sign, digits with an optional fraction or a
# bare fraction, an optional exponent; or inf/nan with an optional sign.
NUMBER: re.Pattern[str] = re.compile(
    r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?|[-+]?(?:inf|nan)"
)
RELATIVE_TOLERANCE: float = 1e-15


@dataclass(frozen=True)
class Case:
    name: str
    stdin: bytes | None
    args: list[str]


@dataclass(frozen=True)
class Run:
    stdout: str
    stderr: str
    status: int
    dumps: dict[str, str]


def parse_stdin(field: str) -> bytes | None:
    if field == "-":
        return None
    if field.startswith("file:"):
        return (TESTS / field.removeprefix("file:")).read_bytes()
    if field.startswith("text:"):
        return field.removeprefix("text:").replace("\\n", "\n").encode()
    raise ValueError(f"corpus: bad STDIN field {field!r}")


def load_corpus() -> list[Case]:
    cases: list[Case] = []
    for line in (HERE / "corpus.txt").read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, stdin, args = (part.strip() for part in line.split("|", 2))
        cases.append(Case(name, parse_stdin(stdin), shlex.split(args)))
    return cases


def run(engine: str, case: Case, exception_file: str, timeout: float) -> Run:
    args: list[str] = [exception_file if a == "EF" else a for a in case.args]
    with tempfile.TemporaryDirectory() as scratch:
        # -dump writes text.asm/data.asm into the working directory, so a
        # dump case runs in a scratch directory with the program's path made
        # absolute; every other case runs from tests/.
        dump: bool = "-dump" in args
        cwd: pathlib.Path = pathlib.Path(scratch) if dump else TESTS
        if dump:
            args = [str(TESTS / a) if a.endswith(".s") else a for a in args]
        try:
            done: subprocess.CompletedProcess[bytes] = subprocess.run(
                [engine, *args],
                input=case.stdin if case.stdin is not None else b"",
                capture_output=True,
                check=False,  # a nonzero exit status is part of what is compared
                cwd=cwd,
                env={},
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            return Run("", f"<timed out after {timeout} s>", -1, {})
        dumps: dict[str, str] = {
            name: (cwd / name).read_text(errors="replace")
            for name in ("text.asm", "data.asm")
            if dump and (cwd / name).exists()
        }
    stderr: str = "\n".join(
        line
        for line in done.stderr.decode(errors="replace").split("\n")
        if not line.startswith("Loaded: ")
    )
    return Run(done.stdout.decode(errors="replace"), stderr, done.returncode, dumps)


def last_digit_unit(text: str) -> float:
    """The value of one unit in the last printed digit of a number."""
    mantissa, _, exponent = text.lower().partition("e")
    decimals: int = len(mantissa.partition(".")[2])
    return 10.0 ** (int(exponent or 0) - decimals)


def close(a_text: str, b_text: str) -> bool:
    """Two printed numbers: equal, very close, or both NaN."""
    if a_text == b_text:
        return True
    a: float = float(a_text)
    b: float = float(b_text)
    if math.isnan(a) and math.isnan(b):
        return True
    if math.isinf(a) or math.isinf(b):
        return a == b
    allowed: float = max(
        RELATIVE_TOLERANCE * max(abs(a), abs(b)),
        max(last_digit_unit(a_text), last_digit_unit(b_text)),
    )
    return abs(a - b) <= allowed


def float_only_difference(a: str, b: str) -> bool:
    """True if lines a and b differ only in float-looking numbers that are close."""
    if NUMBER.sub("#", a) != NUMBER.sub("#", b):
        return False
    pairs: list[tuple[str, str]] = list(zip(NUMBER.findall(a), NUMBER.findall(b)))
    if any(x != y and not re.search(r"[.eEn]", x + y) for x, y in pairs):
        return False  # two different integers: a functional difference
    return all(close(x, y) for x, y in pairs)


def compare_text(label: str, want: str, got: str) -> tuple[str, list[str]]:
    """('PASS' | 'WARN' | 'FAIL', notes) for one output stream."""
    if want == got:
        return "PASS", []
    want_lines: list[str] = want.split("\n")
    got_lines: list[str] = got.split("\n")
    if len(want_lines) != len(got_lines):
        return "FAIL", [f"{label}: {len(want_lines)} lines from C, {len(got_lines)} from the port"]
    notes: list[str] = []
    verdict: str = "PASS"
    for number, (w, g) in enumerate(zip(want_lines, got_lines), start=1):
        if w == g:
            continue
        if float_only_difference(w, g):
            verdict = "WARN" if verdict == "PASS" else verdict
            notes.append(f"{label}:{number}: float text differs: C {w!r}, port {g!r}")
        else:
            notes.append(f"{label}:{number}: C {w!r}, port {g!r}")
            return "FAIL", notes
    return verdict, notes


def compare(want: Run, got: Run) -> tuple[str, list[str]]:
    results: list[tuple[str, list[str]]] = [
        compare_text("stdout", want.stdout, got.stdout),
        compare_text("stderr", want.stderr, got.stderr),
    ]
    for name in sorted(set(want.dumps) | set(got.dumps)):
        results.append(compare_text(name, want.dumps.get(name, ""), got.dumps.get(name, "")))
    if want.status != got.status:
        results.append(("FAIL", [f"exit status: C {want.status}, port {got.status}"]))
    verdicts: list[str] = [verdict for verdict, _ in results]
    worst: str = "FAIL" if "FAIL" in verdicts else "WARN" if "WARN" in verdicts else "PASS"
    return worst, [note for _, notes in results for note in notes]


def resolve_engine(path: str) -> str:
    """An absolute path for an engine named by a path; a bare name stays a PATH lookup."""
    return os.path.abspath(path) if os.sep in path else path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--reference", required=True, help="the C spimulator")
    parser.add_argument("--engine", required=True, help="the port's executable")
    parser.add_argument(
        "--exception-file", default=str(REPO / "src" / "exceptions.s")
    )
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("cases", nargs="*", help="case names (default: all)")
    options = parser.parse_args()
    # The cases run from tests/, so an engine given by a relative path is
    # resolved against the directory the harness was started from.
    reference: str = resolve_engine(options.reference)
    engine: str = resolve_engine(options.engine)
    cases: list[Case] = [
        c for c in load_corpus() if not options.cases or c.name in options.cases
    ]
    counts: dict[str, int] = {"PASS": 0, "WARN": 0, "FAIL": 0}
    for case in cases:
        want: Run = run(reference, case, options.exception_file, options.timeout)
        got: Run = run(engine, case, options.exception_file, options.timeout)
        verdict, notes = compare(want, got)
        counts[verdict] += 1
        print(f"{verdict} {case.name}")
        for note in notes[:10]:
            print(f"    {note}")
    print(f"{counts['PASS']} passed, {counts['WARN']} warned, {counts['FAIL']} failed")
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
