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

/* PURPOSE: Unit tests for the bubble-sort core
 * (tasks/separate-io-from-algorithm-remaining-demos.md).  bubble_sort(int*,
 * int) is already a PURE function (the finished bubble-sort calls it on
 * stdin-read ints; bubble-sort-1-fixed calls it on a fixed array), so it needs
 * no new I/O-separated whole-program variant — only this unit-test battery.  It
 * sorts a table of HARDCODED arrays and prints "<input> => <sorted>" for each,
 * no stdin.
 *
 *     $ spimulator -f bubble-sort-core-test.asm
 *     5 3 8 1 9 2 7 => 1 2 3 5 7 8 9
 *     5 4 3 2 1 => 1 2 3 4 5
 *     ...
 *
 * Golden-tested: run-demo.sh pins the output, so a regression in the sort turns
 * it red.  The battery exercises an unsorted array, an already-sorted one, a
 * reversed one, duplicates, a single element, the empty array, and negatives.
 * bubble_sort below is bubble-sort's, verbatim.  Mirrored in
 * bubble-sort-core-test.asm.
 */

#include "io.h"
#include "crt0.h"

/* The pure core under test (bubble-sort's bubble_sort, verbatim). */
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

/* Print n ints space-separated (no trailing space); nothing for n == 0. */
static void print_ints(const int* a, int n) {
  for (int i = 0; i < n; i++) {
    if (i > 0) {
      print_char(' ');
    }
    print_int(a[i]);
  }
}

/* The test battery.  Arrays are mutable (bubble_sort sorts in place); the empty
 * case has length 0 so bubble_sort and print_ints touch no element. */
static int c0[] = {5, 3, 8, 1, 9, 2, 7};
static int c1[] = {1, 2, 3, 4, 5};
static int c2[] = {5, 4, 3, 2, 1};
static int c3[] = {3, 1, 3, 2, 1};
static int c4[] = {42};
static int c5[] = {-3, 5, -1, 0, 2};
static int* cases[] = {c0, c1, c2, c3, c4, 0, c5};
static int lens[] = {7, 5, 5, 5, 1, 0, 5};

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int case_count = (int)(sizeof(lens) / sizeof(lens[0]));
  for (int i = 0; i < case_count; i++) {
    print_ints(cases[i], lens[i]); /* the input */
    print_string(" => ");
    bubble_sort(cases[i], lens[i]);
    print_ints(cases[i], lens[i]); /* now sorted */
    print_char('\n');
  }
  return 0;
}
