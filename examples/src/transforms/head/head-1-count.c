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

/* PURPOSE: Rung 1 of the head ladder (tasks/incremental-example-versions.md).
 * The head FILTER, nothing else: copy the first 10 lines of stdin to stdout,
 * then stop.  No argv — a fixed count, stdin only.  The finished head (rung 2)
 * adds argv handling: the `-n N` flag and a FILE argument.  Splitting them lets
 * a reader meet the line-counting byte loop before the command-line parsing.
 *
 *     $ printf 'a\nb\nc\n...' | spimulator -f head-1-count.asm   (first 10 lines)
 *
 * The lesson: a stream-state filter — read bytes, echo them, count newlines,
 * stop at the limit.  Mirrored in head-1-count.asm (read_char / print_char).
 */

#include "io.h"
#include "crt0.h"

#define N 10 /* fixed line count (the finished head makes this the -n flag) */

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int line_count = 0;
  int c;
  while (line_count < N && (c = read_char()) != -1) {
    print_char((char)c);
    if (c == '\n') {
      line_count++;
    }
  }
  return 0;
}
