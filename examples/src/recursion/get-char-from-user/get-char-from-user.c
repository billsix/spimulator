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

/* PURPOSE: Get a character from the user, print out the value.
 *          Terminate on EOF (Ctrl-D in an interactive terminal,
 *          or pipe close when stdin is piped in).
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -o get-char-from-user get-char-from-user.c ../../print-char.c ../../print-int.c ../../print-string.c ../../read-char.c ../../count-chars.c
 *     $ printf 'abc\n' | ./get-char-from-user
 *
 *   each paired MIPS assembly version, in spimulator:
 *     $ printf 'abc\n' | spimulator -f get-char-from-user-1.asm
 *     $ printf 'abc\n' | spimulator -f get-char-from-user-2.asm
 *
 *   get-char-from-user-1.asm is the deliberately broken version: its
 *   misaligned stack slot raises address-error exceptions.  -2.asm
 *   is the fixed one; it prints the value before the character.
 */
/* clang-format on */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  int ch = read_char();
  while (ch != -1) {
    if (ch != '\n') {
      print_string("ch was ");
      print_char((char)ch);
      print_string(", value ");
      print_int(ch);
      print_string("\n");
    }
    ch = read_char();
  }
  os_exit(0);
}
