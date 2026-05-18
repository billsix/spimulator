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

/* PURPOSE: A port of `sbase/wc -cl` — count bytes and lines on
 *          standard input, then print:
 *
 *              <bytes> bytes, <lines> lines
 *
 *          Same shape as 31-commaAndPeriodCounter (a read loop
 *          with two counters), now with the labels of a real
 *          Unix utility.
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  int byte_count = 0;
  int line_count = 0;
  int ch = read_char();
  while (ch != -1) {                   /* -1 = EOF */
    byte_count = byte_count + 1;
    if (ch == '\n') line_count = line_count + 1;
    ch = read_char();
  }
  print_int(byte_count);
  print_string(" bytes, ");
  print_int(line_count);
  print_string(" lines\n");
  os_exit(0);
}
