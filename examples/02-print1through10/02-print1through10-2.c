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

/* Purpose: */
/* Print out the numbers 1 through 10, each on their own line */

#include <inttypes.h>
#include <stdlib.h>

#include "platformabstraction.h"

struct main_stack_frame {
  int32_t i;
  int32_t return_value;
};

int main(int argc, char *argv[]) {

  struct main_stack_frame msf = {.i = 0, .return_value = EXIT_SUCCESS};
  struct main_stack_frame *main_stack_frame = &msf;

beginningOfLoop:
  if (!(main_stack_frame->i <= 10))
    goto endOfLoop;
loopBody:
  print_int(main_stack_frame->i);
  print_string("\n");
  main_stack_frame->i = main_stack_frame->i + 1;
  goto beginningOfLoop;
endOfLoop:
  return main_stack_frame->return_value;
}
