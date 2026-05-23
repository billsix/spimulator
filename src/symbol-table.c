/* SPIM S20 MIPS simulator.
   Code to maintain symbol table to resolve symbolic labels.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "data.h"
#include "parser.h"
#include "symbol-table.h"
#include "tokens.h"
#include "assembler-event.h"

/* Local functions: */

static void get_hash(char* name, int* slot_no, label** entry);
static void resolve_a_label_sub(label* sym, mips_instruction* instruction,
                                mem_addr pc);

/* Keep track of the memory location that a label represents.  If we
   see a reference to a label that is not yet defined, then record the
   reference so that we can patch up the instruction when the label is
   defined.

   At the end of a file, we flush the hash table of all non-global
   labels so they can't be seen in other files.	 */

static label* local_labels = nullptr; /* Labels local to current file. */

#define HASHBITS 30

#define LABEL_HASH_TABLE_SIZE 8191

/* Map from name of a label to a label structure. */

static label* label_hash_table[LABEL_HASH_TABLE_SIZE];

/* Initialize the symbol table by removing and freeing old entries. */

void initialize_symbol_table(void) {
  int i;

  for (i = 0; i < LABEL_HASH_TABLE_SIZE; i++) {
    label *x, *n;

    for (x = label_hash_table[i]; x != nullptr; x = n) {
      free(x->name);
      n = x->next;
      free(x);
    }
    label_hash_table[i] = nullptr;
  }

  local_labels = nullptr;
}

/* Lookup for a label with the given NAME.  Set the SLOT_NO to be the hash
   table bucket that contains (or would contain) the label's record.  If the
   record is already in the table, set ENTRY to point to it.  Otherwise,
   set ENTRY to be nullptr. */

static void get_hash(char* name, int* slot_no, label** entry) {
  int hi;
  int i;
  label* cursor;
  int len;

  /* Compute length of name in len.  */
  for (len = 0; name[len]; len++);

  /* Compute hash code */
  hi = len;
  for (i = 0; i < len; i++) hi = ((hi * 613) + (unsigned)(name[i]));

  hi &= (1 << HASHBITS) - 1;
  hi %= LABEL_HASH_TABLE_SIZE;

  *slot_no = hi;
  /* Search table for entry */
  for (cursor = label_hash_table[hi]; cursor; cursor = cursor->next)
    if (streq(cursor->name, name)) {
      *entry = cursor; /* <-- return if found */
      return;
    }
  *entry = nullptr;
}

/* Lookup label with NAME.  Either return its symbol table entry or nullptr
   if it is not in the table. */

label* label_is_defined(char* name) {
  int hi;
  label* entry;

  get_hash(name, &hi, &entry);

  return (entry);
}

/* Return a label with a given NAME.  If an label with that name has
   previously been looked-up, the same node is returned this time.  */

label* lookup_label(char* name) {
  int hi;
  label *entry, *new_entry;

  get_hash(name, &hi, &entry);

  if (entry != nullptr) return (entry);

  /* Not found, create one, add to chain */
  new_entry = (label*)xmalloc(sizeof(label));
  new_entry->name = str_copy(name);
  new_entry->addr = 0;
  new_entry->global_flag = 0;
  new_entry->const_flag = 0;
  new_entry->gp_flag = 0;
  new_entry->uses = nullptr;

  new_entry->next = label_hash_table[hi];
  label_hash_table[hi] = new_entry;
  return new_entry; /* <-- return if created */
}

/* Record that the label named NAME refers to ADDRESS.	If RESOLVE_USES is
   true, resolve all references to it.  Return the label structure. */

label* record_label(char* name, mem_addr address, int resolve_uses) {
  label* l = lookup_label(name);

  if (!l->gp_flag) {
    if (l->addr != 0) {
      parse_error("Label is defined for the second time");
      return (l);
    }
    l->addr = address;
  }

  asm_fire_label_def(address, l->name, l->global_flag != 0);

  if (resolve_uses) {
    resolve_label_uses(l);
  }

  if (!l->global_flag) {
    l->next_local = local_labels;
    local_labels = l;
  }
  return (l);
}

/* Make the label named NAME global.  Return its symbol. */

label* make_label_global(char* name) {
  label* l = lookup_label(name);

  l->global_flag = 1;
  return (l);
}

/* Record that an INSTRUCTION uses the as-yet undefined SYMBOL. */

