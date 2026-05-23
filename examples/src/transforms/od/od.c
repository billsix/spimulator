// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `od -c` — dump input as 16 bytes per
 *          row, each byte shown as its printable character (or
 *          a backslash escape, or 3-digit octal).
 *
 *          Invocation:
 *              od            -> stdin
 *              od -          -> stdin
 *              od FILE       -> reads FILE
 *
 *          Output format:
 *              0000000   h   e   l   l   o   ,       w   o   r ...
 *              0000020  \n
 *              0000022
 *
 *          The leading offset is a 7-digit octal number,
 *          right-aligned.  Each byte takes 4 columns: 3 chars
 *          + 1 space.  Common control chars get named escapes
 *          (\n \t \r \0); other non-printables go as 3-char
 *          octal (e.g. \033 for ESC).
 */

#include "io.h"
#include "crt0.h"

static void print_offset(int off) {
  /* 7-digit octal, right-aligned, zero-padded */
  char buf[8];
  buf[7] = 0;
  for (int i = 6; i >= 0; i--) {
    buf[i] = '0' + (off & 7);
    off >>= 3;
  }
  print_string(buf);
}

static void print_byte_c(unsigned char b) {
  /* 3-char representation, right-aligned to 3 chars when possible.
     This is what `od -c` does. */
  if (b == '\0') {
    print_string("  \\0");
    return;
  }
  if (b == '\b') {
    print_string("  \\b");
    return;
  }
  if (b == '\t') {
    print_string("  \\t");
    return;
  }
  if (b == '\n') {
    print_string("  \\n");
    return;
  }
  if (b == '\v') {
    print_string("  \\v");
    return;
  }
  if (b == '\f') {
    print_string("  \\f");
    return;
  }
  if (b == '\r') {
    print_string("  \\r");
    return;
  }
  if (b >= 0x20 && b < 0x7f) {
    print_char(' ');
    print_char(' ');
    print_char(' ');
    print_char((char)b);
    return;
  }
  /* 3-digit octal */
  print_char(' ');
  print_char('0' + ((b >> 6) & 7));
  print_char('0' + ((b >> 3) & 7));
  print_char('0' + (b & 7));
}

int my_main(int argc, char** argv) {
  int fd = STDIN;
  if (argc > 2) {
    print_string("usage: od [FILE|-]\n");
    return 1;
  }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("od: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }

  int offset = 0;
  int col = 0;
  char c;
  while (os_read(fd, &c, 1) > 0) {
    if (col == 0) {
      print_offset(offset);
    }
    print_byte_c((unsigned char)c);
    col++;
    offset++;
    if (col == 16) {
      print_char('\n');
      col = 0;
    }
  }
  if (col > 0) print_char('\n');
  print_offset(offset);
  print_char('\n');

  if (fd != STDIN) os_close(fd);
  return 0;
}
