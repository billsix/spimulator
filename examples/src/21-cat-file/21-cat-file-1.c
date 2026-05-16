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

/* PURPOSE: A port of `sbase/cat <file>` — copy the contents of the
 *          file named on the command line to standard output.
 *
 *          Combines two ideas the curriculum has already shown:
 *
 *             - 11-cat   : the read/write block-I/O loop
 *             - 15-nologin : open/close on a hardcoded path
 *             - 19-echo  : argv handling via the x86_64 _start shim
 *
 *          The only newness is replacing the *hardcoded* path in
 *          15-nologin with one that comes from argv[1].
 *
 *          Invocation:
 *              spimulator -f 21-cat-file.asm /etc/motd
 *              ./21-cat-file-1 /etc/motd                  # on Linux
 *
 *          Exit status: 0 on a clean read-to-EOF, 1 if the file
 *          cannot be opened or argv is malformed.
 */

#include "io.h"

#define BUFSIZE 4096

#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

int my_main(int argc, char **argv) {
  if (argc != 2) {
    print_string("usage: cat-file FILE\n");
    return 1;
  }

  int fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
  if (fd < 0) {
    print_string("cat-file: cannot open ");
    print_string(argv[1]);
    print_char('\n');
    return 1;
  }

  static char buf[BUFSIZE];
  long n;
  while ((n = os_read(fd, buf, sizeof(buf))) > 0)
    os_write(STDOUT, buf, n);

  os_close(fd);
  return n < 0 ? 1 : 0;
}
