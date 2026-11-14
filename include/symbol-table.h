/* SPIM S20 MIPS simulator.
   Data structures for symbolic addresses.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef SYM_TBL_H
#define SYM_TBL_H

#include "spim.h"
#include "instruction.h"

typedef struct label_use {
  mips_instruction* instruction; /* nullptr => Data, not code */
  mem_addr address;
  int32_t offset; /* Data only: the constant added to the label (`lab+4`) */
  int width; /* Data only: bytes stored, 1 (.byte), 2 (.half) or 4 (.word) */
  struct label_use* next;
} label_use;

/* Symbol table information on a label. */

typedef struct label {
  char* name;               /* Name of label */
  long address;                /* Address of label or 0 if not yet defined */
  bool global_flag : 1;     /* => declared global */
  bool global_pointer_flag : 1;         /* => referenced off gp */
  bool constant_flag : 1;      /* => constant value (in addr) */
  struct label* next;       /* Hash table link */
  struct label* next_local; /* Link in list of local labels */
  label_use* uses;          /* List of instructions that reference */
} label;                    /* label that has not yet been defined */

#define SYMBOL_IS_DEFINED(SYM) ((SYM)->address != 0)

/* Exported functions: */

mem_addr find_symbol_address(const char* symbol);
void flush_local_labels(bool issue_undefined_warnings);
void initialize_symbol_table(void);
label* label_is_defined(const char* name);
[[nodiscard]] label* lookup_label(const char* name);
label* make_label_global(const char* name);
void print_symbols(void);
void print_undefined_symbols(void);
label* record_label(const char* name, mem_addr address, bool resolve_uses);
void record_data_uses_symbol(mem_addr location, label* symbol, int32_t offset,
                             int width);
void record_instruction_uses_symbol(mips_instruction* instruction, label* symbol);
[[nodiscard]] char* undefined_symbol_string(void);
void resolve_a_label(label* symbol, mips_instruction* instruction);
void resolve_label_uses(label* symbol);

/* Iterate over every currently-defined symbol in the table, invoking
   `cb` with each one and the caller-supplied `ctx`. Order is undefined
   (hash-bucket order). Used by the REPL's tab-completion to enumerate
   labels for the `breakpoint` / `delete` commands. */
void for_each_label(void (*callback)(const label* l, void* ctx), void* context);

#endif
