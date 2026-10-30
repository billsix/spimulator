// Copyright (c) 2021-2026 William Emerison Six
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/* PURPOSE: Rung 3 of the RPN ladder (tasks/rpn-incremental-versions.md).
 * Introduces the FPU: stack values become DOUBLES with an optional '.'-fraction,
 * and every operator is a floating-point op — so '/' now yields decimals
 * (1 3 / == 0.333333), where rung 2's integer '/' truncated.  This is the one
 * representation-change rung.  Still no error handling — under/overflow, bad
 * input and empty make the final rung, rpn, which this becomes once they are
 * added.
 *
 *     $ printf '3 4 + 2 * 1 3 / -\n' | spimulator -f rpn-3-float.asm
 *     13.666666
 *
 * Values are doubles; the operand stack holds doubles (8 bytes each on the asm
 * side's $sp stack: push = addi $sp,-8 + sdc1, pop = ldc1 + addi $sp,8).
 * Display reuses rpn/calc's fixed deterministic printer so C and asm match
 * byte-for-byte.  '-' is always subtract (no negative literals; '.5' is written
 * '0.5').
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

#define STACK_MAX 64   /* maximum evaluation-stack depth */
#define FRAC_DIGITS 6  /* fractional digits printed for a non-integral result */

/* Print `value` deterministically — identical to rpn/calc's printer. */
static void print_double(double value) {
  if (value != value) {
    print_string("nan");
    return;
  }
  if (value < 0.0) {
    print_char('-');
    value = -value;
  }
  if (value != 0.0 && value + value == value) {
    print_string("inf");
    return;
  }
  int integer_part = (int)value;
  double fraction = value - (double)integer_part;
  print_int(integer_part);
  if (fraction == 0.0) {
    return;
  }
  print_char('.');
  for (int i = 0; i < FRAC_DIGITS; i++) {
    fraction *= 10.0;
    int digit = (int)fraction;
    print_char((char)('0' + digit));
    fraction -= (double)digit;
  }
}

/* True for the four binary operators this calculator understands. */
static int is_operator(int c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  double stack[STACK_MAX];
  int depth = 0;
  int c = read_char();

  while (c != -1) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      c = read_char();
      continue;
    }
    if (c >= '0' && c <= '9') {
      double mantissa = 0.0;
      int fractional_digits = 0;
      while (c >= '0' && c <= '9') {
        mantissa = mantissa * 10.0 + (double)(c - '0');
        c = read_char();
      }
      if (c == '.') {
        c = read_char();
        while (c >= '0' && c <= '9') {
          mantissa = mantissa * 10.0 + (double)(c - '0');
          fractional_digits++;
          c = read_char();
        }
      }
      double value = mantissa;
      for (int i = 0; i < fractional_digits; i++) {
        value = value / 10.0;
      }
      stack[depth++] = value;
      continue; /* c already holds the next unconsumed character */
    }
    if (is_operator(c)) {
      double right = stack[--depth];
      double left = stack[--depth];
      double result;
      if (c == '+') {
        result = left + right;
      } else if (c == '-') {
        result = left - right;
      } else if (c == '*') {
        result = left * right;
      } else {
        result = left / right; /* div by zero -> inf/nan, shown */
      }
      stack[depth++] = result;
      c = read_char();
      continue;
    }
    c = read_char(); /* well-formed input assumed */
  }

  print_double(stack[depth - 1]); /* top of stack */
  print_char('\n');
  return 0;
}
