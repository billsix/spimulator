// Permission is hereby granted for anyone to do anything that they
// want with this material—you may freely reprint it, redistribute it,
// amend it or do whatever you like with it provided that
// you include an acknowledgement of the original authorship and copyright in
// the form of a link to this page. In doing so
// you must accept that you do so strictly on your own
// liability and that you accept any consequences with no liability whatsoever
// remaining with the original authors. If you find the material useful
// and happen to encounter one of the authors, it is unlikely
// that they will refuse offers to buy them a drink. You
// may therefore like to consider this material ‘drinkware’. (Offer void where
// prohibited by law, in which case fawning and flattery may be substituted.)

// https://publications.gbdirect.co.uk//c_book/copyright.html

// Copyright (c) 2021 William Emerison Six
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

#include "platformabstraction.h"

struct main_stack_frame {
  int32_t comma_count;
  int32_t stop_count;
  char this_char;
  int32_t return_code;
};

int main(int argc, char *argv[]) {

  struct main_stack_frame msf = {.comma_count = 0,
                                 .stop_count = 0,
                                 .this_char = 0,
                                 .return_code = EXIT_SUCCESS};
  struct main_stack_frame *main_stack_frame = &msf;

  main_stack_frame->this_char = read_char();

loopBegin:
  if (main_stack_frame->this_char == EOF)
    goto loopEnd;
  if (main_stack_frame->this_char != '.')
    goto notAPeriod;
  main_stack_frame->stop_count = main_stack_frame->stop_count + 1;
notAPeriod:
  if (main_stack_frame->this_char != ',')
    goto notAComma;
  main_stack_frame->comma_count = main_stack_frame->comma_count + 1;
notAComma:
  main_stack_frame->this_char = read_char();
  goto loopBegin;

loopEnd:

  print_int(main_stack_frame->comma_count);
  print_string(" commas, ");
  print_int(main_stack_frame->stop_count);
  print_string(" stops\n");
  return main_stack_frame->return_code;
}
