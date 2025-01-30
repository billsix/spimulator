// Copyright (c) 2021-2024 William Emerison Six
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
//

/* Purpose */
// Print out all charaters.  Ascii are 0-127

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

struct main_stack_frame {
  char c;
  int32_t return_code;
};

int main(int argc, char *argv[]) {
  struct main_stack_frame msf = {.c = CHAR_MIN, .return_code = 0};
  struct main_stack_frame *main_stack_frame = &msf;

  operating_system_print_int(main_stack_frame->c);
  operating_system_print_string("\n");
loopBegin:
  main_stack_frame->c = main_stack_frame->c + 1;
  operating_system_print_int(main_stack_frame->c);
  operating_system_print_string("\n");
  if (main_stack_frame->c != CHAR_MAX) goto loopBegin;

  return main_stack_frame->return_code;
}
