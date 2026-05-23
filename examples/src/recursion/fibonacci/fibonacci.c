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

/* PURPOSE: Compute Fibonacci(N) two ways — iterative and
 *          recursive — and print both results, so the reader can
 *          compare instruction count AND call-tree shape between
 *          equivalent algorithms.
 *
 *          Second demo from PLAN-cs-demos.md.  N comes from
 *          argv[1].  Output is one line per implementation:
 *
 *              iter: <fib(N)>
 *              rec:  <fib(N)>
 *
 *          On the asm side this introduces a NEW idea: a
 *          stack-frame-per-recursive-call.  Earlier demos that
 *          had subroutines (subrountines, 08-testStrings,
 *          cksum, factorial) all got by with a single
 *          callee-save register holding `$ra` and at most a
 *          couple of locals across one `jal`.  fib_rec breaks
 *          that pattern because EVERY recursive call would
 *          overwrite the same `$s` register, so each call needs
 *          its OWN little frame on the stack: saved `$ra`,
 *          saved `n`, and the intermediate `fib(n-1)` that has
 *          to survive across the second recursive call.
 *
 *          See fibonacci.asm for the frame layout and the
 *          symbol-table commentary on why $s* alone isn't
 *          enough this time.
 *
 *          Overflow: fib(46) doesn't fit in a signed 32-bit
 *          int, and print_int will render that as a negative
 *          number.  That's the lesson — the same algorithm gives
 *          the same garbage in both implementations, since the
 *          overflow comes from the type, not the algorithm.
 *
 *          Invocation:
 *              spimulator -f fibonacci.asm 10
 *              ./fibonacci-1 10                       # on Linux
 */

#include "io.h"
#include "crt0.h" /* provides _start; calls my_main(argc, argv) */

static int fib_iter(int n) {
  int a = 0, b = 1;
  for (int i = 0; i < n; i++) {
    int t = a + b;
    a = b;
    b = t;
  }
  return a;
}

static int fib_rec(int n) {
  if (n < 2) return n;
  return fib_rec(n - 1) + fib_rec(n - 2);
}

int my_main(int argc, char** argv) {
  if (argc != 2) {
    print_string("usage: fibonacci N\n");
    return 1;
  }
  int n = parse_int(argv[1]);

  print_string("iter: ");
  print_int(fib_iter(n));
  print_char('\n');

  print_string("rec:  ");
  print_int(fib_rec(n));
  print_char('\n');

  return 0;
}
