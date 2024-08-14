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

/* Purpose: */
/* Print out the numbers 1 through 10, each on their own line */

#include <inttypes.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

/*struct main_stack_frame {
  int32_t i;
  int32_t return_value;
  };*/

#define MAIN_STACK_FRAME_OFFSET_TO_I 0
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE \
  (MAIN_STACK_FRAME_OFFSET_TO_I + SIZE_OF_INT32_T)
#define SIZE_OF_MAIN_STACK_FRAME \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {
  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  /*
  struct main_stack_frame main_stack_frame = {.i = 0,
                                              .return_value = EXIT_SUCCESS};

   */
  {
    int32_t i_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
            /*src*/ &i_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    int return_value_in_instruction = EXIT_SUCCESS;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_value_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

beginningOfLoop: {
  /*
if (!(main_stack_frame.i <= 10))
  goto endOfLoop;
   */
  int32_t i_in_register;
  xmemcpy(/*dest*/ &i_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
          SIZE_OF_INT32_T);
  if (!(i_in_register <= 10)) goto endOfLoop;
}
loopBody: {
  //   operating_system_print_int(main_stack_frame.i);
  int32_t i_in_register;
  xmemcpy(/*dest*/ &i_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
          SIZE_OF_INT32_T);
  operating_system_print_int(i_in_register);
}
  operating_system_print_string("\n");
  {
    //   main_stack_frame.i = main_stack_frame.i + 1;
    int32_t i_in_register;
    xmemcpy(/*dest*/ &i_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
            SIZE_OF_INT32_T);
    int32_t incrementedI_in_register = i_in_register + 1;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
            /*src*/ &incrementedI_in_register,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  goto beginningOfLoop;
endOfLoop: {
  //   return main_stack_frame.return_value;
  int32_t return_value_in_register;
  xmemcpy(/*dest*/ &return_value_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
          SIZE_OF_INT32_T);
  frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;
  return return_value_in_register;
}
}
