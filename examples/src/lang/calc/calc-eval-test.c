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

/* PURPOSE: Unit tests for the I/O-separated calculator core
 * (tasks/examples-separate-io-from-algorithm.md).  Because calc-eval's parser
 * depends on a CHARACTER ARRAY, not on I/O, it can be tested directly: this demo
 * runs `eval()` on a battery of HARDCODED expression strings and prints
 * "<expr> => <result>" for each.  No stdin is read at all — that is the whole
 * point of separating the algorithm from the I/O.
 *
 *     $ spimulator -f calc-eval-test.asm
 *     2+3 => 5
 *     10/4 => 2.500000
 *     ...
 *
 * This is a golden-tested unit test: run-demo.sh pins the expected output, so a
 * regression in parse_factor/parse_term/parse_expr (which now operate on the
 * buffer) turns the golden red.  The eval core below is calc-eval's, verbatim
 * (each calc version is self-contained, like the rest of the ladder); only the
 * driver — a case table instead of a stdin loop — differs.  Mirrored in
 * calc-eval-test.asm.
 */

#include "io.h"
#include "crt0.h"

#define FRAC_DIGITS 6

static const char* cursor; /* read position within the expression buffer */
static int cur;            /* lookahead character (-1 at end of the buffer) */
static int had_error;      /* set when the expression fails to parse */

static void advance(void) {
  if (*cursor == '\0') {
    cur = -1;
  } else {
    cur = (unsigned char)*cursor;
    cursor++;
  }
}

static void skip_blanks(void) {
  while (cur == ' ' || cur == '\t' || cur == '\r') {
    advance();
  }
}

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

static double parse_expr(void);

/* factor := NUMBER | '-' factor | '(' expr ')' */
static double parse_factor(void) {
  skip_blanks();
  if (cur == '-') {
    advance();
    return -parse_factor();
  }
  if (cur == '(') {
    advance();
    double value = parse_expr();
    skip_blanks();
    if (cur == ')') {
      advance();
    } else {
      had_error = 1;
    }
    return value;
  }
  double mantissa = 0.0;
  int fractional_digits = 0;
  int seen_digit = 0;
  while (cur >= '0' && cur <= '9') {
    mantissa = mantissa * 10.0 + (double)(cur - '0');
    seen_digit = 1;
    advance();
  }
  if (cur == '.') {
    advance();
    while (cur >= '0' && cur <= '9') {
      mantissa = mantissa * 10.0 + (double)(cur - '0');
      fractional_digits++;
      seen_digit = 1;
      advance();
    }
  }
  if (!seen_digit) {
    had_error = 1;
    return 0.0;
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
    skip_blanks();
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
    skip_blanks();
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

/* The pure core under test: evaluate a NUL-terminated expression string. */
static double eval(const char* expr) {
  cursor = expr;
  had_error = 0;
  advance();
  double value = parse_expr();
  skip_blanks();
  if (cur != -1) {
    had_error = 1;
  }
  return value;
}

/* The test battery: each string is fed to eval() and its result printed, so the
 * pinned golden is the expected-value oracle. */
static const char* cases[] = {
    "2+3", "10/4", "(3+4)*2-10/4", "-5+3", "3*-2", "2 + 3 * 4", ".5*2", "(2+3",
};

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int case_count = (int)(sizeof(cases) / sizeof(cases[0]));
  for (int i = 0; i < case_count; i++) {
    double value = eval(cases[i]);
    print_string(cases[i]);
    print_string(" => ");
    if (had_error) {
      print_string("error");
    } else {
      print_double(value);
    }
    print_char('\n');
  }
  return 0;
}
