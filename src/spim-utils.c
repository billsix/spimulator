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
#include "inst.h"
#include "data.h"
#include "reg.h"
#include "mem.h"
#include "scanner.h"
#include "parser.h"
#include "tokens.h"
#include "run.h"
#include "sym-tbl.h"

/* Internal functions: */

static mem_addr copy_int_to_stack(int n);
static mem_addr copy_str_to_stack(char* s);
static void delete_all_breakpoints(void);

int exception_occurred;

/* Records the ExcCode of the first non-syscall, non-breakpoint, non-interrupt
   exception seen during a run of the user program.  Set by raise_exception()
   in run.c; read by main() after run_program() returns to derive a non-zero
   shell exit status.  Stays at -1 if no such exception fired.
   This is distinct from exception_occurred, which is per-step transient and
   gets reset before every run_spim() call. */
int first_bad_exception = -1;

int initial_text_size = TEXT_SIZE;

int initial_data_size = DATA_SIZE;

mem_addr initial_data_limit = DATA_LIMIT;

int initial_stack_size = STACK_SIZE;

mem_addr initial_stack_limit = STACK_LIMIT;

int initial_k_text_size = K_TEXT_SIZE;

int initial_k_data_size = K_DATA_SIZE;

mem_addr initial_k_data_limit = K_DATA_LIMIT;

/* Initialize or reinitialize the state of the machine. */

void initialize_world(char* exception_file_names, bool print_message) {
  /* Allocate the floating point registers */
  if (FGR == nullptr) FPR = (double*)xmalloc(FPR_LENGTH * sizeof(double));
  /* Allocate the memory */
  make_memory(initial_text_size, initial_data_size, initial_data_limit,
              initial_stack_size, initial_stack_limit, initial_k_text_size,
              initial_k_data_size, initial_k_data_limit);
  initialize_registers();
  initialize_inst_tables();
  initialize_symbol_table();
  k_text_begins_at_point(K_TEXT_BOT);
  k_data_begins_at_point(K_DATA_BOT);
  data_begins_at_point(DATA_BOT);
  text_begins_at_point(TEXT_BOT);

  if (exception_file_names != nullptr) {
    bool old_bare = bare_machine;
    bool old_accept = accept_pseudo_insts;
    char* filename;
    char* files;

    /* Save machine state */
    bare_machine = false; /* Exception handler uses extended machine */
    accept_pseudo_insts = true;

    /* strtok modifies the string, so we must back up the string prior to use.
     */
    if ((files = str_copy(exception_file_names)) == nullptr)
      fatal_error("Insufficient memory to complete.\n");

    for (filename = strtok(files, ";"); filename != nullptr;
         filename = strtok(nullptr, ";")) {
      if (!read_assembly_file(filename))
        fatal_error("Cannot read exception handler: %s\n", filename);

      if (print_message) write_output(message_out, "Loaded: %s\n", filename);
    }

    free(files);

    /* Restore machine state */
    bare_machine = old_bare;
    accept_pseudo_insts = old_accept;

    if (!bare_machine) {
      (void)make_label_global("main"); /* In case .globl main forgotten */
      (void)record_label("main", 0, 0);
    }
  }
  scanner_init(stdin);
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
  memclr(FPR, FPR_LENGTH * sizeof(double));
  FGR = (float*)FPR;
  FWR = (int*)FPR;

  memclr(R, R_LENGTH * sizeof(reg_word));
  R[REG_SP] = STACK_TOP - BYTES_PER_WORD - 4096; /* Initialize $sp */
  HI = LO = 0;
  PC = 0;

  CP0_BadVAddr = 0;
  CP0_Count = 0;
  CP0_Compare = 0;
  CP0_Status = (CP0_Status_CU & 0x30000000) | CP0_Status_IM | CP0_Status_UM;
  CP0_Cause = 0;
  CP0_EPC = 0;
#ifdef SPIM_BIGENDIAN
  CP0_Config = CP0_Config_BE;
#else
  CP0_Config = 0;
#endif

  FIR = FIR_W | FIR_D | FIR_S; /* Word, double, & single implemented */
  FCSR = 0x0;
}

/* Read file NAME, which should contain assembly code.  Returns
   true on a successful read (possibly with parse errors recorded
   via parse_errors_seen), false if the file could not be opened. */

bool read_assembly_file(char* name) {
  FILE* file = fopen(name, "rt");

  if (file == nullptr) {
    error("Cannot open file: `%s'\n", name);
    return false;
  } else {
    parser_init(file, name);
    (void)parse_file();
    fclose(file);
    flush_local_labels(!parse_error_occurred);
    end_of_assembly_file();
    return true;
  }
}

mem_addr starting_address(void) {
  return (find_symbol_address(DEFAULT_RUN_LOCATION));
}

#define MAX_ARGS 10000

