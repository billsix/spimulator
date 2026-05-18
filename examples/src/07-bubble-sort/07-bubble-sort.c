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

/* PURPOSE: Bubble sort a hardcoded 10-element int array in
 *          place.  Print the array before and after so the
 *          before/after comparison is one diff:
 *
 *              before: 9 4 1 7 6 2 8 3 5 0
 *              after:  0 1 2 3 4 5 6 7 8 9
 *
 *          Fourth demo from PLAN-cs-demos.md.  Introduces three
 *          new ideas on the asm side:
 *
 *             - **nested loops** — first demo with a `for`
 *               inside another `for`.  Each loop carries its own
 *               index in its own register.
 *             - **in-place mutation of a `.word`-initialised
 *               .data array** — earlier demos wrote into
 *               `.space`-allocated scratch buffers (12-rev's
 *               line buffer, 25-tee's fd array); this demo
 *               mutates a fully-initialised `.data` array.
 *               The `.word` directive's job is to lay the
 *               initial state down; nothing about the directive
 *               makes the result read-only.
 *             - **swap via a temp register** — `int t = a[j];
 *               a[j] = a[j+1]; a[j+1] = t;` becomes a load,
 *               another load, then two stores in the opposite
 *               direction.  The temp lives in a $t-reg between
 *               the two loads.
 *
 *          Algorithm: naive O(N²).  The lesson here is the asm,
 *          not the algorithm — bubble sort is the simplest
 *          comparison sort to write out by hand, and that's why
 *          it shows up in textbooks despite being slow.  When
 *          students later want to compare sort algorithms by
 *          instruction count, they can replace this demo's
 *          inner with insertion / selection / quicksort and
 *          spim's `-explain` will count the instructions for
 *          them.
 *
 *          Invocation:
 *              spimulator -f 07-bubble-sort.asm
 *              ./07-bubble-sort-1                      # on Linux
 */

#include "io.h"

#define N 10

static int data[N] = {9, 4, 1, 7, 6, 2, 8, 3, 5, 0};

static void bubble_sort(int *a, int n) {
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

static void print_array(int *a, int n) {
  for (int i = 0; i < n; i++) {
    print_int(a[i]);
    print_char(' ');
  }
  print_char('\n');
}

__attribute__((noreturn)) void _start(void) {
  print_string("before: ");
  print_array(data, N);
  bubble_sort(data, N);
  print_string("after:  ");
  print_array(data, N);
  os_exit(0);
}
