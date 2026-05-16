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

/* PURPOSE: A simplified port of `sbase/tr 'a-z' 'A-Z'` — copy
 *          stdin to stdout, upcasing every lowercase ASCII byte.
 *          The transformation is hardcoded; a real `tr` reads its
 *          translation table from argv.
 *
 *          'A' is ASCII 65, 'a' is ASCII 97 — exactly 32 apart —
 *          so the upcase is just `ch - 32` (or equivalently
 *          `ch & ~0x20`).  We use subtraction here.
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  int ch = read_char();
  while (ch != -1) {
    if (ch >= 'a' && ch <= 'z')
      ch = ch - ('a' - 'A');           /* 'a' - 'A' = 32 */
    print_char((char)ch);
    ch = read_char();
  }
  os_exit(0);
}
