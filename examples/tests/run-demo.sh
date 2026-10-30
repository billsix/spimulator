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
# optional stdin to pipe in, and whether to also verify exit status.
expected_status=""
input=""
case "$NAME" in
  strings)
    # First non-library demo with a pinned golden: printable-run
    # extraction over deliberately binary-ish stdin.
    expected=$SRC_DIR/transforms/strings/strings.expected
    input=$SRC_DIR/transforms/strings/strings.input
    asm_files="-f $SRC_DIR/transforms/strings/strings.asm"
    ;;
  ctype-demo)
    expected=$LIB_DIR/libctype-demo/ctype-demo.expected
    asm_files="-f $LIB_DIR/libctype/libctype.asm \
               -f $LIB_DIR/libctype-demo/ctype-demo.asm"
    ;;
  str-demo)
    expected=$LIB_DIR/libstr-demo/str-demo.expected
    asm_files="-f $LIB_DIR/libstr/libstr.asm \
               -f $LIB_DIR/libstr-demo/str-demo.asm"
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
  rpn-1-stack)
    # Rung 1 of the rpn ladder: push whitespace-separated integers, print top.
    expected=$SRC_DIR/algorithms/rpn/rpn-1-stack.expected
    input=$SRC_DIR/algorithms/rpn/rpn-1-stack.input
    asm_files="-f $SRC_DIR/algorithms/rpn/rpn-1-stack.asm"
    ;;
  rpn-2-ops)
    # Rung 2 of the rpn ladder: the four operators, integer.
    expected=$SRC_DIR/algorithms/rpn/rpn-2-ops.expected
    input=$SRC_DIR/algorithms/rpn/rpn-2-ops.input
    asm_files="-f $SRC_DIR/algorithms/rpn/rpn-2-ops.asm"
    ;;
  rpn-3-float)
    # Rung 3 of the rpn ladder: the FPU — doubles + decimals.
    expected=$SRC_DIR/algorithms/rpn/rpn-3-float.expected
    input=$SRC_DIR/algorithms/rpn/rpn-3-float.input
    asm_files="-f $SRC_DIR/algorithms/rpn/rpn-3-float.asm"
    ;;
  rpn)
    expected=$SRC_DIR/algorithms/rpn/rpn.expected
    input=$SRC_DIR/algorithms/rpn/rpn.input
    asm_files="-f $SRC_DIR/algorithms/rpn/rpn.asm"
    ;;
  calc-1-numbers)
    # Rung 1 of the calc ladder (tasks/calc-incremental-versions.md): read &
    # print one whole integer per line.
    expected=$SRC_DIR/lang/calc/calc-1-numbers.expected
    input=$SRC_DIR/lang/calc/calc-1-numbers.input
    asm_files="-f $SRC_DIR/lang/calc/calc-1-numbers.asm"
    ;;
  calc-2-addsub)
    # Rung 2: integer '+'/'-' left-to-right.
    expected=$SRC_DIR/lang/calc/calc-2-addsub.expected
    input=$SRC_DIR/lang/calc/calc-2-addsub.input
    asm_files="-f $SRC_DIR/lang/calc/calc-2-addsub.asm"
    ;;
  calc-3-muldiv)
    # Rung 3: integer '*'/'/' binding tighter than '+'/'-'.
    expected=$SRC_DIR/lang/calc/calc-3-muldiv.expected
    input=$SRC_DIR/lang/calc/calc-3-muldiv.input
    asm_files="-f $SRC_DIR/lang/calc/calc-3-muldiv.asm"
    ;;
  calc-4-parens)
    # Rung 4: parentheses for grouping (mutual recursion expr<->factor).
    expected=$SRC_DIR/lang/calc/calc-4-parens.expected
    input=$SRC_DIR/lang/calc/calc-4-parens.input
    asm_files="-f $SRC_DIR/lang/calc/calc-4-parens.asm"
    ;;
  calc-5-neg)
    # Rung 5: unary minus / negative integers.
    expected=$SRC_DIR/lang/calc/calc-5-neg.expected
    input=$SRC_DIR/lang/calc/calc-5-neg.input
    asm_files="-f $SRC_DIR/lang/calc/calc-5-neg.asm"
    ;;
  calc-6-float)
    # Rung 6: the FPU — doubles + decimal literals; '/' now yields decimals.
    expected=$SRC_DIR/lang/calc/calc-6-float.expected
    input=$SRC_DIR/lang/calc/calc-6-float.input
    asm_files="-f $SRC_DIR/lang/calc/calc-6-float.asm"
    ;;
  calc-sdt)
    expected=$SRC_DIR/lang/calc/calc-sdt.expected
    input=$SRC_DIR/lang/calc/calc-sdt.input
    asm_files="-f $SRC_DIR/lang/calc/calc-sdt.asm"
    ;;
  calc-tree)
    # Shares calc-sdt's golden + input: the AST-building version must produce
    # byte-identical output to the syntax-directed one.
    expected=$SRC_DIR/lang/calc/calc-sdt.expected
    input=$SRC_DIR/lang/calc/calc-sdt.input
    asm_files="-f $SRC_DIR/lang/calc/calc-tree.asm"
    ;;
  *)
    fail "unknown demo: $NAME"
    ;;
esac

[ -f "$expected" ] || fail "missing golden: $expected"

# --- Run the C side ---
# Demo executables install/build with a `spimulator-example-` prefix so the
# coreutil-named ones (cp, head, wc, …) don't shadow system tools on PATH.
"$C_BIN_DIR/spimulator-example-$NAME" < "${input:-/dev/null}" > "$out_c" 2>&1
c_status=$?

# --- Run the asm side under spim ---
$SPIM -exception_file "$EF" -noexplain -quiet $asm_files \
  < "${input:-/dev/null}" > "$out_asm" 2>&1
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
