// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `base64` encoder — read bytes, output
 *          the canonical RFC 4648 base64 representation.
 *
 *          Invocation:
 *              base64            -> stdin
 *              base64 -          -> stdin
 *              base64 FILE       -> reads FILE
 *
 *          Wraps output at 76 columns (the standard width).
 *          End-of-data padding: '=' for the missing third byte,
 *          or '==' for the missing second and third bytes.
 *
 *          New asm idea: **bit-twiddling across byte triples**.
 *          Take 3 input bytes = 24 bits, split into four 6-bit
 *          indices, look up each in a 64-char alphabet.  First
 *          curriculum demo where a single output unit spans
 *          more than one input byte.
 */

#include "io.h"
#include "crt0.h"

static const char alpha[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int out_col = 0;

static void emit(char c) {
  print_char(c);
  out_col++;
  if (out_col == 76) {
    print_char('\n');
    out_col = 0;
  }
}

int my_main(int argc, char** argv) {
  int fd = STDIN;
  if (argc > 2) {
    print_string("usage: base64 [FILE|-]\n");
    return 1;
  }
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
    if (fd < 0) {
      print_string("base64: cannot open ");
      print_string(argv[1]);
      print_char('\n');
      return 1;
    }
  }

  unsigned char trio[3];
  int filled;
  while (1) {
    filled = 0;
    while (filled < 3) {
      char c;
      long n = os_read(fd, &c, 1);
      if (n <= 0) break;
      trio[filled++] = (unsigned char)c;
    }
    if (filled == 0) break;

    unsigned int b0 = trio[0];
    unsigned int b1 = (filled > 1) ? trio[1] : 0;
    unsigned int b2 = (filled > 2) ? trio[2] : 0;

    emit(alpha[(b0 >> 2) & 0x3f]);
    emit(alpha[((b0 << 4) | (b1 >> 4)) & 0x3f]);
    if (filled > 1)
      emit(alpha[((b1 << 2) | (b2 >> 6)) & 0x3f]);
    else
      emit('=');
    if (filled > 2)
      emit(alpha[b2 & 0x3f]);
    else
      emit('=');

    if (filled < 3) break;
  }
  if (out_col > 0) print_char('\n');

  if (fd != STDIN) os_close(fd);
  return 0;
}
