#!/usr/bin/env bash
# Build each spike, diff its stdout against the C reference, print timings.
# Run from anywhere; writes build products under a temp dir.
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
gcc -O2 -std=gnu23 -o "$tmp/ref" "$here/ref.c" || exit 1
"$tmp/ref" > "$tmp/ref.out" 2> "$tmp/ref.err"
echo "C:      $(cat "$tmp/ref.err")"
check() {  # name, then the command
  local name=$1; shift
  if "$@" > "$tmp/$name.out" 2> "$tmp/$name.err" && diff -q "$tmp/ref.out" "$tmp/$name.out" > /dev/null; then
    echo "$name: identical; $(head -1 "$tmp/$name.err")"
  else
    echo "$name: DIFFERS"; diff "$tmp/ref.out" "$tmp/$name.out"; status=1
  fi
}
check python python3 "$here/spike.py"
cargo build --release -q --manifest-path "$here/rust/Cargo.toml" --target-dir "$tmp/rust" 2> /dev/null || status=1
check rust "$tmp/rust/release/spike"
javac -d "$tmp/java" "$here/java/Spike.java" || status=1
check java java -cp "$tmp/java" Spike
exit $status
