#!/usr/bin/env bash
# Proves CFormat.hs (pure Haskell, from the exact binary value) prints the C
# engine's float formats -- %.18g, %.8f, %.17g, %g, %#-13.6g --
# byte-identically to glibc printf: generates ~158K doubles (random bit
# patterns, decimal-boundary values, subnormals, +-0, +-inf, +-nan, floats),
# formats them with C and with Haskell, and compares.  Needs gcc, ghc, python3.
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 "$here/gen.py" > "$work/in.txt"
gcc -O2 -o "$work/cfmt" "$here/cfmt.c"
ghc -O2 -outputdir "$work" -o "$work/hsfmt" "$here/CFormat.hs" > /dev/null
"$work/cfmt" < "$work/in.txt" > "$work/c.out"
"$work/hsfmt" < "$work/in.txt" > "$work/hs.out"
cmp "$work/c.out" "$work/hs.out"
echo "identical: $(wc -l < "$work/c.out") values"
