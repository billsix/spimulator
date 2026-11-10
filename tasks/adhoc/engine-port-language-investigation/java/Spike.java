// Java port spike: same output as ref.c. int is 32-bit two's complement and
// wraps; there is no unsigned int, so unsigned ops use the Integer/Long helpers.
public final class Spike {
  static int hi, lo;

  static void p(String name, int v) {
    System.out.printf("%-22s 0x%08x%n", name, v);
  }

  static void divS(int a, int b) {
    if (b != 0 && !(a == Integer.MIN_VALUE && b == -1)) { lo = a / b; hi = a % b; }
  }

  static int runLoop() {
    int[] prog = {0x3c09002d, 0x3529c6c0, 0x24080000, 0x01094021, 0x2529ffff, 0x1520fffd, 0x0000000c};
    int[] r = new int[32];
    int pc = 0;
    while (true) {
      int w = prog[pc >>> 2];
      int op = w >>> 26, rs = (w >>> 21) & 31, rt = (w >>> 16) & 31, rd = (w >>> 11) & 31;
      int simm = (short) w;
      int npc = pc + 4;
      switch (op) {
        case 0x00 -> {
          if ((w & 63) == 0x21) r[rd] = r[rs] + r[rt];
          else if ((w & 63) == 0x0c) return r[8];
        }
        case 0x0f -> r[rt] = (w & 0xffff) << 16;
        case 0x0d -> r[rt] = r[rs] | (w & 0xffff);
        case 0x09 -> r[rt] = r[rs] + simm;
        case 0x05 -> { if (r[rs] != r[rt]) npc = pc + 4 + simm * 4; }
        default -> { return 0xdeadbeef; }
      }
      r[0] = 0;
      pc = npc;
    }
  }

  /** C's %.18g; Java's %g never strips trailing zeros, so it cannot be used directly. */
  static String g18(double x) {
    return new java.math.BigDecimal(x).round(new java.math.MathContext(18)).stripTrailingZeros().toPlainString();
  }

  public static void main(String[] args) {
    p("addu_wrap", 0x7fffffff + 1);
    int traps;
    try { Math.addExact(0x7fffffff, 1); traps = 0; } catch (ArithmeticException e) { traps = 1; }
    p("add_overflow_traps", traps);
    p("sra", 0x80000000 >> 4);
    p("srl", 0x80000000 >>> 4);
    p("sllv_shift33", 1 << 33); // Java masks int shift counts to 5 bits, like MIPS
    long m = -2L * 3;
    p("mult_-2x3_hi", (int) (m >> 32));
    p("mult_-2x3_lo", (int) m);
    long mu = Integer.toUnsignedLong(0xffffffff) * Integer.toUnsignedLong(0xffffffff);
    p("multu_max_hi", (int) (mu >>> 32));
    p("multu_max_lo", (int) mu);
    hi = lo = 0x11111111; divS(-7, 2); p("div_-7/2_lo", lo); p("div_-7/2_hi", hi);
    hi = lo = 0x11111111; divS(5, 0); p("div_by_zero_lo", lo);
    hi = lo = 0x11111111; divS(Integer.MIN_VALUE, -1); p("div_min/-1_lo", lo);
    byte b = (byte) 0x80;
    p("lb_0x80", b);
    p("lbu_0x80", b & 0xff);
    p("slt_-1<1", 0xffffffff < 1 ? 1 : 0);
    p("sltu_-1<1", Integer.compareUnsigned(0xffffffff, 1) < 0 ? 1 : 0);
    p("mtc1_bits_1.0f", Float.floatToRawIntBits(Float.intBitsToFloat(0x3f800000)));
    long db = Double.doubleToRawLongBits(1.0);
    p("double_1.0_hi", (int) (db >>> 32));
    p("double_1.0_lo", (int) db);
    float total = 0.1f + 0.2f;
    p("add.s_0.1+0.2_bits", Float.floatToRawIntBits(total));
    p("cvt.w.s_-2.7", (int) -2.7f); // saturates (NaN -> 0) where C is UB
    p("round.w.s_-2.7_spim", (int) ((double) -2.7f + 0.5));
    System.out.printf("print_float(0.1f+0.2f) %.8f%n", (double) total);
    System.out.printf("print_double(0.1+0.2)  %s%n", g18(0.1 + 0.2));
    long t0 = System.nanoTime();
    int r = runLoop();
    double secs = (System.nanoTime() - t0) / 1e9;
    p("loop_sum_1..3000000", r);
    System.err.printf("loop seconds: %.3f%n", secs);
    System.err.printf("pitfall: Java %%.18g of 0.1+0.2 prints %.18g (C: 0.300000000000000044)%n", 0.1 + 0.2);
  }
}
