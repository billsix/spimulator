#!/bin/bash
# The port's atof (cStrtod in src/CFormat.hs) against the C library's, on
# ~60K strings: decimal literals of every length and exponent, signs, white
# space, junk after the number, hex floats, inf/nan, and no number at all.
# It reads the program's read_float/read_double input and the assembler's
# floating-point literals.  Exit 0 when every result has the same bits.
#
#   [CONTAINER] port/haskell/tests/check-strtod.sh
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME=${HOME:-$work}
python3 - > "$work/in.txt" <<'PY'
import random
random.seed(2)
fixed = ["", "-", "+", ".", "e5", "1e", "1e+", "1.5e", " 42", "\t-3.25xyz", "inf", "-INF", "Infinity",
         "nan", "-nan", "NaN(123)", "0x1p3", "0x.8", "0X1.8P-2", "0x", "0xg", "1e400", "-1e400", "1e-400",
         "4.9e-324", "2.4703282292062327e-324", "2.4703282292062328e-324", "1.7976931348623158e308",
         "0.1", "0.30000000000000004", "123456789012345678901234567890", "007", "1.", ".5", "5.e3"]
for s in fixed:
    print(s)
for _ in range(60000):
    digits = "".join(random.choice("0123456789") for _ in range(random.randint(1, 25)))
    k = random.randint(0, len(digits))
    s = digits[:k] + ("." if random.random() < 0.8 else "") + digits[k:]
    if random.random() < 0.6:
        s += random.choice("eE") + random.choice(["", "+", "-"]) + str(random.randint(0, 340))
    if random.random() < 0.3:
        s = "-" + s
    print(s)
PY
gcc -O2 -o "$work/strtod" "$here/strtod.c" || exit 1
ghc -O1 -i"$here/../src" -outputdir "$work" -o "$work/hsstrtod" "$here/StrtodCheck.hs" > /dev/null || exit 1
"$work/strtod" < "$work/in.txt" > "$work/c.out" || exit 1
"$work/hsstrtod" < "$work/in.txt" > "$work/hs.out" || exit 1
if cmp "$work/c.out" "$work/hs.out"; then
  echo "cStrtod: identical to the C library on $(wc -l < "$work/c.out") strings"
else
  paste -d '|' "$work/c.out" "$work/hs.out" "$work/in.txt" | awk -F'|' '$1 != $2' | head -10
  exit 1
fi
