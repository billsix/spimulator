"""Machine arithmetic: every 32-bit and IEEE-single conversion in one place.

C has fixed-width integers; Python's ``int`` never overflows.  The C engine
relies on 32-bit wraparound (``addu``), on reading one register as signed or
unsigned (``slt`` vs ``sltu``), on truncating division, and on the host's
``float`` and ``double``.  This module spells each of those C behaviors out
as a small function, so the rest of the port can say which one it means.

The convention used everywhere in the port: **a register or memory word is
held as a signed 32-bit value** (``-2**31 .. 2**31-1``), the same as the C
engine's ``reg_word`` (``int32_t``).  Use :func:`u32` where C casts to
``uint32_t`` / ``mem_addr``.

Floating point: Python's ``float`` is an IEEE double, the same as C's
``double``.  A C ``float`` is emulated by rounding through 4 bytes
(:func:`f32`).  For +, -, *, / and sqrt this gives exactly the single
precision result, because a double holds more than twice the bits of a
float, so rounding twice cannot differ from rounding once.

Python raises exceptions where C (on x86) quietly produces a special value:
``1.0 / 0.0``, ``math.sqrt(-1.0)``, ``int(nan)``, packing a too-large
double into 4 bytes.  The helpers below return what the x86 hardware
returns instead.
"""

import math
import struct

M32: int = 0xFFFFFFFF
"""Mask for the low 32 bits."""

_WORD: struct.Struct = struct.Struct("<i")
_FLOAT: struct.Struct = struct.Struct("<f")
_DOUBLE: struct.Struct = struct.Struct("<d")
_TWO_WORDS: struct.Struct = struct.Struct("<ii")

# The NaN x86 produces for an invalid operation (0/0, inf-inf, sqrt(-1)):
# sign bit set, quiet bit set.  C prints it as "-nan".
DEFAULT_NAN: float = _DOUBLE.unpack(struct.pack("<Q", 0xFFF8000000000000))[0]


def u32(x: int) -> int:
    """The unsigned 32-bit value of x, as C's ``(uint32_t)x``.

    Example:
        >>> u32(-1) == 0xffffffff
        True
        >>> u32(0x1_0000_0005)
        5
    """
    return x & M32


def s32(x: int) -> int:
    """The signed 32-bit value of x, as C's ``(int32_t)x``.

    Example:
        >>> s32(0xffffffff)
        -1
        >>> s32(0x7fffffff + 1)
        -2147483648
    """
    x &= M32
    return x - 0x1_0000_0000 if x & 0x8000_0000 else x


def s16(x: int) -> int:
    """The signed value of the low 16 bits, as C's ``(int16_t)x``.

    Example:
        >>> s16(0xffff), s16(0x7fff), s16(0x18000)
        (-1, 32767, -32768)
    """
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def s8(x: int) -> int:
    """The signed value of the low 8 bits, as C's ``(int8_t)x``.

    Example:
        >>> s8(0xff), s8(0x7f)
        (-1, 127)
    """
    x &= 0xFF
    return x - 0x100 if x & 0x80 else x


def u8(x: int) -> int:
    """The low 8 bits, as C's ``(uint8_t)x``.

    Example:
        >>> u8(-1)
        255
    """
    return x & 0xFF


def add_overflows(a: int, b: int) -> bool:
    """True if the signed 32-bit sum a + b does not fit, as C's ``ckd_add``.

    Python integers do not overflow, so compare the exact sum with the range.

    Example:
        >>> add_overflows(0x7fffffff, 1), add_overflows(-1, 1)
        (True, False)
    """
    return not -0x8000_0000 <= a + b <= 0x7FFF_FFFF


def sub_overflows(a: int, b: int) -> bool:
    """True if the signed 32-bit difference a - b does not fit (``ckd_sub``).

    Example:
        >>> sub_overflows(-0x80000000, 1), sub_overflows(0, 1)
        (True, False)
    """
    return not -0x8000_0000 <= a - b <= 0x7FFF_FFFF


def c_div(a: int, b: int) -> int:
    """Integer division truncating toward zero, as C's ``a / b``.

    Python's ``//`` rounds toward minus infinity: ``-7 // 2 == -4``, while
    C gives ``-3``.

    Example:
        >>> c_div(-7, 2), -7 // 2
        (-3, -4)
    """
    q: int = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def c_rem(a: int, b: int) -> int:
    """The remainder that goes with :func:`c_div`, as C's ``a % b``.

    Its sign follows the dividend: ``-7 % 2`` is ``-1`` in C, ``1`` in Python.

    Example:
        >>> c_rem(-7, 2), -7 % 2
        (-1, 1)
    """
    return a - c_div(a, b) * b


def leading_zeros(x: int) -> int:
    """How many 0 bits lead the 32-bit word x (C23 ``stdc_leading_zeros``).

    Example:
        >>> leading_zeros(1), leading_zeros(0), leading_zeros(-1)
        (31, 32, 0)
    """
    return 32 - u32(x).bit_length()


def leading_ones(x: int) -> int:
    """How many 1 bits lead the 32-bit word x (``stdc_leading_ones``).

    Example:
        >>> leading_ones(-1), leading_ones(0x80000000), leading_ones(0)
        (32, 1, 0)
    """
    return leading_zeros(~x)


