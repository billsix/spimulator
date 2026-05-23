/* SPIM S20 MIPS simulator.
   Code to create, maintain and access memory.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "inst.h"
#include "reg.h"
#include "mem.h"

/* Exported Variables: */

reg_word gpr[R_LENGTH];
reg_word HI, LO;
int HI_present, LO_present;
mem_addr PC, nPC;
double* fp_double_view; /* Dynamically allocate so overlay */
float* fp_single_view;  /* is possible */
int* fp_int_view;    /* is possible */
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

static mem_word bad_mem_read(mem_addr addr, int mask);
static void bad_mem_write(mem_addr addr, mem_word value, int mask);
static mips_instruction* bad_text_read(mem_addr addr);
static void bad_text_write(mem_addr addr, mips_instruction* instruction);
static void free_instructions(mips_instruction** instruction, int n);
static mem_word read_memory_mapped_IO(mem_addr addr);
static void write_memory_mapped_IO(mem_addr addr, mem_word value);

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
                 int stack_limit, int k_text_size, int k_data_size,
                 int k_data_limit) {
  if (data_size <= 65536) data_size = 65536;
  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */

  if (text_seg == nullptr)
    text_seg = (mips_instruction**)xmalloc(BYTES_TO_INST(text_size));
  else {
    free_instructions(text_seg, (text_top - TEXT_BOT) / BYTES_PER_WORD);
    text_seg = (mips_instruction**)realloc(text_seg, BYTES_TO_INST(text_size));
  }
  memset(text_seg, 0, BYTES_TO_INST(text_size));
  text_top = TEXT_BOT + text_size;

  data_size = ROUND_UP(data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (data_seg == nullptr)
    data_seg = (mem_word*)xmalloc(data_size);
  else
    data_seg = (mem_word*)realloc(data_seg, data_size);
  memset(data_seg, 0, data_size);
  data_seg_b = (int8_t*)data_seg;
  data_seg_h = (short*)data_seg;
  data_top = DATA_BOT + data_size;
  data_size_limit = data_limit;

  stack_size = ROUND_UP(stack_size, BYTES_PER_WORD); /* Keep word aligned */
  if (stack_seg == nullptr)
    stack_seg = (mem_word*)xmalloc(stack_size);
  else
    stack_seg = (mem_word*)realloc(stack_seg, stack_size);
  memset(stack_seg, 0, stack_size);
  stack_seg_b = (int8_t*)stack_seg;
  stack_seg_h = (short*)stack_seg;
  stack_bot = STACK_TOP - stack_size;
  stack_size_limit = stack_limit;

  if (k_text_seg == nullptr)
    k_text_seg = (mips_instruction**)xmalloc(BYTES_TO_INST(k_text_size));
  else {
    free_instructions(k_text_seg, (k_text_top - K_TEXT_BOT) / BYTES_PER_WORD);
    k_text_seg = (mips_instruction**)realloc(k_text_seg, BYTES_TO_INST(k_text_size));
  }
  memset(k_text_seg, 0, BYTES_TO_INST(k_text_size));
  k_text_top = K_TEXT_BOT + k_text_size;

  k_data_size = ROUND_UP(k_data_size, BYTES_PER_WORD); /* Keep word aligned */
  if (k_data_seg == nullptr)
    k_data_seg = (mem_word*)xmalloc(k_data_size);
  else
    k_data_seg = (mem_word*)realloc(k_data_seg, k_data_size);
  memset(k_data_seg, 0, k_data_size);
  k_data_seg_b = (int8_t*)k_data_seg;
  k_data_seg_h = (short*)k_data_seg;
  k_data_top = K_DATA_BOT + k_data_size;
  k_data_size_limit = k_data_limit;

  text_modified = true;
  data_modified = true;
}

/* Free the storage used by the old instructions in memory. */

static void free_instructions(mips_instruction** instruction, int n) {
  for (; n > 0; n--, instruction++)
    if (*instruction) free_inst(*instruction);
}

/* Expand the data segment by adding N bytes. */

void expand_data(int addl_bytes) {
  int delta = ROUND_UP(addl_bytes, BYTES_PER_WORD); /* Keep word aligned */
  int old_size = data_top - DATA_BOT;
  int new_size = old_size + delta;
  int8_t* p;

  if ((addl_bytes < 0) || (new_size > data_size_limit)) {
    error("Can't expand data segment by %d bytes to %d bytes\n", addl_bytes,
          new_size);
    run_error("Use -ldata # with # > %d\n", new_size);
  }
  data_seg = (mem_word*)realloc(data_seg, new_size);
  if (data_seg == nullptr) fatal_error("realloc failed in expand_data\n");

  data_seg_b = (int8_t*)data_seg;
  data_seg_h = (short*)data_seg;
  data_top += delta;

  /* Zero new memory */
  for (p = data_seg_b + old_size; p < data_seg_b + new_size;) *p++ = 0;
}

/* Expand the stack segment by adding N bytes.  Can't use REALLOC
   since it copies from bottom of memory blocks and stack grows down from
   top of its block. */

void expand_stack(int addl_bytes) {
  int delta = ROUND_UP(addl_bytes, BYTES_PER_WORD); /* Keep word aligned */
  int old_size = STACK_TOP - stack_bot;
  int new_size = old_size + MAX(delta, old_size);
  mem_word* new_seg;
  mem_word *po, *pn;

  if ((addl_bytes < 0) || (new_size > stack_size_limit)) {
    run_error(
        "Can't expand stack segment by %d bytes to %d bytes.\nUse -lstack # "
        "with # > %d\n",
        addl_bytes, new_size, new_size);
  }

  new_seg = (mem_word*)xmalloc(new_size);
  memset(new_seg, 0, new_size);

  po = stack_seg + (old_size / BYTES_PER_WORD - 1);
  pn = new_seg + (new_size / BYTES_PER_WORD - 1);
  for (; po >= stack_seg;) *pn-- = *po--;

  free(stack_seg);
  stack_seg = new_seg;
  stack_seg_b = (int8_t*)stack_seg;
  stack_seg_h = (short*)stack_seg;
  stack_bot -= (new_size - old_size);
}

/* Expand the kernel data segment by adding N bytes. */

void expand_k_data(int addl_bytes) {
  int delta = ROUND_UP(addl_bytes, BYTES_PER_WORD); /* Keep word aligned */
  int old_size = k_data_top - K_DATA_BOT;
  int new_size = old_size + delta;
  int8_t* p;

  if ((addl_bytes < 0) || (new_size > k_data_size_limit)) {
    run_error(
        "Can't expand kernel data segment by %d bytes to %d bytes.\nUse "
        "-lkdata # with # > %d\n",
        addl_bytes, new_size, new_size);
  }
  k_data_seg = (mem_word*)realloc(k_data_seg, new_size);
  if (k_data_seg == nullptr) fatal_error("realloc failed in expand_k_data\n");

  k_data_seg_b = (int8_t*)k_data_seg;
  k_data_seg_h = (short*)k_data_seg;
  k_data_top += delta;

  /* Zero new memory */
  for (p = k_data_seg_b + old_size / BYTES_PER_WORD;
       p < k_data_seg_b + new_size / BYTES_PER_WORD;)
    *p++ = 0;
}

/* Access memory */

void* mem_reference(mem_addr addr) {
  if ((addr >= TEXT_BOT) && (addr < text_top))
    return addr - TEXT_BOT + (char*)text_seg;
  else if ((addr >= DATA_BOT) && (addr < data_top))
    return addr - DATA_BOT + (char*)data_seg;
  else if ((addr >= stack_bot) && (addr < STACK_TOP))
    return addr - stack_bot + (char*)stack_seg;
  else if ((addr >= K_TEXT_BOT) && (addr < k_text_top))
    return addr - K_TEXT_BOT + (char*)k_text_seg;
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top))
    return addr - K_DATA_BOT + (char*)k_data_seg;
  else {
    run_error("Memory address out of bounds\n");
    return nullptr;
  }
}

