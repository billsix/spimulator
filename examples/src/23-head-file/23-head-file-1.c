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

/* PURPOSE: A port of `sbase/head -n N <file>` — read at most N
 *          lines from the named file and write them to stdout.
 *
 *          The richest argv demo so far.  Argv parsing combines:
 *             - flag check  : argv[1] == "-n"   (08-style str-eq)
 *             - atoi        : argv[2]           (20-style)
 *             - filename    : argv[3]           (21-style)
 *
 *          The early-termination loop body is the same shape as
 *          13-head, just sourced from a fd rather than from
 *          read_char's hard-coded stdin.
 *
 *          Invocation:
 *              spimulator -f 23-head-file.asm -n 5 /etc/passwd
 *              ./23-head-file-1 -n 5 /etc/passwd          # on Linux
 *
 *          Exit: 0 on a clean N-lines-or-EOF, 1 on usage error or
 *          missing file.
 */

#include "io.h"

#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

static int str_eq(const char *a, const char *b) {
  while (*a == *b) {
    if (*a == 0) return 1;
    a++;
    b++;
  }
  return 0;
}

int my_main(int argc, char **argv) {
  if (argc != 4 || !str_eq(argv[1], "-n")) {
    print_string("usage: head-file -n N FILE\n");
    return 1;
  }
  int n = parse_int(argv[2]);

  int fd = (int)os_open(argv[3], OS_O_RDONLY, 0);
  if (fd < 0) {
    print_string("head-file: cannot open ");
    print_string(argv[3]);
    print_char('\n');
    return 1;
  }

  int lines = 0;
  char c;
  while (lines < n) {
    long r = os_read(fd, &c, 1);
    if (r <= 0) break;
    os_write(STDOUT, &c, 1);
    if (c == '\n') lines++;
  }

  os_close(fd);
  return 0;
}