/* Initialize the SPIM stack from a string containing the command line. */

void initialize_stack(const char* command_line) {
  int argc = 0;
  char* argv[MAX_ARGS];
  char* a;
  char* args = str_copy((char*)command_line); /* Destructively modify string */
  char* orig_args = args;

  while (*args != '\0') {
    /* Skip leading blanks */
    while (*args == ' ' || *args == '\t') args++;

    /* First non-blank char */
    a = args;

    /* Last non-blank, non-null char */
    while (*args != ' ' && *args != '\t' && *args != '\0') args++;

    /* Terminate word */
    if (a != args) {
      if (*args != '\0') *args++ = '\0'; /* Null terminate */

      argv[argc++] = a;

      if (MAX_ARGS == argc) {
        break; /* If too many, ignore rest of list */
      }
    }
  }

  initialize_run_stack(argc, argv);
  free(orig_args);
}

/* Initialize the SPIM stack with ARGC, ARGV, and ENVP data. */

#ifdef _MSC_VER
#define environ _environ
#endif

void initialize_run_stack(int argc, char** argv) {
  char** p;
  extern char** environ;
  int i, j = 0, env_j;
  mem_addr addrs[10000];

  R[REG_SP] = STACK_TOP - 1; /* Initialize $sp */

  /* Put strings on stack: */
  /* env: */
  for (p = environ; *p != nullptr; p++) addrs[j++] = copy_str_to_stack(*p);
  env_j = j;

  /* argv; */
  for (i = 0; i < argc; i++) addrs[j++] = copy_str_to_stack(argv[i]);

  /* Align stack pointer for word-size data */
  R[REG_SP] = R[REG_SP] & ~3;  /* Round down to nearest word */
  R[REG_SP] -= BYTES_PER_WORD; /* First free word on stack */
  R[REG_SP] = R[REG_SP] & ~7;  /* Double-word align stack-pointer*/

  /* Build vectors on stack: */
  /* env: */
  (void)copy_int_to_stack(0); /* Null-terminate vector */
  for (i = env_j - 1; i >= 0; i--) R[REG_A2] = copy_int_to_stack(addrs[i]);

  /* argv: */
  (void)copy_int_to_stack(0); /* Null-terminate vector */
  for (i = j - 1; i >= env_j; i--) R[REG_A1] = copy_int_to_stack(addrs[i]);

  /* argc: */
  R[REG_A0] = argc;
  mem_write_word(R[REG_SP], argc); /* Leave argc on stack */
}

static mem_addr copy_str_to_stack(char* s) {
  int i = (int)strlen(s);
  while (i >= 0) {
    mem_write_byte(R[REG_SP], s[i]);
    R[REG_SP] -= 1;
    i -= 1;
  }
  return ((mem_addr)R[REG_SP] + 1); /* Leaves stack pointer byte-aligned!! */
}

static mem_addr copy_int_to_stack(int n) {
  mem_write_word(R[REG_SP], n);
  R[REG_SP] -= BYTES_PER_WORD;
  return ((mem_addr)R[REG_SP] + BYTES_PER_WORD);
}

/* Run the program, starting at PC, for STEPS instructions. Display each
   instruction before executing if DISPLAY is true.  If CONT_BKPT is
   true, then step through a breakpoint. CONTINUABLE is true if
   execution can continue. Return true if breakpoint is encountered. */

bool run_program(mem_addr pc, int steps, bool display, bool cont_bkpt,
                 bool* continuable) {
  if (cont_bkpt && inst_is_breakpoint(pc)) {
    mem_addr addr = PC == 0 ? pc : PC;

    delete_breakpoint(addr);
    exception_occurred = 0;
    *continuable = run_spim(addr, 1, display);
    add_breakpoint(addr);
    steps -= 1;
    pc = PC;
  }

  exception_occurred = 0;
  *continuable = run_spim(pc, steps, display);
  if (exception_occurred && CP0_ExCode == ExcCode_Bp) {
    /* Turn off EXL bit, so subsequent interrupts set EPC since the break is
    handled by SPIM code, not MIPS code. */
    CP0_Status &= ~CP0_Status_EXL;
    return true;
  } else
    return false;
}

/* Record of where a breakpoint was placed and the instruction previously
   in memory. */

typedef struct bkptrec {
  mem_addr addr;
  instruction* inst;
  struct bkptrec* next;
} bkpt;

static bkpt* bkpts = nullptr;

/* Set a breakpoint at memory location ADDR. */

void add_breakpoint(mem_addr addr) {
  bkpt* rec = (bkpt*)xmalloc(sizeof(bkpt));

  rec->next = bkpts;
  rec->addr = addr;

  if ((rec->inst = set_breakpoint(addr)) != nullptr)
    bkpts = rec;
  else {
    if (exception_occurred)
      error("Cannot put a breakpoint at address 0x%08x\n", addr);
    else
      error("No instruction to breakpoint at address 0x%08x\n", addr);
    free(rec);
  }
}

