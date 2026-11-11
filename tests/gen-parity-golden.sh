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

# shellcheck source=tests/parity-capture.sh
. "$TESTS_DIR/parity-capture.sh"

# ast_parity: tt.core.s only.
capture tt.core.s default > "$TESTS_DIR/golden.ast_parity.txt"

# ast_parity_all: every program in tests/parity-programs.txt.
capture_all > "$TESTS_DIR/golden.ast_parity_all.txt"

echo "Regenerated goldens in $TESTS_DIR:"
echo "  golden.ast_parity.txt      ($(wc -l < "$TESTS_DIR/golden.ast_parity.txt") lines)"
echo "  golden.ast_parity_all.txt  ($(wc -l < "$TESTS_DIR/golden.ast_parity_all.txt") lines)"
