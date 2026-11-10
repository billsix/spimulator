# Sourced by tests/run-test.sh and tests/gen-parity-golden.sh: dump the
# assembled text and data segments of a program, the "AST parity" format.
#
# "AST parity": the assembler parses a program into a tree (the AST) and then
# emits machine code from it.  When the old parser was replaced (2026-08-03),
# its machine code for a set of programs was saved as goldens; the parity
# tests check that today's assembler emits the same bytes.
#
# Needs SPIM, EF (the exception file) and TESTS_DIR set by the caller.

# parity_flags FLAGS -> the spimulator flags for a parity-programs.txt entry
parity_flags() {
  if [ "$1" = default ]; then echo "-exception_file $EF"; else echo "$1"; fi
}

# capture PROG FLAGS -> print PROG's text and data dumps under ### headers,
# with the `; NNN: source` annotations stripped (they follow scanner state,
# not the bytes).  Returns 1, printing nothing, if either dump is missing.
capture() {
  local prog=$1 flags d seg
  flags=$(parity_flags "$2")
  d=$(mktemp -d)
  # shellcheck disable=SC2086  # flags is a word list on purpose
  ( cd "$d" && "$SPIM" $flags -dump -f "$TESTS_DIR/$prog" \
      </dev/null >/dev/null 2>&1 ) || true
  if [ ! -f "$d/text.asm" ] || [ ! -f "$d/data.asm" ]; then
    rm -rf "$d"
    return 1
  fi
  for seg in text data; do
    echo "### $prog :: $seg ###"
    sed 's/[[:space:]]*;.*$//' "$d/$seg.asm"
  done
  rm -rf "$d"
}

# capture_all -> capture every program in parity-programs.txt; returns 1 (and
# names the program on stderr) if any wrote no dump.
capture_all() {
  local prog flags status=0
  while read -r prog flags; do
    case "$prog" in ''|'#'*) continue ;; esac
    capture "$prog" "$flags" || {
      echo "parity: $prog wrote no dump" >&2
      status=1
    }
  done < "$TESTS_DIR/parity-programs.txt"
  return $status
}
