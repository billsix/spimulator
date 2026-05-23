/* SPIM S20 MIPS simulator.
   Declarations of registers and code for accessing them.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef REG_H
#define REG_H

#include <stdint.h>
#include "spim.h"

typedef int32_t reg_word;
typedef uint32_t u_reg_word;

/* General purpose registers: */
constexpr int R_LENGTH = 32;

extern reg_word gpr[R_LENGTH];

extern reg_word HI, LO;

extern mem_addr PC, nPC;

/* Argument passing registers */
constexpr int REG_V0 = 2;
constexpr int REG_A0 = 4;
constexpr int REG_A1 = 5;
constexpr int REG_A2 = 6;
constexpr int REG_A3 = 7;
constexpr int REG_FA0 = 12;
constexpr int REG_SP = 29;

/* Result registers (REG_RES aliases REG_V0; REG_FRES is $f0). */
constexpr int REG_RES = 2;
constexpr int REG_FRES = 0;

/* $gp register */
constexpr int REG_GP = 28;

extern char* int_reg_names[];

/* Coprocessor registers: */

extern reg_word coprocessor_control_registers[4][32],
    coprocessor_registers[4][32];

/* Exception handling registers (Coprocessor 0): */

/* BadVAddr register: */
constexpr int CP0_BadVAddr_Reg = 8;
#define CP0_BadVAddr (coprocessor_registers[0][CP0_BadVAddr_Reg])

/* Count register: */
constexpr int CP0_Count_Reg = 9;
#define CP0_Count (coprocessor_registers[0][CP0_Count_Reg]) /* ToDo */

/* Compare register: */
constexpr int CP0_Compare_Reg = 11;
#define CP0_Compare (coprocessor_registers[0][CP0_Compare_Reg]) /* ToDo */

/* Status register: */
constexpr int CP0_Status_Reg = 12;
#define CP0_Status (coprocessor_registers[0][CP0_Status_Reg])
/* Implemented fields: */
constexpr uint32_t CP0_Status_CU = 0xf0000000;
constexpr uint32_t CP0_Status_IM = 0x0000ff00;
constexpr uint32_t CP0_Status_IM7 = 0x00008000; /* HW Int 5 */
constexpr uint32_t CP0_Status_IM6 = 0x00004000; /* HW Int 4 */
constexpr uint32_t CP0_Status_IM5 = 0x00002000; /* HW Int 3 */
constexpr uint32_t CP0_Status_IM4 = 0x00001000; /* HW Int 2 */
constexpr uint32_t CP0_Status_IM3 = 0x00000800; /* HW Int 1 */
constexpr uint32_t CP0_Status_IM2 = 0x00000400; /* HW Int 0 */
constexpr uint32_t CP0_Status_IM1 = 0x00000200; /* SW Int 1 */
constexpr uint32_t CP0_Status_IM0 = 0x00000100; /* SW Int 0 */
constexpr uint32_t CP0_Status_UM = 0x00000010;
constexpr uint32_t CP0_Status_EXL = 0x00000002;
constexpr uint32_t CP0_Status_IE = 0x00000001;
constexpr uint32_t CP0_Status_Mask = CP0_Status_CU | CP0_Status_UM |
                                     CP0_Status_IM | CP0_Status_EXL |
                                     CP0_Status_IE;

/* Cause register: */
constexpr int CP0_Cause_Reg = 13;
#define CP0_Cause (coprocessor_registers[0][CP0_Cause_Reg])
/* Implemented fields: */
constexpr uint32_t CP0_Cause_BD = 0x80000000;
constexpr uint32_t CP0_Cause_IP = 0x0000ff00;
constexpr uint32_t CP0_Cause_IP7 = 0x00008000; /* HW Int 5 */
constexpr uint32_t CP0_Cause_IP6 = 0x00004000; /* HW Int 4 */
constexpr uint32_t CP0_Cause_IP5 = 0x00002000; /* HW Int 3 */
constexpr uint32_t CP0_Cause_IP4 = 0x00001000; /* HW Int 2 */
constexpr uint32_t CP0_Cause_IP3 = 0x00000800; /* HW Int 1 */
constexpr uint32_t CP0_Cause_IP2 = 0x00000400; /* HW Int 0 */
constexpr uint32_t CP0_Cause_IP1 = 0x00000200; /* SW Int 1 */
constexpr uint32_t CP0_Cause_IP0 = 0x00000100; /* SW Int 0 */
constexpr uint32_t CP0_Cause_ExcCode = 0x0000007c;
constexpr uint32_t CP0_Cause_Mask =
    CP0_Cause_BD | CP0_Cause_IP | CP0_Cause_IP7 | CP0_Cause_IP6 |
    CP0_Cause_IP5 | CP0_Cause_IP4 | CP0_Cause_IP3 | CP0_Cause_IP2 |
    CP0_Cause_ExcCode;
