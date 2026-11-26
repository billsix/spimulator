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

char* integer_register_names[32] = {
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
    "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
    "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};

static memory_address format_partial_line(string_stream* stream,
                                          memory_address address);

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

void format_registers(string_stream* stream,
                      bool print_general_purpose_registers_hex,
                      bool print_floating_point_register_hex) {
  int i;

  stream_printf(stream, " PC      = %08x   ", PROGRAM_COUNTER);
  stream_printf(stream, "EPC     = %08x  ",
                coprocessor_0_exception_program_counter);
  stream_printf(stream, " Cause   = %08x  ", coprocessor_0_cause);
  stream_printf(stream, " BadVAddr= %08x\n", coprocessor_0_bad_virtual_address);
  stream_printf(stream, " Status  = %08x   ", coprocessor_0_status);
  stream_printf(stream, "HI      = %08x  ", MULTIPLY_DIVIDE_HIGH);
  stream_printf(stream, " LO      = %08x\n", MULTIPLY_DIVIDE_LOW);

  const char* const general_register_format =
      print_general_purpose_registers_hex ? "R%-2d (%2s) = %08x"
                                          : "R%-2d (%2s) = %-10d";
  const char* const general_register_padding =
      print_general_purpose_registers_hex ? "  " : " ";

  stream_printf(stream, "\t\t\t\t General Registers\n");
  for (i = 0; i < 8; i++) {
    stream_printf(stream, general_register_format, i, integer_register_names[i],
                  general_purpose_registers[i]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 8,
                  integer_register_names[i + 8],
                  general_purpose_registers[i + 8]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 16,
                  integer_register_names[i + 16],
                  general_purpose_registers[i + 16]);
    stream_printf(stream, "%s", general_register_padding);
    stream_printf(stream, general_register_format, i + 24,
                  integer_register_names[i + 24],
                  general_purpose_registers[i + 24]);
    stream_printf(stream, "\n");
  }

  stream_printf(stream, "\n FIR    = %08x   ",
                floating_point_implementation_register);
  stream_printf(stream, " FCSR    = %08x   ",
                floating_point_control_status_register);

  stream_printf(stream, "\t\t\t      Double Floating Point Registers\n");

  const char* floating_point_format = print_floating_point_register_hex
                                          ? "FP%-2d=%08x,%08x"
                                          : "FP%-2d = %#-13.6g";
  const char* const floating_point_padding = " ";

  if (print_floating_point_register_hex)
    for (i = 0; i < 4; i += 1) {
      int32_t w[2];

      double_words(floating_point_double_view[i], w);
      stream_printf(stream, floating_point_format, 2 * i, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(floating_point_double_view[i + 4], w);
      stream_printf(stream, floating_point_format, 2 * i + 8, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(floating_point_double_view[i + 8], w);
      stream_printf(stream, floating_point_format, 2 * i + 16, w[0], w[1]);
      stream_printf(stream, "%s", floating_point_padding);

      double_words(floating_point_double_view[i + 12], w);
      stream_printf(stream, floating_point_format, 2 * i + 24, w[0], w[1]);
      stream_printf(stream, "\n");
    }
  else
    for (i = 0; i < 4; i += 1) {
      stream_printf(stream, floating_point_format, 2 * i,
                    floating_point_double_view[i]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 8,
                    floating_point_double_view[i + 4]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 16,
                    floating_point_double_view[i + 8]);
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, 2 * i + 24,
                    floating_point_double_view[i + 12]);
      stream_printf(stream, "\n");
    }

  floating_point_format =
      print_floating_point_register_hex ? "FP%-2d=%08x" : "FP%-2d = %#-13.6g";

  stream_printf(stream, "\t\t\t      Single Floating Point Registers\n");

  if (print_floating_point_register_hex)
    for (i = 0; i < 8; i += 1) {
      /* Use pointers to cast to ints without invoking float->int conversion
         so we can just print the bits. */
      stream_printf(stream, floating_point_format, i,
                    float_bits(FLOATING_POINT_REGISTER_SINGLE(i)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 8,
                    float_bits(FLOATING_POINT_REGISTER_SINGLE(i + 8)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 16,
                    float_bits(FLOATING_POINT_REGISTER_SINGLE(i + 16)));
      stream_printf(stream, "%s", floating_point_padding);

      stream_printf(stream, floating_point_format, i + 24,
                    float_bits(FLOATING_POINT_REGISTER_SINGLE(i + 24)));
      stream_printf(stream, "\n");
    }
  else
    for (i = 0; i < 8; i += 1) {
      stream_printf(stream, floating_point_format, i,
                    FLOATING_POINT_REGISTER_SINGLE(i));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 8,
                    FLOATING_POINT_REGISTER_SINGLE(i + 8));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 16,
                    FLOATING_POINT_REGISTER_SINGLE(i + 16));
      stream_printf(stream, "%s", floating_point_padding);
      stream_printf(stream, floating_point_format, i + 24,
                    FLOATING_POINT_REGISTER_SINGLE(i + 24));
      stream_printf(stream, "\n");
    }
}

/* Write to the stream a printable representation of the instructions in
   memory addresses: FROM...TO. */

void format_instructions(string_stream* stream, memory_address from,
                         memory_address to) {
  for (memory_address i = from; i < to; i += 4) {
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
  static string_stream stream;
  stream_clear(&stream);
  format_instructions(&stream, TEXT_BOTTOM, text_top);

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

void format_data_segments(string_stream* stream) {
  stream_printf(stream, "\tDATA\n");
  format_memory(stream, DATA_BOTTOM, data_top);

  stream_printf(stream, "\n\tSTACK\n");
  format_memory(
      stream,
      (memory_address)ROUND_DOWN(general_purpose_registers[29], BYTES_PER_WORD),
      STACK_TOP);

  stream_printf(stream, "\n\tKERNEL DATA\n");
  format_memory(stream, KERNEL_DATA_BOTTOM, kernel_data_top);
}

constexpr int BYTES_PER_LINE = 4 * BYTES_PER_WORD;

/* Write to the stream a printable representation of the data in memory
   address: FROM...TO. */

void format_memory(string_stream* stream, memory_address from,
                   memory_address to) {
  memory_word value;
  memory_address i = ROUND_UP(from, BYTES_PER_WORD);
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

static memory_address format_partial_line(string_stream* stream,
                                          memory_address address) {
  if ((address % BYTES_PER_LINE) != 0) {
    stream_printf(stream, "[0x%08x]		      ", address);

    for (; (address % BYTES_PER_LINE) != 0; address += BYTES_PER_WORD) {
      stream_printf(stream, "  0x%08x",
                    (unsigned int)memory_read_word(address));
    }

    stream_printf(stream, "\n");
  }

  return address;
}
