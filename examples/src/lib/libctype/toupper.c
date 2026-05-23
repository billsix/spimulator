/* toupper(c) — uppercase ASCII letter if c is lowercase; else c
 * unchanged.
 *
 * Adapted from musl libc src/ctype/toupper.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * `c & 0x5f` clears bit 5, which is exactly the difference
 * between 'a'=0x61 and 'A'=0x41 (and similarly for the rest of
 * a..z vs A..Z).  The islower guard is needed because the mask
 * would also corrupt non-letter bytes (e.g. '0'=0x30 & 0x5f =
 * 0x10, which is wrong).
 */

#include "libctype.h"

int toupper(int c) {
  if (islower(c)) return c & 0x5f;
  return c;
}
