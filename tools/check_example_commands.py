#!/usr/bin/env python3
"""Run the BUILD AND RUN commands documented in each example .c, verbatim.

Every teaching demo under examples/src/ carries a header block (see
tasks/reference/example-c-teaching-style.md) of this shape:

    /* BUILD AND RUN, from this file's directory:
     *
     *   native Linux build, then run:
     *     $ gcc -std=gnu23 -nostdlib -I../.. -o echo echo.c ../../print-char.c ...
     *     $ ./echo hello world
     *
     *   the paired MIPS assembly, in spimulator:
     *     $ spimulator -f echo.asm hello world
     *
     *   the same assembly, in the Python port:
     *     $ spimulator-python -f echo.asm hello world
     */

A demo whose assembly comes in numbered versions (NAME-1.asm, NAME-2.asm)
lists one spimulator line per version.  The lines after them run the same
assembly through each finished port of the engine (port/README.md), one
`spimulator-NAME` line per spimulator line.  The checker copies
examples/src/ to a scratch directory (the commands write binaries and, for
tee/cp/touch, output files), then for each demo runs the `$ ` lines with bash
from the demo's directory.  A demo passes when the compile exits 0 and every
simulator run prints the same stdout, and exits with the same status, as the
native run (except the versions listed in DIFFERS_ON_PURPOSE, which need only
run).  stderr is not compared: spimulator reports its own load messages
there.  A `spimulator-NAME` line is checked only when --port NAME=PATH says
which executable it runs; without that option such lines are skipped, and
the summary says how many.

Usage:
    tools/check_example_commands.py [--spimulator PATH] [--port NAME=PATH ...]
                                    [--jobs N] [FILE.c ...]

With no files, every teaching .c is checked, and one without a block is a
failure (the IO helpers at the top of examples/src, the three library
sources and the template carry none).
--spimulator names the binary to run as `spimulator` (default: the one on
PATH), so the documented line stays verbatim; --port python=PATH likewise
names the one to run as `spimulator-python` (repeatable, one per port).

Exit status: 0 if every demo passes, 1 otherwise.
"""

import argparse
import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parents[1]
SRC: pathlib.Path = ROOT / "examples" / "src"
MARK: str = "BUILD AND RUN"
TIMEOUT_SECONDS: int = 60

# Assembly versions whose run is not meant to match the native one; each
# header says why.  For these the checker only requires that the documented
# command runs to completion.  Keyed by the .asm path under examples/src.
DIFFERS_ON_PURPOSE: dict[str, str] = {
    "recursion/get-char-from-user/get-char-from-user-1.asm": (
        "the deliberately broken version; it raises address-error exceptions"
    ),
    "recursion/get-char-from-user/get-char-from-user-2.asm": (
        "prints the character and its value in the other order from the C"
    ),
    "extras/subrountines/subrountines-1.asm": (
        "never sets $v0 before main returns, so the exit status is leftover"
    ),
    "extras/testStringsForEquality/testStringsForEquality-1.asm": (
        "prints only the comparison results, without the C version's labels"
    ),
}


# Sources that are not demos, so carry no BUILD AND RUN block.
NOT_DEMOS: tuple[str, ...] = (
    "lib/libctype/",
    "lib/libstdlib/",
    "lib/libstr/",
    "template/",
)


def is_demo(path: pathlib.Path) -> bool:
    rel: str = path.relative_to(SRC).as_posix()
    return "/" in rel and not rel.startswith(NOT_DEMOS)


def commands(text: str) -> list[str]:
    """The `$ ` command lines of the BUILD AND RUN block, in order."""
    start: int = text.index(MARK)
    end: int = text.index("*/", start)
    prefix: str = " *     $ "
    return [
        line[len(prefix) :]
        for line in text[start:end].splitlines()
        if line.startswith(prefix)
    ]


def run(
    cmd: str, cwd: pathlib.Path, env: dict[str, str]
) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ["bash", "-c", cmd],
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=TIMEOUT_SECONDS,
        check=False,
    )


def simulator_of(cmd: str) -> str:
    """The simulator a documented line runs: `spimulator` or `spimulator-NAME`."""
    return next(w for w in cmd.split() if w.startswith("spimulator"))


