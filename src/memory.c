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

reg_word gpr[R_LENGTH];
reg_word HI, LO;
int HI_present, LO_present;
mem_addr PC, nPC;
double* fp_double_view; /* Dynamically allocate so overlay */
float* fp_single_view;  /* is possible */
int32_t* fp_int_view;   /* is possible */
reg_word coprocessor_control_registers[4][32], coprocessor_registers[4][32];

mips_instruction** text_seg;
bool text_modified; /* => text segment was written */
mem_addr text_top;
mem_word* data_seg;
bool data_modified; /* => a data segment was written */
short* data_seg_h;  /* Points to same vector as DATA_SEG */
int8_t* data_seg_b; /* Ditto */
mem_addr data_top;
mem_addr gp_midpoint; /* Middle of $gp area */
mem_word* stack_seg;
short* stack_seg_h;  /* Points to same vector as STACK_SEG */
int8_t* stack_seg_b; /* Ditto */
mem_addr stack_bot;
mips_instruction** k_text_seg;
mem_addr k_text_top;
mem_word* k_data_seg;
short* k_data_seg_h;
int8_t* k_data_seg_b;
mem_addr k_data_top;

/* Local functions: */

static mem_word bad_memory_read(mem_addr address, int mask);
static void bad_memory_write(mem_addr address, mem_word value, int mask);
static mips_instruction* bad_text_read(mem_addr address);
static void bad_text_write(mem_addr address, const mips_instruction* instruction);
static void free_instructions(mips_instruction** instruction, int n);
static mem_word read_memory_mapped_IO(mem_addr address);
static void write_memory_mapped_IO(mem_addr address, mem_word value);

/* Local variables: */

static int32_t data_size_limit, stack_size_limit, k_data_size_limit;

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

#define BYTES_TO_INST(N) \
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
  if (kernel_text_size < (int)(EXCEPTION_ADDR - K_TEXT_BOT) + BYTES_PER_WORD)
    fatal_error(
        "-sktext %d: the kernel text segment must hold the exception handler "
        "at 0x80000180 (at least 388 bytes)\n",
        kernel_text_size);
  if (data_size <= 65536) data_size = 65536;
  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */

  if (text_seg == nullptr)
    text_seg = (mips_instruction**)allocate_or_exit(BYTES_TO_INST((size_t)text_size));
  else {
    free_instructions(text_seg, (int)((text_top - TEXT_BOT) / BYTES_PER_WORD));
    text_seg = (mips_instruction**)reallocate_or_exit(text_seg,
                                            BYTES_TO_INST((size_t)text_size));
  }
  memset(text_seg, 0, BYTES_TO_INST((size_t)text_size));
  text_top = TEXT_BOT + (mem_addr)text_size;

  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (data_seg == nullptr)
    data_seg = (mem_word*)allocate_or_exit((size_t)data_size);
  else
    data_seg = (mem_word*)reallocate_or_exit(data_seg, (size_t)data_size);
  memset(data_seg, 0, (size_t)data_size);
  data_seg_b = (int8_t*)data_seg;
  data_seg_h = (short*)data_seg;
  data_top = DATA_BOT + (mem_addr)data_size;
  data_size_limit = data_limit;

  stack_size = ROUND_UP(stack_size, BYTES_PER_WORD); /* Keep word aligned */
  if (stack_seg == nullptr)
    stack_seg = (mem_word*)allocate_or_exit((size_t)stack_size);
  else
    stack_seg = (mem_word*)reallocate_or_exit(stack_seg, (size_t)stack_size);
  memset(stack_seg, 0, (size_t)stack_size);
  stack_seg_b = (int8_t*)stack_seg;
  stack_seg_h = (short*)stack_seg;
  stack_bot = STACK_TOP - (mem_addr)stack_size;
  stack_size_limit = stack_limit;

  if (k_text_seg == nullptr)
    k_text_seg =
        (mips_instruction**)allocate_or_exit(BYTES_TO_INST((size_t)kernel_text_size));
  else {
    free_instructions(k_text_seg,
                      (int)((k_text_top - K_TEXT_BOT) / BYTES_PER_WORD));
    k_text_seg = (mips_instruction**)reallocate_or_exit(
        k_text_seg, BYTES_TO_INST((size_t)kernel_text_size));
  }
  memset(k_text_seg, 0, BYTES_TO_INST((size_t)kernel_text_size));
  k_text_top = K_TEXT_BOT + (mem_addr)kernel_text_size;

  kernel_data_size = ROUND_UP(kernel_data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (k_data_seg == nullptr)
    k_data_seg = (mem_word*)allocate_or_exit((size_t)kernel_data_size);
  else
    k_data_seg = (mem_word*)reallocate_or_exit(k_data_seg, (size_t)kernel_data_size);
  memset(k_data_seg, 0, (size_t)kernel_data_size);
  k_data_seg_b = (int8_t*)k_data_seg;
  k_data_seg_h = (short*)k_data_seg;
  k_data_top = K_DATA_BOT + (mem_addr)kernel_data_size;
  k_data_size_limit = kernel_data_limit;

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
  const int old_size = (int)(data_top - DATA_BOT);
  const int new_size = old_size + delta;

  if ((additional_bytes < 0) || (new_size > data_size_limit)) {
    error("Can't expand data segment by %d bytes to %d bytes\n", additional_bytes,
          new_size);
    run_error("Use -ldata # with # > %d\n", new_size);
  }
  data_seg = (mem_word*)realloc(data_seg, (size_t)new_size);
  if (data_seg == nullptr) fatal_error("realloc failed in expand_data\n");

  data_seg_b = (int8_t*)data_seg;
  data_seg_h = (short*)data_seg;
  data_top += (mem_addr)delta;

  /* Zero new memory */
  for (int8_t* p = data_seg_b + old_size; p < data_seg_b + new_size;) *p++ = 0;
}

