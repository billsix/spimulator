/* SPIM S20 MIPS simulator.
   Code to create, maintain and access memory.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"

/* Exported Variables: */

register_word general_purpose_registers[GENERAL_PURPOSE_REGISTER_COUNT];
register_word MULTIPLY_DIVIDE_HIGH, MULTIPLY_DIVIDE_LOW;
memory_address PROGRAM_COUNTER;
double* floating_point_double_view;   /* Dynamically allocate so overlay */
float* floating_point_single_view;    /* is possible */
int32_t* floating_point_integer_view; /* is possible */
register_word coprocessor_control_registers[4][32],
    coprocessor_registers[4][32];

mips_instruction** text_segment;
bool text_modified; /* => text segment was written */
memory_address text_top;
memory_word* data_segment;
bool data_modified;            /* => a data segment was written */
short* data_segment_halfwords; /* Points to same vector as DATA_SEG */
int8_t* data_segment_bytes;    /* Ditto */
memory_address data_top;
memory_address global_pointer_midpoint; /* Middle of $gp area */
memory_word* stack_segment;
short* stack_segment_halfwords; /* Points to same vector as STACK_SEG */
int8_t* stack_segment_bytes;    /* Ditto */
memory_address stack_bottom;
mips_instruction** kernel_text_segment;
memory_address kernel_text_top;
memory_word* kernel_data_segment;
short* kernel_data_segment_halfwords;
int8_t* kernel_data_segment_bytes;
memory_address kernel_data_top;

/* Local functions: */

static memory_word bad_memory_read(memory_address address, int mask);
static void bad_memory_write(memory_address address, memory_word value,
                             int mask);
static mips_instruction* bad_text_read(memory_address address);
static void bad_text_write(memory_address address,
                           const mips_instruction* instruction);
static void free_instructions(mips_instruction** instruction, int n);
static memory_word read_memory_mapped_IO(memory_address address);
static void write_memory_mapped_IO(memory_address address, memory_word value);

/* Local variables: */

static int32_t data_size_limit, stack_size_limit, kernel_data_size_limit;

/* Memory is allocated in five chunks:
        text, data, stack, kernel text, and kernel data.

   The arrays are independent and have different semantics.

   text is allocated from 0x400000 up and only contains INSTRUCTIONs.
   It does not expand.

   data is allocated from 0x10000000 up.  It can be extended by the
   SBRK system call.  Programs can only read and write this segment.

   stack grows from 0x7fffefff down.  It is automatically extended.
   Programs can only read and write this segment.

   k_text is like text, except its is allocated from 0x80000000 up.

   k_data is like data, but is allocated from 0x90000000 up.

   Both kernel text and kernel data can only be accessed in kernel mode.
*/

/* The text segments contain pointers to instructions, not actual
   instructions, so they must be allocated large enough to hold as many
   pointers as there would be instructions (the two differ on machines in
   which pointers are not 32 bits long).  The following calculations round
   up in case size is not a multiple of BYTES_PER_WORD.  */

#define BYTES_TO_INSTRUCTION(N) \
  (((N) + BYTES_PER_WORD - 1) / BYTES_PER_WORD * sizeof(mips_instruction*))

