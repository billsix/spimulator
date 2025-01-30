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
//
//

#include <inttypes.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

/*
struct main_stack_frame {
  int32_t result1;
  int32_t result2;
  int32_t return_value;
};
*/

#define MAIN_STACK_FRAME_OFFSET_TO_RESULT_1 0
#define MAIN_STACK_FRAME_OFFSET_TO_RESULT_2 \
  (MAIN_STACK_FRAME_OFFSET_TO_RESULT_1 + SIZE_OF_INT32_T)
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE \
  (MAIN_STACK_FRAME_OFFSET_TO_RESULT_2 + SIZE_OF_INT32_T)
#define SIZE_OF_MAIN_STACK_FRAME \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

/*
struct mxPlusB_stack_frame {
  int32_t m;
  int32_t x;
  int32_t b;

  int32_t *address_of_return_value;
  void *
      address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates;
  void *stack_frame_of_caller;
};
*/

#define MXPLUSB_STACK_FRAME_OFFSET_TO_M 0
#define MXPLUSB_STACK_FRAME_OFFSET_TO_X \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_M + SIZE_OF_INT32_T)
#define MXPLUSB_STACK_FRAME_OFFSET_TO_B \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_X + SIZE_OF_INT32_T)
#define MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_VALUE \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_B + SIZE_OF_INT32_T)
#define MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_INSTRUCTION_OF_CALLER \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_VALUE +                    \
   SIZE_OF_ADDRESS_OF_BYTE)
#define MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_STACK_FRAME_OF_CALLER     \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_INSTRUCTION_OF_CALLER + \
   SIZE_OF_ADDRESS_OF_BYTE)
#define SIZE_OF_MXPLUSB_STACK_FRAME                                 \
  (MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_STACK_FRAME_OF_CALLER + \
   +SIZE_OF_ADDRESS_OF_BYTE)

int main(int32_t argc, char *argv[]) {
  goto main_label;

mxPlusBLabel: {
  // get m, x, and b, and calculate
  int32_t result_in_register;
  {
    int32_t m_in_register;
    xmemcpy(/*dest*/ &m_in_register,
            /*src*/ frame_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_M,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    int32_t x_in_register;
    xmemcpy(/*dest*/ &x_in_register,
            /*src*/ frame_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_X,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    int32_t b_in_register;
    xmemcpy(/*dest*/ &b_in_register,
            /*src*/ frame_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_B,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    int32_t m_times_x_in_register = m_in_register * x_in_register;
    result_in_register = m_times_x_in_register + b_in_register;
  }

  // put the value in the correct place
  {
    int32_t *address_of_return_value_in_register;
    xmemcpy(/*dest*/ &address_of_return_value_in_register,
            /*src*/ frame_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_VALUE,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
    xmemcpy(/*dest*/ address_of_return_value_in_register,
            /*src*/ &result_in_register,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // get
  // address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates,
  // and the stack frame.  set the stack frame, go to the address
  {
    void *returnAddress;
    xmemcpy(
        /*dest*/ &returnAddress,
        /*src*/ frame_pointer +
            MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_INSTRUCTION_OF_CALLER,
        /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
    void *stack_frame_of_caller;
    xmemcpy(/*dest*/ &stack_frame_of_caller,
            /*src*/ frame_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_STACK_FRAME_OF_CALLER,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
    frame_pointer = stack_frame_of_caller;
    goto *returnAddress;
  }
}
main_label: {
  /*
struct main_stack_frame msf = {
    .result1 = 0,
    .result2 = 0,
    .return_value = 0,
};
struct main_stack_frame *main_stack_frame = &msf;
current_stack_frame = (void *)main_stack_frame;
  */
  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;
  // set result 1
  {
    int32_t result1_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_1,
            /*src*/ &result1_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set result 2
  {
    int32_t result2_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_2,
            /*src*/ &result2_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set return value
  {
    int32_t return_value_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_value_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

  /*
  struct mxPlusB_stack_frame firstMxPlusBCall = {
      .m = 1,
      .x = 2,
      .b = 3,
      .address_of_return_value = &main_stack_frame->result1,
      .address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates
  =
          &&continueMainPt1,
      .stack_frame_of_caller = current_stack_frame};
  */

  // make room for the first call to MXPlusB
  stack_pointer = frame_pointer - SIZE_OF_MXPLUSB_STACK_FRAME;

  // set m
  {
    int32_t m_in_instruction = 1;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_M,
            /*src*/ &m_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set x
  {
    int32_t x_in_instruction = 2;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_X,
            /*src*/ &x_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set b
  {
    int32_t b_in_instruction = 3;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_B,
            /*src*/ &b_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set address of return value
  {
    void *address_of_result1_in_register =
        frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_1;
    xmemcpy(/*dest*/ stack_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_VALUE,
            /*src*/ &address_of_result1_in_register,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  // set
  // address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates
  {
    void *continueAddress = &&continueMainPt1;
    xmemcpy(
        /*dest*/ stack_pointer +
            MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_INSTRUCTION_OF_CALLER,
        /*src*/ &continueAddress,
        /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  // set address of stack frame of caller
  {
    xmemcpy(/*dest*/ stack_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_STACK_FRAME_OF_CALLER,
            /*src*/ &frame_pointer,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  frame_pointer = stack_pointer;
  goto mxPlusBLabel;
continueMainPt1:

  // print the result
  {
    int32_t result1_in_register;
    xmemcpy(/*dest*/ &result1_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_1,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
    operating_system_print_int(result1_in_register);
  }
  operating_system_print_string("\n");

  /*   struct mxPlusB_stack_frame secondMxPlusBCall = { */
  /*       .m = 4, */
  /*       .x = 5, */
  /*       .b = 6, */
  /*       .address_of_return_value = &main_stack_frame->result2, */
  /*       .address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates
   * = */
  /*           &&continueMainPt2, */
  /*       .stack_frame_of_caller = current_stack_frame}; */

  // make room for the first call to MXPlusB
  stack_pointer = frame_pointer - SIZE_OF_MXPLUSB_STACK_FRAME;

  // set m
  {
    int32_t m_in_instruction = 4;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_M,
            /*src*/ &m_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set x
  {
    int32_t x_in_instruction = 5;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_X,
            /*src*/ &x_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set b
  {
    int32_t b_in_instruction = 6;
    xmemcpy(/*dest*/ stack_pointer + MXPLUSB_STACK_FRAME_OFFSET_TO_B,
            /*src*/ &b_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set address of return value
  {
    void *address_of_result2_in_register =
        frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_1;
    xmemcpy(/*dest*/ stack_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_VALUE,
            /*src*/ &address_of_result2_in_register,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  // set
  // address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates
  {
    void *continueAddress = &&continueMainPt2;
    xmemcpy(
        /*dest*/ stack_pointer +
            MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_RETURN_INSTRUCTION_OF_CALLER,
        /*src*/ &continueAddress,
        /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  // set address of stack frame of caller
  {
    xmemcpy(/*dest*/ stack_pointer +
                MXPLUSB_STACK_FRAME_OFFSET_TO_ADDRESS_OF_STACK_FRAME_OF_CALLER,
            /*src*/ &frame_pointer,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
  }
  frame_pointer = stack_pointer;
  goto mxPlusBLabel;
continueMainPt2:

  // print the result
  {
    int32_t result2_in_register;
    xmemcpy(/*dest*/ &result2_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RESULT_1,
            /*numberOfBytes*/ SIZE_OF_ADDRESS_OF_BYTE);
    operating_system_print_int(result2_in_register);
  }
  operating_system_print_string("\n");

  frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;

  exit(EXIT_SUCCESS);
}
}
