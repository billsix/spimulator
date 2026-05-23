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

/* PURPOSE: A port of `sbase/echo` — print the command-line
 *          arguments, separated by spaces, with a trailing
 *          newline.  Implicitly skips argv[0] (the program path).
 *
 *          First /examples demo to take argv.  On the C side it
 *          needs a small inline-asm `_start` shim to pull argc
 *          and argv off the kernel-supplied stack (Linux doesn't
 *          deliver them in registers; the C calling convention
 *          can't express that layout, so we drop to asm for the
 *          eight-line crt0).  The same pattern lives in
 *          /pgu/src/c/toupper-nomm-simplified.c and is the only
 *          arch-specific code in this tree.
 *
 *          On the spim side, the runtime ALREADY puts argc in $a0
 *          and argv in $a1 by the time main runs — so the asm is
 *          plain.  See echo.asm.
 */

#include "io.h"
#include "crt0.h" /* provides _start; calls my_main(argc, argv).
                       See crt0.h for the per-arch
                       kernel-stack -> my_main conventions. */

int my_main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    print_string(argv[i]);
    if (i + 1 < argc) print_char(' ');
  }
  print_char('\n');
  return 0;
}
