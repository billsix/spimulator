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

/* PURPOSE: A port of `sbase/rev` — reverse each line of stdin.
 *
 *          Reads a line into a buffer, walks the buffer backwards
 *          while writing each byte, then emits the trailing
 *          newline.  Repeats per line until EOF.
 *
 *          The buffer is a fixed 256 bytes; lines longer than that
 *          have the excess silently dropped.  Real `rev` grows
 *          the buffer with `realloc` — beyond what this lesson is
 *          about.
 */

#include "io.h"

#define BUFSIZE 256

static char buf[BUFSIZE];

static void flush_reversed(int len) {
  for (int i = len - 1; i >= 0; i--)
    print_char(buf[i]);
  print_char('\n');
}

__attribute__((noreturn)) void _start(void) {
  int len = 0;
  int ch = read_char();
  while (ch != -1) {
    if (ch == '\n') {
      flush_reversed(len);
      len = 0;
    } else if (len < BUFSIZE) {
      buf[len] = (char)ch;
      len = len + 1;
    }
    /* else: line too long, drop char */
    ch = read_char();
  }
  if (len > 0) flush_reversed(len);   /* trailing line without \n */
  os_exit(0);
}
