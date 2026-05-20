#!/usr/bin/env bash
# Test runner driven by meson test().
#
# Usage: run-test.sh <spim-binary> <exception-file> <test-name>
#
# Each test name names a known pattern below.  The driver handles
# stdin / argv / output checks per test.  Exits 0 on pass, 1 on fail.

set -u

if [ $# -lt 3 ]; then
  echo "usage: $0 <spim> <exception-file> <test-name>" >&2
  exit 2
fi

SPIM=$1
EF=$2
NAME=$3

# Resolve the spim binary and exception file to absolute paths before cd'ing
# into the tests dir, so the caller can pass relative paths.
case "$SPIM" in /*) ;; *) SPIM=$(realpath "$SPIM") ;; esac
case "$EF"   in /*) ;; *) EF=$(realpath "$EF") ;; esac

TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$TESTS_DIR"

out=$(mktemp); trap 'rm -f "$out"' EXIT
fail() { echo "FAIL: $NAME — $1" >&2; [ -s "$out" ] && tail -5 "$out" >&2; exit 1; }
expect_sentinel() {
  tail -n 1 "$out" | grep -q '^Passed all tests$' || fail "missing 'Passed all tests' sentinel"
}
expect_exit() {
  [ "$1" = "$2" ] || fail "expected exit=$1, got exit=$2"
}

case "$NAME" in
  # --- Dockerfile smoke set: tests that print "Passed all tests" ---
  bare)
    "$SPIM" -delayed_branches -delayed_loads -noexception -f tt.bare.s >"$out" 2>&1
    expect_sentinel
    ;;
  core)
    "$SPIM" -exception_file "$EF" -f tt.core.s <tt.in >"$out" 2>&1
    expect_sentinel
    ;;
  le)
    "$SPIM" -exception_file "$EF" -f tt.le.s >"$out" 2>&1
    expect_sentinel
    ;;
  dir)
    "$SPIM" -exception_file "$EF" -f tt.dir.s >"$out" 2>&1
    expect_sentinel
    ;;
  octal_escape)
    "$SPIM" -exception_file "$EF" -f tt.octal_escape.s >"$out" 2>&1
    expect_sentinel
    ;;
  argv)
    "$SPIM" -exception_file "$EF" -f tt.argv.s alpha beta gamma >"$out" 2>&1
    expect_sentinel
    ;;
  read_int_eof)
    printf '5\n7\n' | "$SPIM" -exception_file "$EF" -f tt.read_int_eof.s >"$out" 2>&1
    expect_sentinel
    ;;

  # --- Tests with custom pass criteria ---
  read_char_eof)
    # Test outputs 'abc' (no trailing newline) then 'Passed all tests\n', so
    # 'Passed all tests' is NOT on its own line.  grep -q for the substring,
    # not the sentinel pattern.
    echo -n abc | "$SPIM" -exception_file "$EF" -f tt.read_char_eof.s >"$out" 2>&1
    grep -q 'Passed all tests' "$out" || fail "missing 'Passed all tests' substring"
    ;;
  args_cmd)
    # REPL test: read a file, then change argv twice via the 'args' command,
    # verify each run sees the new argv[1].
    printf 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber\nrun\nquit\n' \
      | "$SPIM" -exception_file "$EF" >"$out" 2>&1
    grep -q 'argv1=z' "$out" || fail "first run didn't see argv1=z"
    grep -q 'argv1=a' "$out" || fail "second run didn't see argv1=a"
    ;;

  # --- Unix-process conformance tests (exit codes) ---
  return_value)
    # main returns 42, should be the shell exit status.
    "$SPIM" -exception_file "$EF" -f tt.return_value.s >"$out" 2>&1; rc=$?
    expect_exit 42 "$rc"
    ;;
  unaligned)
    # Unaligned fetch raises Exception 4 (AdEL); spim should exit 128+4 = 132.
    "$SPIM" -exception_file "$EF" -f tt.unaligned.s >"$out" 2>&1; rc=$?
    expect_exit 132 "$rc"
    ;;
  missing_main)
    # `main` undefined → spim should exit 1.
    "$SPIM" -exception_file "$EF" -f tt.missing_main.s >"$out" 2>&1; rc=$?
    expect_exit 1 "$rc"
    ;;
  parse_error)
    # Intentional parse error → spim should exit 2.
    "$SPIM" -exception_file "$EF" -f tt.parse_error.s >"$out" 2>&1; rc=$?
    expect_exit 2 "$rc"
    ;;
  stderr_split)
    # Banner on stderr, 'ok' on stdout.  Verify stdout is exactly 'ok\n'.
    stdout_only=$("$SPIM" -exception_file "$EF" -f tt.stderr_split.s 2>/dev/null)
    [ "$stdout_only" = "ok" ] || fail "stdout was '$stdout_only', expected 'ok'"
    ;;

  explain_parity)
    # Sanity check that -explain doesn't perturb execution semantics:
    # run the same program at -explain=0 (no narration) and -explain=2
    # (full narration); the assembled text + data dumps must be
    # byte-identical regardless of explain level.
    dir0=$(mktemp -d); dir2=$(mktemp -d)
    trap 'rm -f "$out"; rm -rf "$dir0" "$dir2"' EXIT
    ( cd "$dir0" && "$SPIM" -exception_file "$EF" -noexplain -dump \
        -f "$TESTS_DIR/tt.core.s" </dev/null >/dev/null 2>&1 )
    ( cd "$dir2" && "$SPIM" -exception_file "$EF" -explain=2 -dump \
        -f "$TESTS_DIR/tt.core.s" </dev/null >/dev/null 2>&1 )
    diff -q "$dir0/text.asm" "$dir2/text.asm" >/dev/null \
      || fail "text.asm differs between explain=0 and explain=2"
    diff -q "$dir0/data.asm" "$dir2/data.asm" >/dev/null \
      || fail "data.asm differs between explain=0 and explain=2"
    ;;

  divide_by_zero)
    # spim's div pseudo-op traps on zero divisor.  The default
    # exception handler reports ExcCode 9 (Bp) and continues, so
    # the program exits 0 from its own exit syscall.  Verify both:
    # the stderr report fired AND the program reached its exit.
    "$SPIM" -exception_file "$EF" -f tt.divide_by_zero.s >"$out" 2>&1; rc=$?
    expect_exit 0 "$rc"
    grep -q 'Exception 9' "$out" \
      || fail "stderr missing 'Exception 9' diagnostic"
    ;;

  explain)
    # -explain=2 narration: byte-diff against the pinned golden file.
    #
    # The simulated stack-pointer values depend on the host environment
    # size (env vars consume stack space before argv is placed), so run
    # spim under `env -i` with a fixed minimal environment to match the
    # conditions under which the golden file was captured.  Strip the
    # path-dependent "Loaded: ..." line before diffing.
    env -i "$SPIM" -exception_file "$EF" -explain=2 -f tt.explain.s 2>&1 \
      | grep -v '^Loaded: ' > "$out"
    expected=$(mktemp)
    trap 'rm -f "$out" "$expected"' EXIT
    grep -v '^Loaded: ' tt.explain.expected > "$expected"
    if ! diff -q "$expected" "$out" > /dev/null; then
      fail "explain output differs from golden; regen with: env -i ./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s > tests/tt.explain.expected 2>&1"
    fi
    ;;

  *)
    echo "unknown test name: $NAME" >&2
    exit 2
    ;;
esac

echo "PASS: $NAME"
