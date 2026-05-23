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

/* absolute / labsolute: this library exposes these functions
 * under longer names instead of the standard C library's
 * `abs` / `labs`.  Two reasons:
 *   1. `abs` is a reserved MIPS pseudoinstruction in spim
 *      (`abs $rd, $rs`), so a hand-written .asm file can't
 *      use `abs` as a label at all.  Renaming avoids the
 *      collision without a C-vs-asm naming mismatch.
 *   2. The longer name is self-documenting for students who
 *      haven't memorized libc conventions yet.
 * Library mapping:  absolute  <->  C library's abs(int)
 *                   labsolute <->  C library's labs(long)
 */
int absolute(int x);
long labsolute(long x);

#endif