/* Expand the stack segment by adding N bytes.  Can't use REALLOC
   since it copies from bottom of memory blocks and stack grows down from
   top of its block. */

void expand_stack(int additional_bytes) {
  const int delta =
      ROUND_UP(additional_bytes, BYTES_PER_WORD); /* Keep word aligned */
  const int old_size = (int)(STACK_TOP - stack_bot);
  const int new_size = old_size + MAX(delta, old_size);
  mem_word* new_segment;
  mem_word *old_pointer, *new_pointer;

  if ((additional_bytes < 0) || (new_size > stack_size_limit)) {
    run_error(
        "Can't expand stack segment by %d bytes to %d bytes.\nUse -lstack # "
        "with # > %d\n",
        additional_bytes, new_size, new_size);
  }

  new_segment = (mem_word*)allocate_or_exit((size_t)new_size);
  memset(new_segment, 0, (size_t)new_size);

  old_pointer = stack_seg + (old_size / BYTES_PER_WORD - 1);
  new_pointer = new_segment + (new_size / BYTES_PER_WORD - 1);
  for (; old_pointer >= stack_seg;) *new_pointer-- = *old_pointer--;

  free(stack_seg);
  stack_seg = new_segment;
  stack_seg_b = (int8_t*)stack_seg;
  stack_seg_h = (short*)stack_seg;
  stack_bot -= (mem_addr)(new_size - old_size);
}

/* Expand the kernel data segment by adding N bytes. */

void expand_kernel_data(int additional_bytes) {
  const int delta =
      ROUND_UP(additional_bytes, BYTES_PER_WORD); /* Keep word aligned */
  const int old_size = (int)(k_data_top - K_DATA_BOT);
  const int new_size = old_size + delta;
  int8_t* p;

  if ((additional_bytes < 0) || (new_size > k_data_size_limit)) {
    run_error(
        "Can't expand kernel data segment by %d bytes to %d bytes.\nUse "
        "-lkdata # with # > %d\n",
        additional_bytes, new_size, new_size);
  }
  k_data_seg = (mem_word*)realloc(k_data_seg, (size_t)new_size);
  if (k_data_seg == nullptr) fatal_error("realloc failed in expand_k_data\n");

  k_data_seg_b = (int8_t*)k_data_seg;
  k_data_seg_h = (short*)k_data_seg;
  k_data_top += (mem_addr)delta;

  /* Zero new memory.  p is a byte pointer, so the sizes are byte offsets
     (as in expand_data). */
  for (p = k_data_seg_b + old_size; p < k_data_seg_b + new_size;) *p++ = 0;
}

/* Access memory */

