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

/* PURPOSE: The I/O-separated RPN calculator
 * (tasks/examples-separate-io-from-algorithm.md).  The SAME language and output
 * as rpn, but the algorithm is separated from the I/O: the core is a function
 *
 *     double eval(const char* expr);   // sets `status`; does no I/O
 *
 * that evaluates a NUL-terminated RPN expression string and returns the top of
 * the evaluation stack, and my_main does all the reading and printing — it
 * slurps stdin into a buffer and hands that buffer to eval().  Nothing in the
 * evaluator calls read_char; it walks the buffer.  So eval() is reusable and
 * testable from a string literal, independent of stdin (see rpn-eval-test).
 *
 *     $ echo '3 4 + 2 * 1 3 / -' | spimulator -f rpn-eval.asm
 *     13.666666
 *
 * eval() cannot print — an error is not I/O it can do — so instead of rpn's
 * inline "stack underflow"/exit it records WHICH error in `status` and returns;
 * my_main maps the status to rpn's exact message + exit code.  The behavior is
 * therefore identical to rpn, so this shares rpn's golden (rpn.input /
 * rpn.expected): same input, same output — only the boundary between algorithm
 * and I/O moved.  Mirrored in rpn-eval.asm, where the evaluator reads a byte
 * from a .data buffer instead of from syscall 12.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

#define STACK_MAX 64   /* maximum evaluation-stack depth */
#define FRAC_DIGITS 6  /* fractional digits printed for a non-integral result */
#define INPUT_MAX 256  /* longest whole input eval() is handed */

/* The outcome of an eval(): a value (EVAL_OK) or which error stopped it.
 * my_main turns each error into rpn's message + exit 1. */
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

/* Advance the cursor over the NUL-terminated buffer (no I/O here). */
static void advance(void) {
  if (*cursor == '\0') {
    cur = -1;
  } else {
    cur = (unsigned char)*cursor;
    cursor++;
  }
}

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

/* eval — the PURE core: evaluate a NUL-terminated RPN expression string.
 * Returns the top of the evaluation stack.  On an error it sets `status` to the
 * kind (and returns 0.0, which the caller ignores).  No I/O: tokens come from
 * the buffer via advance(), not from read_char(). */
double eval(const char* expr) {
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
      /* Parse a number as an integer mantissa plus a count of fractional
       * digits, then divide by 10 once per fractional digit. */
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
      continue; /* cur already holds the next unconsumed character */
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
        result = left / right; /* '/' — div by zero yields inf/nan, shown */
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

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  /* Slurp all of stdin into a buffer (rpn treats the whole input as one
   * whitespace-separated token stream — newlines are just blanks), then hand
   * the buffer to the pure evaluator. */
  char input[INPUT_MAX];
  int n = 0;
  int c = read_char();
  while (c != -1) {
    if (n < INPUT_MAX - 1) {
      input[n++] = (char)c;
    }
    c = read_char();
  }
  input[n] = '\0';

  double value = eval(input);
  if (status == EVAL_OVERFLOW) {
    print_string("stack overflow\n");
    return 1;
  }
  if (status == EVAL_UNDERFLOW) {
    print_string("stack underflow\n");
    return 1;
  }
  if (status == EVAL_BAD_INPUT) {
    print_string("bad input\n");
    return 1;
  }
  if (status == EVAL_EMPTY) {
    print_string("empty\n");
    return 1;
  }
  print_double(value);
  print_char('\n');
  return 0;
}
