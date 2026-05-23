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

/* PURPOSE: A simplified port of `sbase/expand` — replace every
 *          tab in the input with as many spaces as needed to
 *          reach the next 8-column boundary.  Tab width is
 *          hard-coded; real `expand` takes `-t N`.
 *
 *          Real-Unix-style argv handling:
 *              expand            -> stdin
 *              expand -          -> stdin
 *              expand FILE       -> reads FILE
 */

#include "io.h"
#include "crt0.h"

#define TAB_WIDTH 8

int my_main(int argc, char** argv) {
  int fd = STDIN;

  if (argc > 2) {
    print_string("usage: expand [FILE|-]\n");
    return 1;
  }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("expand: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }

  int col = 0;
  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (c == '\t') {
      int spaces = TAB_WIDTH - (col % TAB_WIDTH);
      for (int i = 0; i < spaces; i++) print_char(' ');
      col = col + spaces;
    } else if (c == '\n') {
      print_char('\n');
      col = 0;
    } else {
      print_char(c);
      col = col + 1;
    }
  }

  if (fd != STDIN) os_close(fd);
  return 0;
}
