// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `comm A B` — compare two sorted files
 *          line by line.  Three columns of output:
 *
 *              <line>           — only in A
 *              \t<line>         — only in B
 *              \t\t<line>       — in both
 *
 *          Invocation:
 *              comm A B
 *
 *          New asm idea: **two file descriptors open
 *          simultaneously**.  Read one line from each; compare;
 *          emit; advance whichever is lower (or both on equal).
 *          Real comm requires inputs to be sorted; we don't
 *          enforce it (garbage in, garbage out).
 */

#include "io.h"
#include "crt0.h"

#define LINEMAX 1024

static char line_a[LINEMAX];
static char line_b[LINEMAX];

/* Read one line from fd into buf (up to cap-1 chars + NUL).
 * Returns line length on success, -1 on EOF before any byte.
 * Strips the trailing '\n'. */
static int read_line(int fd, char *buf, int cap) {
  int len = 0;
  char c;
  long n;
  while ((n = os_read(fd, &c, 1)) > 0) {
    if (c == '\n') {
      buf[len] = 0;
      return len;
    }
    if (len < cap - 1) buf[len++] = c;
  }
  if (len == 0) return -1;           /* EOF and nothing in buffer */
  buf[len] = 0;
  return len;
}

static int line_cmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

int my_main(int argc, char **argv) {
  if (argc != 3) {
    print_string("usage: comm A B\n");
    return 1;
  }
  int fa = (int)os_open(argv[1], OS_O_RDONLY, 0);
  if (fa < 0) { print_string("comm: cannot open "); print_string(argv[1]); print_char('\n'); return 1; }
  int fb = (int)os_open(argv[2], OS_O_RDONLY, 0);
  if (fb < 0) { print_string("comm: cannot open "); print_string(argv[2]); print_char('\n'); os_close(fa); return 1; }

  int la = read_line(fa, line_a, LINEMAX);
  int lb = read_line(fb, line_b, LINEMAX);

  while (la >= 0 || lb >= 0) {
    if (la < 0) {
      /* only in B */
      print_char('\t');
      print_string(line_b);
      print_char('\n');
      lb = read_line(fb, line_b, LINEMAX);
    } else if (lb < 0) {
      /* only in A */
      print_string(line_a);
      print_char('\n');
      la = read_line(fa, line_a, LINEMAX);
    } else {
      int c = line_cmp(line_a, line_b);
      if (c == 0) {
        /* in both */
        print_char('\t');
        print_char('\t');
        print_string(line_a);
        print_char('\n');
        la = read_line(fa, line_a, LINEMAX);
        lb = read_line(fb, line_b, LINEMAX);
      } else if (c < 0) {
        print_string(line_a);
        print_char('\n');
        la = read_line(fa, line_a, LINEMAX);
      } else {
        print_char('\t');
        print_string(line_b);
        print_char('\n');
        lb = read_line(fb, line_b, LINEMAX);
      }
    }
  }

  os_close(fa);
  os_close(fb);
  return 0;
}
