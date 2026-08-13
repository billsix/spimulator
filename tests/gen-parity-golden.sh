#!/usr/bin/env bash
# Regenerate the AST-emit golden dumps used by the `ast_parity` and
# `ast_parity_all` regression tests.
#
# Usage: gen-parity-golden.sh <spim-binary> [<exception-file>]
#
# These goldens capture the assembled text + data segment dumps (with
# the `; NNN: source` scanner annotations stripped) for a set of
# representative programs.  They were first captured from the SDT parser
# just before it was removed (2026-08-03); the tests then verify that
# the AST-only emitter reproduces the same bytes.  Run this only when an
# INTENTIONAL codegen change lands, and review the diff before committing.

set -eu

if [ $# -lt 1 ]; then
  echo "usage: $0 <spim-binary> [<exception-file>]" >&2
  exit 2
fi

SPIM=$1
TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
EF=${2:-$TESTS_DIR/../src/exceptions.s}
case "$SPIM" in /*) ;; *) SPIM=$(realpath "$SPIM") ;; esac
case "$EF"   in /*) ;; *) EF=$(realpath "$EF") ;; esac

capture() { # $1=prog -> append bare text+data dump (with headers) to stdout
  local prog=$1 d
  d=$(mktemp -d)
  ( cd "$d" && "$SPIM" -exception_file "$EF" -dump \
      -f "$TESTS_DIR/$prog" </dev/null >/dev/null 2>&1 ) || true
  local seg
  for seg in text data; do
    if [ -f "$d/$seg.asm" ]; then
      echo "### $prog :: $seg ###"
      sed 's/[[:space:]]*;.*$//' "$d/$seg.asm"
    fi
  done
  rm -rf "$d"
}

# ast_parity: tt.core.s only.
capture tt.core.s > "$TESTS_DIR/golden.ast_parity.txt"

# ast_parity_all: the representative program set (keep in sync with the
# `progs` list in run-test.sh's ast_parity_all case).
: > "$TESTS_DIR/golden.ast_parity_all.txt"
for prog in \
    tt.alu.bare.s tt.argv.s tt.bare.s tt.be.s tt.core.s tt.dir.s \
    tt.divide_by_zero.s tt.explain.s tt.fpu.bare.s tt.io.s tt.le.s \
    tt.listing.s tt.missing_main.s tt.octal_escape.s tt.pseudo.s \
    tt.read_char_eof.s tt.read_int_eof.s tt.return_value.s \
    tt.stderr_split.s tt.unaligned.s; do
  [ -f "$TESTS_DIR/$prog" ] && capture "$prog" >> "$TESTS_DIR/golden.ast_parity_all.txt"
done

echo "Regenerated goldens in $TESTS_DIR:"
echo "  golden.ast_parity.txt      ($(wc -l < "$TESTS_DIR/golden.ast_parity.txt") lines)"
echo "  golden.ast_parity_all.txt  ($(wc -l < "$TESTS_DIR/golden.ast_parity_all.txt") lines)"
