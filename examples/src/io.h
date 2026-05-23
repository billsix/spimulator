/* io.h — small set of printing/reading helpers built on top of
 *        os.h's inline-asm syscall wrappers.
 *
 * Each helper is one short .c file with a transparent body — the
 * student can read the helper and see exactly which os_* syscall
 * it makes.  The mapping to a hand-written .asm version of the
 * same demo is direct:
 *
 *     print_string(s)  ->  count chars + os_write
 *     print_int(v)     ->  integer2string + count chars + os_write
 *     print_char(c)    ->  os_write of one byte
 *     read_char()      ->  os_read of one byte
 *
 * No libc.  Each demo defines its own _start() and ends with
 * os_exit(...).
 */

#ifndef IO_H
#define IO_H

#include "os.h"

int count_chars(const char* s);
void integer2string(int value, char* buffer);
int parse_int(const char* s);

void print_string(const char* s);
void print_int(int value);
void print_uint(unsigned int value);
void print_char(char c);

/* Returns 0..255 for a byte read on STDIN, or -1 on EOF/error. */
int read_char(void);

/* Read a decimal integer from STDIN.  Skips leading whitespace,
 * accepts optional leading sign.  On success, writes the parsed
 * value into *out and returns 0.  Returns -1 on EOF before any
 * digit is seen.  Matches the spim asm-side `read_int` (syscall
 * 5) which sets $a3 on EOF — same API shape. */
int read_int_from_stdin(int* out);

#endif /* IO_H */
