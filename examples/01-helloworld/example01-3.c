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

/*
struct main_stack_frame {
  int32_t argc;
  char** argv;
  int32_t return_value;
  };
*/

#define MAIN_STACK_FRAME_OFFSET_TO_ARGC 0
#define MAIN_STACK_FRAME_OFFSET_TO_ARGV                                        \
  (MAIN_STACK_FRAME_OFFSET_TO_ARGC + SIZE_OF_INT32_T)
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE                                \
  (MAIN_STACK_FRAME_OFFSET_TO_ARGV + SIZE_OF_INT32_T)
#define SIZE_OF_MAIN_STACK_FRAME                                               \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_ADDRESS_OF_BYTE)

int main(int argc, char *argv[]) {

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

    // set return value
    {
      int toCopy = EXIT_SUCCESS;
      xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
              /*src*/ &toCopy,
              /*numberOfBytes*/ sizeof(int));
    }
  }

  print_string("hello world\n");
  {
    int32_t return_value;
    xmemcpy(/*dest*/ &return_value,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            SIZE_OF_INT32_T);
    return return_value;
  }
}