mips_instruction* mem_read_inst(mem_addr addr) {
  if ((addr >= TEXT_BOT) && (addr < text_top) && !(addr & 0x3))
    return text_seg[(addr - TEXT_BOT) >> 2];
  else if ((addr >= K_TEXT_BOT) && (addr < k_text_top) && !(addr & 0x3))
    return k_text_seg[(addr - K_TEXT_BOT) >> 2];
  else
    return bad_text_read(addr);
}

reg_word mem_read_byte(mem_addr addr) {
  if ((addr >= DATA_BOT) && (addr < data_top))
    return data_seg_b[addr - DATA_BOT];
  else if ((addr >= stack_bot) && (addr < STACK_TOP))
    return stack_seg_b[addr - stack_bot];
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top))
    return k_data_seg_b[addr - K_DATA_BOT];
  else
    return bad_mem_read(addr, 0);
}

reg_word mem_read_half(mem_addr addr) {
  if ((addr >= DATA_BOT) && (addr < data_top) && !(addr & 0x1))
    return data_seg_h[(addr - DATA_BOT) >> 1];
  else if ((addr >= stack_bot) && (addr < STACK_TOP) && !(addr & 0x1))
    return stack_seg_h[(addr - stack_bot) >> 1];
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top) && !(addr & 0x1))
    return k_data_seg_h[(addr - K_DATA_BOT) >> 1];
  else
    return bad_mem_read(addr, 0x1);
}

