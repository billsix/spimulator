/* SPIM S20 MIPS simulator.
   Declarations of registers and code for accessing them.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef REG_H
#define REG_H

#include <stdint.h>
#include "spim.h"

typedef int32_t register_word;
typedef uint32_t unsigned_register_word;

/* General purpose registers: */
constexpr int GENERAL_PURPOSE_REGISTER_COUNT = 32;

extern register_word general_purpose_registers[GENERAL_PURPOSE_REGISTER_COUNT];

extern register_word MULTIPLY_DIVIDE_HIGH, MULTIPLY_DIVIDE_LOW;

extern memory_address PROGRAM_COUNTER, next_program_counter;

/* Argument passing registers */
constexpr int VALUE_REGISTER_0 = 2;
constexpr int ARGUMENT_REGISTER_0 = 4;
constexpr int ARGUMENT_REGISTER_1 = 5;
constexpr int ARGUMENT_REGISTER_2 = 6;
constexpr int ARGUMENT_REGISTER_3 = 7;
constexpr int FLOATING_POINT_ARGUMENT_REGISTER_0 = 12;
constexpr int STACK_POINTER_REGISTER = 29;

/* Result registers (RESULT_REGISTER aliases VALUE_REGISTER_0; FLOATING_POINT_RESULT_REGISTER is $f0). */
constexpr int RESULT_REGISTER = 2;
constexpr int FLOATING_POINT_RESULT_REGISTER = 0;

/* $gp register */
constexpr int GLOBAL_POINTER_REGISTER = 28;

extern char* integer_register_names[];

/* Coprocessor registers: */

extern register_word coprocessor_control_registers[4][32],
    coprocessor_registers[4][32];

/* Exception handling registers (Coprocessor 0): */

/* BadVAddr register: */
constexpr int COPROCESSOR_0_BAD_VIRTUAL_ADDRESS_REGISTER_NUMBER = 8;
#define coprocessor_0_bad_virtual_address (coprocessor_registers[0][COPROCESSOR_0_BAD_VIRTUAL_ADDRESS_REGISTER_NUMBER])

/* Count register: */
constexpr int COPROCESSOR_0_COUNT_REGISTER_NUMBER = 9;
#define coprocessor_0_count (coprocessor_registers[0][COPROCESSOR_0_COUNT_REGISTER_NUMBER]) /* ToDo */

/* Compare register: */
constexpr int COPROCESSOR_0_COMPARE_REGISTER_NUMBER = 11;
#define coprocessor_0_compare (coprocessor_registers[0][COPROCESSOR_0_COMPARE_REGISTER_NUMBER]) /* ToDo */

/* Status register: */
constexpr int COPROCESSOR_0_STATUS_REGISTER_NUMBER = 12;
#define coprocessor_0_status (coprocessor_registers[0][COPROCESSOR_0_STATUS_REGISTER_NUMBER])
/* Implemented fields: */
constexpr uint32_t COPROCESSOR_0_STATUS_COPROCESSOR_USABLE = 0xf0000000;
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK = 0x0000ff00;
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_7 = 0x00008000; /* HW Int 5 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_6 = 0x00004000; /* HW Int 4 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_5 = 0x00002000; /* HW Int 3 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_4 = 0x00001000; /* HW Int 2 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_3 = 0x00000800; /* HW Int 1 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_2 = 0x00000400; /* HW Int 0 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_1 = 0x00000200; /* SW Int 1 */
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_MASK_0 = 0x00000100; /* SW Int 0 */
constexpr uint32_t COPROCESSOR_0_STATUS_USER_MODE = 0x00000010;
constexpr uint32_t COPROCESSOR_0_STATUS_EXCEPTION_LEVEL = 0x00000002;
constexpr uint32_t COPROCESSOR_0_STATUS_INTERRUPT_ENABLE = 0x00000001;
constexpr uint32_t COPROCESSOR_0_STATUS_MASK = COPROCESSOR_0_STATUS_COPROCESSOR_USABLE | COPROCESSOR_0_STATUS_USER_MODE |
                                     COPROCESSOR_0_STATUS_INTERRUPT_MASK | COPROCESSOR_0_STATUS_EXCEPTION_LEVEL |
                                     COPROCESSOR_0_STATUS_INTERRUPT_ENABLE;