void* memory_reference(mem_addr address) {
  if ((address >= TEXT_BOT) && (address < text_top))
    return address - TEXT_BOT + (char*)text_seg;
  else if ((address >= DATA_BOT) && (address < data_top))
    return address - DATA_BOT + (char*)data_seg;
  else if ((address >= stack_bot) && (address < STACK_TOP))
    return address - stack_bot + (char*)stack_seg;
  else if ((address >= K_TEXT_BOT) && (address < k_text_top))
    return address - K_TEXT_BOT + (char*)k_text_seg;
  else if ((address >= K_DATA_BOT) && (address < k_data_top))
    return address - K_DATA_BOT + (char*)k_data_seg;
  else {
    run_error("Memory address out of bounds\n");
    return nullptr;
  }
}

mips_instruction* memory_read_instruction(mem_addr address) {
  if ((address >= TEXT_BOT) && (address < text_top) && !(address & 0x3))
    return text_seg[(address - TEXT_BOT) >> 2];
  else if ((address >= K_TEXT_BOT) && (address < k_text_top) && !(address & 0x3))
    return k_text_seg[(address - K_TEXT_BOT) >> 2];
  else
    return bad_text_read(address);
}

reg_word memory_read_byte(mem_addr address) {
  if ((address >= DATA_BOT) && (address < data_top))
    return data_seg_b[address - DATA_BOT];
  else if ((address >= stack_bot) && (address < STACK_TOP))
    return stack_seg_b[address - stack_bot];
  else if ((address >= K_DATA_BOT) && (address < k_data_top))
    return k_data_seg_b[address - K_DATA_BOT];
  else
    return bad_memory_read(address, 0);
}

reg_word memory_read_half(mem_addr address) {
  if ((address >= DATA_BOT) && (address < data_top) && !(address & 0x1))
    return data_seg_h[(address - DATA_BOT) >> 1];
  else if ((address >= stack_bot) && (address < STACK_TOP) && !(address & 0x1))
    return stack_seg_h[(address - stack_bot) >> 1];
  else if ((address >= K_DATA_BOT) && (address < k_data_top) && !(address & 0x1))
    return k_data_seg_h[(address - K_DATA_BOT) >> 1];
  else
    return bad_memory_read(address, 0x1);
}

reg_word memory_read_word(mem_addr address) {
  if ((address >= DATA_BOT) && (address < data_top) && !(address & 0x3))
    return data_seg[(address - DATA_BOT) >> 2];
  else if ((address >= stack_bot) && (address < STACK_TOP) && !(address & 0x3))
    return stack_seg[(address - stack_bot) >> 2];
  else if ((address >= K_DATA_BOT) && (address < k_data_top) && !(address & 0x3))
    return k_data_seg[(address - K_DATA_BOT) >> 2];
  else
    return bad_memory_read(address, 0x3);
}

bool memory_peek_instruction(mem_addr address, mips_instruction** out) {
  if ((address & 0x3) != 0) {
    return false;
  } else if ((address >= TEXT_BOT) && (address < text_top)) {
    *out = text_seg[(address - TEXT_BOT) >> 2];
    return true;
  } else if ((address >= K_TEXT_BOT) && (address < k_text_top)) {
    *out = k_text_seg[(address - K_TEXT_BOT) >> 2];
    return true;
  } else {
    return false;
  }
}

bool memory_peek(mem_addr address, int mask, reg_word* out) {
  *out = 0;
  if ((address & (mem_addr)mask) != 0) {
    return false;
  } else if (((address >= DATA_BOT) && (address < data_top)) ||
             ((address >= stack_bot) && (address < STACK_TOP)) ||
             ((address >= K_DATA_BOT) && (address < k_data_top))) {
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
  } else if ((address >= TEXT_BOT) && (address < text_top)) {
    /* bad_memory_read's text-segment branch reads the encoding and has no
       side effects for an aligned address. */
    *out = bad_memory_read(address, mask);
    return true;
  } else {
    return false;
  }
}

void memory_write_instruction(mem_addr address, mips_instruction* instruction) {
  text_modified = true;
  if ((address >= TEXT_BOT) && (address < text_top) && !(address & 0x3))
    text_seg[(address - TEXT_BOT) >> 2] = instruction;
  else if ((address >= K_TEXT_BOT) && (address < k_text_top) && !(address & 0x3))
    k_text_seg[(address - K_TEXT_BOT) >> 2] = instruction;
  else
    bad_text_write(address, instruction);
}