void record_inst_uses_symbol(mips_instruction* instruction, label* sym) {
  label_use* u = (label_use*)xmalloc(sizeof(label_use));

  if (data_dir) /* Want to free up original instruction */
  {
    u->instruction = copy_inst(instruction);
    u->addr = current_data_pc();
  } else {
    u->instruction = instruction;
    u->addr = current_text_pc();
  }
  u->next = sym->uses;
  sym->uses = u;
  asm_fire_forward_ref(u->addr, sym->name);
}

/* Record that a memory LOCATION uses the as-yet undefined SYMBOL. */

void record_data_uses_symbol(mem_addr location, label* sym) {
  label_use* u = (label_use*)xmalloc(sizeof(label_use));

  u->instruction = nullptr;
  u->addr = location;
  u->next = sym->uses;
  sym->uses = u;
  asm_fire_forward_ref(location, sym->name);
}

/* Given a newly-defined LABEL, resolve the previously encountered
   instructions and data locations that refer to the label. */

void resolve_label_uses(label* sym) {
  label_use* use;
  label_use* next_use;

  for (use = sym->uses; use != nullptr; use = next_use) {
    resolve_a_label_sub(sym, use->instruction, use->addr);
    if (use->instruction != nullptr && use->addr >= DATA_BOT &&
        use->addr < stack_bot) {
      mem_write_word(use->addr, inst_encode(use->instruction));
      free_inst(use->instruction);
    }
    asm_fire_forward_resolved(use->addr, sym->name, (int32_t)sym->addr);
    next_use = use->next;
    free(use);
  }
  sym->uses = nullptr;
}

/* Resolve the newly-defined label in INSTRUCTION. */

void resolve_a_label(label* sym, mips_instruction* instruction) {
  resolve_a_label_sub(sym, instruction,
                      (data_dir ? current_data_pc() : current_text_pc()));
}

static void resolve_a_label_sub(label* sym, mips_instruction* instruction,
                                mem_addr pc) {
  if (instruction == nullptr) {
    /* Memory data: */
    mem_write_word(pc, sym->addr);
  } else {
    /* Instruction: */
    if (EXPR(instruction)->pc_relative)
      EXPR(instruction)->offset = 0 - pc; /* Instruction may have moved */

    if (EXPR(instruction)->symbol == nullptr ||
        SYMBOL_IS_DEFINED(EXPR(instruction)->symbol)) {
      int32_t value;
      int32_t field_mask;

      if (opcode_is_branch(OPCODE(instruction))) {
        int val;

        /* Drop low two bits since instructions are on word boundaries. */
        val = sign_ex(eval_imm_expr(EXPR(instruction))); /* 16->32 bits */
        val = (val >> 2) & 0xffff; /* right shift, 32->16 bits */

        if (delayed_branches) val -= 1;

        value = val;
        field_mask = 0xffff;
      } else if (opcode_is_jump(OPCODE(instruction))) {
        value = eval_imm_expr(EXPR(instruction));
        if ((value & 0xf0000000) != (pc & 0xf0000000)) {
          error(
              "Target of jump differs in high-order 4 bits from instruction pc "
              "0x%x\n",
              pc);
        }
        /* Drop high four bits, since they come from the PC and the
               low two bits since instructions are on word boundaries. */
        value = (value & 0x0fffffff) >> 2;
        field_mask =
            0xffffffff; /* Already checked that value fits in instruction */
      } else if (opcode_is_load_store(OPCODE(instruction))) {
        /* Label's location is an address */
        value = eval_imm_expr(EXPR(instruction));
        field_mask = 0xffff;

        if (value & 0x8000) {
          /* LW/SW sign extends offset. Compensate by adding 1 to high 16 bits.
           */
          mips_instruction* prev_inst;
          mips_instruction* prev_prev_inst;
          prev_inst = mem_read_inst(pc - BYTES_PER_WORD);
          prev_prev_inst = mem_read_inst(pc - 2 * BYTES_PER_WORD);

          if (prev_inst != nullptr && OPCODE(prev_inst) == TOK_LUI_OPCODE &&
              EXPR(instruction)->symbol == EXPR(prev_inst)->symbol &&
              IMM(prev_inst) == 0) {
            /* Check that previous instruction was LUI and it has no immediate,
               otherwise it will have compensated for sign-extension */
            EXPR(prev_inst)->offset += 0x10000;
          }
          /* There is an ADDU instruction before the LUI if the
             LW/SW instruction uses an index register: skip over the ADDU. */
          else if (prev_prev_inst != nullptr &&
                   OPCODE(prev_prev_inst) == TOK_LUI_OPCODE &&
                   EXPR(instruction)->symbol == EXPR(prev_prev_inst)->symbol &&
                   IMM(prev_prev_inst) == 0) {
            EXPR(prev_prev_inst)->offset += 0x10000;
          }
        }
      } else {
        /* Label's location is a value */
        value = eval_imm_expr(EXPR(instruction));
        field_mask = 0xffff;
      }

      if ((value & ~field_mask) != (int32_t)0 &&
          (value & ~field_mask) != (int32_t)0xffff0000) {
        error("Immediate value is too large for field: ");
        print_inst(pc);
      }
      if (opcode_is_jump(OPCODE(instruction)))
        SET_TARGET(instruction, value); /* Don't mask so it is sign-extended */
      else
        SET_IMM(instruction, value); /* Ditto */
      SET_ENCODING(instruction, inst_encode(instruction));
    } else
      error("Resolving undefined symbol: %s\n",
            (EXPR(instruction)->symbol == nullptr)
                ? ""
                : EXPR(instruction)->symbol->name);
  }
}

