/* SPIM S20 MIPS simulator.
   Misc. routines for SPIM.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

#include "spim.h"
#include "version.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "data.h"
#include "registers.h"
#include "memory.h"
#include "scanner.h"
#include "parser.h"
#include "tokens.h"
#include "run.h"
#include "symbol-table.h"

/* Internal functions: */

static memory_address copy_integer_to_stack(int n);
static memory_address copy_string_to_stack(const char* s);
static void delete_all_breakpoints(void);
static bool load_embedded_exception_handler(bool print_message);

/* Sentinel object — see spim-utils.h for the design rationale.  The
   string content is purely for diagnostic / debugger inspection; only
   the address matters for the dispatch check inside initialize_world. */
char SPIM_DEFAULT_EXCEPTIONS_SENTINEL[] = "<embedded exceptions.s>";

/* Default exception handler bytes, baked into the binary at compile
   time via C23 #embed.  Used when neither the SPIM_EXCEPTION_HANDLER
   env var nor the -exception_file CLI flag is set; see the dispatch
   in initialize_world. */
static const unsigned char embedded_exceptions_bytes[] = {
#embed "exceptions.s"
};

bool exception_occurred;

/* Records the ExcCode of the first non-syscall, non-breakpoint, non-interrupt
   exception seen during a run of the user program.  Set by raise_exception()
   in run.c; read by main() after run_program() returns to derive a non-zero
   shell exit status.  Stays at -1 if no such exception fired.
   This is distinct from exception_occurred, which is per-step transient and
   gets reset before every run_spim() call. */
int first_bad_exception = -1;

int program_exit_status(void) {
  /* An explicit exit2(N) with N != 0 says what the program meant; otherwise
     a runtime fault (address, bus, alignment, overflow, ...) lifts the
     status to 128 + ExcCode, the shell's "128 + signum" form. */
  if (first_bad_exception != -1 && spim_return_value == 0)
    return 128 + first_bad_exception;
  else
    return spim_return_value;
}

int initial_text_size = TEXT_SIZE;

int initial_data_size = DATA_SIZE;

memory_address initial_data_limit = DATA_LIMIT;

int initial_stack_size = STACK_SIZE;

memory_address initial_stack_limit = STACK_LIMIT;

int initial_kernel_text_size = KERNEL_TEXT_SIZE;

int initial_kernel_data_size = KERNEL_DATA_SIZE;

memory_address initial_kernel_data_limit = KERNEL_DATA_LIMIT;

/* Initialize or reinitialize the state of the machine. */

void initialize_world(const char* exception_file_names, bool print_message) {
  /* Allocate the floating point registers */
  if (floating_point_single_view == nullptr)
    floating_point_double_view = (double*)allocate_or_exit(
        FLOATING_POINT_REGISTER_LENGTH * sizeof(double));
  /* Allocate the memory */
  make_memory(initial_text_size, initial_data_size, (int)initial_data_limit,
              initial_stack_size, (int)initial_stack_limit,
              initial_kernel_text_size, initial_kernel_data_size,
              (int)initial_kernel_data_limit);
  initialize_registers();
  initialize_symbol_table();
  kernel_text_begins_at_point(KERNEL_TEXT_BOTTOM);
  kernel_data_begins_at_point(KERNEL_DATA_BOTTOM);
  data_begins_at_point(DATA_BOTTOM);
  text_begins_at_point(TEXT_BOTTOM);

  if (exception_file_names != nullptr) {
    const bool old_bare = bare_machine;
    const bool old_accept = accept_pseudo_instructions;

    /* Save machine state */
    bare_machine = false; /* Exception handler uses extended machine */
    accept_pseudo_instructions = true;

    if (exception_file_names == SPIM_DEFAULT_EXCEPTIONS_SENTINEL) {
      /* No user override — load the #embed'd default. */
      if (!load_embedded_exception_handler(print_message))
        fatal_error("Cannot parse embedded exception handler\n");
    } else {
      /* strtok modifies the string, so we must back up the string prior to
         use. */
      char* const files = strdup(exception_file_names);
      if (files == nullptr) fatal_error("Insufficient memory to complete.\n");

      for (char* filename = strtok(files, ";"); filename != nullptr;
           filename = strtok(nullptr, ";")) {
        if (!read_assembly_file(filename))
          fatal_error("Cannot read exception handler: %s\n", filename);

        if (print_message) write_output(message_out, "Loaded: %s\n", filename);
      }

      free(files);
    }

    /* Restore machine state */
    bare_machine = old_bare;
    accept_pseudo_instructions = old_accept;

    if (!bare_machine) {
      (void)make_label_global("main"); /* In case .globl main forgotten */
      (void)record_label("main", 0, false);
    }
  }
  scanner_initialize(stdin);
  delete_all_breakpoints();
}

