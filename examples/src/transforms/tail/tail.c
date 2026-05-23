// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `tail -n N` — print the last N lines of
 *          the input.
 *
 *          Invocation:
 *              tail                -> stdin, N=10
 *              tail -              -> stdin, N=10
 *              tail FILE           -> reads FILE, N=10
 *              tail -n N           -> stdin
 *              tail -n N -         -> stdin
 *              tail -n N FILE      -> reads FILE
 *
 *          New asm idea: **ring buffer** — fixed array of N
 *          line slots, write-index modulo N.  When the input
 *          ends, the ring holds the LAST N complete lines (or
 *          fewer if input was shorter).
 *
 *          Caps: max N = 64 lines; each line up to 1024 chars.
 */

#include "io.h"
#include "crt0.h"

#define MAX_N 64
#define LINEMAX 1024

static char ring[MAX_N][LINEMAX];
static int ring_len[MAX_N];
static int cur_line_len = 0;
static char cur_line[LINEMAX];

static int str_eq(const char* a, const char* b) {
  while (*a == *b) {
    if (*a == 0) return 1;
    a++;
    b++;
  }
  return 0;
}

static int is_dash(const char* s) { return s[0] == '-' && s[1] == 0; }

int my_main(int argc, char** argv) {
  int n = 10;
  int fd = STDIN;
  const char* file_arg = 0;

  /* Parse [-n N] [FILE|-] */
  if (argc == 1) {
    /* defaults */
  } else if (argc == 2) {
    file_arg = argv[1];
  } else if (argc == 3 && str_eq(argv[1], "-n")) {
    n = parse_int(argv[2]);
  } else if (argc == 4 && str_eq(argv[1], "-n")) {
    n = parse_int(argv[2]);
    file_arg = argv[3];
  } else {
    print_string("usage: tail [-n N] [FILE|-]\n");
    return 1;
  }

  if (n < 1 || n > MAX_N) {
    print_string("tail: N must be 1..64\n");
    return 1;
  }

  if (file_arg && !is_dash(file_arg)) {
    fd = (int)os_open(file_arg, OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("tail: cannot open ");
      print_string(file_arg);
      print_char('\n');
      return 1;
    }
  }

  int slot = 0;  /* next write slot in the ring */
  int total = 0; /* total complete lines seen */
  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (c == '\n') {
      /* Commit cur_line to the ring at `slot` */
      for (int i = 0; i < cur_line_len; i++) ring[slot][i] = cur_line[i];
      ring_len[slot] = cur_line_len;
      slot = (slot + 1) % n;
      total++;
      cur_line_len = 0;
    } else if (cur_line_len < LINEMAX) {
      cur_line[cur_line_len++] = c;
    }
  }

  /* Output: print min(total, n) lines, starting from oldest. */
  int count = (total < n) ? total : n;
  int start = (total <= n) ? 0 : slot;
  for (int i = 0; i < count; i++) {
    int s = (start + i) % n;
    os_write(STDOUT, ring[s], ring_len[s]);
    os_write(STDOUT, "\n", 1);
  }
  /* Trailing partial line (no '\n') gets printed too if it's
   * within the last N. */
  if (cur_line_len > 0) {
    os_write(STDOUT, cur_line, cur_line_len);
    os_write(STDOUT, "\n", 1);
  }

  if (fd != STDIN) os_close(fd);
  return 0;
}