/* Remove all local (non-global) label from the table. */

void flush_local_labels(int issue_undef_warnings) {
  label* l;

  for (l = local_labels; l != nullptr; l = l->next_local) {
    int hi;
    label *entry, *cursor, *prev;

    get_hash(l->name, &hi, &entry);

    for (cursor = label_hash_table[hi], prev = nullptr; cursor;
         prev = cursor, cursor = cursor->next)
      if (cursor == entry) {
        if (prev == nullptr)
          label_hash_table[hi] = cursor->next;
        else
          prev->next = cursor->next;
        if (issue_undef_warnings && entry->addr == 0 && !entry->const_flag)
          error("Warning: local symbol %s was not defined\n", entry->name);
        /* Can't free label since IMM_EXPR's still reference it */
        break;
      }
  }
  local_labels = nullptr;
}

/* Return the address of SYMBOL or 0 if it is undefined. */

mem_addr find_symbol_address(char* symbol) {
  label* l = lookup_label(symbol);

  if (l == nullptr || l->addr == 0)
    return 0;
  else
    return (l->addr);
}

/* Print all symbols in the table. */

void print_symbols(void) {
  int i;
  label* l;

  for (i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (l = label_hash_table[i]; l != nullptr; l = l->next)
      write_output(message_out, "%s%s at 0x%08x\n",
                   l->global_flag ? "g\t" : "\t", l->name, l->addr);
}

/* Iterate over every currently-defined symbol in the table. */

void for_each_label(void (*cb)(const label* l, void* ctx), void* ctx) {
  int i;
  label* l;
  for (i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (l = label_hash_table[i]; l != nullptr; l = l->next)
      if (SYMBOL_IS_DEFINED(l)) cb(l, ctx);
}

/* Print all undefined symbols in the table. */

void print_undefined_symbols(void) {
  int i;
  label* l;

  for (i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (l = label_hash_table[i]; l != nullptr; l = l->next)
      if (l->addr == 0) write_output(message_out, "%s\n", l->name);
}

/* Return a string containing the names of all undefined symbols in the
   table, seperated by a newline character.  Return nullptr if no symbols
   are undefined. */

char* undefined_symbol_string(void) {
  int buffer_length = 128;
  int string_length = 0;
  char* buffer = (char*)malloc(buffer_length);

  int i;
  label* l;

  for (i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (l = label_hash_table[i]; l != nullptr; l = l->next)
      if (l->addr == 0) {
        int name_length = (int)strlen(l->name);
        int after_length = string_length + name_length + 2;
        if (buffer_length < after_length) {
          buffer_length = MAX(2 * buffer_length, 2 * after_length);
          buffer = (char*)realloc(buffer, buffer_length);
        }
        memcpy(buffer + string_length, l->name, name_length);
        string_length += name_length;
        buffer[string_length] = '\n';
        string_length += 1;
        buffer[string_length] = '\0'; /* After end of string */
      }

  if (string_length != 0)
    return (buffer);
  else {
    free(buffer);
    return (nullptr);
  };
}
