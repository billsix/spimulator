/* tolower(c) — lowercase ASCII letter if c is uppercase; else c
 * unchanged.
 *
 * Adapted from musl libc src/ctype/tolower.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * `c | 32` sets bit 5, which is the only bit different between
 * 'A'=0x41 and 'a'=0x61.  Guarded by isupper for the same reason
 * toupper guards on islower — the OR would also corrupt
 * non-letter bytes.
 */

#include "libctype.h"

int tolower(int c) {
  if (isupper(c)) return c | 32;
  return c;
}
