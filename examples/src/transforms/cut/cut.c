// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `cut -c N-M` — print byte positions
 *          N..M (1-indexed, inclusive) of each input line.
 *
 *          Invocation:
 *              cut -c N-M           -> stdin
 *              cut -c N-M -         -> stdin
 *              cut -c N-M FILE      -> reads FILE
 *
 *          Real cut also supports lists like `1,3,5-7`; we do
 *          a single range.  Real cut has -f (fields) and -b
 *          (bytes-vs-chars); those are out of scope.
 *
 *          New asm idea: **range parsing in argv** — split
 *          "N-M" into two ints at the '-' character.
 */

#include "io.h"
#include "crt0.h"

#define LINEMAX 4096

static char line[LINEMAX];

/* Parse "N-M" into *lo, *hi.  Returns 0 on success, -1 on
 * parse error.  Both halves required. */
static int parse_range(const char* s, int* lo, int* hi) {
  int v = 0;
  if (*s < '0' || *s > '9') return -1;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s++ - '0');
  }
  *lo = v;
  if (*s != '-') return -1;
  s++;
  if (*s < '0' || *s > '9') return -1;
  v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s++ - '0');
  }
  *hi = v;
  return 0;
}

int my_main(int argc, char** argv) {
  /* Require: cut -c N-M [FILE|-] */
  if (argc < 3 || argc > 4 || argv[1][0] != '-' || argv[1][1] != 'c' ||
      argv[1][2] != 0) {
    print_string("usage: cut -c N-M [FILE|-]\n");
    return 1;
  }
  int lo, hi;
  if (parse_range(argv[2], &lo, &hi) < 0 || lo < 1 || hi < lo) {
    print_string("cut: bad range; need N-M with 1<=N<=M\n");
    return 1;
  }

  int fd = STDIN;
  if (argc == 4 && !(argv[3][0] == '-' && argv[3][1] == 0)) {
    fd = (int)os_open(argv[3], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("cut: cannot open ");
      print_string(argv[3]);
      print_char('\n');
      return 1;
    }
  }

  int len = 0;
  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (c == '\n') {
      /* Emit chars at positions lo..hi (1-indexed, inclusive). */
      for (int i = lo - 1; i < hi && i < len; i++) print_char(line[i]);
      print_char('\n');
      len = 0;
    } else if (len < LINEMAX) {
      line[len++] = c;
    }
  }
  /* Handle final line without '\n' */
  if (len > 0) {
    for (int i = lo - 1; i < hi && i < len; i++) print_char(line[i]);
    print_char('\n');
  }

  if (fd != STDIN) os_close(fd);
  return 0;
}
