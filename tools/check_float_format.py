#!/usr/bin/env python3
"""Check the Python port's float formatter against glibc's printf.

The C engine prints doubles in five formats (``%.18g``, ``%.8f``, ``%.17g``, ``%g`` and
``%#-13.6g``), and each port must print them byte for byte the same. This compiles
``tools/float_format_oracle.c`` (glibc's printf on hex bit patterns), runs it over about
158,000 doubles, and formats the same values with ``port/python/spimulator_py/cformat.py``.
The values are random bit patterns, values near every decimal exponent from 1e-30 to 1e29,
subnormals, +-0, +-inf, +-nan, eighths and single-precision values, seeded so every run
checks the same ones. The Haskell port's formatter has its own check
(``port/haskell/tests/check-format.sh``), and the Rust port's runs under ``cargo test``.

Usage, from anywhere::

    python3 tools/check_float_format.py

Exits 0 when every line is identical; otherwise prints the first differences and exits 1.
"""

import pathlib
import random
import struct
import subprocess
import sys
import tempfile

REPO_ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parents[1]
ORACLE: pathlib.Path = REPO_ROOT / "tools" / "float_format_oracle.c"
FORMATS: tuple[str, ...] = ("%.18g", "%.8f", "%.17g", "%g", "%#-13.6g")

# Values where formatting is easy to get wrong, beside the random ones.
SPECIAL: tuple[float, ...] = (
    0.0, -0.0, 0.1 + 0.2, 1 / 3, 1.0, 1e-5, 1e-4, 0.0001234, 123456789012345678.0, 1e17,
    1e18, 1e19, 5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, float("inf"),
    float("-inf"), float("nan"), -float("nan"), 0.5, 2.5, 0.125, 0.000000005, 0.000000015,
    0.000000025, 999999999999999999.0, 0.99999999999999999, 9.5, 1e-310, 3.0e10, -7.25,
)  # fmt: skip


def bits_of(value: float) -> int:
    """The 64-bit pattern of a double.

    Args:
        value: The double.

    Returns:
        Its IEEE 754 bits as an unsigned integer.
    """
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def value_of(bits: int) -> float:
    """The double with a 64-bit pattern.

    Args:
        bits: IEEE 754 bits as an unsigned integer.

    Returns:
        The double.
    """
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def test_values() -> list[int]:
    """The bit patterns to check, the same on every run.

    Returns:
        About 158,000 64-bit patterns.
    """
    rng: random.Random = random.Random(1)
    near_exponents: list[int] = [
        bits_of(rng.uniform(0.5, 10) * 10.0**exponent)
        for exponent in range(-30, 30)
        for _ in range(300)
    ]
    # Eighths, then a single-precision value, alternating, as port/haskell/tests/gen.py does.
    small_and_single: list[int] = [
        bits
        for i in range(1, 20000)
        for bits in (
            bits_of(i / 8),
            bits_of(struct.unpack("<f", struct.pack("<f", rng.uniform(-1e6, 1e6)))[0]),
        )
    ]
    random_patterns: list[int] = [rng.getrandbits(64) for _ in range(100000)]
    return (
        [bits_of(v) for v in SPECIAL]
        + random_patterns
        + near_exponents
        + small_and_single
    )


def python_lines(patterns: list[int]) -> list[str]:
    """Format each value with the Python port, in the oracle's line layout.

    Args:
        patterns: The bit patterns.

    Returns:
        One line per value: the five formats, the last one in brackets.
    """
    sys.path.insert(0, str(REPO_ROOT / "port" / "python"))
    # Imported here, after the path insert: the port is run from its tree, not installed.
    from spimulator_py.cformat import c_format

    return [
        " ".join(c_format(spec, value_of(bits)) for spec in FORMATS[:-1])
        + f" [{c_format(FORMATS[-1], value_of(bits))}]"
        for bits in patterns
    ]


def oracle_lines(patterns: list[int]) -> list[str]:
    """Format each value with glibc's printf.

    Args:
        patterns: The bit patterns.

    Returns:
        One line per value, as the oracle prints it.
    """
    with tempfile.TemporaryDirectory() as work:
        binary: pathlib.Path = pathlib.Path(work) / "oracle"
        subprocess.run(["cc", "-O2", "-o", str(binary), str(ORACLE)], check=True)
        done: subprocess.CompletedProcess[str] = subprocess.run(
            [str(binary)],
            input="\n".join(f"{bits:x}" for bits in patterns) + "\n",
            capture_output=True,
            text=True,
            check=True,
        )
    return done.stdout.splitlines()


def main() -> int:
    """Compare the Python port's formatting with glibc's.

    Returns:
        0 when every line matches, 1 otherwise.
    """
    patterns: list[int] = test_values()
    want: list[str] = oracle_lines(patterns)
    got: list[str] = python_lines(patterns)
    differences: list[tuple[int, str, str]] = [
        (bits, c_line, py_line)
        for bits, c_line, py_line in zip(patterns, want, got, strict=True)
        if c_line != py_line
    ]
    if differences:
        for bits, c_line, py_line in differences[:10]:
            print(f"{bits:016x}\n  glibc:  {c_line}\n  python: {py_line}")
        print(f"{len(differences)} of {len(patterns)} values differ")
        return 1
    else:
        print(f"cformat.py: identical to glibc on {len(patterns)} values")
        return 0


if __name__ == "__main__":
    sys.exit(main())
