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

/* PURPOSE: Call a subroutine y = m*x + b twice and print each
 *          result on its own line.
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -o subrountines subrountines.c ../../print-int.c ../../print-string.c ../../count-chars.c
 *     $ ./subrountines
 *
 *   each paired MIPS assembly version, in spimulator:
 *     $ spimulator -f subrountines-1.asm
 *     $ spimulator -f subrountines-2.asm
 *
 *   the same assembly versions, in the Python port:
 *     $ spimulator-python -f subrountines-1.asm
 *     $ spimulator-python -f subrountines-2.asm
 *
 *   subrountines-1.asm never sets $v0 before main returns, so its exit
 *   status is whatever was left there; the printed output matches.
 */
/* clang-format on */

#include "io.h"

int mxPlusB(int m, int x, int b) {
  int result = m * x + b;
  return result;
}

__attribute__((noreturn)) void _start(void) {
  int result1 = mxPlusB(1, 2, 3);
  print_int(result1);
  print_string("\n");
  int result2 = mxPlusB(4, 5, 6);
  print_int(result2);
  print_string("\n");
  os_exit(0);
}