void make_memory(int text_size, int data_size, int data_limit, int stack_size,
                 int stack_limit, int kernel_text_size, int kernel_data_size,
                 int kernel_data_limit) {
  /* A text segment that can hold no instruction, or a kernel text segment
     that stops short of the exception vector, would make every exception
     jump to an address outside its segment, and the run would loop. */
  /* dispatch-check: ok -- two independent checks of two flags, not one value */
  if (text_size < BYTES_PER_WORD)
    fatal_error(
        "-stext %d: the text segment needs at least one word (4 bytes)\n",
        text_size);
  if (kernel_text_size <
      (int)(EXCEPTION_ADDRESS - KERNEL_TEXT_BOTTOM) + BYTES_PER_WORD)
    fatal_error(
        "-sktext %d: the kernel text segment must hold the exception handler "
        "at 0x80000180 (at least 388 bytes)\n",
        kernel_text_size);
  if (data_size <= 65536) data_size = 65536;
  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */

  if (text_segment == nullptr)
    text_segment = (mips_instruction**)allocate_or_exit(
        BYTES_TO_INSTRUCTION((size_t)text_size));
  else {
    free_instructions(text_segment,
                      (int)((text_top - TEXT_BOTTOM) / BYTES_PER_WORD));
    text_segment = (mips_instruction**)reallocate_or_exit(
        text_segment, BYTES_TO_INSTRUCTION((size_t)text_size));
  }
  memset(text_segment, 0, BYTES_TO_INSTRUCTION((size_t)text_size));
  text_top = TEXT_BOTTOM + (memory_address)text_size;

  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (data_segment == nullptr)
    data_segment = (memory_word*)allocate_or_exit((size_t)data_size);
  else
    data_segment =
        (memory_word*)reallocate_or_exit(data_segment, (size_t)data_size);
  memset(data_segment, 0, (size_t)data_size);
  data_segment_bytes = (int8_t*)data_segment;
  data_segment_halfwords = (short*)data_segment;
  data_top = DATA_BOTTOM + (memory_address)data_size;
  data_size_limit = data_limit;

  stack_size = ROUND_UP(stack_size, BYTES_PER_WORD); /* Keep word aligned */
  if (stack_segment == nullptr)
    stack_segment = (memory_word*)allocate_or_exit((size_t)stack_size);
  else
    stack_segment =
        (memory_word*)reallocate_or_exit(stack_segment, (size_t)stack_size);
  memset(stack_segment, 0, (size_t)stack_size);
  stack_segment_bytes = (int8_t*)stack_segment;
  stack_segment_halfwords = (short*)stack_segment;
  stack_bottom = STACK_TOP - (memory_address)stack_size;
  stack_size_limit = stack_limit;

  if (kernel_text_segment == nullptr)
    kernel_text_segment = (mips_instruction**)allocate_or_exit(
        BYTES_TO_INSTRUCTION((size_t)kernel_text_size));
  else {
    free_instructions(
        kernel_text_segment,
        (int)((kernel_text_top - KERNEL_TEXT_BOTTOM) / BYTES_PER_WORD));
    kernel_text_segment = (mips_instruction**)reallocate_or_exit(
        kernel_text_segment, BYTES_TO_INSTRUCTION((size_t)kernel_text_size));
  }
  memset(kernel_text_segment, 0,
         BYTES_TO_INSTRUCTION((size_t)kernel_text_size));
  kernel_text_top = KERNEL_TEXT_BOTTOM + (memory_address)kernel_text_size;

  kernel_data_size =
      ROUND_UP(kernel_data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (kernel_data_segment == nullptr)
    kernel_data_segment =
        (memory_word*)allocate_or_exit((size_t)kernel_data_size);
  else
    kernel_data_segment = (memory_word*)reallocate_or_exit(
        kernel_data_segment, (size_t)kernel_data_size);
  memset(kernel_data_segment, 0, (size_t)kernel_data_size);
  kernel_data_segment_bytes = (int8_t*)kernel_data_segment;
  kernel_data_segment_halfwords = (short*)kernel_data_segment;
  kernel_data_top = KERNEL_DATA_BOTTOM + (memory_address)kernel_data_size;
  kernel_data_size_limit = kernel_data_limit;

  text_modified = true;
  data_modified = true;
}

/* Free the storage used by the old instructions in memory. */

static void free_instructions(mips_instruction** instruction, int n) {
  for (; n > 0; n--, instruction++)
    if (*instruction) free_instruction(*instruction);
}

/* Expand the data segment by adding N bytes. */

void expand_data(int additional_bytes) {
  const int delta =
      ROUND_UP(additional_bytes, BYTES_PER_WORD); /* Keep word aligned */
  const int old_size = (int)(data_top - DATA_BOTTOM);
  const int new_size = old_size + delta;

  if ((additional_bytes < 0) || (new_size > data_size_limit)) {
    error("Can't expand data segment by %d bytes to %d bytes\n",
          additional_bytes, new_size);
    run_error("Use -ldata # with # > %d\n", new_size);
  }
  data_segment = (memory_word*)realloc(data_segment, (size_t)new_size);
  if (data_segment == nullptr) fatal_error("realloc failed in expand_data\n");

  data_segment_bytes = (int8_t*)data_segment;
  data_segment_halfwords = (short*)data_segment;
  data_top += (memory_address)delta;

  /* Zero new memory */
  for (int8_t* p = data_segment_bytes + old_size;
       p < data_segment_bytes + new_size;)
    *p++ = 0;
}

/* Expand the stack segment by adding N bytes.  Can't use REALLOC
   since it copies from bottom of memory blocks and stack grows down from
   top of its block. */

void expand_stack(int additional_bytes) {
  const int delta =
      ROUND_UP(additional_bytes, BYTES_PER_WORD); /* Keep word aligned */
  const int old_size = (int)(STACK_TOP - stack_bottom);
  const int new_size = old_size + MAX(delta, old_size);
  memory_word* new_segment;
  memory_word *old_pointer, *new_pointer;

  if ((additional_bytes < 0) || (new_size > stack_size_limit)) {
    run_error(
        "Can't expand stack segment by %d bytes to %d bytes.\nUse -lstack # "
        "with # > %d\n",
        additional_bytes, new_size, new_size);
  }

  new_segment = (memory_word*)allocate_or_exit((size_t)new_size);
  memset(new_segment, 0, (size_t)new_size);

  old_pointer = stack_segment + (old_size / BYTES_PER_WORD - 1);
  new_pointer = new_segment + (new_size / BYTES_PER_WORD - 1);
  for (; old_pointer >= stack_segment;) *new_pointer-- = *old_pointer--;

  free(stack_segment);
  stack_segment = new_segment;
  stack_segment_bytes = (int8_t*)stack_segment;
  stack_segment_halfwords = (short*)stack_segment;
  stack_bottom -= (memory_address)(new_size - old_size);
}

/* Expand the kernel data segment by adding N bytes. */

void expand_kernel_data(int additional_bytes) {
  const int delta =
      ROUND_UP(additional_bytes, BYTES_PER_WORD); /* Keep word aligned */
  const int old_size = (int)(kernel_data_top - KERNEL_DATA_BOTTOM);
  const int new_size = old_size + delta;
  int8_t* p;

  if ((additional_bytes < 0) || (new_size > kernel_data_size_limit)) {
    run_error(
        "Can't expand kernel data segment by %d bytes to %d bytes.\nUse "
        "-lkdata # with # > %d\n",
        additional_bytes, new_size, new_size);
  }
  kernel_data_segment =
      (memory_word*)realloc(kernel_data_segment, (size_t)new_size);
  if (kernel_data_segment == nullptr)
    fatal_error("realloc failed in expand_k_data\n");

  kernel_data_segment_bytes = (int8_t*)kernel_data_segment;
  kernel_data_segment_halfwords = (short*)kernel_data_segment;
  kernel_data_top += (memory_address)delta;

  /* Zero new memory.  p is a byte pointer, so the sizes are byte offsets
     (as in expand_data). */
  for (p = kernel_data_segment_bytes + old_size;
       p < kernel_data_segment_bytes + new_size;)
    *p++ = 0;
}

/* Access memory */

void* memory_reference(memory_address address) {
  if ((address >= TEXT_BOTTOM) && (address < text_top))
    return address - TEXT_BOTTOM + (char*)text_segment;
  else if ((address >= DATA_BOTTOM) && (address < data_top))
    return address - DATA_BOTTOM + (char*)data_segment;
  else if ((address >= stack_bottom) && (address < STACK_TOP))
    return address - stack_bottom + (char*)stack_segment;
  else if ((address >= KERNEL_TEXT_BOTTOM) && (address < kernel_text_top))
    return address - KERNEL_TEXT_BOTTOM + (char*)kernel_text_segment;
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top))
    return address - KERNEL_DATA_BOTTOM + (char*)kernel_data_segment;
  else {
    run_error("Memory address out of bounds\n");
    return nullptr;
  }
}

