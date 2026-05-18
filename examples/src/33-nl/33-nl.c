// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `nl` — number each line of the input.
 *
 *          Invocation:
 *              nl            -> stdin
 *              nl -          -> stdin
 *              nl FILE       -> reads FILE
 *
 *          Output: "     N\t<line>" — N right-aligned in a
 *          6-char field, then a tab, then the original line.
 *          That's the canonical real-nl format minus the
 *          header/footer/section-marker support.
 *
 *          New asm idea: **width-padded integer output** —
 *          inline helper that counts digits and prints leading
 *          spaces before print_int.
 */

#include "io.h"
#include "crt0.h"

#define PADWIDTH 6

static void print_padded(int n, int width) {
  /* count digits */
  int digits = 1;
  int t = n;
  while (t >= 10) { digits++; t /= 10; }
  for (int i = 0; i < width - digits; i++) print_char(' ');
  print_int(n);
}

int my_main(int argc, char **argv) {
  int fd = STDIN;
  if (argc > 2) { print_string("usage: nl [FILE|-]\n"); return 1; }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) { print_string("nl: cannot open "); print_string(argv[1]); print_char('\n'); return 1; }
  }

  int line_num = 1;
  int line_started = 0;
  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (!line_started) {
      print_padded(line_num, PADWIDTH);
      print_char('\t');
      line_started = 1;
    }
    print_char(c);
    if (c == '\n') {
      line_num++;
      line_started = 0;
    }
  }

  if (fd != STDIN) os_close(fd);
  return 0;
}
