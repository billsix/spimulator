/* SPIM S20 MIPS simulator.
   Interface to code to manipulate data segment directives.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef DATA_H
#define DATA_H

#include "spim.h"

/* Exported functions: */

void align_data(int alignment);
mem_addr current_data_pc(void);
void data_begins_at_point(mem_addr addr);
void enable_data_alignment(void);
void end_of_assembly_file(void);
void extern_directive(char* name, int size);
void increment_data_pc(int value);
void k_data_begins_at_point(mem_addr addr);
void lcomm_directive(char* name, int size);
void set_data_alignment(int);
void set_data_pc(mem_addr addr);
void set_text_pc(mem_addr addr);
void store_byte(int value);
void store_double(double* value);
void store_float(double* value);
void store_half(int value);
void store_string(char* string, int length, bool null_terminate);
void store_word(int value);
void user_kernel_data_segment(bool to_kernel);

#endif
