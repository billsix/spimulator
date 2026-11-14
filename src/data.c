/* SPIM S20 MIPS simulator.
   Code to manipulate data segment directives.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "symbol-table.h"
#include "parser.h"
#include "run.h"
#include "data.h"
#include "assembler-event.h"

/* The first 64K of the data segment are dedicated to small data
   segment, which is pointed to by $gp. This register points to the
   middle of the segment, so we can use the full offset field in an
   instruction. */

static memory_address
    next_data_location; /* Location for next datum in user process */

static memory_address
    next_kernel_data_location; /* Location for next datum in kernel */

static bool in_kernel = false; /* => data goes to kdata, not data */

static inline memory_address data_location(void) {
  return in_kernel ? next_kernel_data_location : next_data_location;
}

static memory_address next_global_pointer_item_address; /* Address of next item
                                                           accessed off $gp */

static bool enable_data_auto_alignment =
    true; /* => align literal to natural bound */

/* If TO_KERNEL is true, subsequent data will be placed in the
   kernel data segment.  If false, data will go to the user's data
   segment.*/

void user_kernel_data_segment(bool to_kernel) { in_kernel = to_kernel; }

void end_of_assembly_file(void) {
  in_kernel = false;
  enable_data_auto_alignment = true;
}

/* Set the point at which the first datum is stored to be ADDRESS +
   64K.	 The 64K increment allocates an area pointed to by register
   $gp, which is initialized. */

void data_begins_at_point(memory_address address) {
  if (bare_machine)
    next_data_location = address;
  else {
    next_global_pointer_item_address = address;
    global_pointer_midpoint = address + 32 * kilo;
    general_purpose_registers[GLOBAL_POINTER_REGISTER] =
        (register_word)global_pointer_midpoint;
    next_data_location = address + 64 * kilo;
  }
}

/* Set the point at which the first datum is stored in the kernel's
   data segment. */

void kernel_data_begins_at_point(memory_address address) {
  next_kernel_data_location = address;
}

/* Arrange that the next datum is stored on a memory boundary with its
   low ALIGNMENT bits equal to 0.  If argument is 0, disable automatic
   alignment.*/

void align_data(int alignment) {
  if (alignment == 0)
    enable_data_auto_alignment = false;
  else if (in_kernel) {
    next_kernel_data_location =
        (next_kernel_data_location + (1u << alignment) - 1) &
        (0xffffffff << alignment);
    fix_current_label_address(next_kernel_data_location);
  } else {
    next_data_location = (next_data_location + (1u << alignment) - 1) &
                         (0xffffffff << alignment);
    fix_current_label_address(next_data_location);
  }
}

void set_data_alignment(int alignment) {
  if (enable_data_auto_alignment) align_data(alignment);
}

void enable_data_alignment(void) { enable_data_auto_alignment = true; }

/* Set the location (in user or kernel data space) for the next datum. */

void set_data_location(memory_address address) {
  if (in_kernel)
    next_kernel_data_location = address;
  else
    next_data_location = address;
}

/* Return the address at which the next datum will be stored.  */

memory_address current_data_location(void) { return (data_location()); }

/* Bump the address at which the next data will be stored by DELTA
   bytes. */

void increment_data_location(int delta) {
  if (in_kernel) {
    next_kernel_data_location += (memory_address)delta;
    if (kernel_data_top <= next_kernel_data_location)
      expand_kernel_data((int)ROUND_UP(
          next_kernel_data_location - kernel_data_top + 1, 64 * kilo));
  } else {
    next_data_location += (memory_address)delta;
    if (data_top <= next_data_location)
      expand_data((int)ROUND_UP(next_data_location - data_top + 1, 64 * kilo));
  }
}

/* Process a .extern NAME SIZE directive. */

void extern_directive(const char* name, int size) {
  label* symbol = make_label_global(name);

  if (!bare_machine &&
      !symbol->global_pointer_flag  // Not already a global symbol
      && size > 0 && size <= SMALL_DATA_SEGMENT_MAX_SIZE &&
      next_global_pointer_item_address + (memory_address)size <
          global_pointer_midpoint + 32 * kilo) {
    symbol->global_pointer_flag = true;
    symbol->address = next_global_pointer_item_address;
    next_global_pointer_item_address += (memory_address)size;
  }
}

