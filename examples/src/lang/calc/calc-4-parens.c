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

/* PURPOSE: Rung 4 of the calculator ladder (tasks/calc-incremental-versions.md).
 * Adds parentheses for grouping (CCSS 5.OA.A.1).  Still integers, no spaces, no
 * sign, no error handling (well-formed input assumed).
 *
 *     $ printf '(2+3)*4\n' | spimulator -f calc-4-parens.asm
 *     20
 *
 *     expr   := term   (('+' | '-') term)*
 *     term   := factor (('*' | '/') factor)*
 *     factor := DIGITS | '(' expr ')'
 *
 * The one new idea: MUTUAL RECURSION.  factor can be a parenthesized expr, so
 * factor calls expr and expr (via term) calls factor.  In the asm port that is
 * the recursion chapter's frame discipline with a real payload — the operand is
 * saved on the stack across the recursive parse_expr() call.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

static int cur; /* current lookahead character (-1 at EOF) */

static void advance(void) { cur = read_char(); }

/* Accumulate a run of decimal digits into an int (well-formed input assumed). */
static int parse_number(void) {
  int value = 0;
  while (cur >= '0' && cur <= '9') {
    value = value * 10 + (cur - '0');
    advance();
  }
  return value;
}

static int parse_expr(void); /* forward decl for the '(' expr ')' rule */

/* factor := DIGITS | '(' expr ')' */
static int parse_factor(void) {
  if (cur == '(') {
    advance();
    int value = parse_expr();
    if (cur == ')') {
      advance();
    }
    return value;
  }
  return parse_number();
}

/* term := factor (('*' | '/') factor)* */
static int parse_term(void) {
  int value = parse_factor();
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
static int parse_expr(void) {
  int value = parse_term();
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

  int value;

  advance(); /* prime the first lookahead character */
  while (cur != -1) {
    if (cur == '\n') { /* blank line */
      advance();
      continue;
    }
    value = parse_expr();
    print_int(value);
    print_char('\n');
    if (cur == '\n') {
      advance();
    }
  }
  return 0;
}
