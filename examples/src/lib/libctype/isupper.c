/* isupper(c) — 1 if c is an ASCII uppercase letter, else 0.
 *
 * Adapted from musl libc src/ctype/isupper.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Single unsigned range check, same shape as isdigit.
 */

#include "libctype.h"

int isupper(int c) { return (unsigned)c - 'A' < 26; }
