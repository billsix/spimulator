/* PURPOSE: Read one byte from stdin.
 *
 * Returns the byte as an unsigned value in 0..255, or -1 if the
 * stream is at end-of-file or os_read failed.  Matches the
 * libc getchar() contract closely enough for the demos that loop
 * "until EOF".  The spim equivalent is `li $v0, 12 ; syscall`
 * with the result then sitting in $v0.
 */

#include "io.h"

int read_char(void) {
  unsigned char c;
  long n = os_read(STDIN, &c, 1);
  if (n <= 0) {
    return -1;
  }
  return c;
}
