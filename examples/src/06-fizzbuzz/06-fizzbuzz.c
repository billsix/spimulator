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

/* PURPOSE: The classic FizzBuzz interview problem.  For i in
 *          1..100: print "FizzBuzz" if i is divisible by both 3
 *          and 5, "Fizz" if only by 3, "Buzz" if only by 5,
 *          otherwise the number itself.  One result per line.
 *
 *          First demo from PLAN-cs-demos.md.  No argv, no stdin —
 *          a pure compute-and-print exercise that exercises:
 *
 *             - modulo as an algorithm primitive (becomes
 *               `div`/`mfhi` on MIPS)
 *             - multi-way branching (an if/else-if chain in C
 *               becomes a cascade of conditional branches in asm)
 *             - mixed-format output: a string OR an int on each
 *               line, decided at run time
 *
 *          The "is divisible by 15" test comes first; checking
 *          15 short-circuits the 3-and-5 case (only one
 *          comparison instead of two on the FizzBuzz lines).
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  for (int i = 1; i <= 100; i++) {
    if (i % 15 == 0)
      print_string("FizzBuzz");
    else if (i % 3 == 0)
      print_string("Fizz");
    else if (i % 5 == 0)
      print_string("Buzz");
    else
      print_int(i);
    print_char('\n');
  }
  os_exit(0);
}
