#!/usr/bin/env bash
# Compare two spim binaries' output on every assembly file in the repo: the "before" oracle for a
# refactor that must not change behavior.
#
# Usage: tasks/adhoc/c-switch-and-else-if-chains/compare_engines.sh BEFORE_SPIM AFTER_SPIM
#
# For each .s/.asm under tests/, examples/src/ and pgu/src/ (plus helloworld.s), it runs both
# binaries four ways and compares stdout, stderr and exit status:
#   disasm   load the file in the REPL and print the text segments;
#   ast      -print-ast-json;
#   listing  -listing - (the assemble-time event trace);
#   explain  run it at -explain=4 with stdin from /dev/null (output capped at 2 MB).
# Then one fixed REPL session exercises the print and args commands. It prints each difference
# and exits 1 if there was any; a case the before binary cannot reproduce itself is skipped.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(git -C "$here" rev-parse --show-toplevel)"
before="$1"
after="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
cases=0
skipped=0

# run NAME SPIM DIR ARGS... : run SPIM with ARGS (stdin from $work/stdin) and record its result.
run() {
  local name="$1" spim="$2" dir="$3"
  shift 3
  (cd "$dir" && timeout 20 "$spim" "$@" <"$work/stdin" 2>"$work/$name.err" | head -c 2000000 >"$work/$name.out"
   echo "${PIPESTATUS[0]}" >"$work/$name.rc")
}

# same A B : whether runs A and B gave identical stdout, stderr and exit status.
same() {
  cmp -s "$work/$1.out" "$work/$2.out" && cmp -s "$work/$1.err" "$work/$2.err" &&
    cmp -s "$work/$1.rc" "$work/$2.rc"
}

# compare LABEL DIR ARGS... : run both binaries with ARGS and report any difference. The "before"
# binary runs twice; a case where it disagrees with itself (a program that reads the clock, or
# that the timeout cuts off at a different point) is reported as skipped, not compared.
compare() {
  local label="$1" dir="$2"
  shift 2
  run before "$before" "$dir" "$@"
  run again "$before" "$dir" "$@"
  if ! same before again; then
    echo "SKIPPED (the before binary is not deterministic here): $label"
    skipped=$((skipped + 1))
    return
  fi
  run after "$after" "$dir" "$@"
  cases=$((cases + 1))
  for part in out err rc; do
    if ! cmp -s "$work/before.$part" "$work/after.$part"; then
      echo "DIFF ($part): $label"
      diff "$work/before.$part" "$work/after.$part" | head -10
      status=1
    fi
  done
}

mapfile -t files < <(cd "$root" && git ls-files 'tests/*.s' 'examples/src/*.s' 'examples/src/*.asm' \
  'pgu/src/*.s' 'helloworld.s')
for file in "${files[@]}"; do
  dir="$root/$(dirname "$file")"
  base="$(basename "$file")"
  printf 'load "%s"\ndisasm\nexit\n' "$base" >"$work/stdin"
  compare "disasm $file" "$dir" -noexception
  : >"$work/stdin"
  compare "ast $file" "$dir" -print-ast-json -file "$base"
  compare "listing $file" "$dir" -listing - -file "$base"
  compare "explain $file" "$dir" -explain=4 -file "$base"
done

# The print and args REPL commands, every token kind each one dispatches on.
cat >"$work/stdin" <<'EOF'
load "helloworld.s"
print $t0
print
print $f2
print
print 0x400000
print
print 4($sp)
print main
print pc
print hi
print nosuchlabel
print "x"
args one 2 "three" , four
step 3
print $v0
exit
EOF
compare "repl print/args" "$root"

echo "$cases comparisons, $skipped skipped, status $status"
exit "$status"
