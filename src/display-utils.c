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

static mem_addr format_partial_line(str_stream* ss, mem_addr addr);

/* Write to the stream the contents of the machine's registers, in a wide
   variety of formats. */

void format_registers(str_stream* ss, int print_gpr_hex, int print_fpr_hex) {
  int i;
  char *grstr, *fpstr;
  char *grfill, *fpfill;

  ss_printf(ss, " PC      = %08x   ", PC);
  ss_printf(ss, "EPC     = %08x  ", CP0_EPC);
  ss_printf(ss, " Cause   = %08x  ", CP0_Cause);
  ss_printf(ss, " BadVAddr= %08x\n", CP0_BadVAddr);
  ss_printf(ss, " Status  = %08x   ", CP0_Status);
  ss_printf(ss, "HI      = %08x  ", HI);
  ss_printf(ss, " LO      = %08x\n", LO);

  if (print_gpr_hex)
    grstr = "R%-2d (%2s) = %08x", grfill = "  ";
  else
    grstr = "R%-2d (%2s) = %-10d", grfill = " ";

  ss_printf(ss, "\t\t\t\t General Registers\n");
  for (i = 0; i < 8; i++) {
    ss_printf(ss, grstr, i, int_reg_names[i], gpr[i]);
    ss_printf(ss, grfill);
    ss_printf(ss, grstr, i + 8, int_reg_names[i + 8], gpr[i + 8]);
    ss_printf(ss, grfill);
    ss_printf(ss, grstr, i + 16, int_reg_names[i + 16], gpr[i + 16]);
    ss_printf(ss, grfill);
    ss_printf(ss, grstr, i + 24, int_reg_names[i + 24], gpr[i + 24]);
    ss_printf(ss, "\n");
  }

  ss_printf(ss, "\n FIR    = %08x   ", FIR);
  ss_printf(ss, " FCSR    = %08x   ", FCSR);

  ss_printf(ss, "\t\t\t      Double Floating Point Registers\n");

  if (print_fpr_hex)
    fpstr = "FP%-2d=%08x,%08x", fpfill = " ";
  else
    fpstr = "FP%-2d = %#-13.6g", fpfill = " ";

  if (print_fpr_hex)
    for (i = 0; i < 4; i += 1) {
      int *r1, *r2;

      /* Use pointers to cast to ints without invoking float->int conversion
         so we can just print the bits. */
      r1 = (int*)&fp_double_view[i];
      r2 = r1 + 1;
      ss_printf(ss, fpstr, 2 * i, *r1, *r2);
      ss_printf(ss, fpfill);

      r1 = (int*)&fp_double_view[i + 4];
      r2 = r1 + 1;
      ss_printf(ss, fpstr, 2 * i + 8, *r1, *r2);
      ss_printf(ss, fpfill);

      r1 = (int*)&fp_double_view[i + 8];
      r2 = r1 + 1;
      ss_printf(ss, fpstr, 2 * i + 16, *r1, *r2);
      ss_printf(ss, fpfill);

      r1 = (int*)&fp_double_view[i + 12];
      r2 = r1 + 1;
      ss_printf(ss, fpstr, 2 * i + 24, *r1, *r2);
      ss_printf(ss, "\n");
    }
  else
    for (i = 0; i < 4; i += 1) {
      ss_printf(ss, fpstr, 2 * i, fp_double_view[i]);
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, 2 * i + 8, fp_double_view[i + 4]);
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, 2 * i + 16, fp_double_view[i + 8]);
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, 2 * i + 24, fp_double_view[i + 12]);
      ss_printf(ss, "\n");
    }

  if (print_fpr_hex)
    fpstr = "FP%-2d=%08x", fpfill = " ";
  else
    fpstr = "FP%-2d = %#-13.6g", fpfill = " ";

  ss_printf(ss, "\t\t\t      Single Floating Point Registers\n");

  if (print_fpr_hex)
    for (i = 0; i < 8; i += 1) {
      /* Use pointers to cast to ints without invoking float->int conversion
         so we can just print the bits. */
      ss_printf(ss, fpstr, i, *(int*)&FPR_S(i));
      ss_printf(ss, fpfill);

      ss_printf(ss, fpstr, i + 8, *(int*)&FPR_S(i + 8));
      ss_printf(ss, fpfill);

      ss_printf(ss, fpstr, i + 16, *(int*)&FPR_S(i + 16));
      ss_printf(ss, fpfill);

      ss_printf(ss, fpstr, i + 24, *(int*)&FPR_S(i + 24));
      ss_printf(ss, "\n");
    }
  else
    for (i = 0; i < 8; i += 1) {
      ss_printf(ss, fpstr, i, FPR_S(i));
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, i + 8, FPR_S(i + 8));
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, i + 16, FPR_S(i + 16));
      ss_printf(ss, fpfill);
      ss_printf(ss, fpstr, i + 24, FPR_S(i + 24));
      ss_printf(ss, "\n");
    }
}

