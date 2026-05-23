/* SPIM S20 MIPS simulator.
   Interface to misc. routines for SPIM.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef SPIM_UTILS_H
#define SPIM_UTILS_H

#include "spim.h"
#include "string-stream.h"

/* Triple containing a string and two integers.	 Used in tables
   mapping from a name to values. */

typedef struct {
  char* name;
  int value1;
  int value2;
} name_val_val;

/* Exported functions: */

void add_breakpoint(mem_addr addr);
void delete_breakpoint(mem_addr addr);
void format_data_segs(str_stream* ss);
void format_insts(str_stream* ss, mem_addr from, mem_addr to);
void format_mem(str_stream* ss, mem_addr from, mem_addr to);
void format_registers(str_stream* ss, int print_gpr_hex, int print_fpr_hex);
void initialize_registers(void);
void initialize_stack(const char* command_line);
void initialize_run_stack(int argc, char** argv);
void initialize_world(char* exception_file_names, bool print_message);
void list_breakpoints(void);
name_val_val* map_int_to_name_val_val(name_val_val tbl[], int tbl_len, int num);
name_val_val* map_string_to_name_val_val(name_val_val tbl[], int tbl_len,
                                         char* id);
[[nodiscard]] bool read_assembly_file(char* name);
[[nodiscard]] bool run_program(mem_addr pc, int steps, bool display,
                               bool cont_bkpt, bool* continuable);
mem_addr starting_address(void);
[[nodiscard]] char* str_copy(const char* str);
void write_startup_message(void);
[[nodiscard]] void* xmalloc(int);
[[nodiscard]] void* zmalloc(int);

#endif
