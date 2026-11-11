#!/usr/bin/env bash
# Run REPL sessions and command lines through the C engine and the Python
# port, under env -i, and diff everything each prints (stdout and stderr
# together, then the exit status and any file it wrote).
#
# Usage: diff_repl.sh C_SPIMULATOR
# Exits 1 if any case differs.
set -u
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
C=$1
P=$REPO/port/python/spimulator-python
EF=$REPO/src/exceptions.s
status=0
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp "$REPO"/tests/*.s "$REPO"/tests/tt.in "$work"/
cd "$work" || exit 2

one() { # ENGINE OUT INPUT ARGS...
  local engine=$1 out=$2 input=$3; shift 3
  rm -f ./*.out text.asm data.asm spim.dump x.dump
  printf '%b' "$input" | env -i timeout 60 "$engine" "$@" >"$out" 2>&1
  echo "rc $?" >>"$out"
  # timeout(1)'s own note when the C engine crashes (see -stext 64 below).
  sed -i '/the monitored command dumped core/d' "$out"
  for f in ./*.out text.asm data.asm spim.dump x.dump; do
    [ -f "$f" ] && { echo "== $f"; od -c "$f"; } >>"$out"
  done
  rm -f ./*.out text.asm data.asm spim.dump x.dump
}
case_() { # INPUT ARGS...
  one "$C" c.o "$@"; one "$P" p.o "$@"
  if cmp -s c.o p.o; then echo "SAME $*" | tr '\n' ' '; echo
  else echo "DIFF $*"; diff c.o p.o | head -8; status=1; fi
}

E="-exception_file $EF"
# REPL sessions.
case_ 'load "tt.le.s"\nprint_symbols\nprint_all_regs\nprint_all_regs hex\nquit\n' $E
case_ 'load "tt.le.s"\nbreakpoint main\nlist\nrun\nprint $pc\nstep\n\n\nstep 3\ncontinue\n' $E
case_ 'load "tt.argv.s"\nstep 5\nprint $sp\nprint 4($sp)\nprint main\nprint 0x10000000\n\n\nprint $f0\nprint $hi\nprint $status\n' $E
case_ 'help\n?\nreinitialize\nfoo bar\nli\n' $E
case_ 'load "tt.le.s"\ndelete main\nbreakpoint 0x400000\ndisasm\ndump\ndumpnative "x.dump"\nexit\n' $E
case_ 'load "nofile.s"\nread\nprint\n' $E
case_ '. li $t0 5\nstep\nprint $t0\n' $E
case_ 'load "tt.core.s"\nrun\n' $E
case_ 'load "tt.fpu.bare.s"\nrun\nprint_all_regs\nprint $f2\nprint $f3\n' -bare -noexception
case_ 'load "tt.core.s"\nbreakpoint 0x400030\nrun\nprint_all_regs hex\ncontinue\n' $E
case_ 'read "tt.le.s"\nrun 0x400000\nreinitialize\nload "tt.dir.s"\nrun\nprint foo\nbreakpoint\n' $E
case_ 'step\nprint $sp\nprint 0x80000180\nprint 0x90000000\nprint 0x7ffffffc\nprint 5\n'
case_ 'load "tt.le.s"\nstep 2\n\nprint $pc\nprint (\nprint 4($t0\n' $E
case_ 'load "tt.alu.bare.s"\nrun\n' -bare -noexception
case_ 'read "tt.args-cmd.s"\nargs zebra 12 "x y"\nrun\nargs\nrun\nquit\n' $E
# Command lines.
case_ '' -assemble -f tt.le.s
case_ '' -zork -f tt.le.s
case_ '' -f missing.s
case_ '' -nopseudo -f tt.le.s
case_ '' -quiet -f tt.le.s
case_ '' -bare -f tt.le.s
case_ '' -f
case_ '' -listing
case_ '' -listing /nonexistent/x -f tt.le.s
case_ '' -exception_file nofile.s -f tt.le.s
case_ '' -mapped_io -f tt.le.s
case_ '' -full_dump -f tt.le.s
case_ '' -sdata 100 -stext 8192 -f tt.le.s
case_ '' -stext 64 -f tt.le.s
case_ '' tt.le.s a b c
case_ '' -noexception -f tt.le.s
case_ '' -delayed_branches -f tt.le.s
case_ '' ""
exit $status