#define CP0_ExCode ((CP0_Cause & CP0_Cause_ExcCode) >> 2)

/* EPC register: */
constexpr int CP0_EPC_Reg = 14;
#define CP0_EPC (coprocessor_registers[0][CP0_EPC_Reg])

/* Config register: */
constexpr int CP0_Config_Reg = 16;
#define CP0_Config (coprocessor_registers[0][CP0_Config_Reg])
/* Implemented fields: */
constexpr uint32_t CP0_Config_BE = 0x000080000;
constexpr uint32_t CP0_Config_AT = 0x000060000;
constexpr uint32_t CP0_Config_AR = 0x00001c000;
constexpr uint32_t CP0_Config_MT = 0x000000380;
constexpr uint32_t CP0_Config_Mask =
    CP0_Config_BE | CP0_Config_AT | CP0_Config_AR | CP0_Config_MT;

/* Floating Point Coprocessor (1) registers.

   This is the MIPS32, Revision 1 FPU register set. It contains 32, 32-bit
   registers (either 32 single or 16 double precision), as in the R2010.
   The MIPS32, Revision 2 or MIPS64 register set has 32 of each type of
   register. */

constexpr int FGR_LENGTH = 32;
constexpr int FPR_LENGTH = 16;

extern double* fp_double_view; /* Dynamically allocate so overlay */
extern float* fp_single_view;  /* is possible */
extern int* fp_int_view;       /* is possible */

#define FPR_S(REGNO) (fp_single_view[REGNO])

#define FPR_D(REGNO)                                                     \
  (((REGNO) & 0x1) ? (run_error("Odd FP double register number\n"), 0.0) \
                   : fp_double_view[(REGNO) / 2])

#define FPR_W(REGNO) (fp_int_view[REGNO])

#define SET_FPR_S(REGNO, VALUE)             \
  {                                         \
    fp_single_view[REGNO] = (float)(VALUE); \
  }

#define SET_FPR_D(REGNO, VALUE)                      \
  {                                                  \
    if ((REGNO) & 0x1)                               \
      run_error("Odd FP double register number\n");  \
    else                                             \
      fp_double_view[(REGNO) / 2] = (double)(VALUE); \
  }

#define SET_FPR_W(REGNO, VALUE)            \
  {                                        \
    fp_int_view[REGNO] = (int32_t)(VALUE); \
  }

/* Floating point control registers: */

#define FCR (coprocessor_registers[1])

constexpr int FIR_REG = 0;
#define FIR (FCR[FIR_REG])

/* Implemented fields: */
constexpr uint32_t FIR_W = 0x0008000;
constexpr uint32_t FIR_D = 0x0001000;
constexpr uint32_t FIR_S = 0x0000800;
constexpr uint32_t FIR_MASK = FIR_W | FIR_D | FIR_S;

constexpr int FCSR_REG = 31;
#define FCSR (FCR[FCSR_REG])

/* Implemented fields: */
constexpr uint32_t FCSR_FCC = 0xfe800000;
constexpr uint32_t FCSR_MASK = FCSR_FCC;
constexpr int CC0_bit = 23;
constexpr int CC1_bit = 25;
#define CC_mask(n) \
  ((((n) == 0) || ((n) > 7)) ? (1 << CC0_bit) : (1 << (CC1_bit + (n) - 1)))
#define FCC(n) (((FCSR & CC_mask(n)) == 0) ? 0 : 1)
#define SET_FCC(n, v)    \
  if ((v) == 0) {        \
    FCSR &= ~CC_mask(n); \
  } else {               \
    FCSR |= CC_mask(n);  \
  }

/* Floating point Cause (not implemented): */
constexpr uint32_t FCSR_Cause_E = 0x00020000;
constexpr uint32_t FCSR_Cause_V = 0x00010000;
constexpr uint32_t FCSR_Cause_Z = 0x00008000;
constexpr uint32_t FCSR_Cause_O = 0x00004000;
constexpr uint32_t FCSR_Cause_U = 0x00002000;
constexpr uint32_t FCSR_Cause_I = 0x00001000;
/* Floating point Enables (not implemented): */
constexpr uint32_t FCSR_Enable_V = 0x00000800;
constexpr uint32_t FCSR_Enable_Z = 0x00000400;
constexpr uint32_t FCSR_Enable_O = 0x00000200;
constexpr uint32_t FCSR_Enable_U = 0x00000100;
constexpr uint32_t FCSR_Enable_I = 0x00000080;
/* Floating point Flags (not implemented): */
constexpr uint32_t FCSR_Flag_V = 0x00000040;
constexpr uint32_t FCSR_Flag_Z = 0x00000020;
constexpr uint32_t FCSR_Flag_O = 0x00000010;
constexpr uint32_t FCSR_Flag_U = 0x00000008;
constexpr uint32_t FCSR_Flag_I = 0x00000004;

#endif
