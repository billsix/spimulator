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

/* PURPOSE: A simplified port of `strings` — print every run of 4 or
 *          more printable ASCII bytes (0x20 space .. 0x7e tilde)
 *          found on stdin, one run per line.  Anything else (control
 *          bytes, high-bit bytes, and EOF) terminates a run.
 *
 *          The interesting part is that runs can be arbitrarily long,
 *          yet no line buffer is needed: only the first MINRUN bytes
 *          of a candidate run are held back (we don't yet know if the
 *          run will qualify).  The moment the run reaches MINRUN the
 *          held bytes are flushed and every further byte streams
 *          straight through.
 *
 *          A real `strings` reads its minimum from `-n N` and also
 *          treats tab as printable; both are omitted here.
 */

#include "io.h"

#define MINRUN 4

__attribute__((noreturn)) void _start(void) {
  char pending[MINRUN]; /* first bytes of a not-yet-qualified run */
  int run;              /* length of the current printable run */
  int i;
  int ch;

  run = 0;
  ch = read_char();
  while (ch != -1) {
    if (ch >= ' ' && ch <= '~') {
      if (run < MINRUN) pending[run] = (char)ch;
      run = run + 1;
      if (run == MINRUN) {
        for (i = 0; i < MINRUN; i = i + 1) print_char(pending[i]);
      } else if (run > MINRUN) {
        print_char((char)ch);
      }
    } else {
      if (run >= MINRUN) print_char('\n');
      run = 0;
    }
    ch = read_char();
  }
  if (run >= MINRUN) print_char('\n');
  os_exit(0);
}
