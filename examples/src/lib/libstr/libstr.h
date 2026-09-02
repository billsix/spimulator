/* libstr.h — string and memory primitives (naive teaching ports).
 *
 * Adapted from musl libc (src/string/).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * A teaching libc: each function trades musl's alignment-aware,
 * word-at-a-time and Two-Way tricks for the naive byte loop that
 * reads on one screen and shows the underlying idea.  The paired
 * libstr.asm mirrors the C algorithm-for-algorithm, so the C
 * binary is the oracle that generates the golden and the asm
 * reproduces it byte-for-byte (see libstr-demo/).
 *
 * This is library #2 of three (libctype -> libstr -> libstdlib).
 * libctype was all one-line range checks; libstr introduces the
 * NUL-sentinel byte loop that strlen / strcmp / strcpy / strchr
 * all vary on, and the count-driven memory loop of memcpy /
 * memset / memmove.
 *
 * Calling-convention contract (every function in this file):
 *   $a0..$a3 — inputs, in C-call order
 *   $v0      — return value (a pointer or an int)
 *   $s*      — preserved (callee-save; none are touched)
 *   $t*      — clobbered freely (caller-save)
 *   $ra      — preserved; every function is a leaf (no jal inside)
 *
 * Behavior matches C99 <string.h> for the ASCII inputs the demo
 * exercises.  As in musl, `char` arguments to the byte-search
 * functions are compared as raw bytes.
 */

#ifndef LIBSTR_H
#define LIBSTR_H

#include <stddef.h> /* size_t, NULL */

size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
char* strchr(const char* s, int c);
void* memchr(const void* s, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);

#endif /* LIBSTR_H */
