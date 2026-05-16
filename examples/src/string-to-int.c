/* PURPOSE: Parse a null-terminated decimal string into a signed
 *          int.  The minimal version of libc's atoi — handles a
 *          single optional `-`, then digits, stops at the first
 *          non-digit.  No whitespace skipping, no octal/hex
 *          recognition, no error reporting.
 *
 *          Used by demos that receive numeric values via argv.
 *
 *          The spim equivalent lives as a small `atoi:` subroutine
 *          inside each demo file (matching the pgu pattern of
 *          self-contained .asm files).
 */

#include "io.h"

int parse_int(const char *s) {
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    int value = 0;
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}
