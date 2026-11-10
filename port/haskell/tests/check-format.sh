#!/bin/bash
# The port's float formatter (src/CFormat.hs) against glibc printf, on
# ~158K doubles (gen.py: random bit patterns, values near every decimal
# exponent, subnormals, +-0, +-inf, +-nan, eighths, single-precision
# values), in the five formats the C engine prints.  Exit 0 when every line
# is byte-identical.
#
#   [CONTAINER] port/haskell/tests/check-format.sh
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME=${HOME:-$work}
python3 "$here/gen.py" > "$work/in.txt" || exit 1
gcc -O2 -o "$work/cfmt" "$here/cfmt.c" || exit 1
ghc -O1 -i"$here/../src" -outputdir "$work" -o "$work/hsfmt" "$here/FormatCheck.hs" > /dev/null || exit 1
"$work/cfmt" < "$work/in.txt" > "$work/c.out" || exit 1
"$work/hsfmt" < "$work/in.txt" > "$work/hs.out" || exit 1
if cmp "$work/c.out" "$work/hs.out"; then
  echo "CFormat: identical to glibc on $(wc -l < "$work/c.out") values"
else
  diff "$work/c.out" "$work/hs.out" | head -10
  exit 1
fi
