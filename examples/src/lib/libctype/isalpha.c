/* isalpha(c) — 1 if c is an ASCII letter (A-Z or a-z), else 0.
 *
 * Adapted from musl libc src/ctype/isalpha.c.
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Trick: ((unsigned)c | 32) folds uppercase to lowercase
 * (the only bit difference between 'A'=0x41 and 'a'=0x61 is bit
 * 5).  Then a single unsigned range check covers both cases.
 */

#include "libctype.h"

int isalpha(int c) { return ((unsigned)c | 32) - 'a' < 26; }
