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

/* PURPOSE: Compute the factorial of a number passed on the
 *          command line and print the result.  Iterative —
 *          a recursive variant pairs with this as the planned
 *          21-fibonacci demo (see PLAN-cs-demos.md).
 *
 *          Invocation:
 *              spimulator -f 20-factorial.asm 5
 *              ./20-factorial-1 5                  # on Linux
 *
 *          Output: just the integer result on its own line.
 *
 *          Bounds: 12! = 479001600 fits in a uint32_t; 13! does
 *          not.  At N > 12 the result silently overflows — a
 *          teaching observation in its own right.
 *
 *          This is the first /examples demo where a numeric
 *          argv argument drives the computation.  See parse_int
 *          (in src/string-to-int.c) — the C-side atoi we just
 *          added to the io library.
 */

#include "io.h"

#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

static unsigned int factorial(int n) {
  unsigned int result = 1;
  while (n > 1) {
    result = result * (unsigned int)n;
    n = n - 1;
  }
  return result;
}

int my_main(int argc, char **argv) {
  if (argc != 2) {
    print_string("usage: factorial N\n");
    return 1;
  }
  int n = parse_int(argv[1]);
  print_uint(factorial(n));
  print_char('\n');
  return 0;
}