reg_word mem_read_word(mem_addr addr) {
  if ((addr >= DATA_BOT) && (addr < data_top) && !(addr & 0x3))
    return data_seg[(addr - DATA_BOT) >> 2];
  else if ((addr >= stack_bot) && (addr < STACK_TOP) && !(addr & 0x3))
    return stack_seg[(addr - stack_bot) >> 2];
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top) && !(addr & 0x3))
    return k_data_seg[(addr - K_DATA_BOT) >> 2];
  else
    return bad_mem_read(addr, 0x3);
}

void mem_write_inst(mem_addr addr, mips_instruction* instruction) {
  text_modified = true;
  if ((addr >= TEXT_BOT) && (addr < text_top) && !(addr & 0x3))
    text_seg[(addr - TEXT_BOT) >> 2] = instruction;
  else if ((addr >= K_TEXT_BOT) && (addr < k_text_top) && !(addr & 0x3))
    k_text_seg[(addr - K_TEXT_BOT) >> 2] = instruction;
  else
    bad_text_write(addr, instruction);
}

void mem_write_byte(mem_addr addr, reg_word value) {
  data_modified = true;
  if ((addr >= DATA_BOT) && (addr < data_top))
    data_seg_b[addr - DATA_BOT] = (int8_t)value;
  else if ((addr >= stack_bot) && (addr < STACK_TOP))
    stack_seg_b[addr - stack_bot] = (int8_t)value;
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top))
    k_data_seg_b[addr - K_DATA_BOT] = (int8_t)value;
  else
    bad_mem_write(addr, value, 0);
}

void mem_write_half(mem_addr addr, reg_word value) {
  data_modified = true;
  if ((addr >= DATA_BOT) && (addr < data_top) && !(addr & 0x1))
    data_seg_h[(addr - DATA_BOT) >> 1] = (short)value;
  else if ((addr >= stack_bot) && (addr < STACK_TOP) && !(addr & 0x1))
    stack_seg_h[(addr - stack_bot) >> 1] = (short)value;
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top) && !(addr & 0x1))
    k_data_seg_h[(addr - K_DATA_BOT) >> 1] = (short)value;
  else
    bad_mem_write(addr, value, 0x1);
}

void mem_write_word(mem_addr addr, reg_word value) {
  data_modified = true;
  if ((addr >= DATA_BOT) && (addr < data_top) && !(addr & 0x3))
    data_seg[(addr - DATA_BOT) >> 2] = (mem_word)value;
  else if ((addr >= stack_bot) && (addr < STACK_TOP) && !(addr & 0x3))
    stack_seg[(addr - stack_bot) >> 2] = (mem_word)value;
  else if ((addr >= K_DATA_BOT) && (addr < k_data_top) && !(addr & 0x3))
    k_data_seg[(addr - K_DATA_BOT) >> 2] = (mem_word)value;
  else
    bad_mem_write(addr, value, 0x3);
}

/* Handle the infrequent and erroneous cases in memory accesses. */

static mips_instruction* bad_text_read(mem_addr addr) {
  RAISE_EXCEPTION(ExcCode_IBE, CP0_BadVAddr = addr);
  return (inst_decode(0));
}

