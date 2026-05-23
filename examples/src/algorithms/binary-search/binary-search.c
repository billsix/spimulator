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

/* PURPOSE: Search a hardcoded sorted 10-element int array for a
 *          target value (read from argv[1]) two ways:
 *
 *             - linear_search: walk from index 0, O(N)
 *             - binary_search: divide and conquer, O(log N)
 *
 *          Print each result on its own line:
 *
 *              linear: 6
 *              binary: 6
 *
 *          On a "not found" result, prints "not found" instead
 *          of the index.
 *
 *          Third demo from PLAN-cs-demos.md.  Introduces two
 *          ideas on the asm side that haven't shown up yet:
 *
 *             - **`.data` arrays via `.word`** — the directive
 *               that lays out N 32-bit cells contiguously and
 *               gives them a single label.
 *             - **strided indexed access** — `&array[i]` becomes
 *               `base + i * 4` on a 32-bit-int array, which is
 *               `sll $t, $i, 2` then `add` on MIPS.
 *
 *          The divide-and-conquer pattern itself is the second
 *          half of the lesson: binary search halves the search
 *          window each iteration, which on MIPS is one `srl`
 *          (logical right shift by 1) rather than a `div` —
 *          a small but tangible "the compiler knows tricks the
 *          CPU has built in" moment.
 *
 *          Invocation:
 *              spimulator -f binary-search.asm 41   # found at index 6
 *              spimulator -f binary-search.asm 50   # not found
 *              ./binary-search-1 41                 # on Linux
 */

#include "io.h"
#include "crt0.h" /* provides _start; calls my_main(argc, argv) */

static const int data[] = {3, 7, 11, 19, 23, 31, 41, 53, 67, 71};
#define DATA_LEN ((int)(sizeof(data) / sizeof(data[0])))

static int linear_search(int target) {
  for (int i = 0; i < DATA_LEN; i++) {
    if (data[i] == target) return i;
  }
  return -1;
}

static int binary_search(int target) {
  int lo = 0, hi = DATA_LEN - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (data[mid] == target) return mid;
    if (data[mid] < target)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

static void print_result(const char* prefix, int idx) {
  print_string(prefix);
  if (idx < 0)
    print_string("not found");
  else
    print_int(idx);
  print_char('\n');
}

int my_main(int argc, char** argv) {
  if (argc != 2) {
    print_string("usage: binary-search TARGET\n");
    return 1;
  }
  int target = parse_int(argv[1]);
  print_result("linear: ", linear_search(target));
  print_result("binary: ", binary_search(target));
  return 0;
}
