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

/* PURPOSE: Rung 1 of the bubble-sort ladder (tasks/incremental-example-versions.md).
 * Bubble sort a FIXED in-program array and print it — just the algorithm (the
 * nested-loop in-place swap), with no input.  The finished bubble-sort (rung 2)
 * adds the separate idea of reading the integers from stdin (syscall 5 with the
 * $a3 EOF flag — the demo's "first to use that syscall").  Splitting them lets a
 * reader meet the sort on territory they already walk, before the new syscall.
 *
 *     $ spimulator -f bubble-sort-1-fixed.asm
 *     1
 *     2
 *     3
 *     5
 *     8
 *     9
 *
 * Naive O(N^2) sort; the lesson is the asm, not the algorithm.  Mirrored in
 * bubble-sort-1-fixed.asm, where the array lives in .data as a `.word` list.
 */

#include "io.h"
#include "crt0.h"

#define N 6

static int data[N] = {5, 2, 8, 1, 9, 3};

static void bubble_sort(int* a, int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1 - i; j++) {
      if (a[j] > a[j + 1]) {
        int t = a[j];
        a[j] = a[j + 1];
        a[j + 1] = t;
      }
    }
  }
}

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  bubble_sort(data, N);

  for (int i = 0; i < N; i++) {
    print_int(data[i]);
    print_char('\n');
  }
  return 0;
}