static void bad_text_write(mem_addr addr, mips_instruction* instruction) {
  RAISE_EXCEPTION(ExcCode_IBE, CP0_BadVAddr = addr);
  mem_write_word(addr, ENCODING(instruction));
}

static mem_word bad_mem_read(mem_addr addr, int mask) {
  mem_word tmp;

  if ((addr & mask) != 0)
    RAISE_EXCEPTION(ExcCode_AdEL, CP0_BadVAddr = addr)
  else if (addr >= TEXT_BOT && addr < text_top)
    switch (mask) {
      case 0x0:
        tmp = ENCODING(text_seg[(addr - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
        tmp = (unsigned)tmp >> (8 * (3 - (addr & 0x3)));
#else
        tmp = (unsigned)tmp >> (8 * (addr & 0x3));
#endif
        return (0xff & tmp);

      case 0x1:
        tmp = ENCODING(text_seg[(addr - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
        tmp = (unsigned)tmp >> (8 * (2 - (addr & 0x2)));
#else
        tmp = (unsigned)tmp >> (8 * (addr & 0x2));
#endif
        return (0xffff & tmp);

      case 0x3: {
        mips_instruction* instruction = text_seg[(addr - TEXT_BOT) >> 2];
        if (instruction == nullptr)
          return 0;
        else
          return (ENCODING(instruction));
      }

      default:
        run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
    }
  else if (addr > data_top &&
           addr < stack_bot
           /* If more than 16 MB below stack, probably is bad data ref */
           && addr > stack_bot - 16 * mega) {
    /* Grow stack segment */
    expand_stack(stack_bot - addr + 4);
    return (0);
  } else if (MM_IO_BOT <= addr && addr <= MM_IO_TOP)
    return (read_memory_mapped_IO(addr));
  else
    /* Address out of range */
    RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = addr)
  return (0);
}

static void bad_mem_write(mem_addr addr, mem_word value, int mask) {
  mem_word tmp;

  if ((addr & mask) != 0) /* Unaligned address fault */
    RAISE_EXCEPTION(ExcCode_AdES, CP0_BadVAddr = addr)
  else if (addr >= TEXT_BOT && addr < text_top) {
    if (text_seg[(addr - TEXT_BOT) >> 2] == nullptr) {
      /* No instruction at address. Only create instruction from
               full-word write. */
      tmp = (mask == 3) ? value : 0;
    } else {
      switch (mask) {
        case 0x0:
          tmp = ENCODING(text_seg[(addr - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
          tmp = ((tmp & ~(0xff << (8 * (3 - (addr & 0x3))))) |
                 (value & 0xff) << (8 * (3 - (addr & 0x3))));
#else
          tmp = ((tmp & ~(0xff << (8 * (addr & 0x3)))) |
                 (value & 0xff) << (8 * (addr & 0x3)));
#endif
          break;

        case 0x1:
          tmp = ENCODING(text_seg[(addr - TEXT_BOT) >> 2]);
#ifdef SPIM_BIGENDIAN
          tmp = ((tmp & ~(0xffff << (8 * (2 - (addr & 0x2))))) |
                 (value & 0xffff) << (8 * (2 - (addr & 0x2))));
#else
          tmp = ((tmp & ~(0xffff << (8 * (addr & 0x2)))) |
                 (value & 0xffff) << (8 * (addr & 0x2)));
#endif
          break;

        case 0x3:
          tmp = value;
          break;

        default:
          tmp = 0;
          run_error("Bad mask (0x%x) in bad_mem_read\n", mask);
      }
      free_inst(text_seg[(addr - TEXT_BOT) >> 2]);
    }

    text_seg[(addr - TEXT_BOT) >> 2] = inst_decode(tmp);
    text_modified = true;
  } else if (addr > data_top &&
             addr < stack_bot
             /* If more than 16 MB below stack, probably is bad data ref */
             && addr > stack_bot - 16 * mega) {
    /* Grow stack segment */
    expand_stack(stack_bot - addr + 4);
    if (addr >= stack_bot) {
      if (mask == 0)
        stack_seg_b[addr - stack_bot] = (char)value;
      else if (mask == 1)
        stack_seg_h[(addr - stack_bot) >> 1] = (short)value;
      else
        stack_seg[(addr - stack_bot) >> 2] = value;
    } else
      RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = addr)

    data_modified = true;
  } else if (MM_IO_BOT <= addr && addr <= MM_IO_TOP)
    write_memory_mapped_IO(addr, value);
  else
    /* Address out of range */
    RAISE_EXCEPTION(ExcCode_DBE, CP0_BadVAddr = addr)
}

/* Memory-mapped IO routines. */

static int recv_control = 0; /* No input */
static int recv_buffer;
static int recv_buffer_full_timer = 0;

static int trans_control = TRANS_READY; /* Ready to write */
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
    /* assert(recv_buffer_full_timer == 0); */
    recv_buffer = get_console_char();
    recv_control |= RECV_READY;
    recv_buffer_full_timer = RECV_INTERVAL;
    if (recv_control & RECV_INT_ENABLE) {
      RAISE_INTERRUPT(RECV_INT_LEVEL);
    }
  }

  if (trans_buffer_full_timer > 0) {
    /* Do not allow output until this interval expires. */
    trans_buffer_full_timer -= 1;
  } else if (!(trans_control & TRANS_READY)) {
    /* Done writing: empty the buffer and raise an interrupt, if interrupts
       are enabled for device. */
    /* assert(trans_buffer_full_timer == 0); */
    trans_control |= TRANS_READY;
    if (trans_control & TRANS_INT_ENABLE) {
      RAISE_INTERRUPT(TRANS_INT_LEVEL);
    }
  }
}

/* Invoked on a write to the memory-mapped IO area. */

static void write_memory_mapped_IO(mem_addr addr, mem_word value) {
  switch (addr) {
    case TRANS_CTRL_ADDR:
      /* Program can only set the interrupt enable, not ready, bit. */
      if ((value & TRANS_INT_ENABLE) != 0) {
        /* Enable interrupts: */
        trans_control |= TRANS_INT_ENABLE;
        if (trans_control & TRANS_READY) {
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
      if ((trans_control & TRANS_READY) != 0) {
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
      if ((value & RECV_INT_ENABLE) != 0) {
        /* Enable interrupts: */
        recv_control |= RECV_INT_ENABLE;
        if (recv_control & RECV_READY) {
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
      run_error("Write to unused memory-mapped IO address (0x%x)\n", addr);
  }
}

/* Invoked on a read in the memory-mapped IO area. */

static mem_word read_memory_mapped_IO(mem_addr addr) {
  switch (addr) {
    case TRANS_CTRL_ADDR:
      return (trans_control);

    case TRANS_BUFFER_ADDR:
      return (trans_buffer & 0xff);

    case RECV_CTRL_ADDR:
      return (recv_control);

    case RECV_BUFFER_ADDR:
      recv_control &= ~RECV_READY; /* Buffer now empty */
      recv_buffer_full_timer = 0;
      CLEAR_INTERRUPT(RECV_INT_LEVEL); /* Clear IP bit in Cause */
      return (recv_buffer & 0xff);

    default:
      run_error("Read from unused memory-mapped IO address (0x%x)\n", addr);
      return (0);
  }
}

/* Misc. routines */

void print_mem(mem_addr addr) {
  mem_word value;

  if ((addr & 0x3) != 0) addr &= ~0x3; /* Address must be word-aligned */

  if (TEXT_BOT <= addr && addr < text_top)
    print_inst(addr);
  else if (DATA_BOT <= addr && addr < data_top) {
    value = mem_read_word(addr);
    write_output(message_out, "Data seg @ 0x%08x (%d) = 0x%08x (%d)\n", addr,
                 addr, value, value);
  } else if (stack_bot <= addr && addr < STACK_TOP) {
    value = mem_read_word(addr);
    write_output(message_out, "Stack seg @ 0x%08x (%d) = 0x%08x (%d)\n", addr,
                 addr, value, value);
  } else if (K_TEXT_BOT <= addr && addr < k_text_top)
    print_inst(addr);
  else if (K_DATA_BOT <= addr && addr < k_data_top) {
    value = mem_read_word(addr);
    write_output(message_out, "Kernel Data seg @ 0x%08x (%d) = 0x%08x (%d)\n",
                 addr, addr, value, value);
  } else
    error("Address 0x%08x (%d) to print_mem is out of bounds\n", addr, addr);
}
