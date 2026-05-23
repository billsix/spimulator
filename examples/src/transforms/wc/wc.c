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

/* PURPOSE: A port of `sbase/wc -cl` — count bytes and lines on
 *          the input, then print:
 *
 *              <bytes> bytes, <lines> lines
 *
 *          Real-Unix-style argv handling:
 *              wc            -> reads stdin
 *              wc -          -> reads stdin (explicit '-')
 *              wc FILE       -> opens FILE and reads it
 *              wc anything-else -> usage error
 */

#include "io.h"
#include "crt0.h" /* provides _start; calls my_main(argc, argv) */

int my_main(int argc, char** argv) {
  int fd = STDIN;

  if (argc > 2) {
    print_string("usage: wc [FILE|-]\n");
    return 1;
  }

  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("wc: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }
  /* Otherwise fd stays at STDIN (both `wc` and `wc -`). */

  int byte_count = 0;
  int line_count = 0;
  char c;
  while (os_read(fd, &c, 1) > 0) {
    byte_count++;
    if (c == '\n') line_count++;
  }

  print_int(byte_count);
  print_string(" bytes, ");
  print_int(line_count);
  print_string(" lines\n");

  if (fd != STDIN) os_close(fd);
  return 0;
}
