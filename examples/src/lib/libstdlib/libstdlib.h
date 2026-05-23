/* libstdlib.h — number conversion + program-control primitives.
 *
 * Adapted from musl libc (src/stdlib/ and src/exit/).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Teaching libc #2 of three (libctype -> libstdlib -> libstr).
 * This library is the first one that COMPOSES other libraries:
 * atoi chains into libctype's isspace + isdigit, which is why
 * the asm version of atoi has a real stack frame (saves $ra
 * across each `jal`) — the first non-leaf in this curriculum's
 * library tier.
 *
 * Calling-convention contract (every function in this file):
 *   $a0..$a3 — inputs (by C-call order)
 *   $v0      — return value
 *   $s*      — preserved (callee-save)
 *   $t*      — clobbered freely (caller-save)
 *   $ra      — preserved by the library
 *
 * Functions added incrementally as the libstdlib plan
 * (../../../../tasks/PLAN-libstdlib.md) progresses.  Currently:
 *   atoi.
 */

#ifndef LIBSTDLIB_H
#define LIBSTDLIB_H

int atoi(const char *s);
int abs(int x);
long labs(long x);

#endif
