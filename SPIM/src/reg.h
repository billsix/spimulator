/* SPIM S20 MIPS simulator.
   Declarations of registers and code for accessing them.

   Copyright (C) 1990-2004 by James Larus (larus@cs.wisc.edu).
   ALL RIGHTS RESERVED.

   SPIM is distributed under the following conditions:

     You may make copies of SPIM for your own use and modify those copies.

     All copies of SPIM must retain my name and copyright notice.

     You may not sell SPIM or distributed SPIM in conjunction with a
     commerical product or service without the expressed written consent of
     James Larus.

   THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE. */


/* $Header: /Software/SPIM/src/reg.h 12    3/11/04 10:15p Larus $
*/


typedef int32 reg_word;
typedef uint32 u_reg_word;


/* General purpose registers: */

#define R_LENGTH	32

extern reg_word R[R_LENGTH];

extern reg_word HI, LO;

extern mem_addr PC, nPC;


/* Argument passing registers */

#define REG_V0		2
#define REG_A0		4
#define REG_A1		5
#define REG_A2		6
#define REG_A3		7
#define REG_FA0		12
#define REG_SP		29


/* Result registers */

#define REG_RES		2
#define REG_FRES	0


/* $gp registers */

#define REG_GP		28



/* Coprocessor registers: */

extern reg_word CCR[4][32], CPR[4][32];



/* Exeception handling registers (Coprocessor 0): */

/* BadVAddr register: */
#define CP0_BadVAddr_Reg 8
#define CP0_BadVAddr	(CPR[0][CP0_BadVAddr_Reg])

/* Count register: */
#define CP0_Count_Reg	9
#define CP0_Count	(CPR[0][CP0_Count_Reg]) /* ToDo */

/* Compare register: */
#define CP0_Compare_Reg	11
#define CP0_Compare	(CPR[0][CP0_Compare_Reg]) /* ToDo */

/* Status register: */
#define CP0_Status_Reg	12
#define CP0_Status	(CPR[0][CP0_Status_Reg])
/* Implemented fields: */
#define CP0_Status_CU	0xf0000000
#define CP0_Status_IM	0x0000ff00
#define CP0_Status_IM7  0x00008000 /* HW Int 5 */
#define CP0_Status_IM6  0x00004000 /* HW Int 4 */
#define CP0_Status_IM5  0x00002000 /* HW Int 3 */
#define CP0_Status_IM4  0x00001000 /* HW Int 2 */
#define CP0_Status_IM3  0x00000800 /* HW Int 1 */
#define CP0_Status_IM2  0x00000400 /* HW Int 0 */
#define CP0_Status_IM1  0x00000200 /* SW Int 1 */
#define CP0_Status_IM0  0x00000100 /* SW Int 0 */
#define CP0_Status_UM	0x00000010
#define CP0_Status_EXL	0x00000002
#define CP0_Status_IE	0x00000001
#define CP0_Status_Mask (CP0_Status_CU		\
			 | CP0_Status_UM	\
			 | CP0_Status_IM	\
			 | CP0_Status_EXL	\
			 | CP0_Status_IE)

/* Cause register: */
#define CP0_Cause_Reg	13
#define CP0_Cause	(CPR[0][CP0_Cause_Reg])
/* Implemented fields: */
#define CP0_Cause_BD	0x80000000
#define CP0_Cause_IP	0x0000ff00
#define CP0_Cause_IP7   0x00008000 /* HW Int 5 */
#define CP0_Cause_IP6   0x00004000 /* HW Int 4 */
#define CP0_Cause_IP5   0x00002000 /* HW Int 3 */
#define CP0_Cause_IP4   0x00001000 /* HW Int 2 */
#define CP0_Cause_IP3   0x00000800 /* HW Int 1 */
#define CP0_Cause_IP2   0x00000400 /* HW Int 0 */
#define CP0_Cause_IP1   0x00000200 /* SW Int 1 */
#define CP0_Cause_IP0   0x00000100 /* SW Int 0 */
#define CP0_Cause_ExcCode 0x0000003c
#define CP0_Cause_Mask	(CP0_Cause_BD		\
			 | CP0_Cause_IP		\
			 | CP0_Cause_IP7	\
			 | CP0_Cause_IP6	\
			 | CP0_Cause_IP5	\
			 | CP0_Cause_IP4	\
			 | CP0_Cause_IP3	\
			 | CP0_Cause_IP2	\
			 | CP0_Cause_ExcCode)
#define CP0_ExCode	((CP0_Cause & CP0_Cause_ExcCode) >> 2)

