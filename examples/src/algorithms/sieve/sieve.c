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

/* PURPOSE: Sieve of Eratosthenes — find all primes up to N
 *          and print them on a single line.
 *
 *          Invocation:
 *              sieve            -> up to 100 (default)
 *              sieve N          -> up to N
 *
 *          Demonstrates **dynamic memory allocation via brk**:
 *          we need a flag byte for each integer in 0..N, and N
 *          is determined at runtime.  Static allocation would
 *          either waste space (oversize the array) or cap N
 *          unnecessarily.
 *
 *          On Linux, `brk()` (or `sbrk`) grows the program's
 *          data segment.  The new pages are zero-filled by the
 *          kernel, which is exactly what the sieve needs
 *          ("no number is yet marked composite").
 *
 *          On the asm side: syscall 9 (sbrk).  Adds the
 *          requested byte count to the data segment top and
 *          returns the PREVIOUS top in $v0 — that's the base
 *          address of the newly-allocated region.
 */

#include "io.h"
#include "crt0.h"

#define DEFAULT_LIMIT 100

int my_main(int argc, char** argv) {
  int limit = DEFAULT_LIMIT;
  if (argc == 2) {
    limit = parse_int(argv[1]);
    if (limit < 2) {
      print_string("usage: sieve [N]   (N >= 2)\n");
      return 1;
    }
  } else if (argc > 2) {
    print_string("usage: sieve [N]\n");
    return 1;
  }

  /* Allocate LIMIT+1 bytes via brk.  The kernel zero-fills new
   * pages, so the flags start at "no number is yet composite". */
  unsigned char* sieve = (unsigned char*)os_brk(0);
  os_brk(sieve + limit + 1);

  for (int i = 2; i * i <= limit; i++) {
    if (!sieve[i]) {
      for (int j = i * i; j <= limit; j += i) {
        sieve[j] = 1;
      }
    }
  }
  for (int i = 2; i <= limit; i++) {
    if (!sieve[i]) {
      print_int(i);
      print_char(' ');
    }
  }
  print_char('\n');
  return 0;
}