# --- floating point --------------------------------------------------------


def float_bits(x: float) -> int:
    """The bit pattern of x rounded to a C ``float``, as a signed word.

    Example:
        >>> hex(u32(float_bits(1.0)))
        '0x3f800000'
        >>> hex(u32(float_bits(1e300)))
        '0x7f800000'
    """
    try:
        return _WORD.unpack(_FLOAT.pack(x))[0]
    except OverflowError:
        # struct refuses a double whose float rounding is infinite; C's
        # (float) cast gives that infinity.
        return _WORD.unpack(_FLOAT.pack(math.copysign(math.inf, x)))[0]


def bits_float(word: int) -> float:
    """The C ``float`` whose bit pattern is word, widened to a double.

    Example:
        >>> bits_float(0x3f800000)
        1.0
    """
    return _FLOAT.unpack(_WORD.pack(s32(word)))[0]


def f32(x: float) -> float:
    """x rounded to single precision, as C's ``(float)x``.

    Example:
        >>> f32(0.1) == 0.1
        False
        >>> f32(0.1) + f32(0.2) == 0.30000000447034836
        True
    """
    return bits_float(float_bits(x))


def double_words(x: float) -> tuple[int, int]:
    """The two signed words of a double in little-endian memory order.

    Word 0 holds the low 32 bits of the IEEE pattern, word 1 the sign,
    exponent and high fraction (as on the little-endian hosts the C engine
    supports).

    Example:
        >>> [hex(u32(w)) for w in double_words(1.0)]
        ['0x0', '0x3ff00000']
    """
    low: int
    high: int
    low, high = _TWO_WORDS.unpack(_DOUBLE.pack(x))
    return low, high


def words_double(low: int, high: int) -> float:
    """The double whose little-endian words are low and high.

    Example:
        >>> words_double(0, 0x3ff00000)
        1.0
    """
    return _DOUBLE.unpack(_TWO_WORDS.pack(s32(low), s32(high)))[0]


def f_div(a: float, b: float) -> float:
    """IEEE division a / b, as the hardware does it (no ZeroDivisionError).

    Example:
        >>> f_div(1.0, 0.0), f_div(-1.0, 0.0), f_div(1.0, -0.0)
        (inf, -inf, -inf)
        >>> math.isnan(f_div(0.0, 0.0))
        True
    """
    if b != 0.0:
        return a / b
    elif math.isnan(a):
        return a
    elif a == 0.0:
        return DEFAULT_NAN
    else:
        return math.copysign(math.inf, a) * math.copysign(1.0, b)


def f_sqrt(x: float) -> float:
    """IEEE square root (a NaN, not ValueError, for a negative argument).

    Example:
        >>> f_sqrt(4.0)
        2.0
        >>> math.isnan(f_sqrt(-1.0))
        True
    """
    if math.isnan(x):
        return x
    elif x < 0.0:
        return DEFAULT_NAN
    else:
        return math.sqrt(x)


def to_s32(x: float) -> int:
    """C's ``(int32_t)x`` for a double x on x86: truncate toward zero.

    C leaves an out-of-range or NaN conversion undefined; x86's
    ``cvttsd2si`` returns 0x80000000 (``INT_MIN``) for it, and the C engine
    inherits that, so this does too.

    Example:
        >>> to_s32(-2.7), to_s32(3.9)
        (-2, 3)
        >>> to_s32(math.nan), to_s32(1e10)
        (-2147483648, -2147483648)
    """
    if math.isnan(x) or math.isinf(x):
        return -0x8000_0000
    t: int = int(x)
    return t if -0x8000_0000 <= t <= 0x7FFF_FFFF else -0x8000_0000


def c_atof(text: bytes) -> float:
    """C's ``atof``: the longest prefix of text that is a number, else 0.0.

    Leading white space is skipped; ``inf``/``nan`` spellings and hex floats
    are accepted as by ``strtod``.

    Example:
        >>> c_atof(b"  3.5abc"), c_atof(b"x"), c_atof(b"-2e3\\n")
        (3.5, 0.0, -2000.0)
    """
    s: str = text.decode("latin-1").lstrip(" \t\n\v\f\r")
    for end in range(len(s), 0, -1):
        candidate: str = s[:end]
        if "_" in candidate or candidate != candidate.strip():
            continue  # float() allows these; strtod stops before them
        try:
            if candidate.lower().lstrip("+-").startswith("0x"):
                return float.fromhex(candidate)
            elif candidate.lower().lstrip("+-").startswith(("in", "na")):
                return _special_float(candidate)
            else:
                return float(candidate)
        except ValueError:
            continue
    return 0.0


def _special_float(text: str) -> float:
    """``strtod``'s infinity and NaN spellings (``inf``, ``infinity``, ``nan``).

    Raises:
        ValueError: text is not one of them.
    """
    body: str = text.lower().lstrip("+-")
    sign: float = -1.0 if text.startswith("-") else 1.0
    if body in ("inf", "infinity"):
        return sign * math.inf
    elif body == "nan" or (body.startswith("nan(") and body.endswith(")")):
        return math.copysign(math.nan, sign)
    else:
        raise ValueError(text)
