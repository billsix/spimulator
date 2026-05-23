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

/* PURPOSE: A port of suckless `ubase/clear` — write the terminal
 *          escape sequence to clear the screen and park the cursor
 *          in the upper-left corner.  Like helloworld, it just
 *          writes a fixed string and exits — but the bytes happen
 *          to be control codes the terminal interprets, not
 *          characters the terminal displays.
 *
 *              \033   = ESC (octal 033, hex 0x1b)
 *              [2J    = erase entire screen
 *              \033[H = move cursor to row 1, column 1
 *
 * Note: octal `\033` instead of the more common hex `\x1b` because
 * spim's `.asciiz` directive recognises `\X` and octal escapes but
 * not the lowercase `\x` form — using octal keeps the C and the
 * .asciiz literally identical.
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  print_string("\033[2J\033[H");
  os_exit(0);
}
