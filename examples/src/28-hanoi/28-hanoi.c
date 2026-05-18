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

/* PURPOSE: Towers of Hanoi.  Print the move sequence that takes
 *          a tower of N disks from peg A to peg C using peg B
 *          as auxiliary, following the classical recursive
 *          three-line algorithm.
 *
 *          Sixth demo from PLAN-cs-demos.md.  Output is 2^N - 1
 *          lines:
 *
 *              Move disk from A to C
 *              Move disk from A to B
 *              ...
 *
 *          For N=3 that's 7 lines; for N=5 it's 31; for N=10
 *          it's 1023 — exponential by construction, since each
 *          recursive level doubles the work plus one.
 *
 *          On the asm side this is the deepest stack-frame demo
 *          in the curriculum so far.  Each recursive call needs
 *          to preserve FOUR arguments (n, src, dst, tmp) plus
 *          $ra, because:
 *
 *             - between the two recursive calls, we print using
 *               src and dst, so those must survive the first call
 *             - the second call uses (n-1, tmp, dst, src) — every
 *               one of those came from the original call's
 *               arguments, and ALL of them were clobbered by the
 *               first recursive call
 *
 *          So the per-call frame holds n, src, dst, tmp, and $ra
 *          — 5 cells, 20 bytes.  27-fibonacci introduced the
 *          stack-frame-per-call pattern with 1 saved value plus
 *          $ra; this demo extends it to 4 saved values.
 *
 *          Recursion depth = N.  Spim's stack is 64 KiB by
 *          default; at 20 bytes per frame we could go ~3000
 *          deep — well past anything humans would type.
 *
 *          Invocation:
 *              spimulator -f 28-hanoi.asm 3
 *              ./28-hanoi-1 3                            # on Linux
 */

#include "io.h"
#include "crt0.h"   /* provides _start; calls my_main(argc, argv) */

static void hanoi(int n, char src, char dst, char tmp) {
  if (n == 0) return;
  hanoi(n - 1, src, tmp, dst);
  print_string("Move disk from ");
  print_char(src);
  print_string(" to ");
  print_char(dst);
  print_char('\n');
  hanoi(n - 1, tmp, dst, src);
}

int my_main(int argc, char **argv) {
  if (argc != 2) {
    print_string("usage: hanoi N\n");
    return 1;
  }
  int n = parse_int(argv[1]);
  hanoi(n, 'A', 'C', 'B');
  return 0;
}
