/* SPIM S20 MIPS simulator.
   Execute SPIM syscalls, both in simulator and bare mode.
   Execute MIPS syscalls in bare mode, when running on MIPS systems.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "spim.h"
#include "string-stream.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "symbol-table.h"
#include "syscall.h"

/* Decides which syscall to execute or simulate.  Returns false upon
   exit syscall and true to continue execution. */

bool do_syscall(void) {
  /* Syscalls for the source-language version of SPIM.  These are easier to
     use than the real syscall and are portable to non-MIPS operating
     systems. */

  switch (general_purpose_registers[VALUE_REGISTER_0]) {
    case PRINT_INT_SYSCALL:
      write_output(console_out, "%d",
                   general_purpose_registers[ARGUMENT_REGISTER_0]);
      break;

    case PRINT_FLOAT_SYSCALL: {
      const float value =
          FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_ARGUMENT_REGISTER_0);

      write_output(console_out, "%.8f", value);
      break;
    }

    case PRINT_DOUBLE_SYSCALL:
      write_output(
          console_out, "%.18g",
          floating_point_double_view[FLOATING_POINT_ARGUMENT_REGISTER_0 / 2]);
      break;

    case PRINT_STRING_SYSCALL:
      write_output(
          console_out, "%s",
          (char*)memory_reference(
              (memory_address)general_purpose_registers[ARGUMENT_REGISTER_0]));
      break;

    case READ_INT_SYSCALL: {
      /* Scanf-style: skips whitespace (including newlines) and reads
         one int per call, leaving the rest of the input buffered — so
         `echo 43 3 12 | spimulator ...` yields all three ints, one per
         syscall, not just the first. */
      long value;
      const int ok = read_integer_input(&value);
      general_purpose_registers[RESULT_REGISTER] = (register_word)value;
      general_purpose_registers[ARGUMENT_REGISTER_3] =
          ok ? 0 : 1; /* 0 = success, 1 = EOF/no number */
      break;
    }

    case READ_FLOAT_SYSCALL: {
      static char string[256];

      read_input(string, 256);
      FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_RESULT_REGISTER) =
          (float)atof(string);
      break;
    }

    case READ_DOUBLE_SYSCALL: {
      static char string[256];

      read_input(string, 256);
      floating_point_double_view[FLOATING_POINT_RESULT_REGISTER] = atof(string);
      break;
    }

    case READ_STRING_SYSCALL: {
      const int n = read_input(
          (char*)memory_reference(
              (memory_address)general_purpose_registers[ARGUMENT_REGISTER_0]),
          general_purpose_registers[ARGUMENT_REGISTER_1]);
      general_purpose_registers[ARGUMENT_REGISTER_3] =
          (n == 0) ? 1 : 0; /* EOF flag */
      data_modified = true;
      break;
    }

    case SBRK_SYSCALL: {
      const memory_address x = data_top;
      expand_data(general_purpose_registers[ARGUMENT_REGISTER_0]);
      general_purpose_registers[RESULT_REGISTER] = (register_word)x;
      data_modified = true;
      break;
    }

    case PRINT_CHARACTER_SYSCALL:
      write_output(console_out, "%c",
                   general_purpose_registers[ARGUMENT_REGISTER_0]);
      break;

    case READ_CHARACTER_SYSCALL: {
      static char string[2];

      const int n = read_input(string, 2);
      if (n == 0) {
        general_purpose_registers[RESULT_REGISTER] =
            -1; /* EOF -- matches getchar()'s convention */
      } else {
        /* Treat the byte as unsigned (0..255) so bytes >= 0x80 don't
           alias with the -1 EOF sentinel via sign extension. */
        general_purpose_registers[RESULT_REGISTER] =
            (register_word)(unsigned char)string[0];
      }
      break;
    }

    case EXIT_SYSCALL:
      spim_return_value = 0;
      return (false);

    case EXIT2_SYSCALL:
      spim_return_value = general_purpose_registers
          [ARGUMENT_REGISTER_0]; /* value passed to spim's exit() call */
      return (false);

    case OPEN_SYSCALL: {
      general_purpose_registers[RESULT_REGISTER] = open(
          (char*)memory_reference(
              (memory_address)general_purpose_registers[ARGUMENT_REGISTER_0]),
          general_purpose_registers[ARGUMENT_REGISTER_1],
          general_purpose_registers[ARGUMENT_REGISTER_2]);
      break;
    }

    case READ_SYSCALL: {
      /* Test if address is valid */
      (void)memory_reference(
          (memory_address)(general_purpose_registers[ARGUMENT_REGISTER_1] +
                           general_purpose_registers[ARGUMENT_REGISTER_2] - 1));
      general_purpose_registers[RESULT_REGISTER] = (register_word)read(
          general_purpose_registers[ARGUMENT_REGISTER_0],
          memory_reference(
              (memory_address)general_purpose_registers[ARGUMENT_REGISTER_1]),
          (size_t)general_purpose_registers[ARGUMENT_REGISTER_2]);
      data_modified = true;
      break;
    }

    case WRITE_SYSCALL: {
      /* Test if address is valid */
      (void)memory_reference(
          (memory_address)(general_purpose_registers[ARGUMENT_REGISTER_1] +
                           general_purpose_registers[ARGUMENT_REGISTER_2] - 1));
      general_purpose_registers[RESULT_REGISTER] = (register_word)write(
          general_purpose_registers[ARGUMENT_REGISTER_0],
          memory_reference(
              (memory_address)general_purpose_registers[ARGUMENT_REGISTER_1]),
          (size_t)general_purpose_registers[ARGUMENT_REGISTER_2]);
      break;
    }

    case CLOSE_SYSCALL: {
      general_purpose_registers[RESULT_REGISTER] =
          close(general_purpose_registers[ARGUMENT_REGISTER_0]);
      break;
    }

    default:
      run_error("Unknown system call: %d\n",
                general_purpose_registers[VALUE_REGISTER_0]);
      break;
  }

  return (true);
}

