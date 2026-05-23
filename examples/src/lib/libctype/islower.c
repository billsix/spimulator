/* islower(c) — 1 if c is an ASCII lowercase letter, else 0.
 *
 * Adapted from musl libc src/ctype/islower.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Single unsigned range check, same shape as isupper.
 */

#include "libctype.h"

int islower(int c) { return (unsigned)c - 'a' < 26; }
