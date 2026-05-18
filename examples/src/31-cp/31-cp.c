// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `cp SRC DST` — copy SRC to DST.
 *
 *          Invocation:
 *              cp SRC DST
 *
 *          Opens SRC for reading, opens DST for writing with
 *          O_CREAT | O_TRUNC | mode 0644.  Block-I/O loop in
 *          between.
 *
 *          First demo where output goes to a CREATED file —
 *          16-cat / 17-nologin opened files read-only.
 *          Demonstrates the file-creation half of open().
 *
 *          No flag machinery (`-a -i -p -R` from real cp are
 *          out of scope).  Single source + dest only.
 */

#include "io.h"
#include "crt0.h"

#define BUFSIZE 4096

int my_main(int argc, char **argv) {
  if (argc != 3) {
    print_string("usage: cp SRC DST\n");
    return 1;
  }

  int src = (int)os_open(argv[1], OS_O_RDONLY, 0);
  if (src < 0) {
    print_string("cp: cannot open ");
    print_string(argv[1]);
    print_char('\n');
    return 1;
  }

  int dst = (int)os_open(argv[2], OS_O_WRONLY | OS_O_CREAT | OS_O_TRUNC, 0644);
  if (dst < 0) {
    print_string("cp: cannot create ");
    print_string(argv[2]);
    print_char('\n');
    os_close(src);
    return 1;
  }

  static char buf[BUFSIZE];
  long n;
  while ((n = os_read(src, buf, sizeof(buf))) > 0)
    os_write(dst, buf, n);

  os_close(src);
  os_close(dst);
  return n < 0 ? 1 : 0;
}