/* Cause register: */
constexpr int COPROCESSOR_0_CAUSE_REGISTER_NUMBER = 13;
#define coprocessor_0_cause (coprocessor_registers[0][COPROCESSOR_0_CAUSE_REGISTER_NUMBER])
/* Implemented fields: */
constexpr uint32_t COPROCESSOR_0_CAUSE_BRANCH_DELAY = 0x80000000;
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPTS_PENDING = 0x0000ff00;
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_7 = 0x00008000; /* HW Int 5 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_6 = 0x00004000; /* HW Int 4 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_5 = 0x00002000; /* HW Int 3 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_4 = 0x00001000; /* HW Int 2 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_3 = 0x00000800; /* HW Int 1 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_2 = 0x00000400; /* HW Int 0 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_1 = 0x00000200; /* SW Int 1 */
constexpr uint32_t COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_0 = 0x00000100; /* SW Int 0 */
constexpr uint32_t COPROCESSOR_0_CAUSE_EXCEPTION_CODE = 0x0000007c;
constexpr uint32_t COPROCESSOR_0_CAUSE_MASK =
    COPROCESSOR_0_CAUSE_BRANCH_DELAY | COPROCESSOR_0_CAUSE_INTERRUPTS_PENDING | COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_7 | COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_6 |
    COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_5 | COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_4 | COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_3 | COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_2 |
    COPROCESSOR_0_CAUSE_EXCEPTION_CODE;
#define coprocessor_0_exception_code (((uint32_t)coprocessor_0_cause & COPROCESSOR_0_CAUSE_EXCEPTION_CODE) >> 2)

/* EPC register: */
constexpr int COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER_REGISTER_NUMBER = 14;
#define coprocessor_0_exception_program_counter (coprocessor_registers[0][COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER_REGISTER_NUMBER])

/* Config register: */
constexpr int COPROCESSOR_0_CONFIG_REGISTER_NUMBER = 16;
#define coprocessor_0_config (coprocessor_registers[0][COPROCESSOR_0_CONFIG_REGISTER_NUMBER])
/* Implemented fields: */
constexpr uint32_t COPROCESSOR_0_CONFIG_BIG_ENDIAN = 0x000080000;
constexpr uint32_t COPROCESSOR_0_CONFIG_ARCHITECTURE_TYPE = 0x000060000;
constexpr uint32_t COPROCESSOR_0_CONFIG_ARCHITECTURE_REVISION = 0x00001c000;
constexpr uint32_t COPROCESSOR_0_CONFIG_MEMORY_MANAGEMENT_UNIT_TYPE = 0x000000380;
constexpr uint32_t COPROCESSOR_0_CONFIG_MASK =
    COPROCESSOR_0_CONFIG_BIG_ENDIAN | COPROCESSOR_0_CONFIG_ARCHITECTURE_TYPE | COPROCESSOR_0_CONFIG_ARCHITECTURE_REVISION | COPROCESSOR_0_CONFIG_MEMORY_MANAGEMENT_UNIT_TYPE;

/* Floating Point Coprocessor (1) registers.

   This is the MIPS32, Revision 1 FPU register set. It contains 32, 32-bit
   registers (either 32 single or 16 double precision), as in the R2010.
   The MIPS32, Revision 2 or MIPS64 register set has 32 of each type of
   register. */

constexpr int FLOATING_POINT_GENERAL_REGISTER_LENGTH = 32;
constexpr int FLOATING_POINT_REGISTER_LENGTH = 16;

extern double* floating_point_double_view; /* Dynamically allocate so overlay */
extern float* floating_point_single_view;  /* is possible */
extern int32_t* floating_point_integer_view;   /* is possible */

/* The views above overlay one buffer: 16 doubles = 32 floats = 32 words. */
static_assert(sizeof(float) == 4 && sizeof(double) == 8,
              "the FP register views assume IEEE binary32/binary64 sizes");

#define FLOATING_POINT_REGISTER_SINGLE(REGNO) (floating_point_single_view[REGNO])

