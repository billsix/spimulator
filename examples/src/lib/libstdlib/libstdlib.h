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

/* _Exit(status) — terminate the program immediately with the
 * given exit status visible to the parent process (the shell's
 * `$?`).  Standard C99 name (note the underscore + capital E).
 *
 * Unlike `exit()`, _Exit does NOT run atexit handlers or flush
 * stdio buffers — it goes straight to the kernel's exit syscall.
 * In this freestanding curriculum we have no stdio buffers and
 * no atexit chain, so the distinction is academic; _Exit is just
 * the right name for "exit, no cleanup."
 *
 * On the spim side this issues syscall 17 (exit2) so the host
 * shell's `$?` reflects `status`.  Syscall 10 (the plain `exit`
 * syscall in spim) ignores its argument and always exits 0 —
 * don't use it from library code.  See
 * /spimulator/tasks/unix-process-conformance.md.
 */
__attribute__((noreturn)) void _Exit(int status);

/* bsearch(key, base, nel, width, cmp) — binary search a sorted
 * array.  Returns a pointer to the matching element, or NULL.
 *
 *   key   address of the value to find
 *   base  start of the sorted array
 *   nel   number of elements
 *   width sizeof each element
 *   cmp   comparator returning <0, 0, >0 like strcmp
 *
 * Note for the asm-side reader: bsearch takes FIVE arguments,
 * but the MIPS o32 ABI passes only four in $a0..$a3.  The 5th
 * arg (cmp) is passed on the caller's stack at 16($sp) — the
 * first time in this curriculum we use the "args 5+ on stack"
 * rule.  The asm bsearch's first instruction reads it from
 * there.
 *
 * The other novelty: cmp is a function pointer.  The asm calls
 * it via `jalr $sX` rather than `jal label` — the first indirect
 * call in this curriculum.  Function pointers in libc happen
 * surprisingly often (qsort, bsearch, signal, atexit, ...);
 * jalr is the gateway. */
void *bsearch(const void *key, const void *base, unsigned nel,
              unsigned width,
              int (*cmp)(const void *, const void *));

#endif