void write_startup_message(void) {
  write_output(message_out, "SPIM %s\n", SPIM_VERSION);
  write_output(message_out, "Copyright 1990-2023 by James Larus.\n");
  write_output(message_out, "All Rights Reserved.\n");
  write_output(message_out, "SPIM is distributed under a BSD license.\n");
  write_output(message_out,
               "See the file README for a full copyright notice.\n");
}

void initialize_registers(void) {
  memset(floating_point_double_view, 0,
         FLOATING_POINT_REGISTER_LENGTH * sizeof(double));
  floating_point_single_view = (float*)floating_point_double_view;
  floating_point_integer_view = (int32_t*)floating_point_double_view;

  memset(general_purpose_registers, 0,
         GENERAL_PURPOSE_REGISTER_COUNT * sizeof(register_word));
  general_purpose_registers[STACK_POINTER_REGISTER] =
      STACK_TOP - BYTES_PER_WORD - 4096; /* Initialize $sp */
  MULTIPLY_DIVIDE_HIGH = MULTIPLY_DIVIDE_LOW = 0;
  PROGRAM_COUNTER = 0;

  coprocessor_0_bad_virtual_address = 0;
  coprocessor_0_count = 0;
  coprocessor_0_compare = 0;
  coprocessor_0_status =
      (COPROCESSOR_0_STATUS_COPROCESSOR_USABLE & 0x30000000) |
      COPROCESSOR_0_STATUS_INTERRUPT_MASK | COPROCESSOR_0_STATUS_USER_MODE;
  coprocessor_0_cause = 0;
  coprocessor_0_exception_program_counter = 0;
#ifdef SPIM_BIGENDIAN
  coprocessor_0_config = COPROCESSOR_0_CONFIG_BIG_ENDIAN;
#else
  coprocessor_0_config = 0;
#endif

  floating_point_implementation_register =
      FLOATING_POINT_IMPLEMENTATION_HAS_WORD |
      FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE |
      FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE; /* Word, double, & single
                                                   implemented */
  floating_point_control_status_register = 0x0;
}

/* Read file NAME, which should contain assembly code.  Returns
   true on a successful read (possibly with parse errors recorded
   via parse_errors_seen), false if the file could not be opened. */

bool read_assembly_file(char* name) {
  FILE* const file = fopen(name, "rt");

  if (file == nullptr) {
    error("Cannot open file: `%s'\n", name);
    return false;
  } else {
    parser_initialize(file, name);
    (void)parse_file();
    fclose(file);
    flush_local_labels(!parse_error_occurred);
    end_of_assembly_file();
    return true;
  }
}

/* Run the parser over the #embed'd default exception handler bytes by
   wrapping them in a memstream via fmemopen.  Same parse path as a
   real file — just no fopen.  Returns false only if fmemopen itself
   fails (effectively never on Linux). */
static bool load_embedded_exception_handler(bool print_message) {
  FILE* const file = fmemopen((void*)embedded_exceptions_bytes,
                              sizeof(embedded_exceptions_bytes), "rt");
  if (file == nullptr) return false;

  parser_initialize(file, "<embedded exceptions.s>");
  (void)parse_file();
  fclose(file);
  flush_local_labels(!parse_error_occurred);
  end_of_assembly_file();

  if (print_message)
    write_output(message_out, "Loaded: <embedded exceptions.s>\n");
  return true;
}

memory_address starting_address(void) {
  return (find_symbol_address(DEFAULT_RUN_LOCATION));
}

constexpr int MAX_ARGUMENTS = 10000;

/* Initialize the SPIM stack from a string containing the command line. */

void initialize_stack(const char* command_line) {
  int argc = 0;
  char* argv[MAX_ARGUMENTS];
  char* a;
  char* arguments =
      strdup((char*)command_line); /* Destructively modify string */
  char* const original_arguments = arguments;

  while (*arguments != '\0') {
    /* Skip leading blanks */
    while (*arguments == ' ' || *arguments == '\t') arguments++;

    /* First non-blank char */
    a = arguments;

    /* Last non-blank, non-null char */
    while (*arguments != ' ' && *arguments != '\t' && *arguments != '\0')
      arguments++;

    /* Terminate word */
    if (a != arguments) {
      if (*arguments != '\0') *arguments++ = '\0'; /* Null terminate */

      argv[argc++] = a;

      if (MAX_ARGUMENTS == argc) {
        break; /* If too many, ignore rest of list */
      }
    }
  }

  initialize_run_stack(argc, argv);
  free(original_arguments);
}