mips_instruction* memory_read_instruction(memory_address address) {
  if ((address >= TEXT_BOTTOM) && (address < text_top) && !(address & 0x3))
    return text_segment[(address - TEXT_BOTTOM) >> 2];
  else if ((address >= KERNEL_TEXT_BOTTOM) && (address < kernel_text_top) &&
           !(address & 0x3))
    return kernel_text_segment[(address - KERNEL_TEXT_BOTTOM) >> 2];
  else
    return bad_text_read(address);
}

register_word memory_read_byte(memory_address address) {
  if ((address >= DATA_BOTTOM) && (address < data_top))
    return data_segment_bytes[address - DATA_BOTTOM];
  else if ((address >= stack_bottom) && (address < STACK_TOP))
    return stack_segment_bytes[address - stack_bottom];
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top))
    return kernel_data_segment_bytes[address - KERNEL_DATA_BOTTOM];
  else
    return bad_memory_read(address, 0);
}

register_word memory_read_half(memory_address address) {
  if ((address >= DATA_BOTTOM) && (address < data_top) && !(address & 0x1))
    return data_segment_halfwords[(address - DATA_BOTTOM) >> 1];
  else if ((address >= stack_bottom) && (address < STACK_TOP) &&
           !(address & 0x1))
    return stack_segment_halfwords[(address - stack_bottom) >> 1];
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top) &&
           !(address & 0x1))
    return kernel_data_segment_halfwords[(address - KERNEL_DATA_BOTTOM) >> 1];
  else
    return bad_memory_read(address, 0x1);
}

