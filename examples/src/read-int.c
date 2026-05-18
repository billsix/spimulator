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

/* C-side equivalent of spim's syscall 5 (read_int).  Skips leading
 * whitespace, reads optional sign, then digits.  Returns -1 on EOF
 * before any digit was found; otherwise stores the parsed int in
 * *out and returns 0.
 *
 * Trailing whitespace (the byte AFTER the last digit) is consumed.
 * That's a minor wrinkle compared to scanf("%d") which doesn't
 * consume the byte that terminated the number, but for our demos
 * it's fine — input is one int per line, the trailing newline gets
 * eaten and the next call starts fresh. */

#include "io.h"

int read_int_from_stdin(int *out) {
  int ch;

  /* Skip leading whitespace, including any blank lines. */
  do {
    ch = read_char();
    if (ch < 0) return -1;          /* EOF before any digit */
  } while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');

  int sign = 1;
  if (ch == '-') {
    sign = -1;
    ch = read_char();
  } else if (ch == '+') {
    ch = read_char();
  }

  if (ch < '0' || ch > '9') return -1;  /* not a number */

  int value = 0;
  while (ch >= '0' && ch <= '9') {
    value = value * 10 + (ch - '0');
    ch = read_char();
    if (ch < 0) break;              /* EOF after digits is OK */
  }
  *out = sign * value;
  return 0;
}
