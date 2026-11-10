/* C reference for the port spikes: same semantics as src/run.c. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint32_t hi, lo;

static void p(const char* name, uint32_t v) { printf("%-22s 0x%08x\n", name, v); }

static void div_s(int32_t a, int32_t b) {
  if (b != 0 && !(a == INT32_MIN && b == -1)) { lo = (uint32_t)(a / b); hi = (uint32_t)(a % b); }
}

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static uint32_t run_loop(void) {
  static const uint32_t prog[] = {0x3c09002d, 0x3529c6c0, 0x24080000, 0x01094021,
                                  0x2529ffff, 0x1520fffd, 0x0000000c};
  uint32_t r[32] = {0}, pc = 0;
  for (;;) {
    uint32_t w = prog[pc / 4];
    uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31;
    int32_t simm = (int16_t)(w & 0xffff);
    uint32_t uimm = w & 0xffff, npc = pc + 4;
    switch (op) {
      case 0x00:
        if ((w & 63) == 0x21) r[rd] = r[rs] + r[rt];
        else if ((w & 63) == 0x0c) return r[8];
        break;
      case 0x0f: r[rt] = uimm << 16; break;
      case 0x0d: r[rt] = r[rs] | uimm; break;
      case 0x09: r[rt] = r[rs] + (uint32_t)simm; break;
      case 0x05: if (r[rs] != r[rt]) npc = pc + 4 + (uint32_t)(simm * 4); break;
      default: return 0xdeadbeef;
    }
    r[0] = 0;
    pc = npc;
  }
}

int main(void) {
  p("addu_wrap", 0x7fffffffu + 1u);
  int32_t s;
  p("add_overflow_traps", __builtin_add_overflow((int32_t)0x7fffffff, 1, &s));
  p("sra", (uint32_t)((int32_t)0x80000000 >> 4));
  p("srl", 0x80000000u >> 4);
  p("sllv_shift33", 1u << (33 & 0x1f));
  int64_t m = (int64_t)-2 * 3; hi = (uint32_t)((uint64_t)m >> 32); lo = (uint32_t)m;
  p("mult_-2x3_hi", hi); p("mult_-2x3_lo", lo);
  uint64_t mu = (uint64_t)0xffffffffu * 0xffffffffu; hi = (uint32_t)(mu >> 32); lo = (uint32_t)mu;
  p("multu_max_hi", hi); p("multu_max_lo", lo);
  hi = lo = 0x11111111; div_s(-7, 2); p("div_-7/2_lo", lo); p("div_-7/2_hi", hi);
  hi = lo = 0x11111111; div_s(5, 0); p("div_by_zero_lo", lo);
  hi = lo = 0x11111111; div_s(INT32_MIN, -1); p("div_min/-1_lo", lo);
  uint8_t byte = 0x80;
  p("lb_0x80", (uint32_t)(int32_t)(int8_t)byte); p("lbu_0x80", byte);
  p("slt_-1<1", (int32_t)0xffffffff < 1); p("sltu_-1<1", 0xffffffffu < 1u);
  p("mtc1_bits_1.0f", f2u(u2f(0x3f800000)));
  double d = 1.0; uint64_t db; memcpy(&db, &d, 8);
  p("double_1.0_hi", (uint32_t)(db >> 32)); p("double_1.0_lo", (uint32_t)db);
  float a = 0.1f, b = 0.2f; float sum = a + b;
  p("add.s_0.1+0.2_bits", f2u(sum));
  p("cvt.w.s_-2.7", (uint32_t)(int32_t)-2.7f);
  p("round.w.s_-2.7_spim", (uint32_t)(int32_t)((double)-2.7f + 0.5));
  printf("print_float(0.1f+0.2f) %.8f\n", (double)sum);
  printf("print_double(0.1+0.2)  %.18g\n", 0.1 + 0.2);
  clock_t t0 = clock();
  uint32_t r = run_loop();
  double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
  p("loop_sum_1..3000000", r);
  fprintf(stderr, "loop seconds: %.3f\n", secs);
  return 0;
}
