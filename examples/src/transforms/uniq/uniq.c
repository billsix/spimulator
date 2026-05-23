// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `uniq` — collapse adjacent duplicate
 *          lines in the input.
 *
 *          Invocation:
 *              uniq            -> stdin
 *              uniq -          -> stdin
 *              uniq FILE       -> reads FILE
 *
 *          The asm pattern this introduces: a "previous line"
 *          cache held in a buffer.  After reading each line,
 *          compare to the prev buffer; if different, print and
 *          copy current to prev.
 *
 *          Real uniq has -c, -d, -u flags; we do the default
 *          collapse-only behaviour.
 */

#include "io.h"
#include "crt0.h"

#define LINEMAX 1024

static char prev_line[LINEMAX];
static int prev_len = -1; /* -1 = "no previous line yet" */
static char cur_line[LINEMAX];
static int cur_len = 0;

static int lines_equal(const char* a, int alen, const char* b, int blen) {
  if (alen != blen) return 0;
  for (int i = 0; i < alen; i++)
    if (a[i] != b[i]) return 0;
  return 1;
}

static void emit_if_new(void) {
  if (prev_len < 0 || !lines_equal(cur_line, cur_len, prev_line, prev_len)) {
    os_write(STDOUT, cur_line, cur_len);
    os_write(STDOUT, "\n", 1);
    /* copy cur into prev */
    for (int i = 0; i < cur_len; i++) prev_line[i] = cur_line[i];
    prev_len = cur_len;
  }
}

int my_main(int argc, char** argv) {
  int fd = STDIN;
  if (argc > 2) {
    print_string("usage: uniq [FILE|-]\n");
    return 1;
  }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("uniq: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }

  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (c == '\n') {
      emit_if_new();
      cur_len = 0;
    } else if (cur_len < LINEMAX) {
      cur_line[cur_len++] = c;
    }
  }
  if (cur_len > 0) emit_if_new(); /* final line without trailing \n */

  if (fd != STDIN) os_close(fd);
  return 0;
}
