/* SPIM S20 MIPS simulator.
   Execute SPIM syscalls, both in simulator and bare mode.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* Exported functions. */

bool do_syscall(void);
void handle_exception(void);

/* Syscall numbers, passed in $v0. */
enum spim_syscall : int32_t {
  PRINT_INT_SYSCALL = 1,
  PRINT_FLOAT_SYSCALL = 2,
  PRINT_DOUBLE_SYSCALL = 3,
  PRINT_STRING_SYSCALL = 4,

  READ_INT_SYSCALL = 5,
  READ_FLOAT_SYSCALL = 6,
  READ_DOUBLE_SYSCALL = 7,
  READ_STRING_SYSCALL = 8,

  SBRK_SYSCALL = 9,

  EXIT_SYSCALL = 10,

  PRINT_CHARACTER_SYSCALL = 11,
  READ_CHARACTER_SYSCALL = 12,

  OPEN_SYSCALL = 13,
  READ_SYSCALL = 14,
  WRITE_SYSCALL = 15,
  CLOSE_SYSCALL = 16,

  EXIT2_SYSCALL = 17,
};

#endif
