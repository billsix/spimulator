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

/* PURPOSE: A port of `sbase/cat` — concatenate input to stdout.
 *
 *          Real-Unix-style argv handling:
 *              cat            -> reads stdin
 *              cat -          -> reads stdin (explicit '-')
 *              cat FILE       -> opens FILE and reads it
 *
 *          Block-at-a-time I/O via `os_read` / `os_write` —
 *          moving bytes one at a time would be 4096× more
 *          syscalls per page of input.
 */

#include "io.h"
#include "crt0.h"

#define BUFSIZE 4096

int my_main(int argc, char** argv) {
  int fd = STDIN;

  if (argc > 2) {
    print_string("usage: cat [FILE|-]\n");
    return 1;
  }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("cat: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }

  static char buf[BUFSIZE];
  long n;
  while ((n = os_read(fd, buf, sizeof(buf))) > 0) os_write(STDOUT, buf, n);

  if (fd != STDIN) os_close(fd);
  return n < 0 ? 1 : 0;
}
