// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `tac` — reverse the order of lines.
 *
 *          Invocation:
 *              tac            -> stdin
 *              tac -          -> stdin
 *              tac FILE       -> reads FILE
 *
 *          Reads ALL input into a sbrk'd buffer that grows on
 *          demand, then walks the buffer backwards emitting
 *          each line.
 *
 *          New asm idea: **dynamic memory via sbrk**.  Unlike
 *          sieve (where sbrk is called once with a known
 *          size), tac doesn't know how big the input is — it
 *          calls sbrk repeatedly to grow the buffer as bytes
 *          arrive.
 *
 *          Trailing-newline handling: the last line of input
 *          may or may not end in '\n'.  We add one to its
 *          output if missing, matching real tac.
 */

#include "io.h"
#include "crt0.h"

#define GROW_BY 4096

int my_main(int argc, char **argv) {
  int fd = STDIN;
  if (argc > 2) { print_string("usage: tac [FILE|-]\n"); return 1; }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) { print_string("tac: cannot open "); print_string(argv[1]); print_char('\n'); return 1; }
  }

  /* Read everything into a sbrk'd buffer. */
  char *buf = (char *)os_brk(0);
  long total = 0;
  long capacity = 0;

  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (total >= capacity) {
      capacity += GROW_BY;
      os_brk(buf + capacity);
    }
    buf[total++] = c;
  }

  if (fd != STDIN) os_close(fd);

  /* Walk backwards. */
  long i = total;
  while (i > 0) {
    long end = i;
    long start;
    if (buf[end - 1] == '\n') {
      /* Line ends with newline at index end-1.  Find start. */
      start = end - 1;
      while (start > 0 && buf[start - 1] != '\n') start--;
    } else {
      /* Final line (no trailing newline). */
      start = end;
      while (start > 0 && buf[start - 1] != '\n') start--;
    }
    os_write(STDOUT, buf + start, end - start);
    if (buf[end - 1] != '\n') os_write(STDOUT, "\n", 1);
    i = start;
  }
  return 0;
}
