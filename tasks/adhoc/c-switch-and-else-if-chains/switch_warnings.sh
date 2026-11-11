#!/usr/bin/env bash
# Record clang's -Wswitch-default and -Wswitch-enum diagnostics for every src/*.c file.
# Usage: tasks/adhoc/c-switch-and-else-if-chains/switch_warnings.sh BUILD_DIR
# BUILD_DIR is a configured clang meson build; output goes to data/switch_warnings.txt.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(git -C "$here" rev-parse --show-toplevel)"
build="$(cd "$1" && pwd)"
flags=(-I"$build/spimulator.p" -I"$build" -I"$root" -I"$root/include" -I/usr/include/editline
  -D_FILE_OFFSET_BITS=64 -DHAVE_LIBEDIT=1 -std=gnu23 -fsyntax-only
  -Wno-everything -Wswitch-default -Wswitch-enum)
mkdir -p "$here/data"
for f in "$root"/src/*.c; do
  clang "${flags[@]}" "$f" 2>&1
done | grep -E 'warning:' | sed "s|$root/||" > "$here/data/switch_warnings.txt"
grep -oE '\[-W[a-z-]+\]' "$here/data/switch_warnings.txt" | sort | uniq -c