/* Delete all breakpoints at memory location ADDR. */

void delete_breakpoint(mem_addr addr) {
  bkpt *p, *b;
  int deleted_one = 0;

  for (p = nullptr, b = bkpts; b != nullptr;)
    if (b->addr == addr) {
      bkpt* n;

      mem_write_inst(addr, b->inst);
      if (p == nullptr)
        bkpts = b->next;
      else
        p->next = b->next;
      n = b->next;
      free(b);
      b = n;
      deleted_one = 1;
    } else
      p = b, b = b->next;
  if (!deleted_one) error("No breakpoint to delete at 0x%08x\n", addr);
}

static void delete_all_breakpoints(void) {
  bkpt *b, *n;

  for (b = bkpts, n = nullptr; b != nullptr; b = n) {
    n = b->next;
    free(b);
  }
  bkpts = nullptr;
}

/* List all breakpoints. */

void list_breakpoints(void) {
  bkpt* b;

  if (bkpts)
    for (b = bkpts; b != nullptr; b = b->next)
      write_output(message_out, "Breakpoint at 0x%08x\n", b->addr);
  else
    write_output(message_out, "No breakpoints set\n");
}

/* Utility routines */

/* Return the entry in the linear TABLE of length LENGTH with key STRING.
   TABLE must be sorted on the key field.
   Return nullptr if no such entry exists. */

name_val_val* map_string_to_name_val_val(name_val_val tbl[], int tbl_len,
                                         char* id) {
  int low = 0;
  int hi = tbl_len - 1;

  while (low <= hi) {
    int mid = (low + hi) / 2;
    char *idp = id, *np = tbl[mid].name;

    while (*idp == *np && *idp != '\0') {
      idp++;
      np++;
    }

    if (*np == '\0' && *idp == '\0') /* End of both strings */
      return (&tbl[mid]);
    else if (*idp > *np)
      low = mid + 1;
    else
      hi = mid - 1;
  }

  return nullptr;
}

/* Return the entry in the linear TABLE of length LENGTH with VALUE1 field NUM.
   TABLE must be sorted on the VALUE1 field.
   Return nullptr if no such entry exists. */

name_val_val* map_int_to_name_val_val(name_val_val tbl[], int tbl_len,
                                      int num) {
  int low = 0;
  int hi = tbl_len - 1;

  while (low <= hi) {
    int mid = (low + hi) / 2;

    if (tbl[mid].value1 == num)
      return (&tbl[mid]);
    else if (num > tbl[mid].value1)
      low = mid + 1;
    else
      hi = mid - 1;
  }

  return nullptr;
}

#ifdef NEED_VSPRINTF
char* vsprintf(str, fmt, args)
char *str, *fmt;
va_list* args;
{
  FILE _strbuf;

  _strbuf._flag = _IOWRT + _IOSTRG;
  _strbuf._ptr = str;
  _strbuf._cnt = 32767;
  _doprnt(fmt, args, &_strbuf);
  putc('\0', &_strbuf);
  return (str);
}
#endif

#ifdef NEED_STRTOL
unsigned long strtol(const char* str, const char** eptr, int base) {
  long result;

  if (base != 0 && base != 16)
    fatal_error("SPIM's strtol only works for base 16 (not base %d)\n", base);
  if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
    str += 2;
    sscanf(str, "%lx", &result);
  } else if (base == 16) {
    sscanf(str, "%lx", &result);
  } else {
    sscanf(str, "%ld", &result);
  }
  return (result);
}
#endif

#ifdef NEED_STRTOUL
unsigned long strtoul(const char* str, char** eptr, int base) {
  unsigned long result;

  if (base != 0 && base != 16)
    fatal_error("SPIM's strtoul only works for base 16 (not base %d)\n", base);
  if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
    str += 2;
    sscanf(str, "%lx", &result);
  } else if (base == 16) {
    sscanf(str, "%lx", &result);
  } else {
    sscanf(str, "%ld", &result);
  }
  return (result);
}
#endif

char* str_copy(const char* str) {
  const int len_to_copy = (int)strlen(str) + 1;
  char* const new_str = (char*)xmalloc(len_to_copy);
  strlcpy(new_str, str, len_to_copy);
  return new_str;
}

void* xmalloc(int size) {
  void* x = (void*)malloc(size);

  if (x == 0) fatal_error("Out of memory at request for %d bytes.\n");
  return (x);
}

/* Allocate a zero'ed block of storage. */

void* zmalloc(int size) {
  void* z = (void*)malloc(size);

  if (z == 0) fatal_error("Out of memory at request for %d bytes.\n");

  memclr(z, size);
  return (z);
}