void handle_exception(void) {
  if (!quiet && coprocessor_0_exception_code != EXCEPTION_CODE_INTERRUPT)
    error("Exception occurred at PC=0x%08x\n",
          coprocessor_0_exception_program_counter);

  exception_occurred = false;
  PROGRAM_COUNTER = EXCEPTION_ADDRESS;

  switch (coprocessor_0_exception_code) {
    case EXCEPTION_CODE_INTERRUPT:
      break;

    case EXCEPTION_CODE_ADDRESS_ERROR_LOAD:
      if (!quiet)
        error("  Unaligned address in instruction/data fetch: 0x%08x\n",
              coprocessor_0_bad_virtual_address);
      break;

    case EXCEPTION_CODE_ADDRESS_ERROR_STORE:
      if (!quiet)
        error("  Unaligned address in store: 0x%08x\n",
              coprocessor_0_bad_virtual_address);
      break;

    case EXCEPTION_CODE_INSTRUCTION_BUS_ERROR:
      if (!quiet)
        error("  Bad address in text read: 0x%08x\n",
              coprocessor_0_bad_virtual_address);
      break;

    case EXCEPTION_CODE_DATA_BUS_ERROR:
      if (!quiet)
        error("  Bad address in data/stack read: 0x%08x\n",
              coprocessor_0_bad_virtual_address);
      break;

    case EXCEPTION_CODE_SYSTEM_CALL:
      if (!quiet) error("  Error in syscall\n");
      break;

    case EXCEPTION_CODE_BREAKPOINT:
      exception_occurred = false;
      return;

    case EXCEPTION_CODE_RESERVED_INSTRUCTION:
      if (!quiet) error("  Reserved instruction execution\n");
      break;

    case EXCEPTION_CODE_COPROCESSOR_UNUSABLE:
      if (!quiet) error("  Coprocessor unuable\n");
      break;

    case EXCEPTION_CODE_ARITHMETIC_OVERFLOW:
      if (!quiet) error("  Arithmetic overflow\n");
      break;

    case EXCEPTION_CODE_TRAP:
      if (!quiet) error("  Trap\n");
      break;

    case EXCEPTION_CODE_FLOATING_POINT_EXCEPTION:
      if (!quiet) error("  Floating point\n");
      break;

    default:
      if (!quiet)
        error("Unknown exception: %d\n", coprocessor_0_exception_code);
      break;
  }
}
