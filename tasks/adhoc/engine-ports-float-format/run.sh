#!/usr/bin/env bash
# Proves that pure-language formatters print the C engine's float formats --
# %.18g, %.8f, %.17g, %g, %#-13.6g -- byte-identically to glibc printf:
#   CFormat.hs  GHC Haskell, rounding the exact Rational value of each double;
#   cformat.rs  Rust, on std's exactly rounded {:.N} / {:.Ne} plus C's %g rules;
#   cformat.py  Python, the % operator plus a "-nan" fix.
# gen.py emits ~158K doubles (random bit patterns, decimal-boundary values,
# subnormals, +-0, +-inf, +-nan, floats); each formatter's output is compared
# with C's.  Needs gcc and python3; ghc and rustc are each run if present.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 "$here/gen.py" > "$work/in.txt" || exit 1
gcc -O2 -o "$work/cfmt" "$here/cfmt.c" || exit 1
"$work/cfmt" < "$work/in.txt" > "$work/c.out" || exit 1
status=0
if command -v ghc > /dev/null; then
    ghc -O2 -outputdir "$work" -o "$work/hsfmt" "$here/CFormat.hs" > /dev/null &&
        "$work/hsfmt" < "$work/in.txt" > "$work/hs.out" &&
        cmp "$work/c.out" "$work/hs.out" &&
        echo "haskell: identical on $(wc -l < "$work/c.out") values" || status=1
fi
if command -v rustc > /dev/null; then
    rustc -O -o "$work/rsfmt" "$here/cformat.rs" &&
        "$work/rsfmt" < "$work/in.txt" > "$work/rs.out" &&
        cmp "$work/c.out" "$work/rs.out" &&
        echo "rust: identical on $(wc -l < "$work/c.out") values" || status=1
fi
python3 "$here/cformat.py" < "$work/in.txt" > "$work/py.out" &&
    cmp "$work/c.out" "$work/py.out" &&
    echo "python: identical on $(wc -l < "$work/c.out") values" || status=1
exit $status