/* EPC register: */
#define CP0_EPC_Reg	14
#define CP0_EPC		(CPR[0][CP0_EPC_Reg])

/* Config register: */
#define CP0_Config_Reg	16
#define CP0_Config	(CPR[0][CP0_Config_Reg])
/* Implemented fields: */
#define CP0_Config_BE	0x000080000
#define CP0_Config_AT	0x000060000
#define CP0_Config_AR	0x00001c000
#define CP0_Config_MT	0x000000380
#define CP0_Config_Mask (CP0_Config_BE		\
			 | CP0_Config_AT	\
			 | CP0_Config_AR	\
			 | CP0_Config_MT)



/* Floating Point Coprocessor (1) registers.

   This is the MIPS32, Revision 1 FPU register set. It contains 32, 32-bit
   registers (either 32 single or 16 double precision), as in the R2010.
   The MIPS32, Revision 2 or MIPS64 register set has 32 of each type of
   register. */

#define FGR_LENGTH	32
#define FPR_LENGTH	16

extern double *FPR;		/* Dynamically allocate so overlay */
extern float *FGR;		/* is possible */
extern int *FWR;		/* is possible */


#ifdef LITTLEENDIAN
#define FPR_S(REGNO)	(FGR[REGNO])
#else
/* Flip low-order bit of register number, so that even and odd 32-bit float
   registers reverse and correctly overlap the double registers:

   0              63              0              63
   -----------------              -----------------
   |   1   |   0   |	  =>      |   0   |   1   |
   -----------------              -----------------

   Fortunately, this is not necessary on little endian machines. */
#define FPR_S(REGNO)	(FGR[(REGNO) ^ 0x1])
#endif

#define FPR_D(REGNO)	(((REGNO) & 0x1) \
			 ? (run_error ("Odd FP double register number\n") \
			    ? 0.0 : 0.0) \
			 : FPR[(REGNO) / 2])

#define FPR_W(REGNO)	(FWR[REGNO])


#ifdef LITTLEENDIAN
#define SET_FPR_S(REGNO, VALUE)	{FGR[REGNO] = (float) (VALUE);}
#else
#define SET_FPR_S(REGNO, VALUE)	{FGR[(REGNO) ^ 0x1] = (float) (VALUE);}
#endif

#define SET_FPR_D(REGNO, VALUE) {if ((REGNO) & 0x1) \
				 run_error ("Odd FP double register number\n"); \
				 else FPR[(REGNO) / 2] = (double) (VALUE);}

#define SET_FPR_W(REGNO, VALUE) {FWR[REGNO] = (int32) (VALUE);}


/* Floating point control registers: */

#define FCR		(CPR[1])


#define FIR_REG		0
#define FIR		(FCR[FIR_REG])
/* Implemented fields: */
#define FIR_W		0x0008000
#define FIR_D		0x0001000
#define FIR_S		0x0000800
#define FIR_MASK	(FIR_W | FIR_D | FIR_S)

#define FCCR_REG	25
#define FCCR		(FCR[FCCR_REG])
/* Implemented fields: */
#define FCCR_FCC	0x000000ff
#define FCCR_MASK	(FCCR_FCC)

#define FEXR_REG	26
#define FEXR		(FCR[FEXR_REG])
/* No implemented fields */

#define FENR_REG	28
#define FENR		(FCR[FENR_REG])
/* No implemented fields */

#define FCSR_REG	31
#define FCSR		(FCR[FCSR_REG])
/* Implemented fields: */
#define FCSR_FCC	0xfe800000
#define FCSR_MASK	(FCSR_FCC)
/* Floating point Cause (not implemented): */
#define FCSR_Cause_E	0x00020000
#define FCSR_Cause_V	0x00010000
#define FCSR_Cause_Z	0x00008000
#define FCSR_Cause_O	0x00004000
#define FCSR_Cause_U	0x00002000
#define FCSR_Cause_I	0x00001000
/* Floating point Enables (not implemented): */
#define FCSR_Enable_V	0x00000800
#define FCSR_Enable_Z	0x00000400
#define FCSR_Enable_O	0x00000200
#define FCSR_Enable_U	0x00000100
#define FCSR_Enable_I	0x00000080
/* Floating point Flags (not implemented): */
#define FCSR_Flag_V	0x00000040
#define FCSR_Flag_Z	0x00000020
#define FCSR_Flag_O	0x00000010
#define FCSR_Flag_U	0x00000008
#define FCSR_Flag_I	0x00000004