void memory_write_byte(mem_addr address, reg_word value) {
  data_modified = true;
  if ((address >= DATA_BOT) && (address < data_top))
    data_seg_b[address - DATA_BOT] = (int8_t)value;
  else if ((address >= stack_bot) && (address < STACK_TOP))
    stack_seg_b[address - stack_bot] = (int8_t)value;
  else if ((address >= K_DATA_BOT) && (address < k_data_top))
    k_data_seg_b[address - K_DATA_BOT] = (int8_t)value;
  else
    bad_memory_write(address, value, 0);
}

void memory_write_half(mem_addr address, reg_word value) {
  data_modified = true;
  if ((address >= DATA_BOT) && (address < data_top) && !(address & 0x1))
    data_seg_h[(address - DATA_BOT) >> 1] = (short)value;
  else if ((address >= stack_bot) && (address < STACK_TOP) && !(address & 0x1))
    stack_seg_h[(address - stack_bot) >> 1] = (short)value;
  else if ((address >= K_DATA_BOT) && (address < k_data_top) && !(address & 0x1))
    k_data_seg_h[(address - K_DATA_BOT) >> 1] = (short)value;
  else
    bad_memory_write(address, value, 0x1);
}

void memory_write_word(mem_addr address, reg_word value) {
  data_modified = true;
  if ((address >= DATA_BOT) && (address < data_top) && !(address & 0x3))
    data_seg[(address - DATA_BOT) >> 2] = (mem_word)value;
  else if ((address >= stack_bot) && (address < STACK_TOP) && !(address & 0x3))
    stack_seg[(address - stack_bot) >> 2] = (mem_word)value;
  else if ((address >= K_DATA_BOT) && (address < k_data_top) && !(address & 0x3))
    k_data_seg[(address - K_DATA_BOT) >> 2] = (mem_word)value;
  else
    bad_memory_write(address, value, 0x3);
}

/* Handle the infrequent and erroneous cases in memory accesses. */

static mips_instruction* bad_text_read(mem_addr address) {
  RAISE_EXCEPTION(ExcCode_IBE, CP0_BadVAddr = (reg_word)address);
  return (instruction_decode(0));
}

static void bad_text_write(mem_addr address, const mips_instruction* instruction) {
  RAISE_EXCEPTION(ExcCode_IBE, CP0_BadVAddr = (reg_word)address);
  memory_write_word(address, ENCODING(instruction));
}

/* The encoding of the user-text word holding ADDR, or 0 for a word no
   instruction was stored in (as a word load of it reads). */
static mem_word text_word_at(mem_addr address) {
  mips_instruction* const instruction = text_seg[(address - TEXT_BOT) >> 2];
  return instruction == nullptr ? 0 : ENCODING(instruction);
}

static mem_word bad_memory_read(mem_addr address, int mask) {
  mem_word temporary;

  if ((address & (mem_addr)mask) != 0)
    RAISE_EXCEPTION(ExcCode_AdEL, CP0_BadVAddr = (reg_word)address)
  else if (address >= TEXT_BOT && address < text_top)
    switch (mask) {
      case 0x0:
        temporary = text_word_at(address);
#ifdef SPIM_BIGENDIAN
        tmp = (unsigned)tmp >> (8 * (3 - (addr & 0x3)));
#else
        temporary = (mem_word)((unsigned)temporary >> (8 * (address & 0x3)));
#endif
        return (0xff & temporary);

      case 0x1:
        temporary = text_word_at(address);
#ifdef SPIM_BIGENDIAN
        tmp = (unsigned)tmp >> (8 * (2 - (addr & 0x2)));
#else
        temporary = (mem_word)((unsigned)temporary >> (8 * (address & 0x2)));
#endif
        return (0xffff & temporary);

      case 0x3: {
        mips_instruction* const instruction = text_seg[(address - TEXT_BOT) >> 2];
        if (instruction == nullptr)
          return 0;
        else
          return (ENCODING(instruction));
      }

      default:
        run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
    }
  else if (address > data_top &&
           address < stack_bot
           /* If more than 16 MB below stack, probably is bad data ref */
           && address > stack_bot - 16 * mega) {
    /* Grow stack segment */
    expand_stack((int)(stack_bot - address + 4));
    return (0);
  } else if (MM_IO_BOT <= address && address <= MM_IO_TOP)
    return (read_memory_mapped_IO(address));
  else
    /* Address out of range */
    RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = (reg_word)address)
  return (0);
}

