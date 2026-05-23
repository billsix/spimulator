/* PURPOSE: Write a null-terminated string to stdout.
 *
 * Counts the bytes up to the NUL, then makes one os_write
 * syscall.  In a hand-written assembly translation this is the
 * equivalent of `li $v0, 4 ; la $a0, str ; syscall` on spim.
 */

#include "io.h"

void print_string(const char* s) {
  int len = count_chars(s);
  os_write(STDOUT, s, len);
}
