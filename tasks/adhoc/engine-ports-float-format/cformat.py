"""glibc-compatible float formats for the Python port: the % operator, plus a
negative NaN printed as "-nan" (Python prints every NaN as "nan")."""

import math
import struct
import sys


def c_format(spec: str, x: float) -> str:
    """Format x as C's printf(spec, x) would, for one float conversion."""
    text: str = spec % x
    if not (math.isnan(x) and math.copysign(1.0, x) < 0):
        return text
    # Python drops a NaN's sign; glibc prints "-nan". Insert the sign and, in
    # a left-justified field, give back one pad space to keep the width.
    signed: str = text.replace("nan", "-nan", 1)
    return signed[:-1] if len(signed) > len(text) and signed.endswith(" ") else signed


def main() -> None:
    lines: list[str] = []
    for line in sys.stdin:
        x: float = struct.unpack("<d", struct.pack("<Q", int(line, 16)))[0]
        lines.append(
            f"{c_format('%.18g', x)} {c_format('%.8f', x)} {c_format('%.17g', x)}"
            f" {c_format('%g', x)} [{c_format('%#-13.6g', x)}]"
        )
    print("\n".join(lines))


if __name__ == "__main__":
    main()
