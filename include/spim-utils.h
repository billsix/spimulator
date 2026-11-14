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
} name_value_value;

/* Sentinel value for initialize_world's exception_file_names parameter.
   When the parameter equals this pointer (by identity, not strcmp),
   initialize_world loads the exception handler from bytes baked into
   the binary via #embed instead of opening a file.  Any user-supplied
   string (env var, -exception_file argument) has a different address
   and is read from disk as before. */
extern char SPIM_DEFAULT_EXCEPTIONS_SENTINEL[];

/* Exported functions: */

void add_breakpoint(memory_address address);
void delete_breakpoint(memory_address address);
void format_data_segments(string_stream* stream);
void format_instructions(string_stream* stream, memory_address from,
                         memory_address to);
void format_memory(string_stream* stream, memory_address from,
                   memory_address to);
void print_text_listing(void);
void format_registers(string_stream* stream,
                      bool print_general_purpose_registers_hex,
                      bool print_floating_point_register_hex);
void initialize_registers(void);
void initialize_stack(const char* command_line);
void initialize_run_stack(int argc, char** argv);
void initialize_world(const char* exception_file_names, bool print_message);
void list_breakpoints(void);
const name_value_value* map_integer_to_name_value_value(
    const name_value_value table[], int table_length, int number);
const name_value_value* map_string_to_name_value_value(
    const name_value_value table[], int table_length, const char* identifier);
[[nodiscard]] bool read_assembly_file(char* name);
[[nodiscard]] bool run_program(memory_address program_counter, int steps,
                               bool display, bool continue_breakpoint,
                               bool* continuable);
memory_address starting_address(void);
/* The status the program exits with: its exit/exit2 value, or 128 + the
   ExcCode of the first runtime fault when that value is 0. */
int program_exit_status(void);
void write_startup_message(void);
[[nodiscard]] void* allocate_or_exit(size_t);
[[nodiscard]] void* reallocate_or_exit(void*, size_t);
[[nodiscard]] void* allocate_zeroed_or_exit(size_t);

#endif