static void bad_memory_write(mem_addr address, mem_word value, int mask) {
  mem_word temporary;

  if ((address & (mem_addr)mask) != 0) /* Unaligned address fault */
    RAISE_EXCEPTION(ExcCode_AdES, CP0_BadVAddr = (reg_word)address)
  else if (address >= TEXT_BOT && address < text_top) {
    if (text_seg[(address - TEXT_BOT) >> 2] == nullptr) {
      /* No instruction at address. Only create instruction from
               full-word write. */
      temporary = (mask == 3) ? value : 0;
    } else {
      switch (mask) {
        case 0x0:
          temporary = ENCODING(text_seg[(address - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
          tmp =
              (mem_word)(((uint32_t)tmp &
                          ~(0xffu << (8 * (3 - (addr & 0x3))))) |
                         ((uint32_t)value & 0xffu) << (8 * (3 - (addr & 0x3))));
#else
          temporary = (mem_word)(((uint32_t)temporary & ~(0xffu << (8 * (address & 0x3)))) |
                           ((uint32_t)value & 0xffu) << (8 * (address & 0x3)));
#endif
          break;

        case 0x1:
          temporary = ENCODING(text_seg[(address - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
          tmp = (mem_word)(((uint32_t)tmp &
                            ~(0xffffu << (8 * (2 - (addr & 0x2))))) |
                           ((uint32_t)value & 0xffffu)
                               << (8 * (2 - (addr & 0x2))));
#else
          temporary = (mem_word)(((uint32_t)temporary & ~(0xffffu << (8 * (address & 0x2)))) |
                           ((uint32_t)value & 0xffffu) << (8 * (address & 0x2)));
#endif
          break;

        case 0x3:
          temporary = value;
          break;

        default:
          temporary = 0;
          run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
      }
      free_instruction(text_seg[(address - TEXT_BOT) >> 2]);
    }

    text_seg[(address - TEXT_BOT) >> 2] = instruction_decode(temporary);
    text_modified = true;
  } else if (address > data_top &&
             address < stack_bot
             /* If more than 16 MB below stack, probably is bad data ref */
             && address > stack_bot - 16 * mega) {
    /* Grow stack segment */
    expand_stack((int)(stack_bot - address + 4));
    if (address >= stack_bot) {
      if (mask == 0)
        stack_seg_b[address - stack_bot] = (char)value;
      else if (mask == 1)
        stack_seg_h[(address - stack_bot) >> 1] = (short)value;
      else
        stack_seg[(address - stack_bot) >> 2] = value;
    } else
      RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = (reg_word)address)

    data_modified = true;
  } else if (MM_IO_BOT <= address && address <= MM_IO_TOP)
    write_memory_mapped_IO(address, value);
  else
    /* Address out of range */
    RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = (reg_word)address)
}

/* Memory-mapped IO routines. */

static uint32_t recv_control = 0; /* No input */
static int recv_buffer;
static int recv_buffer_full_timer = 0;

static uint32_t trans_control = TRANS_READY; /* Ready to write */
static int trans_buffer;
static int trans_buffer_full_timer = 0;

/* Check if input is available and output is possible.  If so, update the
   memory-mapped control registers and buffers. */

void check_memory_mapped_IO(void) {
  if (recv_buffer_full_timer > 0) {
    /* Do not check for more input until this interval expires. */
    recv_buffer_full_timer -= 1;
  } else if (console_input_available()) {
    /* Read new char into the buffer and raise an interrupt, if interrupts
       are enabled for device. */
    /* assert(receiver_buffer_full_timer == 0); */
    recv_buffer = get_console_char();
    recv_control |= RECV_READY;
    recv_buffer_full_timer = RECV_INTERVAL;
    if ((uint32_t)recv_control & RECV_INT_ENABLE) {
      RAISE_INTERRUPT(RECV_INT_LEVEL);
    }
  } else {
    /* No console input is waiting: nothing to receive. */
  }

  if (trans_buffer_full_timer > 0) {
    /* Do not allow output until this interval expires. */
    trans_buffer_full_timer -= 1;
  } else if (!((uint32_t)trans_control & TRANS_READY)) {
    /* Done writing: empty the buffer and raise an interrupt, if interrupts
       are enabled for device. */
    /* assert(transmitter_buffer_full_timer == 0); */
    trans_control |= TRANS_READY;
    if ((uint32_t)trans_control & TRANS_INT_ENABLE) {
      RAISE_INTERRUPT(TRANS_INT_LEVEL);
    }
  } else {
    /* The transmitter is already ready: nothing to finish. */
  }
}

/* Invoked on a write to the memory-mapped IO area. */

static void write_memory_mapped_IO(mem_addr address, mem_word value) {
  switch (address) {
    case TRANS_CTRL_ADDR:
      /* Program can only set the interrupt enable, not ready, bit. */
      if (((uint32_t)value & TRANS_INT_ENABLE) != 0) {
        /* Enable interrupts: */
        trans_control |= TRANS_INT_ENABLE;
        if ((uint32_t)trans_control & TRANS_READY) {
          /* Raise interrupt on enabling a ready transmitter */
          RAISE_INTERRUPT(TRANS_INT_LEVEL);
        }
      } else {
        /* Disable interrupts: */
        trans_control &= ~TRANS_INT_ENABLE;
        CLEAR_INTERRUPT(TRANS_INT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case TRANS_BUFFER_ADDR:
      /* Ignore write if device is not ready. */
      if (((uint32_t)trans_control & TRANS_READY) != 0) {
        /* Write char: */
        trans_buffer = value & 0xff;
        put_console_char((char)trans_buffer);
        /* Device is busy for a while: */
        trans_control &= ~TRANS_READY;
        trans_buffer_full_timer = TRANS_LATENCY;
        CLEAR_INTERRUPT(TRANS_INT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case RECV_CTRL_ADDR:
      /* Program can only set the interrupt enable, not ready, bit. */
      if (((uint32_t)value & RECV_INT_ENABLE) != 0) {
        /* Enable interrupts: */
        recv_control |= RECV_INT_ENABLE;
        if ((uint32_t)recv_control & RECV_READY) {
          /* Raise interrupt on enabling a ready receiver */
          RAISE_INTERRUPT(RECV_INT_LEVEL);
        }
      } else {
        /* Disable interrupts: */
        recv_control &= ~RECV_INT_ENABLE;
        CLEAR_INTERRUPT(RECV_INT_LEVEL); /* Clear IP bit in Cause */
      }
      break;

    case RECV_BUFFER_ADDR:
      /* Nop: program can't change buffer. */
      break;

    default:
      run_error("Write to unused memory-mapped IO address (0x%x)\n", address);
  }
}

/* Invoked on a read in the memory-mapped IO area. */

static mem_word read_memory_mapped_IO(mem_addr address) {
  switch (address) {
    case TRANS_CTRL_ADDR:
      return ((mem_word)trans_control);

    case TRANS_BUFFER_ADDR:
      return (trans_buffer & 0xff);

    case RECV_CTRL_ADDR:
      return ((mem_word)recv_control);

    case RECV_BUFFER_ADDR:
      recv_control &= ~RECV_READY; /* Buffer now empty */
      recv_buffer_full_timer = 0;
      CLEAR_INTERRUPT(RECV_INT_LEVEL); /* Clear IP bit in Cause */
      return (recv_buffer & 0xff);

    default:
      run_error("Read from unused memory-mapped IO address (0x%x)\n", address);
      return (0);
  }
}

/* Misc. routines */

void print_memory(mem_addr address) {
  mem_word value;

  if ((address & 0x3) != 0)
    address &= (mem_addr)~0x3; /* Address must be word-aligned */

  if (TEXT_BOT <= address && address < text_top)
    print_instruction(address);
  else if (DATA_BOT <= address && address < data_top) {
    value = memory_read_word(address);
    write_output(message_out, "Data seg @ 0x%08x (%d) = 0x%08x (%d)\n", address,
                 address, value, value);
  } else if (stack_bot <= address && address < STACK_TOP) {
    value = memory_read_word(address);
    write_output(message_out, "Stack seg @ 0x%08x (%d) = 0x%08x (%d)\n", address,
                 address, value, value);
  } else if (K_TEXT_BOT <= address && address < k_text_top)
    print_instruction(address);
  else if (K_DATA_BOT <= address && address < k_data_top) {
    value = memory_read_word(address);
    write_output(message_out, "Kernel Data seg @ 0x%08x (%d) = 0x%08x (%d)\n",
                 address, address, value, value);
  } else
    error("Address 0x%08x (%d) to print_mem is out of bounds\n", address, address);
}
