#!/usr/bin/env bash
# Regenerate the integer-conversion catalog for tasks/integer-conversion-hardening.md.
# READ-ONLY analysis: builds the spimulator target under clang with
# -Wconversion -Wsign-conversion, captures the diagnostics, runs analyze.py -> data/.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(git -C "$here" rev-parse --show-toplevel)"
b="$(mktemp -d)"
CC=clang CFLAGS='-Wconversion -Wsign-conversion' meson setup "$b" "$root" \
    --buildtype=debug -Dline_editing=disabled -Dcore_werror=false >/dev/null
meson compile -C "$b" spimulator >"$b/raw.log" 2>&1 || true
ver="$(clang --version | sed -n '1s/.*version \([0-9.]*\).*/\1/p')"
python3 "$here/analyze.py" "$b/raw.log" "$ver"
rm -rf "$b"
