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

struct main_stack_frame {
  int32_t result1;
  int32_t result2;
  int32_t return_value;
};

struct mxPlusB_stack_frame {
  int32_t m;
  int32_t x;
  int32_t b;

  int32_t *address_of_return_value;
  void *
      address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates;
  void *stack_frame_of_caller;
};

int main(int32_t argc, char *argv[]) {
  void *current_stack_frame;

  goto main_label;

mxPlusBLabel: {
  struct mxPlusB_stack_frame current_mxplusb_stack_frame =
      *((struct mxPlusB_stack_frame *)current_stack_frame);

  *(current_mxplusb_stack_frame.address_of_return_value) =
      current_mxplusb_stack_frame.m * current_mxplusb_stack_frame.x +
      current_mxplusb_stack_frame.b;

  void *returnAddress =
      current_mxplusb_stack_frame
          .address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates;
  current_stack_frame = current_mxplusb_stack_frame.stack_frame_of_caller;
  goto *returnAddress;
}
main_label: {
  struct main_stack_frame msf = {
      .result1 = 0,
      .result2 = 0,
      .return_value = 0,
  };
  struct main_stack_frame *main_stack_frame = &msf;

  current_stack_frame = (void *)main_stack_frame;

  struct mxPlusB_stack_frame firstMxPlusBCall = {
      .m = 1,
      .x = 2,
      .b = 3,
      .address_of_return_value = &main_stack_frame->result1,
      .address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates =
          &&continueMainPt1,
      .stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&firstMxPlusBCall;
  goto mxPlusBLabel;
continueMainPt1:

  operating_system_print_int(main_stack_frame->result1);
  operating_system_print_string("\n");

  struct mxPlusB_stack_frame secondMxPlusBCall = {
      .m = 4,
      .x = 5,
      .b = 6,
      .address_of_return_value = &main_stack_frame->result2,
      .address_of_instruction_of_caller_to_be_executed_after_mxPlusB_stack_frame_terminates =
          &&continueMainPt2,
      .stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&secondMxPlusBCall;
  goto mxPlusBLabel;

continueMainPt2:
  operating_system_print_int(main_stack_frame->result2);
  operating_system_print_string("\n");

  exit(EXIT_SUCCESS);
}
}
