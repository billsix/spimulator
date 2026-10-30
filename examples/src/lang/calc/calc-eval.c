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

/* PURPOSE: The I/O-separated calculator (tasks/examples-separate-io-from-algorithm.md).
 * The SAME language and output as calc-sdt, but the algorithm is separated from
 * the I/O: the core is a PURE function
 *
 *     double eval(const char* expr);   // sets a had_error flag; does no I/O
 *
 * that parses and evaluates a NUL-terminated expression string, and my_main
 * does all the reading and printing — it reads one line of stdin into a buffer
 * and hands that buffer to eval().  Nothing in the parser calls read_char; it
 * walks the buffer.  So eval() is reusable and testable from a string literal,
 * independent of stdin — the shape the mini C compiler's front end needs.
 *
 *     $ echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-eval.asm
 *     11.500000
 *
 *     expr   := term   (('+' | '-') term)*
 *     term   := factor (('*' | '/') factor)*
 *     factor := NUMBER | '-' factor | '(' expr ')'
 *
 * Shares calc-sdt's golden (calc-sdt.input / .expected): same input, same
 * output — only the boundary between algorithm and I/O moved.  Mirrored in
 * calc-eval.asm, where the parser reads a byte from a .data line buffer instead
 * of from syscall 12.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

#define FRAC_DIGITS 6 /* fractional digits printed for a non-integral result */
#define LINE_MAX 256  /* longest input line eval() is handed */

static const char* cursor; /* read position within the expression buffer */
static int cur;            /* lookahead character (-1 at end of the buffer) */
static int had_error;      /* set when the expression fails to parse */

/* Advance the cursor over the NUL-terminated buffer (no I/O here). */
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

/* Print `value` deterministically — identical to rpn/calc-sdt's printer. */
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

/* eval — the PURE core: evaluate a NUL-terminated expression string.  No I/O.
 * Sets had_error on a malformed expression or trailing garbage. */
double eval(const char* expr) {
  cursor = expr;
  had_error = 0;
  advance();
  double value = parse_expr();
  skip_blanks();
  if (cur != -1) {
    had_error = 1; /* trailing garbage after a complete expression */
  }
  return value;
}

/* True if the line is empty or only blanks (produces no output, like calc-sdt). */
static int blank_line(const char* line) {
  for (const char* p = line; *p != '\0'; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\r') {
      return 0;
    }
  }
  return 1;
}

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  char line[LINE_MAX];
  int c = read_char();

  while (c != -1) {
    int n = 0;
    while (c != -1 && c != '\n') {
      if (n < LINE_MAX - 1) {
        line[n++] = (char)c;
      }
      c = read_char();
    }
    line[n] = '\0';
    if (c == '\n') {
      c = read_char(); /* consume the newline, prime the next line */
    }
    if (blank_line(line)) {
      continue;
    }
    double value = eval(line);
    if (had_error) {
      print_string("error");
    } else {
      print_double(value);
    }
    print_char('\n');
  }
  return 0;
}