register_word memory_read_word(memory_address address) {
  if ((address >= DATA_BOTTOM) && (address < data_top) && !(address & 0x3))
    return data_segment[(address - DATA_BOTTOM) >> 2];
  else if ((address >= stack_bottom) && (address < STACK_TOP) &&
           !(address & 0x3))
    return stack_segment[(address - stack_bottom) >> 2];
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top) &&
           !(address & 0x3))
    return kernel_data_segment[(address - KERNEL_DATA_BOTTOM) >> 2];
  else
    return bad_memory_read(address, 0x3);
}

bool memory_peek_instruction(memory_address address, mips_instruction** out) {
  if ((address & 0x3) != 0) {
    return false;
  } else if ((address >= TEXT_BOTTOM) && (address < text_top)) {
    *out = text_segment[(address - TEXT_BOTTOM) >> 2];
    return true;
  } else if ((address >= KERNEL_TEXT_BOTTOM) && (address < kernel_text_top)) {
    *out = kernel_text_segment[(address - KERNEL_TEXT_BOTTOM) >> 2];
    return true;
  } else {
    return false;
  }
}

bool memory_peek(memory_address address, int mask, register_word* out) {
  *out = 0;
  if ((address & (memory_address)mask) != 0) {
    return false;
  } else if (((address >= DATA_BOTTOM) && (address < data_top)) ||
             ((address >= stack_bottom) && (address < STACK_TOP)) ||
             ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top))) {
    /* In range and aligned, so the reader takes its plain path. */
    switch (mask) {
      case 0x0:
        *out = memory_read_byte(address);
        return true;
      case 0x1:
        *out = memory_read_half(address);
        return true;
      case 0x3:
        *out = memory_read_word(address);
        return true;
      default:
        return false;
    }
  } else if ((address >= TEXT_BOTTOM) && (address < text_top)) {
    /* bad_memory_read's text-segment branch reads the encoding and has no
       side effects for an aligned address. */
    *out = bad_memory_read(address, mask);
    return true;
  } else {
    return false;
  }
}

