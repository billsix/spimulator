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

/* PURPOSE: Read from stdin until EOF (Ctrl-D on Linux).  Print
 *          the number of commas and periods seen.
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  int comma_count = 0;
  int period_count = 0;
  int this_char = read_char();
  while (this_char != -1) {
    if (this_char == '.') period_count = period_count + 1;
    if (this_char == ',') comma_count = comma_count + 1;
    this_char = read_char();
  }
  print_int(comma_count);
  print_string(" commas, ");
  print_int(period_count);
  print_string(" stops\n");
  os_exit(0);
}