def check(
    demo: pathlib.Path, scratch: pathlib.Path, env: dict[str, str], ports: set[str]
) -> tuple[str | None, int]:
    """(None or a failure message, number of port lines skipped) for DEMO.

    A `spimulator-NAME` line runs only when NAME is in ports.
    """
    rel: pathlib.Path = demo.relative_to(SRC)
    lines: list[str] = commands(demo.read_text())
    if len(lines) < 3:
        return f"{rel}: expected at least 3 '$ ' lines, found {len(lines)}", 0
    build, native, *simulated_runs = lines
    cwd: pathlib.Path = scratch / rel.parent
    built: subprocess.CompletedProcess[bytes] = run(build, cwd, env)
    if built.returncode != 0:
        return (
            f"{rel}: build failed ({built.returncode}): {build}\n"
            f"{built.stderr.decode(errors='replace')}"
        ), 0
    ran: subprocess.CompletedProcess[bytes] = run(native, cwd, env)
    skipped: int = 0
    for simulated in simulated_runs:
        port: str = simulator_of(simulated).removeprefix("spimulator").removeprefix("-")
        if port and port not in ports:
            skipped += 1
            continue
        sim: subprocess.CompletedProcess[bytes] = run(simulated, cwd, env)
        asm: str = next(
            w for w in simulated.split() if w.endswith(".asm") and "/" not in w
        )
        if str(rel.parent / asm) in DIFFERS_ON_PURPOSE:
            continue
        elif (ran.stdout, ran.returncode) != (sim.stdout, sim.returncode):
            return (
                f"{rel}: native and {simulator_of(simulated)} runs differ\n"
                f"  native (exit {ran.returncode}): {native}\n"
                f"{ran.stdout.decode(errors='replace')}"
                f"  {simulator_of(simulated)} (exit {sim.returncode}): {simulated}\n"
                f"{sim.stdout.decode(errors='replace')}"
                f"{sim.stderr.decode(errors='replace')}"
            ), skipped
    return None, skipped


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0]
    )
    parser.add_argument("--spimulator", help="binary to run as `spimulator`")
    parser.add_argument(
        "--port",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="binary to run as `spimulator-NAME` (repeatable)",
    )
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("files", nargs="*", type=pathlib.Path)
    args: argparse.Namespace = parser.parse_args()

    demos: list[pathlib.Path] = [f.resolve() for f in args.files] or sorted(
        f for f in SRC.rglob("*.c") if is_demo(f)
    )
    missing: list[str] = [
        f"{d.relative_to(SRC)}: no {MARK} block"
        for d in demos
        if MARK not in d.read_text()
    ]
    demos = [d for d in demos if MARK in d.read_text()]
    ports: dict[str, str] = dict(p.split("=", 1) for p in args.port)
    with tempfile.TemporaryDirectory() as tmp:
        scratch: pathlib.Path = pathlib.Path(tmp) / "src"
        shutil.copytree(SRC, scratch)
        env: dict[str, str] = dict(os.environ)
        programs: dict[str, str] = {f"spimulator-{n}": p for n, p in ports.items()}
        if args.spimulator:
            programs["spimulator"] = args.spimulator
        if programs:
            bindir: pathlib.Path = pathlib.Path(tmp) / "bin"
            bindir.mkdir()
            for name, path in programs.items():
                (bindir / name).symlink_to(pathlib.Path(path).resolve())
            env["PATH"] = f"{bindir}{os.pathsep}{env.get('PATH', '')}"
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results: list[tuple[str | None, int]] = list(
                pool.map(lambda d: check(d, scratch, env, set(ports)), demos)
            )
    failures: list[str] = missing + [m for m, _ in results if m]
    skipped: int = sum(n for _, n in results)
    for message in failures:
        print(message, file=sys.stderr)
    total: int = len(demos) + len(missing)
    print(f"{total - len(failures)}/{total} demos: documented commands agree")
    if skipped:
        print(
            f"note: {skipped} port lines skipped (no --port NAME=PATH for their port)"
        )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
