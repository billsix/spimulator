"""Python port spike: same output as ref.c. Integers are unbounded, so every
32-bit result is masked explicitly; floats are binary64, so single-precision
values are rounded through struct."""

import struct
import sys
import time

M32 = 0xFFFFFFFF


def u32(x: int) -> int:
    return x & M32


def s32(x: int) -> int:
    x &= M32
    return x - (1 << 32) if x & 0x80000000 else x


def s16(x: int) -> int:
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def f32(x: float) -> float:
    """Round a binary64 to binary32 (what every single-precision op needs)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def f2u(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def u2f(u: int) -> float:
    return struct.unpack("<f", struct.pack("<I", u))[0]


def p(name: str, v: int) -> None:
    print(f"{name:<22} 0x{u32(v):08x}")


hi = lo = 0


def div_s(a: int, b: int) -> None:
    global hi, lo
    if b != 0 and not (a == -(2**31) and b == -1):
        q = abs(a) // abs(b)
        if (a < 0) != (b < 0):
            q = -q
        lo, hi = u32(q), u32(a - q * b)  # C truncates toward zero


def run_loop() -> int:
    prog = [0x3C09002D, 0x3529C6C0, 0x24080000, 0x01094021, 0x2529FFFF, 0x1520FFFD, 0x0000000C]
    r = [0] * 32
    pc = 0
    while True:
        w = prog[pc >> 2]
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        npc = pc + 4
        if op == 0x00:
            if w & 63 == 0x21:
                r[rd] = (r[rs] + r[rt]) & M32
            elif w & 63 == 0x0C:
                return r[8]
        elif op == 0x0F:
            r[rt] = (w & 0xFFFF) << 16
        elif op == 0x0D:
            r[rt] = r[rs] | (w & 0xFFFF)
        elif op == 0x09:
            r[rt] = (r[rs] + s16(w)) & M32
        elif op == 0x05:
            if r[rs] != r[rt]:
                npc = pc + 4 + s16(w) * 4
        else:
            return 0xDEADBEEF
        r[0] = 0
        pc = npc


def main() -> None:
    global hi, lo
    p("addu_wrap", 0x7FFFFFFF + 1)
    p("add_overflow_traps", int(not -(2**31) <= 0x7FFFFFFF + 1 <= 2**31 - 1))
    p("sra", s32(0x80000000) >> 4)
    p("srl", 0x80000000 >> 4)
    p("sllv_shift33", 1 << (33 & 0x1F))
    m = -2 * 3
    p("mult_-2x3_hi", m >> 32)
    p("mult_-2x3_lo", m)
    mu = 0xFFFFFFFF * 0xFFFFFFFF
    p("multu_max_hi", mu >> 32)
    p("multu_max_lo", mu)
    hi = lo = 0x11111111
    div_s(-7, 2)
    p("div_-7/2_lo", lo)
    p("div_-7/2_hi", hi)
    hi = lo = 0x11111111
    div_s(5, 0)
    p("div_by_zero_lo", lo)
    hi = lo = 0x11111111
    div_s(-(2**31), -1)
    p("div_min/-1_lo", lo)
    byte = 0x80
    p("lb_0x80", byte - 0x100 if byte & 0x80 else byte)
    p("lbu_0x80", byte)
    p("slt_-1<1", int(s32(0xFFFFFFFF) < 1))
    p("sltu_-1<1", int(0xFFFFFFFF < 1))
    p("mtc1_bits_1.0f", f2u(u2f(0x3F800000)))
    db = struct.unpack("<Q", struct.pack("<d", 1.0))[0]
    p("double_1.0_hi", db >> 32)
    p("double_1.0_lo", db)
    total = f32(f32(0.1) + f32(0.2))
    p("add.s_0.1+0.2_bits", f2u(total))
    p("cvt.w.s_-2.7", int(f32(-2.7)))  # int() truncates toward zero, like C
    p("round.w.s_-2.7_spim", int(f32(-2.7) + 0.5))
    print(f"print_float(0.1f+0.2f) {total:.8f}")
    print(f"print_double(0.1+0.2)  {0.1 + 0.2:.18g}")
    t0 = time.perf_counter()
    r = run_loop()
    secs = time.perf_counter() - t0
    p("loop_sum_1..3000000", r)
    print(f"loop seconds: {secs:.3f}", file=sys.stderr)
    # The traps a naive port falls into (printed for the investigation doc):
    print(f"pitfall: -7 // 2 = {-7 // 2} (C: -3); -7 % 2 = {-7 % 2} (C: -1)", file=sys.stderr)
    print(f"pitfall: 0x7fffffff + 1 unmasked = {0x7FFFFFFF + 1:#x}", file=sys.stderr)
    print(f"pitfall: add.s computed in binary64 prints {0.1 + 0.2:.8f}; "
          f"rounded to binary32 it prints {total:.8f} (spim)", file=sys.stderr)


main()
