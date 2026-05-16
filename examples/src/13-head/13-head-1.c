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

/* PURPOSE: A simplified port of `sbase/head -n 10` — copy the
 *          first 10 lines of stdin to stdout, then stop.
 *
 *          N is hardcoded to 10 because spim doesn't expose argv;
 *          a real `head` reads `-n N` off the command line.  The
 *          interesting lesson is early termination of an I/O
 *          loop — we don't need to drain the rest of stdin once
 *          we've seen enough newlines.
 */

#include "io.h"

#define N 10

__attribute__((noreturn)) void _start(void) {
  int line_count = 0;
  int ch = read_char();
  while (ch != -1) {
    print_char((char)ch);
    if (ch == '\n') {
      line_count = line_count + 1;
      if (line_count == N) break;
    }
    ch = read_char();
  }
  os_exit(0);
}
