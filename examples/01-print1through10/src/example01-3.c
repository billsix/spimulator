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
#include <stdio.h>
#include <stdlib.h>

#include "platformabstraction.h"

/*struct main_stack_frame {
  int32_t argc;
  char ** argv;
  int32_t i;
  int32_t return_value;
  };*/

#define MAIN_STACK_FRAME_OFFSET_TO_ARGC 0
#define MAIN_STACK_FRAME_OFFSET_TO_ARGV                                        \
  (MAIN_STACK_FRAME_OFFSET_TO_ARGC + SIZE_OF_INT32_T)
#define MAIN_STACK_FRAME_OFFSET_TO_I                                           \
  (MAIN_STACK_FRAME_OFFSET_TO_ARGV + SIZE_OF_ADDRESS_OF_BYTE)
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE                                \
  (MAIN_STACK_FRAME_OFFSET_TO_I + SIZE_OF_INT32_T)
#define SIZE_OF_MAIN_STACK_FRAME                                               \
  = (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {

  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  // main's stack frame
  // set i and return value
  {
    // set argc
    {
      xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_ARGC,
              /*src*/ &argc,
              /*numberOfBytes*/ SIZE_OF_INT32_T);
    }
    // set argv
    {
      xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_ARGC,
              /*src*/ &argc,
              /*numberOfBytes*/ SIZE_OF_INT32_T);
    }

    // set i
    {
      int32_t toCopy = 0;
      xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
              /*src*/ &toCopy,
              /*numberOfBytes*/ SIZE_OF_INT32_T);
    }
    // set return value
    {
      int toCopy = EXIT_SUCCESS;
      xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
              /*src*/ &toCopy,
              /*numberOfBytes*/ sizeof(int));
    }
  }

beginningOfLoop : {
  int32_t i;
  xmemcpy(/*dest*/ &i,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
          SIZE_OF_INT32_T) if (!(i <= 10)) goto endOfLoop;
}
loopBody : {
  int32_t i;
  xmemcpy(/*dest*/ &i,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
          SIZE_OF_INT32_T);
  print_int(i);
}
  print_char('\n');
  // increment i
  {
    int32_t i;
    xmemcpy(/*dest*/ &i,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
            SIZE_OF_INT32_T);
    int32_t incrementedI = i + 1;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
            /*src*/ &incrementedI,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  goto beginningOfLoop;
endOfLoop:
  return main_stack_frame.return_value;
}
