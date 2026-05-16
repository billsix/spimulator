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

/* PURPOSE: Print the first 10 rows of Pascal's triangle.
 *
 *              1
 *              1 1
 *              1 2 1
 *              1 3 3 1
 *              1 4 6 4 1
 *              1 5 10 10 5 1
 *              1 6 15 20 15 6 1
 *              1 7 21 35 35 21 7 1
 *              1 8 28 56 70 56 28 8 1
 *              1 9 36 84 126 126 84 36 9 1
 *
 *          Seventh demo from PLAN-cs-demos.md.  No subroutines,
 *          no recursion, no argv — just three nested loops over
 *          a single 11-cell working array in .data.
 *
 *          The key trick is **in-place row update walking
 *          right-to-left**.  Each row[j] of the next row equals
 *          (current row[j] + current row[j-1]).  If you update
 *          left-to-right, you overwrite row[j-1] BEFORE the next
 *          iteration reads it.  Walking right-to-left, the
 *          values you're about to write don't depend on values
 *          you've already written, so the same array doubles as
 *          input and output for each generation.
 *
 *          The asm port leans into this: `.word` array, address
 *          arithmetic with `&row[j-1]` reached via `-4($t1)`
 *          after you've computed `&row[j]` — same offset trick
 *          as 28-bubble-sort's `4($t4)`, but going the other
 *          direction.
 *
 *          Invocation:
 *              spimulator -f 31-pascals-triangle.asm
 *              ./31-pascals-triangle-1                  # on Linux
 */

#include "io.h"

#define ROWS 10

/* 11 cells: one more than ROWS so the last row's rightmost
 * value has somewhere to land.  Initial state is [1, 0, 0, ...,
 * 0] — every row starts with a leading 1.  The right-to-left
 * update propagates that 1 outward each iteration. */
static int row[ROWS + 1] = {1};

__attribute__((noreturn)) void _start(void) {
  for (int n = 0; n < ROWS; n++) {
    /* In-place right-to-left update.  Skipped on the n=0 iter
     * since the loop condition j > 0 fails immediately. */
    for (int j = n; j > 0; j--) row[j] += row[j - 1];

    /* Print this row: row[0..n], space-separated, newline at end. */
    for (int j = 0; j <= n; j++) {
      print_int(row[j]);
      print_char(' ');
    }
    print_char('\n');
  }
  os_exit(0);
}
