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
// Read from std in until EOF (Ctrl-D on Linux)
// Print out number of command and periods

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

struct main_stack_frame {
  int32_t comma_count;
  int32_t period_count;
  char this_char;
  int32_t return_code;
};

int main(int argc, char *argv[]) {
  struct main_stack_frame msf = {.comma_count = 0,
                                 .period_count = 0,
                                 .this_char = 0,
                                 .return_code = EXIT_SUCCESS};
  struct main_stack_frame *main_stack_frame = &msf;

  main_stack_frame->this_char = operating_system_read_char();

loopBegin:
  if (main_stack_frame->this_char == EOF) goto loopEnd;
  if (main_stack_frame->this_char != '.') goto notAPeriod;
  main_stack_frame->period_count = main_stack_frame->period_count + 1;
notAPeriod:
  if (main_stack_frame->this_char != ',') goto notAComma;
  main_stack_frame->comma_count = main_stack_frame->comma_count + 1;
notAComma:
  main_stack_frame->this_char = operating_system_read_char();
  goto loopBegin;

loopEnd:

  operating_system_print_int(main_stack_frame->comma_count);
  operating_system_print_string(" commas, ");
  operating_system_print_int(main_stack_frame->period_count);
  operating_system_print_string(" stops\n");
  return main_stack_frame->return_code;
}
