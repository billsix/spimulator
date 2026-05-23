/* libctype.c — ASCII character classification + case conversion.
 *
 * Adapted from musl libc src/ctype/ (one function per musl source
 * file, consolidated here into one file to mirror the bundled
 * libctype.asm).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Each function below corresponds to the like-named .c file in
 * musl/src/ctype/.  musl's locale-aware variants (`__isdigit_l`
 * etc.) and `weak_alias` glue are dropped — this library is
 * C-locale only.
 */

#include "libctype.h"

/* isdigit — 1 if c in '0'..'9' else 0.
 *
 * Trick: ((unsigned)c - '0') is < 10 only when c is in '0'..'9'.
 * For any c < '0' the subtraction wraps to a huge unsigned value
 * (well above 10).  One subtraction + one unsigned compare. */
int isdigit(int c) { return (unsigned)c - '0' < 10; }

/* isalpha — 1 if c in 'A'..'Z' or 'a'..'z' else 0.
 *
 * Trick: ((unsigned)c | 32) folds uppercase to lowercase (bit 5
 * is the only difference between 'A'=0x41 and 'a'=0x61).  Then
 * one unsigned range check covers both cases. */
int isalpha(int c) { return ((unsigned)c | 32) - 'a' < 26; }

/* isalnum — 1 if c is a letter or digit, else 0.
 *
 * Composed via isalpha + isdigit.  The asm version inlines both
 * to stay a leaf function. */
int isalnum(int c) { return isalpha(c) || isdigit(c); }

/* isupper — 1 if c in 'A'..'Z' else 0.  Single unsigned range
 * check, same shape as isdigit. */
int isupper(int c) { return (unsigned)c - 'A' < 26; }

/* islower — 1 if c in 'a'..'z' else 0.  Same shape as isupper. */
int islower(int c) { return (unsigned)c - 'a' < 26; }

/* isspace — 1 if c is C-locale whitespace, else 0.
 *
 * Whitespace set is { ' ', '\t', '\n', '\v', '\f', '\r' }.
 * '\t' through '\r' are 5 consecutive bytes (0x09..0x0d) so a
 * single unsigned range check handles them; ' ' (0x20) is the
 * odd one out and needs an explicit equality test. */
int isspace(int c) { return c == ' ' || (unsigned)c - '\t' < 5; }

/* toupper — uppercase if c is lowercase, else c unchanged.
 *
 * `c & 0x5f` clears bit 5 (the only bit difference between 'a'
 * and 'A').  Guarded by islower so non-letters pass through. */
int toupper(int c) {
  if (islower(c)) return c & 0x5f;
  return c;
}

/* tolower — lowercase if c is uppercase, else c unchanged.
 *
 * `c | 32` sets bit 5.  Guarded by isupper so non-letters pass
 * through. */
int tolower(int c) {
  if (isupper(c)) return c | 32;
  return c;
}
