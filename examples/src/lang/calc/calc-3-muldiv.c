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

/* PURPOSE: Rung 3 of the calculator ladder (tasks/calc-incremental-versions.md).
 * Adds integer '*' and '/', binding tighter than '+'/'-'.  Still integers, no
 * spaces, no sign, no error handling.
 *
 *     $ printf '2+3*4\n' | spimulator -f calc-3-muldiv.asm
 *     14
 *
 *     expr := term (('+' | '-') term)*
 *     term := DIGITS (('*' | '/') DIGITS)*
 *
 * The one new idea: PRECEDENCE by grammar layering.  '*'/'/' live in a lower
 * rule (term) than '+'/'-' (expr), so the nesting of the two fold loops IS the
 * order of operations — '*' binds tighter because term is consumed whole before
 * expr folds it in.  Integer '/' truncates (10/4 == 2); the FPU arrives at
 * rung 6 (calc-6-float).
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

/* term := DIGITS (('*' | '/') DIGITS)* — binds tighter than expr. */
static int parse_term(void) {
  int value = parse_number();
  while (true) {
    if (cur == '*') {
      advance();
      value = value * parse_number();
    } else if (cur == '/') {
      advance();
      value = value / parse_number();
    } else {
      return value;
    }
  }
}

/* expr := term (('+' | '-') term)* — left-to-right fold over terms. */
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