/* Initialize the SPIM stack with ARGC, ARGV, and ENVP data. */

void initialize_run_stack(int argc, char** argv) {
  char** p;
  extern char** environ;
  int i, j = 0, environment_count;
  memory_address addresses[10000];

  general_purpose_registers[STACK_POINTER_REGISTER] =
      STACK_TOP - 1; /* Initialize $sp */

  /* Put strings on stack: */
  /* env: */
  for (p = environ; *p != nullptr; p++)
    addresses[j++] = copy_string_to_stack(*p);
  environment_count = j;

  /* argv; */
  for (i = 0; i < argc; i++) addresses[j++] = copy_string_to_stack(argv[i]);

  /* Align stack pointer for word-size data */
  general_purpose_registers[STACK_POINTER_REGISTER] =
      general_purpose_registers[STACK_POINTER_REGISTER] &
      ~3; /* Round down to nearest word */
  general_purpose_registers[STACK_POINTER_REGISTER] -=
      BYTES_PER_WORD; /* First free word on stack */
  general_purpose_registers[STACK_POINTER_REGISTER] =
      general_purpose_registers[STACK_POINTER_REGISTER] &
      ~7; /* Double-word align stack-pointer*/

  /* Build vectors on stack: */
  /* env: */
  (void)copy_integer_to_stack(0); /* Null-terminate vector */
  for (i = environment_count - 1; i >= 0; i--)
    general_purpose_registers[ARGUMENT_REGISTER_2] =
        (register_word)copy_integer_to_stack((int)addresses[i]);

  /* argv: */
  (void)copy_integer_to_stack(0); /* Null-terminate vector */
  for (i = j - 1; i >= environment_count; i--)
    general_purpose_registers[ARGUMENT_REGISTER_1] =
        (register_word)copy_integer_to_stack((int)addresses[i]);

  /* argc: */
  general_purpose_registers[ARGUMENT_REGISTER_0] = argc;
  memory_write_word(
      (memory_address)general_purpose_registers[STACK_POINTER_REGISTER],
      argc); /* Leave argc on stack */
}

static memory_address copy_string_to_stack(const char* s) {
  int i = (int)strlen(s);
  while (i >= 0) {
    memory_write_byte(
        (memory_address)general_purpose_registers[STACK_POINTER_REGISTER],
        s[i]);
    general_purpose_registers[STACK_POINTER_REGISTER] -= 1;
    i -= 1;
  }
  return ((memory_address)general_purpose_registers[STACK_POINTER_REGISTER] +
          1); /* Leaves stack pointer byte-aligned!! */
}

static memory_address copy_integer_to_stack(int n) {
  memory_write_word(
      (memory_address)general_purpose_registers[STACK_POINTER_REGISTER], n);
  general_purpose_registers[STACK_POINTER_REGISTER] -= BYTES_PER_WORD;
  return ((memory_address)general_purpose_registers[STACK_POINTER_REGISTER] +
          BYTES_PER_WORD);
}

/* Run the program, starting at PC, for STEPS instructions. Display each
   instruction before executing if DISPLAY is true.  If CONT_BKPT is
   true, then step through a breakpoint. CONTINUABLE is true if
   execution can continue. Return true if breakpoint is encountered. */

bool run_program(memory_address program_counter, int steps, bool display,
                 bool continue_breakpoint, bool* continuable) {
  if (continue_breakpoint && instruction_is_breakpoint(program_counter)) {
    const memory_address address =
        PROGRAM_COUNTER == 0 ? program_counter : PROGRAM_COUNTER;

    delete_breakpoint(address);
    exception_occurred = false;
    *continuable = run_spim(address, 1, display);
    add_breakpoint(address);
    steps -= 1;
    program_counter = PROGRAM_COUNTER;
  }

  exception_occurred = false;
  *continuable = run_spim(program_counter, steps, display);
  if (exception_occurred &&
      (uint32_t)coprocessor_0_exception_code == EXCEPTION_CODE_BREAKPOINT) {
    /* Turn off EXL bit, so subsequent interrupts set EPC since the break is
    handled by SPIM code, not MIPS code. */
    coprocessor_0_status =
        (register_word)((unsigned_register_word)coprocessor_0_status &
                        ~COPROCESSOR_0_STATUS_EXCEPTION_LEVEL);
    return true;
  } else
    return false;
}

