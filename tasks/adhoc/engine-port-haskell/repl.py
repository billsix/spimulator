#!/usr/bin/env python3
"""Drive the REPL of both engines with the same command scripts; compare.

Each script below is fed on stdin (no terminal), from tests/, with a fixed
environment, and stdout, stderr and exit status must match byte for byte.

Usage: repl.py C-SPIMULATOR HASKELL-LAUNCHER
"""

import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
EF = str(REPO / "src" / "exceptions.s")

SCRIPTS = {
    "args": 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber 12 "q s"\nrun\nquit\n',
    "disasm": 'load "tt.le.s"\ndisasm\nexit\n',
    "help": "help\n?\nh\nexit\n",
    "unknown": "bogus 1 2\nfrobnicate\nexit\n",
    "print_regs": 'load "tt.core.s"\nprint $sp\nprint $f2\nprint $f3\nprint $pc\nprint $hi\nprint $status\nprint $badvaddr\nprint $epc\nprint $cause\nprint $fpcond\nprint $lo\nexit\n',
    "print_mem": 'load "tt.le.s"\nprint 0x10010000\n\n\nprint 0x400000\nprint 4($sp)\nprint 4(\nprint main\nprint nosuchlabel\nprint\nexit\n',
    "symbols": 'load "tt.multifile.helper.s"\nload "tt.multifile.s"\nprint_symbols\nexit\n',
    "all_regs": 'load "tt.le.s"\nrun\nprint_all_regs hex\n',
    "all_regs_dec": 'load "tt.fpu.bare.s"\nprint_all_regs\n',
    "step": 'load "tt.le.s"\nstep\nstep 5\n\n\nprint $pc\nexit\n',
    "breakpoint": 'load "tt.le.s"\nbreakpoint main\nlist\nrun\ndisasm\ncontinue\ndelete main\nlist\ndelete main\nexit\n',
    "bkpt_addr": 'load "tt.le.s"\nbreakpoint 0x400010\nbreakpoint 0x400010\nrun\nstep 3\ncontinue\nexit\n',
    "bad_bkpt": "breakpoint 0x10\nbreakpoint 0x10000000\nexit\n",
    "reinit": 'load "tt.le.s"\nreinitialize\nload "tt.dir.s"\nrun\nexit\n',
    "asm_line": 'load "tt.le.s"\n. addiu $t0, $zero, 7\nprint $t0\nexit\n',
    "dump": 'load "tt.le.s"\ndump "a.dump"\ndumpnative "b.dump"\ndump\ndump "/nonexistent/x"\ndump 5\nexit\n',
    "eof": 'load "tt.le.s"\n',
    "no_newline": 'load "tt.le.s"\nprint $sp',
    "read_missing": 'read "nosuch.s"\nread\nload 5\nexit\n',
    "run_input": 'load "tt.read_int_eof.s"\nrun\n5\n7\n',
    "parse_error": 'load "tt.parse_error.s"\nrun\nexit\n',
    "missing_main": 'load "tt.missing_main.s"\nrun\nexit\n',
    "redo_print": 'load "tt.le.s"\nprint $4\n\n\nprint $f0\n\nexit\n',
    "run_twice": 'load "tt.stderr_split.s"\nrun\nrun\nexit\n',
}


def run(engine, script, scratch):
    # The programs come from tests/, linked into a scratch directory so that
    # the dump commands write there.
    for f in (REPO / "tests").iterdir():
        (pathlib.Path(scratch) / f.name).symlink_to(f)
    p = subprocess.run(
        [engine, "-exception_file", EF],
        input=script.encode(),
        capture_output=True,
        cwd=scratch,
        env={"HOME": "/nonexistent"},
        timeout=60,
    )
    files = {f.name: f.read_bytes() for f in pathlib.Path(scratch).glob("*.dump")}
    return p.returncode, p.stdout, p.stderr, files


def main():
    import tempfile

    c_engine, port = (str(pathlib.Path(a).resolve()) for a in sys.argv[1:3])
    bad = 0
    for name, script in SCRIPTS.items():
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            want = run(c_engine, script, a)
            got = run(port, script, b)
        if want == got:
            print("PASS", name)
            continue
        bad += 1
        print("FAIL", name)
        for label, w, g in zip(["status", "stdout", "stderr", "files"], want, got):
            if w != g:
                if isinstance(w, bytes):
                    for k, (x, y) in enumerate(zip(w.split(b"\n"), g.split(b"\n"))):
                        if x != y:
                            print(f"    {label}:{k + 1}: C {x[:150]!r}\n    {label}:{k + 1}: H {y[:150]!r}")
                            break
                    else:
                        print(f"    {label}: lengths {len(w)} {len(g)}")
                else:
                    print(f"    {label}: C {w!r} H {g!r}"[:300])
    print(f"{len(SCRIPTS) - bad} of {len(SCRIPTS)} REPL scripts agree")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
