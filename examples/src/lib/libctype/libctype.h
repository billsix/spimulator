/* libctype.h — ASCII character classification and case conversion.
 *
 * Adapted from musl libc (src/ctype/).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * A teaching libc: each function mirrors musl's pure-ASCII
 * implementation (no locale variants, no wide-char counterparts).
 * Every function is a one-liner range check, which is why this is
 * library #1 of three — establishes the "library = a bundle of
 * leaf functions you can jal into" pattern before libstr and
 * libstdlib add stack-frame discipline on top.
 *
 * Calling-convention contract (every function in this file):
 *   $a0  — input byte (low 8 bits; upper bits ignored)
 *   $v0  — return value (0/1 for classifiers; the converted byte
 *           for to_upper/to_lower)
 *   $s*  — preserved (callee-save)
 *   $t*  — clobbered freely (caller-save)
 *   $ra  — preserved; every function is a leaf (no jal inside)
 *
 * Behavior matches C99 <ctype.h> on a C locale, for argument
 * values in 0..127 plus EOF (-1).  Arguments outside that range
 * are undefined per the standard; we follow musl's "do something
 * sensible by unsigned arithmetic" interpretation.
 */

#ifndef LIBCTYPE_H
#define LIBCTYPE_H

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isspace(int c);
int toupper(int c);
int tolower(int c);

#endif