void memory_write_instruction(memory_address address,
                              mips_instruction* instruction) {
  text_modified = true;
  if ((address >= TEXT_BOTTOM) && (address < text_top) && !(address & 0x3))
    text_segment[(address - TEXT_BOTTOM) >> 2] = instruction;
  else if ((address >= KERNEL_TEXT_BOTTOM) && (address < kernel_text_top) &&
           !(address & 0x3))
    kernel_text_segment[(address - KERNEL_TEXT_BOTTOM) >> 2] = instruction;
  else
    bad_text_write(address, instruction);
}

void memory_write_byte(memory_address address, register_word value) {
  data_modified = true;
  if ((address >= DATA_BOTTOM) && (address < data_top))
    data_segment_bytes[address - DATA_BOTTOM] = (int8_t)value;
  else if ((address >= stack_bottom) && (address < STACK_TOP))
    stack_segment_bytes[address - stack_bottom] = (int8_t)value;
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top))
    kernel_data_segment_bytes[address - KERNEL_DATA_BOTTOM] = (int8_t)value;
  else
    bad_memory_write(address, value, 0);
}

void memory_write_half(memory_address address, register_word value) {
  data_modified = true;
  if ((address >= DATA_BOTTOM) && (address < data_top) && !(address & 0x1))
    data_segment_halfwords[(address - DATA_BOTTOM) >> 1] = (short)value;
  else if ((address >= stack_bottom) && (address < STACK_TOP) &&
           !(address & 0x1))
    stack_segment_halfwords[(address - stack_bottom) >> 1] = (short)value;
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top) &&
           !(address & 0x1))
    kernel_data_segment_halfwords[(address - KERNEL_DATA_BOTTOM) >> 1] =
        (short)value;
  else
    bad_memory_write(address, value, 0x1);
}

void memory_write_word(memory_address address, register_word value) {
  data_modified = true;
  if ((address >= DATA_BOTTOM) && (address < data_top) && !(address & 0x3))
    data_segment[(address - DATA_BOTTOM) >> 2] = (memory_word)value;
  else if ((address >= stack_bottom) && (address < STACK_TOP) &&
           !(address & 0x3))
    stack_segment[(address - stack_bottom) >> 2] = (memory_word)value;
  else if ((address >= KERNEL_DATA_BOTTOM) && (address < kernel_data_top) &&
           !(address & 0x3))
    kernel_data_segment[(address - KERNEL_DATA_BOTTOM) >> 2] =
        (memory_word)value;
  else
    bad_memory_write(address, value, 0x3);
}

/* Handle the infrequent and erroneous cases in memory accesses. */

static mips_instruction* bad_text_read(memory_address address) {
  RAISE_EXCEPTION(EXCEPTION_CODE_INSTRUCTION_BUS_ERROR,
                  coprocessor_0_bad_virtual_address = (register_word)address);
  return (instruction_decode(0));
}

static void bad_text_write(memory_address address,
                           const mips_instruction* instruction) {
  RAISE_EXCEPTION(EXCEPTION_CODE_INSTRUCTION_BUS_ERROR,
                  coprocessor_0_bad_virtual_address = (register_word)address);
  memory_write_word(address, ENCODING(instruction));
}

/* The encoding of the user-text word holding ADDR, or 0 for a word no
   instruction was stored in (as a word load of it reads). */
static memory_word text_word_at(memory_address address) {
  mips_instruction* const instruction =
      text_segment[(address - TEXT_BOTTOM) >> 2];
  return instruction == nullptr ? 0 : ENCODING(instruction);
}

