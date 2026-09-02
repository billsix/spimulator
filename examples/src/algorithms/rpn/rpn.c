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

/* PURPOSE: A reverse-Polish-notation (RPN) calculator, `dc`-flavored and
 *          floating point.  Reads whitespace-separated tokens from stdin:
 *
 *             - a number  (digits, optional single '.' then more digits)
 *               is PUSHED onto an evaluation stack;
 *             - an operator ('+', '-', '*', '/') POPS the top two values,
 *               applies the operation, and PUSHES the result.
 *
 *          At end of input the top of the stack is printed:
 *
 *              $ echo '3 4 + 2 *' | spimulator -f rpn.asm      ->  14
 *              $ echo '1 3 /'     | spimulator -f rpn.asm      ->  0.333333
 *
 *          The evaluation stack IS the lesson: RPN evaluation is a stack
 *          machine, and in the paired rpn.asm the operand stack is the real
 *          MIPS `$sp` stack (push = `addi $sp,-8` + `sdc1`, pop = `ldc1` +
 *          `addi $sp,8`).  This is also the first example to exercise spim's
 *          FPU: `cvt.d.w`, `add.d`/`sub.d`/`mul.d`/`div.d`, `trunc.w.d`.
 *
 *          The C here is the portable reference; rpn.asm is the hand-written
 *          MIPS translation.  Both run the SAME algorithm so their output
 *          matches byte-for-byte (see rpn.expected).
 *
 *          Conventions and the corners this demo deliberately keeps simple:
 *             - '-' is ALWAYS the subtract operator; there are no negative
 *               number literals in the input (negative *results* are fine and
 *               print with a leading '-', e.g. `3 4 -` -> -1).
 *             - A number needs a leading digit ('.5' is rejected; write '0.5').
 *             - Formatting a double for display is its own deep topic; this
 *               demo sidesteps it with a fixed, deterministic printer: an
 *               integral value prints as an integer, otherwise as the integer
 *               part, '.', and exactly FRAC_DIGITS truncated fractional digits.
 *               The integer part must fit in a 32-bit int.
 *             - Stack underflow (too few operands) prints a message and exits
 *               1.  Division by zero is NOT trapped: it yields IEEE inf/nan,
 *               which a real calculator shows too.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

/* STACK_MAX: maximum evaluation-stack depth.
 * FRAC_DIGITS: fractional digits printed for a non-integral result. */
#define STACK_MAX 64
#define FRAC_DIGITS 6

/* Print `value` deterministically (see the header note).  The identical
 * algorithm is implemented in rpn.asm, so the two outputs match exactly. */
static void print_double(double value) {
  if (value != value) {
    print_string("nan"); /* NaN is the only value not equal to itself */
    return;
  }
  if (value < 0.0) {
    print_char('-');
    value = -value;
  }
  if (value != 0.0 && value + value == value) {
    print_string("inf"); /* only 0 and infinity satisfy x + x == x */
    return;
  }
  int integer_part = (int)value; /* truncation toward zero; value >= 0 here */
  double fraction = value - (double)integer_part;
  print_int(integer_part);
  if (fraction == 0.0) {
    return; /* integral value: no fractional part to show */
  }
  print_char('.');
  for (int i = 0; i < FRAC_DIGITS; i++) {
    fraction *= 10.0;
    int digit = (int)fraction; /* truncate to the next digit, 0..9 */
    print_char((char)('0' + digit));
    fraction -= (double)digit;
  }
}

/* True for the four binary operators this calculator understands. */
static int is_operator(int c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

int my_main(int argc, char** argv) {
  /* This demo reads stdin, not the command line, but crt0.h always calls
   * my_main(argc, argv), so the signature is fixed.  Casting the unused
   * parameters to void marks them deliberately-ignored and silences
   * -Wunused-parameter (the build enables it via warning_level=3 / -Wextra).
   * The casts evaluate-and-discard, emitting no code. */
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
      /* Parse a number as an integer mantissa plus a count of fractional
       * digits, then divide by 10 once per fractional digit.  Doing the same
       * sequence of IEEE operations on both sides keeps C and asm identical. */
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
      if (depth >= STACK_MAX) {
        print_string("stack overflow\n");
        return 1;
      }
      stack[depth++] = value;
      continue; /* c already holds the next unconsumed character */
    }

    if (is_operator(c)) {
      if (depth < 2) {
        print_string("stack underflow\n");
        return 1;
      }
      double right = stack[--depth];
      double left = stack[--depth];
      double result;
      if (c == '+')
        result = left + right;
      else if (c == '-')
        result = left - right;
      else if (c == '*')
        result = left * right;
      else
        result = left / right; /* '/' — div by zero yields inf/nan, shown */
      stack[depth++] = result;
      c = read_char();
      continue;
    }

    print_string("bad input\n");
    return 1;
  }

  if (depth < 1) {
    print_string("empty\n");
    return 1;
  }
  print_double(stack[depth - 1]);
  print_char('\n');
  return 0;
}
