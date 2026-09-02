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

/* PURPOSE: A TI-83-style infix calculator, evaluated by syntax-directed
 * translation: a recursive-descent parser that computes the value WHILE it
 * parses — no syntax tree is built, each grammar rule just returns the double
 * it evaluates to.  One expression per line of stdin; the result (or "error")
 * is printed per line.
 *
 *     $ echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-sdt.asm
 *     11.500000
 *
 * The grammar (the teaching artifact itself) is the classic layered form that
 * bakes precedence and associativity into the rule nesting:
 *
 *     expr   := term   (('+' | '-') term)*
 *     term   := factor (('*' | '/') factor)*
 *     factor := NUMBER | '-' factor | '(' expr ')'
 *
 * `expr` and `factor` are mutually recursive (via the '(' expr ')' rule), so
 * the asm port (calc-sdt.asm) exercises mutual recursion with the full
 * $ra/frame discipline: an operand held across a recursive call is saved on
 * the stack.  This SDT approach — evaluate while parsing, no tree — is exactly
 * the technique the mini C compiler uses; a tree-building companion
 * (calc-tree) is planned — see tasks/calc-language.md.
 *
 * Values are doubles (floating point, like a TI-83).  Numbers are digits with
 * an optional single '.' and fraction ('3', '3.5', '.5').  Display reuses
 * rpn's fixed deterministic printer (integral -> integer, else integer part +
 * '.' + 6 truncated fractional digits; nan/inf named), so the C reference and
 * calc-sdt.asm match byte-for-byte.  A malformed line prints "error" and
 * parsing resumes at the next line; division by zero shows inf/nan, not a trap.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

/* Fractional digits printed for a non-integral result. */
#define FRAC_DIGITS 6

static int cur;       /* current lookahead character (-1 at EOF) */
static int had_error; /* set when the current line fails to parse */

static void advance(void) { cur = read_char(); }

/* Skip spaces and tabs WITHIN a line — a newline ends an expression, so it is
 * deliberately not skipped here. */
static void skip_blanks(void) {
  while (cur == ' ' || cur == '\t' || cur == '\r') {
    advance();
  }
}

/* Print `value` deterministically — identical to rpn's printer and to
 * calc-sdt.asm, so all three agree byte-for-byte. */
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
      had_error = 1; /* unbalanced parenthesis */
    }
    return value;
  }
  if ((cur >= '0' && cur <= '9') || cur == '.') {
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
      had_error = 1; /* a lone '.' is not a number */
      return 0.0;
    }
    double value = mantissa;
    for (int i = 0; i < fractional_digits; i++) {
      value = value / 10.0;
    }
    return value;
  }
  had_error = 1; /* unexpected character where a factor was expected */
  return 0.0;
}

/* term := factor (('*' | '/') factor)* */
static double parse_term(void) {
  double value = parse_factor();
  for (;;) {
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
  for (;;) {
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

int my_main(int argc, char** argv) {
  /* This demo reads stdin, not the command line, but crt0.h always calls
   * my_main(argc, argv), so the signature is fixed.  Casting the unused
   * parameters to void marks them deliberately-ignored and silences
   * -Wunused-parameter (the build enables it via warning_level=3 / -Wextra).
   * The casts evaluate-and-discard, emitting no code. */
  (void)argc;
  (void)argv;

  advance(); /* prime the first lookahead character */
  while (cur != -1) {
    skip_blanks();
    if (cur == '\n') { /* blank line */
      advance();
      continue;
    }
    if (cur == -1) {
      break;
    }
    had_error = 0;
    double value = parse_expr();
    skip_blanks();
    if (cur != '\n' && cur != -1) {
      had_error = 1; /* trailing garbage after a complete expression */
    }
    if (had_error) {
      print_string("error");
      while (cur != '\n' &&
             cur != -1) { /* recover: discard the rest of the line */
        advance();
      }
    } else {
      print_double(value);
    }
    print_char('\n');
    if (cur == '\n') {
      advance();
    }
  }
  return 0;
}
