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

/* PURPOSE: A port of `sbase/head` — copy the first N lines of
 *          the input to stdout, then stop.
 *
 *          Real-Unix-style argv handling:
 *              head                -> stdin, N=10
 *              head -              -> stdin, N=10
 *              head FILE           -> file, N=10
 *              head -n N           -> stdin, N from arg
 *              head -n N -         -> stdin, N from arg
 *              head -n N FILE      -> file, N from arg
 *              anything else       -> usage error
 *
 *          Subsumes the old `head-file` demo entirely.
 */

#include "io.h"
#include "crt0.h"

#define DEFAULT_N 10

static int str_eq(const char *a, const char *b) {
  while (*a == *b) {
    if (*a == 0) return 1;
    a++;
    b++;
  }
  return 0;
}

static int is_dash(const char *s) {
  return s[0] == '-' && s[1] == 0;
}

int my_main(int argc, char **argv) {
  int fd = STDIN;
  int n = DEFAULT_N;
  const char *file_arg = 0;

  /* Parse argv.  The shape is:  [-n N] [FILE|-] */
  if (argc == 1) {
    /* head : stdin, N=10 */
  } else if (argc == 2) {
    file_arg = argv[1];                /* "-" or filename */
  } else if (argc == 3 && str_eq(argv[1], "-n")) {
    n = parse_int(argv[2]);            /* head -n N : stdin */
  } else if (argc == 4 && str_eq(argv[1], "-n")) {
    n = parse_int(argv[2]);
    file_arg = argv[3];                /* head -n N FILE|- */
  } else {
    print_string("usage: head [-n N] [FILE|-]\n");
    return 1;
  }

  if (file_arg && !is_dash(file_arg)) {
    fd = (int)os_open(file_arg, OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("head: cannot open ");
      print_string(file_arg);
      print_char('\n');
      return 1;
    }
  }

  int line_count = 0;
  char c;
  while (line_count < n && os_read(fd, &c, 1) > 0) {
    print_char(c);
    if (c == '\n') line_count++;
  }

  if (fd != STDIN) os_close(fd);
  return 0;
}