static memory_word bad_memory_read(memory_address address, int mask) {
  memory_word temporary;

  if ((address & (memory_address)mask) != 0)
    RAISE_EXCEPTION(EXCEPTION_CODE_ADDRESS_ERROR_LOAD,
                    coprocessor_0_bad_virtual_address = (register_word)address)
  else if (address >= TEXT_BOTTOM && address < text_top)
    switch (mask) {
      case 0x0:
        temporary = text_word_at(address);
#ifdef SPIM_BIGENDIAN
        temporary = (unsigned)temporary >> (8 * (3 - (address & 0x3)));
#else
        temporary = (memory_word)((unsigned)temporary >> (8 * (address & 0x3)));
#endif
        return (0xff & temporary);

      case 0x1:
        temporary = text_word_at(address);
#ifdef SPIM_BIGENDIAN
        temporary = (unsigned)temporary >> (8 * (2 - (address & 0x2)));
#else
        temporary = (memory_word)((unsigned)temporary >> (8 * (address & 0x2)));
#endif
        return (0xffff & temporary);

      case 0x3: {
        mips_instruction* const instruction =
            text_segment[(address - TEXT_BOTTOM) >> 2];
        if (instruction == nullptr)
          return 0;
        else
          return (ENCODING(instruction));
      }

      default:
        run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
    }
  else if (address > data_top &&
           address < stack_bottom
           /* If more than 16 MB below stack, probably is bad data ref */
           && address > stack_bottom - 16 * mega) {
    /* Grow stack segment */
    expand_stack((int)(stack_bottom - address + 4));
    return (0);
  } else if (MEMORY_MAPPED_IO_BOTTOM <= address)
    /* The I/O region runs to MEMORY_MAPPED_IO_TOP, the last address. */
    return (read_memory_mapped_IO(address));
  else
    /* Address out of range */
    RAISE_EXCEPTION(EXCEPTION_CODE_DATA_BUS_ERROR,
                    coprocessor_0_bad_virtual_address = (register_word)address)
  return (0);
}

static void bad_memory_write(memory_address address, memory_word value,
                             int mask) {
  memory_word temporary;

  if ((address & (memory_address)mask) != 0) /* Unaligned address fault */
    RAISE_EXCEPTION(EXCEPTION_CODE_ADDRESS_ERROR_STORE,
                    coprocessor_0_bad_virtual_address = (register_word)address)
  else if (address >= TEXT_BOTTOM && address < text_top) {
    if (text_segment[(address - TEXT_BOTTOM) >> 2] == nullptr) {
      /* No instruction at address. Only create instruction from
               full-word write. */
      temporary = (mask == 3) ? value : 0;
    } else {
      switch (mask) {
        case 0x0:
          temporary = ENCODING(text_segment[(address - TEXT_BOTTOM) >> 2]);
#ifdef SPIM_BIGENDIAN
          temporary = (memory_word)(((uint32_t)temporary &
                                     ~(0xffu << (8 * (3 - (address & 0x3))))) |
                                    ((uint32_t)value & 0xffu)
                                        << (8 * (3 - (address & 0x3))));
#else
          temporary =
              (memory_word)(((uint32_t)temporary &
                             ~(0xffu << (8 * (address & 0x3)))) |
                            ((uint32_t)value & 0xffu) << (8 * (address & 0x3)));
#endif
          break;

        case 0x1:
          temporary = ENCODING(text_segment[(address - TEXT_BOTTOM) >> 2]);
#ifdef SPIM_BIGENDIAN
          temporary =
              (memory_word)(((uint32_t)temporary &
                             ~(0xffffu << (8 * (2 - (address & 0x2))))) |
                            ((uint32_t)value & 0xffffu)
                                << (8 * (2 - (address & 0x2))));
#else
          temporary = (memory_word)(((uint32_t)temporary &
                                     ~(0xffffu << (8 * (address & 0x2)))) |
                                    ((uint32_t)value & 0xffffu)
                                        << (8 * (address & 0x2)));
#endif
          break;

        case 0x3:
          temporary = value;
          break;

        default:
          temporary = 0;
          run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
      }
      free_instruction(text_segment[(address - TEXT_BOTTOM) >> 2]);
    }

    text_segment[(address - TEXT_BOTTOM) >> 2] = instruction_decode(temporary);
    text_modified = true;
  } else if (address > data_top &&
             address < stack_bottom
             /* If more than 16 MB below stack, probably is bad data ref */
             && address > stack_bottom - 16 * mega) {
    /* Grow stack segment */
    expand_stack((int)(stack_bottom - address + 4));
    if (address >= stack_bottom) {
      if (mask == 0)
        stack_segment_bytes[address - stack_bottom] = (char)value;
      else if (mask == 1)
        stack_segment_halfwords[(address - stack_bottom) >> 1] = (short)value;
      else
        stack_segment[(address - stack_bottom) >> 2] = value;
    } else
      RAISE_EXCEPTION(
          EXCEPTION_CODE_DATA_BUS_ERROR,
          coprocessor_0_bad_virtual_address = (register_word)address)

    data_modified = true;
  } else if (MEMORY_MAPPED_IO_BOTTOM <= address)
    /* The I/O region runs to MEMORY_MAPPED_IO_TOP, the last address. */
    write_memory_mapped_IO(address, value);
  else
    /* Address out of range */
    RAISE_EXCEPTION(EXCEPTION_CODE_DATA_BUS_ERROR,
                    coprocessor_0_bad_virtual_address = (register_word)address)
}

