/* isalnum(c) — 1 if c is an ASCII letter or digit, else 0.
 *
 * Adapted from musl libc src/ctype/isalnum.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Composed via isalpha + isdigit.  The first library function in
 * this set that calls into other libctype functions — but it's
 * still a leaf as far as the caller is concerned because the two
 * inner calls happen tail-style (no register state to preserve
 * across them).
 */

#include "libctype.h"

int isalnum(int c) { return isalpha(c) || isdigit(c); }