#define FLOATING_POINT_REGISTER_DOUBLE(REGNO)                                                     \
  (((REGNO) & 0x1) ? (run_error("Odd FP double register number\n"), 0.0) \
                   : floating_point_double_view[(REGNO) / 2])

#define FLOATING_POINT_REGISTER_WORD(REGNO) (floating_point_integer_view[REGNO])

#define SET_FLOATING_POINT_REGISTER_SINGLE(REGNO, VALUE)             \
  {                                         \
    floating_point_single_view[REGNO] = (float)(VALUE); \
  }

#define SET_FLOATING_POINT_REGISTER_DOUBLE(REGNO, VALUE)                      \
  {                                                  \
    if ((REGNO) & 0x1)                               \
      run_error("Odd FP double register number\n");  \
    else                                             \
      floating_point_double_view[(REGNO) / 2] = (double)(VALUE); \
  }

#define SET_FLOATING_POINT_REGISTER_WORD(REGNO, VALUE)            \
  {                                        \
    floating_point_integer_view[REGNO] = (int32_t)(VALUE); \
  }

/* Floating point control registers: */

#define floating_point_control_registers (coprocessor_registers[1])

constexpr int FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER = 0;
#define floating_point_implementation_register (floating_point_control_registers[FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER])

/* Implemented fields: */
constexpr uint32_t FLOATING_POINT_IMPLEMENTATION_HAS_WORD = 0x0008000;
constexpr uint32_t FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE = 0x0001000;
constexpr uint32_t FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE = 0x0000800;
constexpr uint32_t FLOATING_POINT_IMPLEMENTATION_MASK = FLOATING_POINT_IMPLEMENTATION_HAS_WORD | FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE | FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE;

constexpr int FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER = 31;
#define floating_point_control_status_register (floating_point_control_registers[FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER])

/* Implemented fields: */
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CONDITION_CODES = 0xfe800000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_MASK = FLOATING_POINT_CONTROL_STATUS_CONDITION_CODES;
constexpr int CONDITION_CODE_0_BIT = 23;
constexpr int CONDITION_CODE_1_BIT = 25;
/* The floating_point_control_status_register bit for condition code N (cc 0 is bit 23, cc 1..7 are bits
   25..31; out-of-range N means cc 0).  1u: for n==7 the shift is
   `1 << 31`, which is UB on a signed `1`. */
static inline unsigned_register_word condition_code_mask(int n) {
  return ((n == 0) || (n > 7)) ? (1u << CONDITION_CODE_0_BIT) : (1u << (CONDITION_CODE_1_BIT + n - 1));
}

static inline int floating_point_condition_code(int n) {
  return (((unsigned_register_word)floating_point_control_status_register & condition_code_mask(n)) == 0) ? 0 : 1;
}

static inline void set_floating_point_condition_code(int n, int v) {
  if (v == 0)
    floating_point_control_status_register = (register_word)((unsigned_register_word)floating_point_control_status_register & ~condition_code_mask(n));
  else
    floating_point_control_status_register = (register_word)((unsigned_register_word)floating_point_control_status_register | condition_code_mask(n));
}

/* Floating point Cause (not implemented): */
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_UNIMPLEMENTED = 0x00020000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_INVALID = 0x00010000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_DIVIDE_BY_ZERO = 0x00008000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_OVERFLOW = 0x00004000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_UNDERFLOW = 0x00002000;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_CAUSE_INEXACT = 0x00001000;
/* Floating point Enables (not implemented): */
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_ENABLE_INVALID = 0x00000800;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_ENABLE_DIVIDE_BY_ZERO = 0x00000400;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_ENABLE_OVERFLOW = 0x00000200;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_ENABLE_UNDERFLOW = 0x00000100;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_ENABLE_INEXACT = 0x00000080;
/* Floating point Flags (not implemented): */
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_FLAG_INVALID = 0x00000040;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_FLAG_DIVIDE_BY_ZERO = 0x00000020;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_FLAG_OVERFLOW = 0x00000010;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_FLAG_UNDERFLOW = 0x00000008;
constexpr uint32_t FLOATING_POINT_CONTROL_STATUS_FLAG_INEXACT = 0x00000004;

#endif
