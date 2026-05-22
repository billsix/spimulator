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

/* PURPOSE: A port of `sbase/tee` — copy stdin to stdout AND to
 *          every file named on the command line.  Truncates each
 *          file on open (no -a append flag).
 *
 *          Why this is the right capstone for Phase C: it has all
 *          three argv ideas at once —
 *
 *             - VARIABLE argc (1..MAX_OUT files, not fixed)
 *             - argv used to OPEN files (21-style, but in a loop)
 *             - per-block FAN-OUT writes to all collected fds
 *
 *          The read/write loop is again cat's shape, but the
 *          inner "write to one fd" becomes "for each fd in our
 *          collected list, write to it."  Output to stdout is
 *          unconditional (the "T" in tee).
 *
 *          Invocation:
 *              echo hello | spimulator -f tee.asm a.txt b.txt
 *              echo hello | ./tee-1 a.txt b.txt          # on Linux
 *
 *          Then `a.txt` and `b.txt` both contain "hello".
 *
 *          Limit: at most MAX_OUT named files (compile-time
 *          constant).  More is a usage error rather than a silent
 *          truncation of the file list.
 */

#include "io.h"

#define BUFSIZE 4096
#define MAX_OUT 8

#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

int my_main(int argc, char **argv) {
  if (argc - 1 > MAX_OUT) {
    print_string("tee: too many output files\n");
    return 1;
  }

  int fds[MAX_OUT];
  int nfds = 0;
  for (int i = 1; i < argc; i++) {
    int fd =
        (int)os_open(argv[i], OS_O_WRONLY | OS_O_CREAT | OS_O_TRUNC, 0644);
    if (fd < 0) {
      print_string("tee: cannot open ");
      print_string(argv[i]);
      print_char('\n');
      return 1;
    }
    fds[nfds++] = fd;
  }

  static char buf[BUFSIZE];
  long n;
  while ((n = os_read(STDIN, buf, sizeof(buf))) > 0) {
    os_write(STDOUT, buf, n);
    for (int i = 0; i < nfds; i++) os_write(fds[i], buf, n);
  }

  for (int i = 0; i < nfds; i++) os_close(fds[i]);
  return n < 0 ? 1 : 0;
}
