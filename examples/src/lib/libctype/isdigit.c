/* isdigit(c) — 1 if c is an ASCII digit ('0'..'9'), else 0.
 *
 * Adapted from musl libc src/ctype/isdigit.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Trick: ((unsigned)c - '0') is < 10 only when c is in '0'..'9'.
 * For any c < '0' the subtraction wraps to a huge unsigned value
 * (well above 10).  One subtraction + one unsigned compare.
 */

#include "libctype.h"

int isdigit(int c) { return (unsigned)c - '0' < 10; }
