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

/* PURPOSE: A port of suckless `sbase/cat` — copy standard input
 *          to standard output, until EOF.  The fundamental Unix
 *          "filter" shape: read a block, write it, repeat.
 *
 *          This is the FIRST demo to do block-at-a-time I/O.
 *          Previous demos (04, 06) read one byte at a time with
 *          read_char (syscall 12); here we use os_read with a
 *          large buffer (syscall 14 in spim) and write the whole
 *          block at once with os_write (syscall 15 in spim).
 *
 *          Block I/O is what every real Unix filter does — moving
 *          bytes one at a time would be 4096× more syscalls per
 *          page of input.
 */

#include "io.h"

#define BUFSIZE 4096

__attribute__((noreturn)) void _start(void) {
  static char buf[BUFSIZE];
  long n;
  while ((n = os_read(STDIN, buf, sizeof(buf))) > 0)
    os_write(STDOUT, buf, n);
  os_exit(n < 0 ? 1 : 0);
}
