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

/* PURPOSE: Unit tests for the I/O-separated RPN core
 * (tasks/examples-separate-io-from-algorithm.md).  Because rpn-eval's evaluator
 * depends on a CHARACTER ARRAY, not on I/O, it can be tested directly: this
 * demo runs `eval()` on a battery of HARDCODED RPN strings and prints
 * "<expr> => <result>" for each — a value, or the error kind eval() reported.
 * No stdin is read at all — that is the whole point of separating the algorithm
 * from the I/O.
 *
 *     $ spimulator -f rpn-eval-test.asm
 *     3 4 + => 7
 *     1 3 / => 0.333333
 *     + => stack underflow
 *     ...
 *
 * This is a golden-tested unit test: run-demo.sh pins the expected output, so a
 * regression in the evaluator (which now walks the buffer) turns the golden
 * red. The eval core below is rpn-eval's, verbatim (each demo is
 * self-contained, like the rest of the ladder); only the driver — a case table
 * instead of a stdin slurp — differs.  Mirrored in rpn-eval-test.asm.
 */

#include "crt0.h"
#include "io.h"

#define STACK_MAX 64
#define FRAC_DIGITS 6

enum eval_status {
  EVAL_OK,
  EVAL_OVERFLOW,
  EVAL_UNDERFLOW,
  EVAL_BAD_INPUT,
  EVAL_EMPTY,
};

static const char* cursor; /* read position within the expression buffer */
static int cur;            /* lookahead character (-1 at end of the buffer) */
static int status;         /* EVAL_OK, or the error kind eval() hit */

static void advance(void) {
  if (*cursor == '\0') {
    cur = -1;
  } else {
    cur = (unsigned char)*cursor;
    cursor++;
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

static int is_operator(int c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

/* The pure core under test: evaluate a NUL-terminated RPN expression string. */
static double eval(const char* expr) {
  double stack[STACK_MAX];
  int depth = 0;

  cursor = expr;
  status = EVAL_OK;
  advance();

  while (cur != -1) {
    if (cur == ' ' || cur == '\n' || cur == '\t' || cur == '\r') {
      advance();
      continue;
    }
    if (cur >= '0' && cur <= '9') {
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
      if (depth >= STACK_MAX) {
        status = EVAL_OVERFLOW;
        return 0.0;
      }
      stack[depth++] = value;
      continue;
    }
    if (is_operator(cur)) {
      if (depth < 2) {
        status = EVAL_UNDERFLOW;
        return 0.0;
      }
      double right = stack[--depth];
      double left = stack[--depth];
      double result;
      if (cur == '+')
        result = left + right;
      else if (cur == '-')
        result = left - right;
      else if (cur == '*')
        result = left * right;
      else
        result = left / right;
      stack[depth++] = result;
      advance();
      continue;
    }
    status = EVAL_BAD_INPUT;
    return 0.0;
  }

  if (depth < 1) {
    status = EVAL_EMPTY;
    return 0.0;
  }
  return stack[depth - 1];
}

/* The test battery: each string is fed to eval() and its result printed, so the
 * pinned golden is the expected-value oracle.  It exercises each operator, a
 * float division, a negative result, the whole-golden expression, a single
 * value and leftover operands, and each of eval()'s error kinds. */
static const char* cases[] = {
    "3 4 +", "3 4 + 2 *", "1 3 /", "3 4 -", "3 4 + 2 * 1 3 / -",
    "5",     "1 2 3",     "+",     "",      "1 2 x",
};

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int case_count = (int)(sizeof(cases) / sizeof(cases[0]));
  for (int i = 0; i < case_count; i++) {
    double value = eval(cases[i]);
    print_string(cases[i]);
    print_string(" => ");
    if (status == EVAL_OK) {
      print_double(value);
    } else if (status == EVAL_OVERFLOW) {
      print_string("stack overflow");
    } else if (status == EVAL_UNDERFLOW) {
      print_string("stack underflow");
    } else if (status == EVAL_BAD_INPUT) {
      print_string("bad input");
    } else {
      print_string("empty");
    }
    print_char('\n');
  }
  return 0;
}