/* Memory-mapped IO routines. */

static uint32_t receiver_control = 0; /* No input */
static int receiver_buffer;
static int receiver_buffer_full_timer = 0;

static uint32_t transmitter_control = TRANSMITTER_READY; /* Ready to write */
static int transmitter_buffer;
static int transmitter_buffer_full_timer = 0;

/* Check if input is available and output is possible.  If so, update the
   memory-mapped control registers and buffers. */

void check_memory_mapped_IO(void) {
  if (receiver_buffer_full_timer > 0) {
    /* Do not check for more input until this interval expires. */
    receiver_buffer_full_timer -= 1;
  } else if (console_input_available()) {
    /* Read new char into the buffer and raise an interrupt, if interrupts
       are enabled for device. */
    /* assert(receiver_buffer_full_timer == 0); */
    receiver_buffer = get_console_char();
    receiver_control |= RECEIVER_READY;
    receiver_buffer_full_timer = RECEIVER_INTERVAL;
    if ((uint32_t)receiver_control & RECEIVER_INTERRUPT_ENABLE) {
      RAISE_INTERRUPT(RECEIVER_INTERRUPT_LEVEL);
    }
  } else {
    /* No console input is waiting: nothing to receive. */
  }

  if (transmitter_buffer_full_timer > 0) {
    /* Do not allow output until this interval expires. */
    transmitter_buffer_full_timer -= 1;
  } else if (!((uint32_t)transmitter_control & TRANSMITTER_READY)) {
    /* Done writing: empty the buffer and raise an interrupt, if interrupts
       are enabled for device. */
    /* assert(transmitter_buffer_full_timer == 0); */
    transmitter_control |= TRANSMITTER_READY;
    if ((uint32_t)transmitter_control & TRANSMITTER_INTERRUPT_ENABLE) {
      RAISE_INTERRUPT(TRANSMITTER_INTERRUPT_LEVEL);
    }
  } else {
    /* The transmitter is already ready: nothing to finish. */
  }
}

/* Invoked on a write to the memory-mapped IO area. */

