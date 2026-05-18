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

/* PURPOSE: gcd(A, B) by the Euclidean algorithm, with A and B
 *          coming from argv[1] and argv[2].  First demo with TWO
 *          numeric command-line args.
 *
 *          The algorithm — gcd(a, b) = gcd(b, a mod b) until b is
 *          zero — is two lines of C and four MIPS instructions
 *          per iteration.  Half the lesson is the algorithm;
 *          the other half is parsing two integers from argv.
 *
 *          Invocation:
 *              spimulator -f 21-gcd.asm 1071 462
 *              ./21-gcd-1 1071 462                        # on Linux
 *
 *          Output: a single decimal number on its own line.
 *
 *          Worked example: gcd(1071, 462) = 21.
 *              1071 mod 462 = 147
 *               462 mod 147 =  21
 *               147 mod  21 =   0  -> 21
 */

#include "io.h"

#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

static int gcd(int a, int b) {
  while (b != 0) {
    int t = a % b;
    a = b;
    b = t;
  }
  return a;
}

int my_main(int argc, char **argv) {
  if (argc != 3) {
    print_string("usage: gcd A B\n");
    return 1;
  }
  int a = parse_int(argv[1]);
  int b = parse_int(argv[2]);
  print_int(gcd(a, b));
  print_char('\n');
  return 0;
}
