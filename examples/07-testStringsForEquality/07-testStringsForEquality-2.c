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
// Test strings for equality and print out the results on stdout

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "platformabstraction.h"

struct main_stack_frame {
  char *str1;
  char *str2;
  char *str3;
  bool result_of_evaluation_ofstr12_eq;
  bool result_of_evaluation_ofstr13_eq;
  bool result_of_evaluation_ofstr23_eq;
  int32_t return_value;
  void *whatToDoAfterProcedureCall; // i.e. return address
};

struct str_eq_stack_frame {
  const char *s1;
  const char *s2;
  bool *return_value;
  void *whatToDoAfterProcedureCall; // i.e. return address
  struct main_stack_frame *stack_frame_of_caller;
};

int main(int argc, char *argv[]) {

  void *current_stack_frame;

  goto main_label;

str_eq_label:
loopBegin:
  if (*((struct str_eq_stack_frame *)current_stack_frame)->s1 !=
      *((struct str_eq_stack_frame *)current_stack_frame)->s2)
    goto loopEnd;
  if (*((struct str_eq_stack_frame *)current_stack_frame)->s1 != 0)
    goto incrementAndContinue;
  *((struct str_eq_stack_frame *)current_stack_frame)->return_value = false;
  goto str_eq_exit;
incrementAndContinue:
  ((struct str_eq_stack_frame *)current_stack_frame)->s1 =
      ((struct str_eq_stack_frame *)current_stack_frame)->s1 + 1;
  ((struct str_eq_stack_frame *)current_stack_frame)->s2 =
      ((struct str_eq_stack_frame *)current_stack_frame)->s2 + 1;
  goto loopBegin;
loopEnd:

  *((struct str_eq_stack_frame *)current_stack_frame)->return_value = true;
str_eq_exit : {
  void *whatToDoAfterProcedureCall =
      ((struct str_eq_stack_frame *)current_stack_frame)
          ->whatToDoAfterProcedureCall;
  current_stack_frame =
      ((struct str_eq_stack_frame *)current_stack_frame)->stack_frame_of_caller;
  goto *whatToDoAfterProcedureCall;
}
main_label : {
  struct main_stack_frame main_stack_frame = {
      .str1 = "str1",
      .str2 = "str2",
      .str3 = "str1",
      .result_of_evaluation_ofstr12_eq = false,
      .result_of_evaluation_ofstr13_eq = false,
      .result_of_evaluation_ofstr23_eq = false,
      .return_value = EXIT_SUCCESS,
      .whatToDoAfterProcedureCall = &&return_label};

  current_stack_frame = (void *)&main_stack_frame;

  struct str_eq_stack_frame first_eq_call = {
      .s1 = main_stack_frame.str1,
      .s2 = main_stack_frame.str2,
      .return_value = &main_stack_frame.result_of_evaluation_ofstr12_eq,
      .whatToDoAfterProcedureCall = &&return_point_after_first_eq_call,
      .stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&first_eq_call;
  goto str_eq_label;

return_point_after_first_eq_call : {
  print_string("str1 compared to str2 is ");
  print_int(((struct main_stack_frame *)current_stack_frame)
                ->result_of_evaluation_ofstr12_eq);
  print_string("\n");

  struct str_eq_stack_frame second_eq_call = {
      .s1 = main_stack_frame.str1,
      .s2 = main_stack_frame.str3,
      .return_value = &main_stack_frame.result_of_evaluation_ofstr13_eq,
      .whatToDoAfterProcedureCall = &&return_point_after_second_eq_call,
      .stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&second_eq_call;
  goto str_eq_label;
}
return_point_after_second_eq_call : {
  print_string("str1 compared to str3 is ");
  print_int(((struct main_stack_frame *)current_stack_frame)
                ->result_of_evaluation_ofstr13_eq);
  print_string("\n");

  struct str_eq_stack_frame third_eq_call = {
      .s1 = main_stack_frame.str2,
      .s2 = main_stack_frame.str3,
      .return_value = &main_stack_frame.result_of_evaluation_ofstr23_eq,
      .whatToDoAfterProcedureCall = &&return_point_after_third_eq_call,
      .stack_frame_of_caller = current_stack_frame};

  current_stack_frame = (void *)&third_eq_call;
  goto str_eq_label;
}
return_point_after_third_eq_call:
  print_string("str2 compared to str3 is ");
  print_int(((struct main_stack_frame *)current_stack_frame)
                ->result_of_evaluation_ofstr23_eq);
  print_string("\n");

return_label:
  return main_stack_frame.return_value;
}
}