static void write_memory_mapped_IO(memory_address address, memory_word value) {
  switch (address) {
    case TRANSMITTER_CONTROL_ADDRESS:
      /* Program can only set the interrupt enable, not ready, bit. */
      if (((uint32_t)value & TRANSMITTER_INTERRUPT_ENABLE) != 0) {
        /* Enable interrupts: */
        transmitter_control |= TRANSMITTER_INTERRUPT_ENABLE;
        if ((uint32_t)transmitter_control & TRANSMITTER_READY) {
          /* Raise interrupt on enabling a ready transmitter */
          RAISE_INTERRUPT(TRANSMITTER_INTERRUPT_LEVEL);
        }
      } else {
        /* Disable interrupts: */
        transmitter_control &= ~TRANSMITTER_INTERRUPT_ENABLE;
        CLEAR_INTERRUPT(
            TRANSMITTER_INTERRUPT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case TRANSMITTER_BUFFER_ADDRESS:
      /* Ignore write if device is not ready. */
      if (((uint32_t)transmitter_control & TRANSMITTER_READY) != 0) {
        /* Write char: */
        transmitter_buffer = value & 0xff;
        put_console_char((char)transmitter_buffer);
        /* Device is busy for a while: */
        transmitter_control &= ~TRANSMITTER_READY;
        transmitter_buffer_full_timer = TRANSMITTER_LATENCY;
        CLEAR_INTERRUPT(
            TRANSMITTER_INTERRUPT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case RECEIVER_CONTROL_ADDRESS:
      /* Program can only set the interrupt enable, not ready, bit. */
      if (((uint32_t)value & RECEIVER_INTERRUPT_ENABLE) != 0) {
        /* Enable interrupts: */
        receiver_control |= RECEIVER_INTERRUPT_ENABLE;
        if ((uint32_t)receiver_control & RECEIVER_READY) {
          /* Raise interrupt on enabling a ready receiver */
          RAISE_INTERRUPT(RECEIVER_INTERRUPT_LEVEL);
        }
      } else {
        /* Disable interrupts: */
        receiver_control &= ~RECEIVER_INTERRUPT_ENABLE;
        CLEAR_INTERRUPT(RECEIVER_INTERRUPT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case RECEIVER_BUFFER_ADDRESS:
      /* Nop: program can't change buffer. */
      break;

    default:
      run_error("Write to unused memory-mapped IO address (0x%x)\n", address);
  }
}

/* Invoked on a read in the memory-mapped IO area. */

static memory_word read_memory_mapped_IO(memory_address address) {
  switch (address) {
    case TRANSMITTER_CONTROL_ADDRESS:
      return ((memory_word)transmitter_control);

    case TRANSMITTER_BUFFER_ADDRESS:
      return (transmitter_buffer & 0xff);

    case RECEIVER_CONTROL_ADDRESS:
      return ((memory_word)receiver_control);

    case RECEIVER_BUFFER_ADDRESS:
      receiver_control &= ~RECEIVER_READY; /* Buffer now empty */
      receiver_buffer_full_timer = 0;
      CLEAR_INTERRUPT(RECEIVER_INTERRUPT_LEVEL); /* Clear IP bit in Cause */
      return (receiver_buffer & 0xff);

    default:
      run_error("Read from unused memory-mapped IO address (0x%x)\n", address);
      return (0);
  }
}

/* Misc. routines */

void print_memory(memory_address address) {
  memory_word value;

  if ((address & 0x3) != 0)
    address &= (memory_address)~0x3; /* Address must be word-aligned */

  if (TEXT_BOTTOM <= address && address < text_top)
    print_instruction(address);
  else if (DATA_BOTTOM <= address && address < data_top) {
    value = memory_read_word(address);
    write_output(message_out, "Data seg @ 0x%08x (%d) = 0x%08x (%d)\n", address,
                 address, value, value);
  } else if (stack_bottom <= address && address < STACK_TOP) {
    value = memory_read_word(address);
    write_output(message_out, "Stack seg @ 0x%08x (%d) = 0x%08x (%d)\n",
                 address, address, value, value);
  } else if (KERNEL_TEXT_BOTTOM <= address && address < kernel_text_top)
    print_instruction(address);
  else if (KERNEL_DATA_BOTTOM <= address && address < kernel_data_top) {
    value = memory_read_word(address);
    write_output(message_out, "Kernel Data seg @ 0x%08x (%d) = 0x%08x (%d)\n",
                 address, address, value, value);
  } else
    error("Address 0x%08x (%d) to print_mem is out of bounds\n", address,
          address);
}
