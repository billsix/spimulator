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

/* PURPOSE: Read decimal integers from stdin (one per line, or
 *          whitespace-separated), bubble sort them in place, and
 *          print the sorted result one per line.
 *
 *          Same shape as `sort -n` for small inputs.  Pipe in
 *          numbers, get sorted numbers out:
 *
 *              $ printf "9\n4\n1\n7\n" | ./bubble-sort
 *              1
 *              4
 *              7
 *              9
 *
 *          New on the asm side: **reading integers from stdin
 *          with EOF detection** — uses spim's syscall 5
 *          (read_int) with the `$a3` EOF flag.  First demo to
 *          use that syscall.  See
 *          `/spimulator/tasks/eof-signaling.md` for the EOF
 *          protocol.
 *
 *          Naive O(N²) sort — the lesson is the asm, not the
 *          algorithm.  Capped at 256 input ints; larger inputs
 *          truncate.
 */

#include "io.h"
#include "crt0.h"

#define MAX_N 256

static int data[MAX_N];

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
  (void)argv;
  if (argc != 1) {
    print_string("usage: bubble-sort   (reads ints from stdin)\n");
    return 1;
  }

  int n = 0;
  int value;
  while (n < MAX_N && read_int_from_stdin(&value) == 0) {
    data[n++] = value;
  }

  bubble_sort(data, n);

  for (int i = 0; i < n; i++) {
    print_int(data[i]);
    print_char('\n');
  }
  return 0;
}
