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
// Test strings for equality and print out the results on stdout

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "operatingsystemfunctions.h"

struct main_stack_frame {
  char *address_of_str1;
  char *address_of_str2;
  char *address_of_str3;
  bool result_of_evaluation_ofstr12_eq;
  bool result_of_evaluation_ofstr13_eq;
  bool result_of_evaluation_ofstr23_eq;
  int32_t return_value;
  // return address
  void
      *address_of_instruction_of_caller_to_execute_after_subrountine_terminates;
};

struct str_eq_stack_frame {
  const char *address_of_s1;
  const char *address_of_s2;
  bool *address_of_value_to_return;
  // i.e. return address
  void
      *address_of_instruction_of_caller_to_execute_after_subrountine_terminates;
  struct main_stack_frame *address_of_stack_frame_of_caller;
};

int main(int argc, char *argv[]) {
  void *current_stack_frame;

  goto main_label;

str_eq_label: {
  struct str_eq_stack_frame current_str_eq_stack_frame =
      *((struct str_eq_stack_frame *)current_stack_frame);
loopBegin:
  if (*current_str_eq_stack_frame.address_of_s1 !=
      *current_str_eq_stack_frame.address_of_s2)
    goto loopEnd;
  if (*current_str_eq_stack_frame.address_of_s1 != 0) goto incrementAndContinue;
  *current_str_eq_stack_frame.address_of_value_to_return = false;
  goto str_eq_exit;
incrementAndContinue:
  current_str_eq_stack_frame.address_of_s1 =
      current_str_eq_stack_frame.address_of_s1 + 1;
  current_str_eq_stack_frame.address_of_s2 =
      current_str_eq_stack_frame.address_of_s2 + 1;
  goto loopBegin;
loopEnd:

  *current_str_eq_stack_frame.address_of_value_to_return = true;
str_eq_exit: {
  void *instruction_of_caller_to_execute_after_subrountine_terminates =
      current_str_eq_stack_frame
          .address_of_instruction_of_caller_to_execute_after_subrountine_terminates;
  current_stack_frame =
      current_str_eq_stack_frame.address_of_stack_frame_of_caller;
  goto *instruction_of_caller_to_execute_after_subrountine_terminates;
}
}
main_label: {
  struct main_stack_frame main_stack_frame = {
      .address_of_str1 = "str1",
      .address_of_str2 = "str2",
      .address_of_str3 = "str1",
      .result_of_evaluation_ofstr12_eq = false,
      .result_of_evaluation_ofstr13_eq = false,
      .result_of_evaluation_ofstr23_eq = false,
      .return_value = EXIT_SUCCESS,
      .address_of_instruction_of_caller_to_execute_after_subrountine_terminates =
          &&return_label};

  current_stack_frame = (void *)&main_stack_frame;

  struct str_eq_stack_frame first_eq_call = {
      .address_of_s1 = main_stack_frame.address_of_str1,
      .address_of_s2 = main_stack_frame.address_of_str2,
      .address_of_value_to_return =
          &main_stack_frame.result_of_evaluation_ofstr12_eq,
      .address_of_instruction_of_caller_to_execute_after_subrountine_terminates =
          &&return_point_after_first_eq_call,
      .address_of_stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&first_eq_call;
  goto str_eq_label;

return_point_after_first_eq_call: {
  operating_system_print_string("str1 compared to str2 is ");
  operating_system_print_int(((struct main_stack_frame *)current_stack_frame)
                                 ->result_of_evaluation_ofstr12_eq);
  operating_system_print_string("\n");

  struct str_eq_stack_frame second_eq_call = {
      .address_of_s1 = main_stack_frame.address_of_str1,
      .address_of_s2 = main_stack_frame.address_of_str3,
      .address_of_value_to_return =
          &main_stack_frame.result_of_evaluation_ofstr13_eq,
      .address_of_instruction_of_caller_to_execute_after_subrountine_terminates =
          &&return_point_after_second_eq_call,
      .address_of_stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&second_eq_call;
  goto str_eq_label;
}
return_point_after_second_eq_call: {
  operating_system_print_string("str1 compared to str3 is ");
  operating_system_print_int(((struct main_stack_frame *)current_stack_frame)
                                 ->result_of_evaluation_ofstr13_eq);
  operating_system_print_string("\n");

  struct str_eq_stack_frame third_eq_call = {
      .address_of_s1 = main_stack_frame.address_of_str2,
      .address_of_s2 = main_stack_frame.address_of_str3,
      .address_of_value_to_return =
          &main_stack_frame.result_of_evaluation_ofstr23_eq,
      .address_of_instruction_of_caller_to_execute_after_subrountine_terminates =
          &&return_point_after_third_eq_call,
      .address_of_stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&third_eq_call;
  goto str_eq_label;
}
return_point_after_third_eq_call:
  operating_system_print_string("str2 compared to str3 is ");
  operating_system_print_int(((struct main_stack_frame *)current_stack_frame)
                                 ->result_of_evaluation_ofstr23_eq);
  operating_system_print_string("\n");

return_label:
  return main_stack_frame.return_value;
}
}
