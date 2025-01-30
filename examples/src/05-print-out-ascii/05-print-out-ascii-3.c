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

/*

struct main_stack_frame {
  char c;
  int32_t return_code;
};
*/

#define MAIN_STACK_FRAME_OFFSET_TO_C 0
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE \
  (MAIN_STACK_FRAME_OFFSET_TO_C +               \
   SIZE_OF_INT32_T)  // even though c is a byte, return code
                     // must be aligned on a 32 bit boundary
#define SIZE_OF_MAIN_STACK_FRAME \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {
  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  /*struct main_stack_frame main_stack_frame = {
    .c = CHAR_MIN,
    .return_code = 0
    };
  */

  {
    int32_t c_in_instruction = CHAR_MIN;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_C,
            /*src*/ &c_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    int32_t return_code_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_code_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

  {
    // operating_system_print_int(main_stack_frame.c);
    char c_in_register;
    xmemcpy(/*dest*/ &c_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_C,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    operating_system_print_int(c_in_register);
    operating_system_print_string("\n");
  }
loopBegin: {
  // main_stack_frame.c = main_stack_frame.c + 1;
  char c_in_register;
  xmemcpy(/*dest*/ &c_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_C,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  c_in_register = c_in_register + 1;

  xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_C,
          /*src*/ &c_in_register,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  operating_system_print_int(c_in_register);
  operating_system_print_string("\n");
}

  {
    //  if (main_stack_frame.c != CHAR_MAX) goto loopBegin;
    char c_in_register;
    xmemcpy(/*dest*/ &c_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_C,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    if (c_in_register != CHAR_MAX) goto loopBegin;
  }
  {
    // return main_stack_frame.return_code;
    int32_t return_code_in_register;
    xmemcpy(/*dest*/ &return_code_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;
    return return_code_in_register;
  }
}
