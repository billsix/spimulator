/* isspace(c) — 1 if c is an ASCII whitespace byte, else 0.
 *
 * Adapted from musl libc src/ctype/isspace.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * The C-locale whitespace set is { ' ', '\t', '\n', '\v', '\f',
 * '\r' }.  '\t' through '\r' are 5 consecutive bytes (0x09..0x0d),
 * so a single unsigned range check ((unsigned)c - '\t' < 5)
 * handles five of the six; ' ' (0x20) is the odd one out and
 * needs an explicit equality test.
 */

#include "libctype.h"

int isspace(int c) { return c == ' ' || (unsigned)c - '\t' < 5; }