/* Process a .lcomm NAME SIZE directive. */

void lcomm_directive(const char* name, int size) {
  if (!bare_machine && size > 0 && size <= SMALL_DATA_SEGMENT_MAX_SIZE &&
      next_global_pointer_item_address + (memory_address)size <
          global_pointer_midpoint + 32 * kilo) {
    label* symbol = record_label(name, next_global_pointer_item_address, true);
    symbol->global_pointer_flag = true;

    next_global_pointer_item_address += (memory_address)size;
    /* Don't need to initialize since memory starts with 0's */
  } else {
    (void)record_label(name, next_data_location, true);

    for (; size > 0; size--) {
      store_byte(0);
    }
  }
}

/* Recursion guard so that one .word/.half/.string directive fires
   exactly one AE_DATA_* event even when the unaligned path or the
   string loop recurses into smaller stores. */
static int data_store_depth = 0;

/* Process a .ascii STRING or .asciiz STRING directive. */

void store_string(char* string, int length, bool null_terminate) {
  const memory_address at = data_location();
  const char* const original = string;
  const int original_length = length;
  data_store_depth++;
  for (; length > 0; string++, length--) {
    store_byte(*string);
  }
  if (null_terminate) {
    store_byte(0);
  }
  data_store_depth--;
  if (data_store_depth == 0)
    assembler_fire_data_string(at, original, original_length, null_terminate);
}

/* Process a .byte EXPRESSION directive. */

void store_byte(int value) {
  const memory_address at = data_location();
  memory_write_byte(at, value);
  increment_data_location(1);
  if (data_store_depth == 0) assembler_fire_data_byte(at, value);
}

/* Process a .half EXPRESSION directive. */

void store_half(int value) {
  const memory_address at = data_location();
  const bool outer = (data_store_depth == 0);
  data_store_depth++;
  if ((data_location() & 0x1) != 0) {
#ifdef SPIM_BIGENDIAN
    store_byte((value >> 8) & 0xff);
    store_byte(value & 0xff);
#else
    store_byte(value & 0xff);
    store_byte((value >> 8) & 0xff);
#endif
  } else {
    memory_write_half(at, value);
    increment_data_location(BYTES_PER_WORD / 2);
  }
  data_store_depth--;
  if (outer) assembler_fire_data_half(at, value);
}

/* Process a .word EXPRESSION directive. */

void store_word(int value) {
  const memory_address at = data_location();
  const bool outer = (data_store_depth == 0);
  data_store_depth++;
  if ((data_location() & 0x3) != 0) {
#ifdef SPIM_BIGENDIAN
    store_half((value >> 16) & 0xffff);
    store_half(value & 0xffff);
#else
    store_half(value & 0xffff);
    store_half((value >> 16) & 0xffff);
#endif
  } else {
    memory_write_word(at, value);
    increment_data_location(BYTES_PER_WORD);
  }
  data_store_depth--;
  if (outer) assembler_fire_data_word(at, value);
}

/* Process a .double EXPRESSION directive. */

void store_double(double* value) {
  const memory_address at = data_location();
  const bool outer = (data_store_depth == 0);
  data_store_depth++;
  memory_word words[2]; /* the double's two words, in host order */
  memcpy(words, value, sizeof words);
  if ((data_location() & 0x7) != 0) {
    store_word(words[0]);
    store_word(words[1]);
  } else {
    memory_write_word(data_location(), words[0]);
    increment_data_location(BYTES_PER_WORD);
    memory_write_word(data_location(), words[1]);
    increment_data_location(BYTES_PER_WORD);
  }
  data_store_depth--;
  if (outer) assembler_fire_data_double(at, *value);
}

/* Process a .float EXPRESSION directive. */

void store_float(double* value) {
  float float_value = (float)*value;
  memory_word bits; /* the float's bit pattern, not its value */
  memcpy(&bits, &float_value, sizeof bits);

  if ((data_location() & 0x3) != 0) {
    store_half(bits & 0xffff);
    store_half((bits >> 16) & 0xffff);
  } else {
    memory_write_word(data_location(), bits);
    increment_data_location(BYTES_PER_WORD);
  }
}
