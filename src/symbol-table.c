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

static void get_hash(const char* name, int* slot_no, label** entry);
static void resolve_a_label_sub(const label* sym, mips_instruction* instruction,
                                mem_addr pc);

/* Keep track of the memory location that a label represents.  If we
   see a reference to a label that is not yet defined, then record the
   reference so that we can patch up the instruction when the label is
   defined.

   At the end of a file, we flush the hash table of all non-global
   labels so they can't be seen in other files.	 */

static label* local_labels = nullptr; /* Labels local to current file. */

constexpr int HASHBITS = 30;

constexpr int LABEL_HASH_TABLE_SIZE = 8191;

/* Map from name of a label to a label structure. */

static label* label_hash_table[LABEL_HASH_TABLE_SIZE];

/* Initialize the symbol table by removing and freeing old entries. */

void initialize_symbol_table(void) {
  for (int i = 0; i < LABEL_HASH_TABLE_SIZE; i++) {
    label* n;

    for (label* x = label_hash_table[i]; x != nullptr; x = n) {
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

static void get_hash(const char* name, int* slot_no, label** entry) {
  const int len = (int)strlen(name);

  /* Compute hash code.  (unsigned)(name[i]) sign-extends a negative
     plain char first; the symbol dump order depends on it. */
  unsigned hi = (unsigned int)len;
  for (int i = 0; i < len; i++) hi = ((hi * 613) + (unsigned)(name[i]));

  hi &= (1 << HASHBITS) - 1;
  hi %= LABEL_HASH_TABLE_SIZE;

  *slot_no = (int)hi;
  /* Search table for entry */
  for (label* cursor = label_hash_table[hi]; cursor; cursor = cursor->next)
    if (streq(cursor->name, name)) {
      *entry = cursor; /* <-- return if found */
      return;
    }
  *entry = nullptr;
}

/* Lookup label with NAME.  Either return its symbol table entry or nullptr
   if it is not in the table. */

label* label_is_defined(const char* name) {
  int hi;
  label* entry;

  get_hash(name, &hi, &entry);

  return (entry);
}

/* Return a label with a given NAME.  If an label with that name has
   previously been looked-up, the same node is returned this time.  */

label* lookup_label(const char* name) {
  int hi;
  label* entry;

  get_hash(name, &hi, &entry);

  if (entry != nullptr) return (entry);

  /* Not found: create an undefined (addr 0), flagless label at the head
     of its hash chain. */
  label* new_entry = (label*)xmalloc(sizeof(label));
  *new_entry = (label){.name = strdup(name), .next = label_hash_table[hi]};
  label_hash_table[hi] = new_entry;
  return new_entry; /* <-- return if created */
}

/* Record that the label named NAME refers to ADDRESS.	If RESOLVE_USES is
   true, resolve all references to it.  Return the label structure. */

label* record_label(const char* name, mem_addr address, bool resolve_uses) {
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

label* make_label_global(const char* name) {
  label* l = lookup_label(name);

  l->global_flag = true;
  return (l);
}

/* Record that an INSTRUCTION uses the as-yet undefined SYMBOL. */

void record_inst_uses_symbol(mips_instruction* instruction, label* sym) {
  label_use* u = (label_use*)xmalloc(sizeof(label_use));

  if (data_dir) /* Want to free up original instruction */
    *u = (label_use){.instruction = copy_inst(instruction),
                     .addr = current_data_pc(),
                     .next = sym->uses};
  else
    *u = (label_use){.instruction = instruction,
                     .addr = current_text_pc(),
                     .next = sym->uses};
  sym->uses = u;
  asm_fire_forward_ref(u->addr, sym->name);
}

/* Record that the WIDTH-byte datum at memory LOCATION holds the as-yet
   undefined SYMBOL plus OFFSET. */

void record_data_uses_symbol(mem_addr location, label* sym, int32_t offset,
                             int width) {
  label_use* u = (label_use*)xmalloc(sizeof(label_use));

  *u = (label_use){
      .addr = location, .offset = offset, .width = width, .next = sym->uses};
  sym->uses = u;
  asm_fire_forward_ref(location, sym->name);
}

/* Store the low WIDTH bytes of VALUE at AT in target byte order, one byte at a
   time, as store_byte/store_half/store_word laid them down (AT need not be
   aligned). */

static void write_data_value(mem_addr at, int32_t value, int width) {
  for (int i = 0; i < width; i++) {
#ifdef SPIM_BIGENDIAN
    int shift = 8 * (width - 1 - i);
#else
    const int shift = 8 * i;
#endif
    mem_write_byte(at + (mem_addr)i,
                   (reg_word)(((uint32_t)value >> shift) & 0xffu));
  }
}

/* Given a newly-defined LABEL, resolve the previously encountered
   instructions and data locations that refer to the label. */

void resolve_label_uses(label* sym) {
  label_use* next_use;

  for (label_use* use = sym->uses; use != nullptr; use = next_use) {
    if (use->instruction == nullptr)
      write_data_value(use->addr, (int32_t)sym->addr + use->offset, use->width);
    else
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

static void resolve_a_label_sub(const label* sym, mips_instruction* instruction,
                                mem_addr pc) {
  if (instruction == nullptr) {
    /* Memory data: */
    mem_write_word(pc, (reg_word)sym->addr);
  } else {
    /* Instruction: */
    if (EXPR(instruction)->pc_relative)
      EXPR(instruction)->offset =
          (int32_t)(0 - pc); /* Instruction may have moved */

    if (EXPR(instruction)->symbol == nullptr ||
        SYMBOL_IS_DEFINED(EXPR(instruction)->symbol)) {
      int32_t value;
      int32_t field_mask;

      if (opcode_is_branch(OPCODE(instruction))) {
        /* The full 32-bit byte offset: the expression asks for its low 16
           bits, which would truncate a branch more than 32 KB away. */
        imm_expr whole = *EXPR(instruction);
        whole.bits = 0;
        const int32_t byte_offset = eval_imm_expr(&whole);
        /* Drop low two bits since instructions are on word boundaries. */
        int32_t word_offset = byte_offset / BYTES_PER_WORD;

        if (delayed_branches) word_offset -= 1;
        if (word_offset < -32768 || word_offset > 32767)
          error(
              "Branch target is out of range (more than 128 KB away) at pc "
              "0x%x\n",
              pc);

        value = (int32_t)((uint32_t)word_offset & 0xffffu);
        field_mask = 0xffff;
      } else if (opcode_is_jump(OPCODE(instruction))) {
        value = eval_imm_expr(EXPR(instruction));
        if (((unsigned int)value & 0xf0000000) != (pc & 0xf0000000)) {
          error(
              "Target of jump differs in high-order 4 bits from instruction pc "
              "0x%x\n",
              pc);
        }
        /* Drop high four bits, since they come from the PC and the
               low two bits since instructions are on word boundaries. */
        value = (value & 0x0fffffff) >> 2;
        field_mask = (int32_t)0xffffffff; /* Already checked that value fits in
                                             instruction */
      } else if (opcode_is_load_store(OPCODE(instruction))) {
        /* Label's location is an address */
        value = eval_imm_expr(EXPR(instruction));
        field_mask = 0xffff;

        if (value & 0x8000) {
          /* LW/SW sign extends offset. Compensate by adding 1 to high 16 bits.
           */
          mips_instruction* prev_inst = mem_read_inst(pc - BYTES_PER_WORD);
          mips_instruction* prev_prev_inst =
              mem_read_inst(pc - 2 * BYTES_PER_WORD);

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

void flush_local_labels(bool issue_undef_warnings) {
  for (label* l = local_labels; l != nullptr; l = l->next_local) {
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

mem_addr find_symbol_address(const char* symbol) {
  label* const l = lookup_label(symbol);

  if (l == nullptr || l->addr == 0)
    return 0;
  else
    return ((mem_addr)l->addr);
}

/* Print all symbols in the table. */

void print_symbols(void) {
  for (int i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (label* l = label_hash_table[i]; l != nullptr; l = l->next)
      write_output(message_out, "%s%s at 0x%08x\n",
                   l->global_flag ? "g\t" : "\t", l->name, (unsigned)l->addr);
}

/* Iterate over every currently-defined symbol in the table. */

void for_each_label(void (*cb)(const label* l, void* ctx), void* ctx) {
  for (int i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (label* l = label_hash_table[i]; l != nullptr; l = l->next)
      if (SYMBOL_IS_DEFINED(l)) cb(l, ctx);
}

/* Print all undefined symbols in the table. */

void print_undefined_symbols(void) {
  for (int i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (label* l = label_hash_table[i]; l != nullptr; l = l->next)
      if (l->addr == 0) write_output(message_out, "%s\n", l->name);
}

/* Return a string containing the names of all undefined symbols in the
   table, seperated by a newline character.  Return nullptr if no symbols
   are undefined. */

char* undefined_symbol_string(void) {
  int buffer_length = 128;
  int string_length = 0;
  char* buffer = (char*)malloc((size_t)buffer_length);

  for (int i = 0; i < LABEL_HASH_TABLE_SIZE; i++)
    for (label* l = label_hash_table[i]; l != nullptr; l = l->next)
      if (l->addr == 0) {
        const int name_length = (int)strlen(l->name);
        const int after_length = string_length + name_length + 2;
        if (buffer_length < after_length) {
          buffer_length = MAX(2 * buffer_length, 2 * after_length);
          buffer = (char*)realloc(buffer, (size_t)buffer_length);
        }
        memcpy(buffer + string_length, l->name, (size_t)name_length);
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
