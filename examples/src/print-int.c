/* PURPOSE: Write the decimal representation of an int to stdout.
 *
 * No libc.  We do the digit conversion inline as an unsigned
 * loop so INT_MIN works correctly (its absolute value does not
 * fit in an int).  The companion routine `integer2string` in
 * integer-to-string.c is the book's unsigned-only version — left
 * alone here so it stays a 1:1 match with the spim assembly.
 *
 * The spim equivalent of this whole function is
 *   `li $v0, 1 ; move $a0, n ; syscall`.
 */

#include "io.h"

void print_int(int value) {
  char buf[16];
  int pos = (int)sizeof(buf); /* write digits right-to-left */
  int is_negative = 0;
  unsigned int u;

  if (value < 0) {
    is_negative = 1;
    u = (unsigned int)(-(value + 1)) + 1u; /* INT_MIN-safe abs */
  } else {
    u = (unsigned int)value;
  }

  do {
    pos--;
    buf[pos] = (char)('0' + (u % 10u));
    u /= 10u;
  } while (u != 0);

  if (is_negative) {
    pos--;
    buf[pos] = '-';
  }

  os_write(STDOUT, &buf[pos], (size_t)((int)sizeof(buf) - pos));
}
