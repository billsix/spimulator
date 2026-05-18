// Copyright (c) 2021-2026 William Emerison Six
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/* PURPOSE: Sieve of Eratosthenes — find all primes up to 100
 *          and print them on a single line.
 *
 *              2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59
 *              61 67 71 73 79 83 89 97
 *
 *          (25 primes; output here line-wrapped for the
 *          comment, but actually one space-separated line.)
 *
 *          Eighth demo from PLAN-cs-demos.md.  The "240 BC
 *          algorithm" is still the best teaching demo for the
 *          idea of `.data` as **working memory** rather than as
 *          constants:
 *
 *             - We allocate a 101-byte flag array (sieve[0..100]).
 *             - For each i from 2 upward, if sieve[i] is still
 *               zero (i.e. i is prime), mark every multiple of
 *               i starting from i*i as composite.
 *             - Walk the array one more time and print every
 *               cell whose flag is still zero.
 *
 *          The outer loop stops at i*i > 100 because any
 *          composite below 101 must have a prime factor at most
 *          √100 = 10; once we've sieved with all primes up to
 *          10, no remaining unmarked cell can be composite.
 *
 *          The inner marking loop starts at i*i (not 2*i)
 *          because any smaller multiple of i was already
 *          marked by a smaller prime.
 *
 *          New on the asm side: **byte-granularity array access
 *          via `lb`/`sb`** (vs `lw`/`sw` for the 32-bit demos).
 *          The stride drops from 4 to 1 — for `i`'th element,
 *          the address is just `base + i`, no `sll by 2` needed.
 *
 *          Invocation:
 *              spimulator -f 09-sieve.asm
 *              ./09-sieve-1                            # on Linux
 */

#include "io.h"

#define LIMIT 100

/* 101 bytes; static globals are zero-initialised in C, so
 * the initial state is "no number is yet marked composite."
 * The asm port uses `.space 101` for the same effect (BSS). */
static unsigned char sieve[LIMIT + 1];

__attribute__((noreturn)) void _start(void) {
  /* Mark composites. */
  for (int i = 2; i * i <= LIMIT; i++) {
    if (!sieve[i]) {
      for (int j = i * i; j <= LIMIT; j += i) {
        sieve[j] = 1;
      }
    }
  }
  /* Print survivors. */
  for (int i = 2; i <= LIMIT; i++) {
    if (!sieve[i]) {
      print_int(i);
      print_char(' ');
    }
  }
  print_char('\n');
  os_exit(0);
}
