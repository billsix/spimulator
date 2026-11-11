"""C's printf float conversions, byte for byte as glibc prints them.

Python's ``%`` operator already formats ``%.8f``, ``%.18g``, ``%.17g``,
``%g`` and ``%#-13.6g`` exactly as glibc does (both format the exact binary
value with correct rounding), with one exception: Python prints every NaN as
``nan``, while glibc prints a NaN whose sign bit is set as ``-nan``.  On x86,
``0.0 / 0.0`` produces exactly such a NaN, so a MIPS program that divides
zero by zero prints ``-nan`` under the C engine.

Checked against glibc's printf on 158,029 doubles in all five formats
(`tools/check_float_format.py`, the meson test `python-float-format`).
"""

import math


def c_format(spec: str, x: float) -> str:
    """Format x as C's ``printf(spec, x)`` would, for one float conversion.

    Args:
        spec: a printf conversion such as ``"%.8f"`` or ``"%#-13.6g"``.
        x: the value.

    Returns:
        str: the text glibc's printf produces.

    Example:
        >>> c_format("%.18g", 0.1 + 0.2)
        '0.300000000000000044'
        >>> c_format("%.8f", -math.nan)
        '-nan'
        >>> c_format("%#-13.6g", -math.nan) + "|"
        '-nan         |'
    """
    text: str = spec % x
    if not (math.isnan(x) and math.copysign(1.0, x) < 0):
        return text
    # Python drops a NaN's sign; glibc prints "-nan".  Insert the sign and, in
    # a left-justified field, give back one pad space to keep the width.
    signed: str = text.replace("nan", "-nan", 1)
    return signed[:-1] if len(signed) > len(text) and signed.endswith(" ") else signed
