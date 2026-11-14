/* SPIM S20 MIPS simulator.
   Utilities for displaying machine contents.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "data.h"
#include "registers.h"
#include "memory.h"
#include "run.h"
#include "symbol-table.h"

char* int_reg_names[32] = {"r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
                           "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                           "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
                           "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};

static mem_addr format_partial_line(str_stream* stream, mem_addr address);

/* Write to the stream the contents of the machine's registers, in a wide
   variety of formats. */

/* The bit patterns of FP register contents, for hex display. */
static void double_words(double d, int32_t w[2]) {
  memcpy(w, &d, 2 * sizeof w[0]);
}

static int32_t float_bits(float f) {
  int32_t bits;
  memcpy(&bits, &f, sizeof bits);
  return bits;
}

void format_registers(str_stream* stream, bool print_general_purpose_registers_hex, bool print_floating_point_register_hex) {
  int i;

  stream_printf(stream, " PC      = %08x   ", PC);
  stream_printf(stream, "EPC     = %08x  ", CP0_EPC);
  stream_printf(stream, " Cause   = %08x  ", CP0_Cause);
  stream_printf(stream, " BadVAddr= %08x\n", CP0_BadVAddr);
  stream_printf(stream, " Status  = %08x   ", CP0_Status);
  stream_printf(stream, "HI      = %08x  ", HI);
  stream_printf(stream, " LO      = %08x\n", LO);

  const char* const general_register_format =
      print_general_purpose_registers_hex ? "R%-2d (%2s) = %08x" : "R%-2d (%2s) = %-10d";
  const char* const general_register_padding = print_general_purpose_registers_hex ? "  " : " ";

  stream_printf(stream, "\t\t\t\t General Registers\n");
  for (i = 0; i < 8; i++) {
    stream_printf(stream, general_register_format, i, int_reg_names[i], gpr[i]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 8, int_reg_names[i + 8], gpr[i + 8]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 16, int_reg_names[i + 16], gpr[i + 16]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 24, int_reg_names[i + 24], gpr[i + 24]);
    stream_printf(stream, "\n");
  }

  stream_printf(stream, "\n FIR    = %08x   ", FIR);
  stream_printf(stream, " FCSR    = %08x   ", FCSR);

  stream_printf(stream, "\t\t\t      Double Floating Point Registers\n");

  const char* floating_point_format = print_floating_point_register_hex ? "FP%-2d=%08x,%08x" : "FP%-2d = %#-13.6g";
  const char* const floating_point_padding = " ";

  if (print_floating_point_register_hex)
    for (i = 0; i < 4; i += 1) {
      int32_t w[2];

      double_words(fp_double_view[i], w);
      stream_printf(stream, floating_point_format, 2 * i, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(fp_double_view[i + 4], w);
      stream_printf(stream, floating_point_format, 2 * i + 8, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(fp_double_view[i + 8], w);
      stream_printf(stream, floating_point_format, 2 * i + 16, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(fp_double_view[i + 12], w);
      stream_printf(stream, floating_point_format, 2 * i + 24, w[0], w[1]);
      stream_printf(stream, "\n");
    }
  else
    for (i = 0; i < 4; i += 1) {
      stream_printf(stream, floating_point_format, 2 * i, fp_double_view[i]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 8, fp_double_view[i + 4]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 16, fp_double_view[i + 8]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 24, fp_double_view[i + 12]);
      stream_printf(stream, "\n");
    }

  floating_point_format = print_floating_point_register_hex ? "FP%-2d=%08x" : "FP%-2d = %#-13.6g";

  stream_printf(stream, "\t\t\t      Single Floating Point Registers\n");

  if (print_floating_point_register_hex)
    for (i = 0; i < 8; i += 1) {
      /* Use pointers to cast to ints without invoking float->int conversion
         so we can just print the bits. */
      stream_printf(stream, floating_point_format, i, float_bits(FPR_S(i)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 8, float_bits(FPR_S(i + 8)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 16, float_bits(FPR_S(i + 16)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 24, float_bits(FPR_S(i + 24)));
      stream_printf(stream, "\n");
    }
  else
    for (i = 0; i < 8; i += 1) {
      stream_printf(stream, floating_point_format, i, FPR_S(i));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 8, FPR_S(i + 8));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 16, FPR_S(i + 16));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 24, FPR_S(i + 24));
      stream_printf(stream, "\n");
    }
}

/* Write to the stream a printable representation of the instructions in
   memory addresses: FROM...TO. */

void format_instructions(str_stream* stream, mem_addr from, mem_addr to) {
  for (mem_addr i = from; i < to; i += 4) {
    mips_instruction* const instruction = memory_read_instruction(i);
    if (instruction != nullptr) {
      format_an_instruction(stream, instruction, i);
    }
  }
}

/* Print a disassembly listing of the loaded user text segment to the
   console: one line per instruction with address, encoded word,
   disassembly, and the source line that produced it — the same shape
   as teaching mode's per-step header.  Used by the REPL `disasm`
   command and by explain mode's once-per-session pre-run listing.
   (Named `disasm`, not `listing`: the -listing CLI flag is the
   assemble-time event trace, a different feature.) */

void print_text_listing(void) {
  static str_stream stream;
  stream_clear(&stream);
  format_instructions(&stream, TEXT_BOT, text_top);

  char* const text = stream_to_string(&stream);
  if (*text == '\0') {
    write_output(message_out, "No program loaded.\n");
    return;
  }
  write_output(message_out, "\n\tUSER TEXT SEGMENT\n");
  write_output(message_out, "%s\n", text);
}

/* Write to the stream a printable representation of the data and stack
   segments. */

void format_data_segments(str_stream* stream) {
  stream_printf(stream, "\tDATA\n");
  format_memory(stream, DATA_BOT, data_top);

  stream_printf(stream, "\n\tSTACK\n");
  format_memory(stream, (mem_addr)ROUND_DOWN(gpr[29], BYTES_PER_WORD), STACK_TOP);

  stream_printf(stream, "\n\tKERNEL DATA\n");
  format_memory(stream, K_DATA_BOT, k_data_top);
}

constexpr int BYTES_PER_LINE = 4 * BYTES_PER_WORD;

/* Write to the stream a printable representation of the data in memory
   address: FROM...TO. */

void format_memory(str_stream* stream, mem_addr from, mem_addr to) {
  mem_word value;
  mem_addr i = ROUND_UP(from, BYTES_PER_WORD);
  int j;

  i = format_partial_line(stream, i);

  for (; i < to;) {
    /* Count consecutive zero words */
    for (j = 0; (i + (uint32_t)j * BYTES_PER_WORD) < to; j += 1) {
      value = memory_read_word(i + (uint32_t)j * BYTES_PER_WORD);
      if (value != 0) {
        break;
      }
    }

    if (j >= 4) {
      /* Block of 4 or more zero memory words: */
      stream_printf(stream, "[0x%08x]...[0x%08x]	0x00000000\n", i,
                i + (uint32_t)j * BYTES_PER_WORD);

      i = i + (uint32_t)j * BYTES_PER_WORD;
      i = format_partial_line(stream, i);
    } else {
      /* Fewer than 4 zero words, print them on a single line: */
      stream_printf(stream, "[0x%08x]		      ", i);
      do {
        value = memory_read_word(i);
        stream_printf(stream, "  0x%08x", (unsigned int)value);
        i += BYTES_PER_WORD;
      } while (i % BYTES_PER_LINE != 0);

      stream_printf(stream, "\n");
    }
  }
}

/* Write to the stream a text line containing a fraction of a
   quadword. Return the address after the last one written.  */

static mem_addr format_partial_line(str_stream* stream, mem_addr address) {
  if ((address % BYTES_PER_LINE) != 0) {
    stream_printf(stream, "[0x%08x]		      ", address);

    for (; (address % BYTES_PER_LINE) != 0; address += BYTES_PER_WORD) {
      const mem_word value = memory_read_word(address);
      stream_printf(stream, "  0x%08x", (unsigned int)value);
    }

    stream_printf(stream, "\n");
  }

  return address;
}
