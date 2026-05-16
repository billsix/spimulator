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

/* PURPOSE: A port of `ubase/nologin` — print the contents of
 *          /etc/nologin.txt if it exists, else a fallback string.
 *          Always exits with status 1 (the original `nologin`
 *          binary signals "no login allowed" to whatever called
 *          it).
 *
 *          First demo to combine open + read + write + close.  The
 *          read/write loop is the same shape as 11-cat, just with
 *          an `os_open`-acquired fd instead of STDIN.
 */

#include "io.h"

#define BUFSIZE 256

__attribute__((noreturn)) void _start(void) {
  static char buf[BUFSIZE];
  int fd = (int)os_open("/etc/nologin.txt", OS_O_RDONLY, 0);
  if (fd >= 0) {
    long n;
    while ((n = os_read(fd, buf, sizeof(buf))) > 0)
      os_write(STDOUT, buf, n);
    os_close(fd);
  } else {
    static const char alt[] = "The account is currently unavailable.\n";
    os_write(STDOUT, alt, sizeof(alt) - 1);
  }
  os_exit(1);
}
