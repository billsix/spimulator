#!/usr/bin/env python3
"""Wide differential test of the Haskell port against the C engine.

Every .s/.asm file in the repository, in several modes (assemble and dump,
the three AST views, the -listing trace, a full run with empty stdin, bare
machine), through both engines; stdout, stderr, exit status and any
text.asm/data.asm/listing they write are compared byte for byte.  A run that
times out in both engines (a program waiting for input that never ends)
counts as agreeing.

Usage (from anywhere):
    differential.py C-SPIMULATOR HASKELL-LAUNCHER [--jobs N] [--only SUBSTRING]
"""

import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[3]
EF = str(REPO / "src" / "exceptions.s")

MODES = {
    "dump": ["-exception_file", EF, "-dump"],
    "full_dump": ["-exception_file", EF, "-full_dump"],
    "bare_dump": ["-bare", "-noexception", "-dump"],
    "ast": ["-noexception", "-print-ast"],
    "json": ["-noexception", "-print-ast-json"],
    "expansion": ["-noexception", "-show-expansion"],
    "listing": ["-exception_file", EF, "-listing", "listing.txt"],
    "run": ["-exception_file", EF],
    "run_builtin": [],
    "assemble": ["-exception_file", EF, "-assemble"],
    "delayed": ["-exception_file", EF, "-delayed_branches", "-delayed_loads"],
    "mapped": ["-exception_file", EF, "-mapped_io"],
    "quiet": ["-quiet", "-exception_file", EF],
    "bare_run": ["-bare", "-noexception"],
}


def run(engine, args, cwd):
    try:
        p = subprocess.run([engine, *args], cwd=cwd, stdin=subprocess.DEVNULL,
                           capture_output=True, env={"LC_ALL": "C"}, timeout=10)
        status = p.returncode
        out, err = p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return ("timeout",)
    files = {}
    for name in sorted(os.listdir(cwd)):
        path = pathlib.Path(cwd) / name
        if path.is_file():
            files[name] = path.read_bytes()
    return (status, out, err, files)


def case(engines, source, mode):
    results = []
    for engine in engines:
        with tempfile.TemporaryDirectory() as scratch:
            args = MODES[mode] + ["-f", str(source)]
            if mode == "assemble":
                # -assemble writes <file>.out next to the program
                local = pathlib.Path(scratch) / source.name
                local.write_bytes(source.read_bytes())
                args = MODES[mode] + ["-f", local.name]
            results.append(run(engine, args, scratch))
    c, h = results
    if c == h:
        return None
    if c == ("timeout",) or h == ("timeout",):
        return f"{mode} {source}: C {c[0]}, port {h[0]}"
    notes = []
    names = ["status", "stdout", "stderr", "files"]
    for i, name in enumerate(names):
        if c[i] != h[i]:
            if name == "files":
                for f in sorted(set(c[3]) | set(h[3])):
                    if c[3].get(f) != h[3].get(f):
                        notes.append(f"file {f} differs")
            elif name == "status":
                notes.append(f"status C {c[0]} port {h[0]}")
            else:
                cl = c[i].split(b"\n")
                hl = h[i].split(b"\n")
                for k, (a, b) in enumerate(zip(cl, hl)):
                    if a != b:
                        notes.append(f"{name}:{k + 1}: C {a[:120]!r} port {b[:120]!r}")
                        break
                else:
                    notes.append(f"{name}: {len(cl)} vs {len(hl)} lines")
    return f"{mode} {source.relative_to(REPO)}: " + "; ".join(notes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("c_engine")
    ap.add_argument("port")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--only", default="")
    ap.add_argument("--modes", default=",".join(MODES))
    o = ap.parse_args()
    sources = sorted(p for p in REPO.rglob("*") if p.suffix in (".s", ".asm")
                     and ".git" not in p.parts and "build" not in p.parts and o.only in str(p))
    jobs = [(s, m) for s in sources for m in o.modes.split(",")]
    engines = [str(pathlib.Path(o.c_engine).resolve()), str(pathlib.Path(o.port).resolve())]
    failures = []
    with concurrent.futures.ThreadPoolExecutor(o.jobs) as pool:
        for r in pool.map(lambda j: case(engines, *j), jobs):
            if r:
                failures.append(r)
                print("DIFF", r, flush=True)
    print(f"{len(jobs) - len(failures)} of {len(jobs)} cases agree")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
