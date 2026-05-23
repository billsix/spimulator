#!/usr/bin/env bash
# Run a library demo on BOTH the C side and the spim asm side,
# verify the stdout matches the pinned `.expected` golden, and
# (for demos that pin one) verify the host shell exit status
# matches `.expected-status`.
#
# Mirrors /spimulator/tests/run-test.sh's structure.  Each demo
# name names a case below that knows which .asm files to load
# for it (libctype always; libstdlib for demos that use it).
#
# Usage:
#   run-demo.sh <c-binary-dir> <spim-binary> <exception-file> <demo-name>
#
# Exits 0 on pass, 1 on fail.

set -u

if [ $# -lt 4 ]; then
  echo "usage: $0 <c-binary-dir> <spim> <exception-file> <demo-name>" >&2
  exit 2
fi

C_BIN_DIR=$1
SPIM=$2
EF=$3
NAME=$4

# Resolve to absolute paths before cd'ing.
case "$C_BIN_DIR" in /*) ;; *) C_BIN_DIR=$(realpath "$C_BIN_DIR") ;; esac
case "$SPIM"      in /*) ;; *) SPIM=$(realpath "$SPIM") ;; esac
case "$EF"        in /*) ;; *) EF=$(realpath "$EF") ;; esac

TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
SRC_DIR=$(cd "$TESTS_DIR/../src" && pwd)
LIB_DIR=$SRC_DIR/lib

out_c=$(mktemp)
out_asm=$(mktemp)
trap 'rm -f "$out_c" "$out_asm"' EXIT

fail() {
  echo "FAIL: $NAME — $1" >&2
  [ -s "$out_c" ]   && { echo "--- C stdout: ---"   >&2; tail -10 "$out_c" >&2; }
  [ -s "$out_asm" ] && { echo "--- asm stdout: ---" >&2; tail -10 "$out_asm" >&2; }
  exit 1
}

# Per-demo: what golden file, what .asm files to load for spim,
# and whether to also verify exit status.
expected_status=""
case "$NAME" in
  ctype-demo)
    expected=$LIB_DIR/libctype-demo/ctype-demo.expected
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libctype-demo/ctype-demo.asm"
    ;;
  atoi-demo)
    expected=$LIB_DIR/libstdlib-demo/atoi-demo.expected
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libstdlib/libstdlib.asm \
               -f $LIB_DIR/libstdlib-demo/atoi-demo.asm"
    ;;
  abs-demo)
    expected=$LIB_DIR/libstdlib-demo/abs-demo.expected
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libstdlib/libstdlib.asm \
               -f $LIB_DIR/libstdlib-demo/abs-demo.asm"
    ;;
  exit-demo)
    expected=$LIB_DIR/libstdlib-demo/exit-demo.expected
    expected_status=$LIB_DIR/libstdlib-demo/exit-demo.expected-status
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libstdlib/libstdlib.asm \
               -f $LIB_DIR/libstdlib-demo/exit-demo.asm"
    ;;
  bsearch-demo)
    expected=$LIB_DIR/libstdlib-demo/bsearch-demo.expected
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libstdlib/libstdlib.asm \
               -f $LIB_DIR/libstdlib-demo/bsearch-demo.asm"
    ;;
  atexit-demo)
    expected=$LIB_DIR/libstdlib-demo/atexit-demo.expected
    expected_status=$LIB_DIR/libstdlib-demo/atexit-demo.expected-status
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libstdlib/libstdlib.asm \
               -f $LIB_DIR/libstdlib-demo/atexit-demo.asm"
    ;;
  *)
    fail "unknown demo: $NAME"
    ;;
esac

[ -f "$expected" ] || fail "missing golden: $expected"

# --- Run the C side ---
"$C_BIN_DIR/$NAME" > "$out_c" 2>&1
c_status=$?

# --- Run the asm side under spim ---
$SPIM -exception_file "$EF" -noexplain -quiet $asm_files > "$out_asm" 2>&1
asm_status=$?

# --- Golden diff (C side) ---
if ! diff -q "$expected" "$out_c" > /dev/null; then
  fail "C stdout differs from golden ($expected); see diff:
$(diff "$expected" "$out_c" | head -10)"
fi

# --- Golden diff (asm side) ---
if ! diff -q "$expected" "$out_asm" > /dev/null; then
  fail "spim stdout differs from golden ($expected); see diff:
$(diff "$expected" "$out_asm" | head -10)"
fi

# --- Exit-status check, if pinned ---
if [ -n "$expected_status" ]; then
  [ -f "$expected_status" ] || fail "missing status golden: $expected_status"
  want=$(tr -d ' \t\n' < "$expected_status")
  [ "$c_status"   = "$want" ] || fail "C exit was $c_status, expected $want"
  [ "$asm_status" = "$want" ] || fail "asm exit was $asm_status, expected $want"
fi

echo "PASS: $NAME"
