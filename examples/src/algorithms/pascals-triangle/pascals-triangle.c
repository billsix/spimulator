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

/* PURPOSE: Print the first N rows of Pascal's triangle.
 *          Default N=10.
 *
 *          Invocation:
 *              pascals-triangle            -> 10 rows
 *              pascals-triangle N          -> N rows (capped at 34)
 *
 *          The key trick is **in-place row update walking
 *          right-to-left**: each row[j] of the next row equals
 *          (current row[j] + current row[j-1]).  Walking right-
 *          to-left, the values you write don't depend on values
 *          you've already written.
 *
 *          Overflow: row 34 is the last that fits in int32_t
 *          (the middle entry of row 35 already overflows).  We
 *          cap N at 34.
 */

#include "io.h"
#include "crt0.h"

#define MAX_ROWS 34
#define DEFAULT_ROWS 10

/* MAX_ROWS+1 cells so the last row's rightmost value lands. */
static int row[MAX_ROWS + 1] = {1};

int my_main(int argc, char **argv) {
  int rows = DEFAULT_ROWS;
  if (argc == 2) {
    rows = parse_int(argv[1]);
    if (rows < 1 || rows > MAX_ROWS) {
      print_string("usage: pascals-triangle [N]   (1 <= N <= 34)\n");
      return 1;
    }
  } else if (argc > 2) {
    print_string("usage: pascals-triangle [N]\n");
    return 1;
  }

  for (int n = 0; n < rows; n++) {
    for (int j = n; j > 0; j--) row[j] += row[j - 1];
    for (int j = 0; j <= n; j++) {
      print_int(row[j]);
      print_char(' ');
    }
    print_char('\n');
  }
  return 0;
}
