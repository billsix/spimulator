#!/usr/bin/env python3
"""Feed each REPL session in repl_corpus/ to the C engine and a port; compare.

Each ``repl_corpus/<name>.in`` is fed on stdin (no terminal) to
``ENGINE -exception_file FILE``, run in a scratch directory that holds a link to
every file in tests/, with an empty environment. The port must give the same
stdout, stderr and exit status, and write the same files (the ``dump``
commands write ``*.dump``), with the same float tolerance as
compare_engines.py: a line that differs only in printed floats that are within
a few ULP is WARN, anything else is FAIL.

Usage:
    compare_repl.py --reference SPIMULATOR --engine PORT
                    [--exception-file FILE] [--timeout SECONDS] [SESSION ...]

With no SESSION names, every session runs. Exit status: 0 when no session
FAILs, 1 otherwise, including when a named session does not exist.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

from compare_engines import REPO, TESTS, TIMED_OUT, Run, compare, resolve_engine

CORPUS: pathlib.Path = pathlib.Path(__file__).resolve().parent / "repl_corpus"


def sessions() -> dict[str, bytes]:
    """Every session, by name.

    Returns:
        dict[str, bytes]: each ``.in`` file's bytes, keyed by its stem.
    """
    return {path.stem: path.read_bytes() for path in sorted(CORPUS.glob("*.in"))}


def run_session(
    engine: str, session: bytes, exception_file: str, timeout: float
) -> Run:
    """Run one session in a fresh scratch directory.

    Args:
        engine: the engine's executable.
        session: the REPL input.
        exception_file: the exception handler to load.
        timeout: seconds before the run counts as hung.

    Returns:
        Run: stdout, stderr (without its ``Loaded:`` line), exit status, and the
        files the session wrote.
    """
    with tempfile.TemporaryDirectory() as scratch:
        directory: pathlib.Path = pathlib.Path(scratch)
        for source in TESTS.iterdir():
            (directory / source.name).symlink_to(source)
        try:
            done: subprocess.CompletedProcess[bytes] = subprocess.run(
                [engine, "-exception_file", exception_file],
                input=session,
                capture_output=True,
                check=False,  # the exit status is part of what is compared
                cwd=directory,
                env={},
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            return Run("", f"<timed out after {timeout} s>", TIMED_OUT, {})
        written: dict[str, str] = {
            path.name: path.read_text(errors="replace")
            for path in sorted(directory.glob("*.dump"))
        }
    stderr: str = "\n".join(
        line
        for line in done.stderr.decode(errors="replace").split("\n")
        if not line.startswith("Loaded: ")
    )
    return Run(done.stdout.decode(errors="replace"), stderr, done.returncode, written)


def main() -> int:
    """Compare every selected session; print one verdict per session.

    Returns:
        int: 0 when no session FAILs, else 1.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0]
    )
    parser.add_argument("--reference", required=True, help="the C spimulator")
    parser.add_argument("--engine", required=True, help="the port's executable")
    parser.add_argument("--exception-file", default=str(REPO / "src" / "exceptions.s"))
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("sessions", nargs="*", help="session names (default: all)")
    options: argparse.Namespace = parser.parse_args()
    reference: str = resolve_engine(options.reference)
    engine: str = resolve_engine(options.engine)
    corpus: dict[str, bytes] = sessions()
    unknown: list[str] = sorted(set(options.sessions) - set(corpus))
    names: list[str] = [
        n for n in corpus if not options.sessions or n in options.sessions
    ]
    if unknown or not names:
        # A selection that runs nothing must not pass.
        print(f"no such session: {', '.join(unknown)}" if unknown else "no sessions")
        return 1
    counts: dict[str, int] = {"PASS": 0, "WARN": 0, "FAIL": 0}
    for name in names:
        want: Run = run_session(
            reference, corpus[name], options.exception_file, options.timeout
        )
        got: Run = run_session(
            engine, corpus[name], options.exception_file, options.timeout
        )
        verdict: str
        notes: list[str]
        if want.status == TIMED_OUT:
            # Both engines hanging would compare equal; a hung reference proves nothing.
            verdict, notes = (
                "FAIL",
                [f"the C engine timed out after {options.timeout} s"],
            )
        else:
            verdict, notes = compare(want, got)
        counts[verdict] += 1
        print(f"{verdict} {name}")
        for note in notes[:10]:
            print(f"    {note}")
    print(f"{counts['PASS']} passed, {counts['WARN']} warned, {counts['FAIL']} failed")
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
