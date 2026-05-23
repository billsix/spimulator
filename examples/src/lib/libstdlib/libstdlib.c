/* libstdlib.c — atoi (and, as the library grows, the rest of
 * <stdlib.h>'s teaching subset: abs, labs, bsearch, _Exit).
 *
 * Adapted from musl libc (src/stdlib/atoi.c initially).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 */

#include "libstdlib.h"
#include "libctype.h"

/* atoi(s) — parse a leading optional sign and decimal digits
 * from s; return the int value.
 *
 * Adapted from musl src/stdlib/atoi.c.
 *
 * Notes mirrored from musl's source:
 * - Skip leading whitespace (POSIX-ish; older C atoi sometimes
 *   didn't, but musl's does).
 * - Accept one optional sign char ('+' or '-').  The C switch
 *   uses fallthrough to consume the sign char in both cases.
 * - **Compute n as a negative number** to avoid overflow on
 *   INT_MIN — if you computed positively and negated at the end,
 *   "-2147483648" would overflow during the positive accumulation
 *   step.
 * - Stop at the first non-digit; no error reporting (matches the
 *   C99 spec for atoi).
 */
int atoi(const char *s) {
  int n = 0, neg = 0;
  while (isspace(*s)) s++;
  switch (*s) {
    case '-':
      neg = 1;
      /* fallthrough */
    case '+':
      s++;
  }
  while (isdigit(*s)) n = 10 * n - (*s++ - '0');
  return neg ? n : -n;
}

/* abs(x) — absolute value of an int.
 *
 * Adapted from musl src/stdlib/abs.c.
 *
 * Note on the INT_MIN edge case: abs(INT_MIN) is undefined behavior
 * in standard C because -INT_MIN can't be represented as int (the
 * positive range is one short).  In two's-complement (which spim
 * and every real MIPS use), -INT_MIN wraps back to INT_MIN, so
 * abs(-2147483648) returns -2147483648.  Same wrinkle in the asm. */
int abs(int x) { return x > 0 ? x : -x; }

/* labs(x) — absolute value of a long.
 *
 * Adapted from musl src/stdlib/labs.c.
 *
 * On MIPS32, `long` is 32 bits (same as int), so labs is
 * algorithmically and structurally identical to abs.  The asm
 * version uses a single shared body. */
long labs(long x) { return x > 0 ? x : -x; }
