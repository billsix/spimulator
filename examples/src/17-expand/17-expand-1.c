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

/* PURPOSE: A simplified port of `sbase/expand` — replace every
 *          tab on stdin with as many spaces as needed to reach
 *          the next 8-column boundary.  Hard-coded 8 because spim
 *          doesn't expose argv; real `expand` takes `-t N`.
 *
 *          The loop maintains a `col` counter (the current column,
 *          starting at 0 after each newline) and emits a variable
 *          number of bytes per input byte.  That state-across-the-
 *          stream pattern is what's new here.
 */

#include "io.h"

#define TAB_WIDTH 8

__attribute__((noreturn)) void _start(void) {
  int col = 0;
  int ch = read_char();
  while (ch != -1) {
    if (ch == '\t') {
      int spaces = TAB_WIDTH - (col % TAB_WIDTH);
      for (int i = 0; i < spaces; i++)
        print_char(' ');
      col = col + spaces;
    } else if (ch == '\n') {
      print_char('\n');
      col = 0;
    } else {
      print_char((char)ch);
      col = col + 1;
    }
    ch = read_char();
  }
  os_exit(0);
}