/* Write to the stream a printable representation of the instructions in
   memory addresses: FROM...TO. */

void format_insts(str_stream* ss, mem_addr from, mem_addr to) {
  mips_instruction* instruction;
  mem_addr i;

  for (i = from; i < to; i += 4) {
    instruction = mem_read_inst(i);
    if (instruction != nullptr) {
      format_an_inst(ss, instruction, i);
    }
  }
}

/* Write to the stream a printable representation of the data and stack
   segments. */

void format_data_segs(str_stream* ss) {
  ss_printf(ss, "\tDATA\n");
  format_mem(ss, DATA_BOT, data_top);

  ss_printf(ss, "\n\tSTACK\n");
  format_mem(ss, ROUND_DOWN(gpr[29], BYTES_PER_WORD), STACK_TOP);

  ss_printf(ss, "\n\tKERNEL DATA\n");
  format_mem(ss, K_DATA_BOT, k_data_top);
}

#define BYTES_PER_LINE (4 * BYTES_PER_WORD)

/* Write to the stream a printable representation of the data in memory
   address: FROM...TO. */

void format_mem(str_stream* ss, mem_addr from, mem_addr to) {
  mem_word val;
  mem_addr i = ROUND_UP(from, BYTES_PER_WORD);
  int j;

  i = format_partial_line(ss, i);

  for (; i < to;) {
    /* Count consecutive zero words */
    for (j = 0; (i + (uint32_t)j * BYTES_PER_WORD) < to; j += 1) {
      val = mem_read_word(i + (uint32_t)j * BYTES_PER_WORD);
      if (val != 0) {
        break;
      }
    }

    if (j >= 4) {
      /* Block of 4 or more zero memory words: */
      ss_printf(ss, "[0x%08x]...[0x%08x]	0x00000000\n", i,
                i + (uint32_t)j * BYTES_PER_WORD);

      i = i + (uint32_t)j * BYTES_PER_WORD;
      i = format_partial_line(ss, i);
    } else {
      /* Fewer than 4 zero words, print them on a single line: */
      ss_printf(ss, "[0x%08x]		      ", i);
      do {
        val = mem_read_word(i);
        ss_printf(ss, "  0x%08x", (unsigned int)val);
        i += BYTES_PER_WORD;
      } while (i % BYTES_PER_LINE != 0);

      ss_printf(ss, "\n");
    }
  }
}

/* Write to the stream a text line containing a fraction of a
   quadword. Return the address after the last one written.  */

static mem_addr format_partial_line(str_stream* ss, mem_addr addr) {
  if ((addr % BYTES_PER_LINE) != 0) {
    ss_printf(ss, "[0x%08x]		      ", addr);

    for (; (addr % BYTES_PER_LINE) != 0; addr += BYTES_PER_WORD) {
      mem_word val = mem_read_word(addr);
      ss_printf(ss, "  0x%08x", (unsigned int)val);
    }

    ss_printf(ss, "\n");
  }

  return addr;
}
