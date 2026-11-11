#!/usr/bin/env python3
"""Differential checks of the Rust port against the C engine beyond the
shared corpus: every assembly file of tests/, examples/ and pgu/ through
-dump, -full_dump, -print-ast, -print-ast-json, -show-expansion, -listing,
-assemble, a plain run, and a set of REPL sessions.

Usage: diff_more.py C_ENGINE PORT [FILTER]

Each case runs in a fresh scratch directory (holding a copy of nothing but
what the case writes), with an empty environment and the same stdin, and
compares stdout, stderr, exit status and the files written.  Prints FAIL
lines and a summary.  FILTER keeps the cases whose name contains it.
"""

import os
import pathlib
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[3]
EF = str(REPO / "src" / "exceptions.s")


def sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = sorted((REPO / "tests").glob("*.s"))
    files += sorted((REPO / "examples" / "src").rglob("*.asm"))
    files += sorted((REPO / "pgu" / "src").rglob("*.asm"))
    files += sorted((REPO / "pgu" / "src").rglob("*.s"))
    return files


def run(engine: str, args: list[str], stdin: bytes, cwd: str) -> tuple:
    copied: list[str] = []
    for a in args:
        if a.startswith("COPY:"):
            source = pathlib.Path(a.removeprefix("COPY:"))
            (pathlib.Path(cwd) / source.name).write_bytes(source.read_bytes())
            a = source.name
        copied.append(a)
    args = copied
    try:
        done = subprocess.run(
            [engine, *args], input=stdin, capture_output=True, cwd=cwd,
            env={}, timeout=20,
        )
    except subprocess.TimeoutExpired:
        return ("timeout",)
    written = {
        p.name: p.read_bytes() for p in sorted(pathlib.Path(cwd).iterdir())
        if p.is_file()
    }
    return (done.stdout, done.stderr, done.returncode, written)


def cases() -> list[tuple[str, list[str], bytes]]:
    out: list[tuple[str, list[str], bytes]] = []
    for path in sources():
        rel = str(path.relative_to(REPO))
        p = str(path)
        out.append((f"dump {rel}", ["-exception_file", EF, "-dump", "-f", p], b""))
        out.append((f"full_dump {rel}", ["-exception_file", EF, "-full_dump", "-f", p], b""))
        out.append((f"print_ast {rel}", ["-noexception", "-print-ast", "-f", p], b""))
        out.append((f"json {rel}", ["-print-ast-json", "-f", p], b""))
        out.append((f"expansion {rel}", ["-show-expansion", "-f", p], b""))
        out.append((f"listing {rel}", ["-listing", "-", "-f", p], b"1\n2\n3\nabc\n"))
        out.append((f"run {rel}", ["-f", p, "arg1", "arg2"], b"7\n8\nhello\n"))
        out.append((f"bare {rel}", ["-bare", "-f", p], b""))
        # -assemble writes NAME.out next to NAME: give it a copy in the
        # scratch directory (COPY: is replaced in run()).
        out.append((f"assemble {rel}", ["-assemble", "-f", f"COPY:{p}"], b""))
    core = str(REPO / "tests" / "tt.core.s")
    le = str(REPO / "tests" / "tt.le.s")
    sessions: dict[str, str] = {
        "repl help": "help\n?\n\nexit\n",
        "repl disasm": f'load "{le}"\ndisasm\nquit\n',
        "repl regs": f'load "{le}"\nprint_all_regs\n',
        "repl regs hex": f'load "{le}"\nprint_all_regs hex\n',
        "repl symbols": f'load "{le}"\nprint_symbols\nexit\n',
        "repl step": f'load "{le}"\nstep 5\n\n\nprint $sp\nprint $f0\nprint $f1\nprint $pc\nprint $hi\nprint $epc\nprint 0x10010000\n\n\nprint main\nprint nosuch\nprint 4($sp)\nexit\n',
        "repl breakpoints": f'load "{le}"\nbreakpoint main\nlist\nrun\ncontinue\ndelete main\nlist\nbreakpoint 0x400000\ndelete 0x400004\ndisasm\nexit\n',
        "repl run twice": f'load "{core}"\nrun\nrun\nexit\n',
        "repl unknown": "frobnicate 3\n5\nexit\n",
        "repl print what": "print\n",
        "repl reinit": f'load "{le}"\nreinitialize\nprint_symbols\nexit\n',
        "repl dump": f'load "{le}"\ndump\ndumpnative "x.bin"\ndump 5\nexit\n',
        "repl asm": f'load "{le}"\n. li $t0, 5\nexit\n',
        "repl args": f'load "{REPO / "tests" / "tt.argv.s"}"\nargs alpha beta gamma\nrun\nexit\n',
        "repl read missing": 'read "nosuch.s"\nread 5\nexit\n',
        "repl eof no newline": "help",
        "repl empty": "",
    }
    for name, text in sessions.items():
        out.append((name, ["-exception_file", EF], text.encode()))
    flags: list[tuple[str, list[str]]] = [
        ("flag unknown", ["-zzz", "-f", le]),
        ("flag explain0", ["-explain=0", "-f", le]),
        ("flag explain bad", ["-explain=9", "-f", le]),
        ("flag nofile", ["-f", "nosuch.s"]),
        ("flag noexc", ["-noexception", "-f", le]),
        ("flag quiet", ["-quiet", "-f", core]),
        ("flag delayed", ["-delayed_branches", "-delayed_loads", "-f", core]),
        ("flag nopseudo", ["-nopseudo", "-f", core]),
        ("flag mapped", ["-mapped_io", "-f", str(REPO / "tests" / "tt.io.s")]),
        ("flag sizes", ["-stext", "70000", "-sdata", "100000", "-sstack", "8192", "-f", core]),
        ("flag listing nofile", ["-listing"]),
        ("flag positional", [le, "x", "y"]),
        ("flag two f", ["-f", le, "-f", core]),
        ("flag ef list", ["-exception_file", f"{EF};{EF}", "-f", le]),
    ]
    for name, args in flags:
        out.append((name, args, b"1\n2\n"))
    return out


def main() -> int:
    c_engine, port = sys.argv[1], sys.argv[2]
    wanted = sys.argv[3] if len(sys.argv) > 3 else ""
    failed = passed = 0
    for name, args, stdin in cases():
        if wanted not in name:
            continue
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            want = run(c_engine, args, stdin, a)
            got = run(port, args, stdin, b)
        if want == got:
            passed += 1
            continue
        failed += 1
        print(f"FAIL {name}")
        if len(want) != len(got):
            print(f"    C {want[0]!r} port {got[0]!r}")
            continue
        for label, w, g in zip(("stdout", "stderr", "status", "files"), want, got):
            if w != g:
                if isinstance(w, bytes):
                    wl, gl = w.split(b"\n"), g.split(b"\n")
                    for i, (x, y) in enumerate(zip(wl, gl)):
                        if x != y:
                            print(f"    {label}:{i + 1}: C {x[:160]!r}")
                            print(f"    {label}:{i + 1}: R {y[:160]!r}")
                            break
                    else:
                        print(f"    {label}: lengths {len(wl)} vs {len(gl)}")
                elif isinstance(w, dict):
                    for key in sorted(set(w) | set(g)):
                        if w.get(key) != g.get(key):
                            print(f"    file {key} differs")
                else:
                    print(f"    {label}: C {w!r} port {g!r}")
    print(f"{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
