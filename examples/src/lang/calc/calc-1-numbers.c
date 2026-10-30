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

/* PURPOSE: Rung 1 of the calculator ladder (tasks/calc-incremental-versions.md).
 * The smallest honest first step toward the calc-sdt calculator: read one
 * whole non-negative INTEGER per line of stdin and print it back, one per
 * line.  No operators, no spaces, no sign, no error handling yet — just
 * tokenize a number and print it.
 *
 *     $ printf '42\n100\n' | spimulator -f calc-1-numbers.asm
 *     42
 *     100
 *
 *     line := DIGITS
 *
 * Integers (not doubles): the FPU arrives at rung 6 (calc-6-float).  Input is
 * assumed well-formed; error recovery is the final rung (calc-sdt).  Reading is
 * byte-at-a-time via read_char() (syscall 12, -1 at EOF), mirrored in the asm
 * port calc-1-numbers.asm.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

static int cur; /* current lookahead character (-1 at EOF) */

static void advance(void) { cur = read_char(); }

/* Accumulate a run of decimal digits into an int.  Assumes at least one digit
 * is present (the caller checked) and the input is well-formed. */
static int parse_number(void) {
  int value = 0;
  while (cur >= '0' && cur <= '9') {
    value = value * 10 + (cur - '0');
    advance();
  }
  return value;
}

int my_main(int argc, char** argv) {
  /* This demo reads stdin, not the command line, but crt0.h always calls
   * my_main(argc, argv), so the signature is fixed; cast the unused parameters
   * to void to silence -Wunused-parameter. */
  (void)argc;
  (void)argv;

  int value;

  advance(); /* prime the first lookahead character */
  while (cur != -1) {
    if (cur == '\n') { /* blank line */
      advance();
      continue;
    }
    value = parse_number();
    print_int(value);
    print_char('\n');
    if (cur == '\n') {
      advance();
    }
  }
  return 0;
}
