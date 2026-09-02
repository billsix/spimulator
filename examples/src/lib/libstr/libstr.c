/* libstr.c — naive string and memory primitives.
 *
 * Adapted from musl libc src/string/ (one function per musl
 * source file, consolidated here into one file to mirror the
 * bundled libstr.asm).
 *   musl:    https://musl.libc.org/
 *   License: MIT — see LICENSE-musl in this directory.
 *
 * Each function is the naive byte-at-a-time version — musl's
 * production code does word-at-a-time copies, alignment
 * shuffling, and (for strstr, which we don't port) Two-Way
 * matching.  Those are faster but bury the idea; the whole point
 * of a teaching library is that the C here and the MIPS in
 * libstr.asm are the SAME small algorithm you can hold in your
 * head at once.
 */

#include "libstr.h"

/* strlen — count bytes up to (not including) the NUL sentinel.
 *
 * THE foundational byte loop: walk a second pointer forward until
 * it lands on the terminator, then the distance is the length. */
size_t strlen(const char* s) {
  const char* p = s;
  while (*p) {
    p++;
  }
  return (size_t)(p - s);
}

/* strcmp — three-way compare of two NUL-terminated strings.
 *
 * Walk both in lockstep while the bytes match and the left isn't
 * NUL.  The loop exits on the first differing byte OR at a shared
 * NUL; either way the byte difference is the answer.  Casting to
 * unsigned char makes the sign well-defined for high bytes.  Most
 * students reach for `if (a<b) return -1; ...`; musl's single
 * subtraction is shorter and returns the same sign. */
int strcmp(const char* a, const char* b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

/* strncmp — strcmp bounded by a byte count.
 *
 * Same lockstep loop, plus a counter: stop after n bytes and
 * report equal.  If we run off the count with everything matched,
 * the strings are equal for the compared prefix. */
int strncmp(const char* a, const char* b, size_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

/* strcpy — copy src (including its NUL) into dst; return dst.
 *
 * The byte loop with the copy done inside the condition: the
 * assignment's value is the byte just written, so the loop stops
 * right after copying the NUL. */
char* strcpy(char* dst, const char* src) {
  char* d = dst;
  while ((*d++ = *src++)) {
  }
  return dst;
}

/* strncpy — copy at most n bytes; NUL-pad the remainder.
 *
 * Two phases: copy while there is source and budget, then fill
 * the rest of the n bytes with NUL.  The gotcha strncpy is famous
 * for: if src is n bytes or longer, dst gets NO terminator. */
char* strncpy(char* dst, const char* src, size_t n) {
  char* d = dst;
  while (n && *src) {
    *d++ = *src++;
    n--;
  }
  while (n) {
    *d++ = '\0';
    n--;
  }
  return dst;
}

/* strchr — first occurrence of c in s, or NULL; the NUL matches.
 *
 * Byte search that returns a pointer instead of a count.  Like
 * musl, searching for '\0' returns the address of the
 * terminator rather than NULL. */
char* strchr(const char* s, int c) {
  char ch = (char)c;
  while (*s) {
    if (*s == ch) {
      return (char*)s;
    }
    s++;
  }
  if (ch == '\0') {
    return (char*)s;
  }
  return NULL;
}

/* memchr — first byte equal to c within the first n bytes of s.
 *
 * strchr's bounded cousin: no NUL sentinel, a count governs the
 * walk, and bytes are compared as unsigned char. */
void* memchr(const void* s, int c, size_t n) {
  const unsigned char* p = s;
  unsigned char ch = (unsigned char)c;
  while (n) {
    if (*p == ch) {
      return (void*)p;
    }
    p++;
    n--;
  }
  return NULL;
}

/* memcpy — copy n bytes from src to dst; return dst.
 *
 * The count-driven copy loop.  Undefined if the regions overlap
 * — that is exactly what memmove exists to handle. */
void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = dst;
  const unsigned char* s = src;
  while (n) {
    *d++ = *s++;
    n--;
  }
  return dst;
}

/* memset — write byte c into the first n bytes of s; return s.
 *
 * The count-driven fill loop — the zero-init/clear primitive. */
void* memset(void* s, int c, size_t n) {
  unsigned char* p = s;
  unsigned char v = (unsigned char)c;
  while (n) {
    *p++ = v;
    n--;
  }
  return s;
}

/* memmove — like memcpy, but correct when the regions overlap.
 *
 * The direction check IS the lesson: when dst is below src, a
 * forward (low-to-high) copy is safe; when dst is above src, we
 * must copy backward (high-to-low) so a byte isn't overwritten
 * before it is read.  Get this wrong and the overlapping case
 * scrambles bytes. */
void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = dst;
  const unsigned char* s = src;
  if (d < s) {
    while (n) {
      *d++ = *s++;
      n--;
    }
  } else {
    while (n) {
      n--;
      d[n] = s[n];
    }
  }
  return dst;
}
