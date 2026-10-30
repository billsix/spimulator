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

/* PURPOSE: Rung 6 of the calculator ladder (tasks/calc-incremental-versions.md).
 * Introduces the FPU: numbers become DOUBLES with an optional '.'-fraction, and
 * every operator becomes a floating-point operation — so '/' now yields decimals
 * (10/4 == 2.5), where rung 3's integer '/' truncated.  This is the one
 * representation-change rung.  Still no spaces, no error handling — those make
 * the final rung, calc-sdt, which this becomes once they are added.
 *
 *     $ printf '10/4\n' | spimulator -f calc-6-float.asm
 *     2.500000
 *
 *     expr   := term   (('+' | '-') term)*
 *     term   := factor (('*' | '/') factor)*
 *     factor := NUMBER | '-' factor | '(' expr ')'
 *     NUMBER := digits with an optional single '.' and fraction
 *
 * Values are doubles (like a TI-83).  Display reuses rpn/calc-sdt's fixed
 * deterministic printer (integral -> integer; else integer part, '.', 6
 * truncated digits; nan/inf named), so C and asm match byte-for-byte.  Reading
 * is byte-at-a-time via read_char() (syscall 12), mirrored in calc-6-float.asm.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

/* Fractional digits printed for a non-integral result. */
#define FRAC_DIGITS 6

static int cur; /* current lookahead character (-1 at EOF) */

static void advance(void) { cur = read_char(); }

/* Print `value` deterministically — identical to rpn/calc-sdt's printer, so all
 * three agree byte-for-byte. */
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

static double parse_expr(void); /* forward decl for the '(' expr ')' rule */

/* factor := NUMBER | '-' factor | '(' expr ')' */
static double parse_factor(void) {
  if (cur == '-') {
    advance();
    return -parse_factor();
  }
  if (cur == '(') {
    advance();
    double value = parse_expr();
    if (cur == ')') {
      advance();
    }
    return value;
  }
  double mantissa = 0.0;
  int fractional_digits = 0;
  while (cur >= '0' && cur <= '9') {
    mantissa = mantissa * 10.0 + (double)(cur - '0');
    advance();
  }
  if (cur == '.') {
    advance();
    while (cur >= '0' && cur <= '9') {
      mantissa = mantissa * 10.0 + (double)(cur - '0');
      fractional_digits++;
      advance();
    }
  }
  double value = mantissa;
  for (int i = 0; i < fractional_digits; i++) {
    value = value / 10.0;
  }
  return value;
}

/* term := factor (('*' | '/') factor)* */
static double parse_term(void) {
  double value = parse_factor();
  while (true) {
    if (cur == '*') {
      advance();
      value = value * parse_factor();
    } else if (cur == '/') {
      advance();
      value = value / parse_factor();
    } else {
      return value;
    }
  }
}

/* expr := term (('+' | '-') term)* */
static double parse_expr(void) {
  double value = parse_term();
  while (true) {
    if (cur == '+') {
      advance();
      value = value + parse_term();
    } else if (cur == '-') {
      advance();
      value = value - parse_term();
    } else {
      return value;
    }
  }
}

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  double value;

  advance(); /* prime the first lookahead character */
  while (cur != -1) {
    if (cur == '\n') { /* blank line */
      advance();
      continue;
    }
    value = parse_expr();
    print_double(value);
    print_char('\n');
    if (cur == '\n') {
      advance();
    }
  }
  return 0;
}