/* Record of where a breakpoint was placed and the instruction previously
   in memory. */

typedef struct breakpoint_record {
  memory_address address;
  mips_instruction* instruction;
  struct breakpoint_record* next;
} breakpoint;

static breakpoint* breakpoints = nullptr;

/* Set a breakpoint at memory location ADDR. */

void add_breakpoint(memory_address address) {
  breakpoint* record = (breakpoint*)allocate_or_exit(sizeof(breakpoint));
  *record = (breakpoint){.address = address,
                         .instruction = set_breakpoint(address),
                         .next = breakpoints};

  if (record->instruction != nullptr)
    breakpoints = record;
  else {
    if (exception_occurred)
      error("Cannot put a breakpoint at address 0x%08x\n", address);
    else
      error("No instruction to breakpoint at address 0x%08x\n", address);
    free(record);
  }
}

/* Delete all breakpoints at memory location ADDR. */

void delete_breakpoint(memory_address address) {
  breakpoint *p, *b;
  bool deleted_one = false;

  for (p = nullptr, b = breakpoints; b != nullptr;)
    if (b->address == address) {
      memory_write_instruction(address, b->instruction);
      if (p == nullptr)
        breakpoints = b->next;
      else
        p->next = b->next;
      breakpoint* const n = b->next;
      free(b);
      b = n;
      deleted_one = true;
    } else
      p = b, b = b->next;
  if (!deleted_one) error("No breakpoint to delete at 0x%08x\n", address);
}

static void delete_all_breakpoints(void) {
  breakpoint* n = nullptr;

  for (breakpoint* b = breakpoints; b != nullptr; b = n) {
    n = b->next;
    free(b);
  }
  breakpoints = nullptr;
}

/* List all breakpoints. */

void list_breakpoints(void) {
  if (breakpoints)
    for (breakpoint* b = breakpoints; b != nullptr; b = b->next)
      write_output(message_out, "Breakpoint at 0x%08x\n", b->address);
  else
    write_output(message_out, "No breakpoints set\n");
}

/* Utility routines */

/* Return the entry in the linear TABLE of length LENGTH with key STRING.
   TABLE must be sorted on the key field (lexicographically — the same
   order strcmp induces; all keys are plain ASCII).
   Return nullptr if no such entry exists. */

static int compare_key_to_name(const void* key, const void* elem) {
  return strcmp((const char*)key, ((const name_value_value*)elem)->name);
}

const name_value_value* map_string_to_name_value_value(
    const name_value_value table[], int table_length, const char* identifier) {
  return bsearch(identifier, table, (size_t)table_length,
                 sizeof(name_value_value), compare_key_to_name);
}

/* Return the entry in the linear TABLE of length LENGTH with VALUE1 field NUM.
   TABLE must be sorted on the VALUE1 field (the generated opcode tables
   are; see opcode-tables.h).
   Return nullptr if no such entry exists. */

static int compare_key_to_value1(const void* key, const void* elem) {
  const int k = *(const int*)key;
  const int v = ((const name_value_value*)elem)->value1;
  return (k > v) - (k < v);
}

const name_value_value* map_integer_to_name_value_value(
    const name_value_value table[], int table_length, int number) {
  return bsearch(&number, table, (size_t)table_length, sizeof(name_value_value),
                 compare_key_to_value1);
}

void* allocate_or_exit(size_t size) {
  void* const x = malloc(size);

  if (x == nullptr)
    fatal_error("Out of memory at request for %zu bytes.\n", size);
  return (x);
}

/* Resize a block of storage; out of memory is fatal, as in allocate_or_exit. */

void* reallocate_or_exit(void* block, size_t size) {
  void* const x = realloc(block, size);

  if (x == nullptr)
    fatal_error("Out of memory at request for %zu bytes.\n", size);
  return (x);
}

/* Allocate a zero'ed block of storage. */

void* allocate_zeroed_or_exit(size_t size) {
  void* const z = calloc(1, size);

  if (z == nullptr)
    fatal_error("Out of memory at request for %zu bytes.\n", size);
  return (z);
}
