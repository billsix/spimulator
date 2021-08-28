/* A Bison parser, made by GNU Bison 3.7.6.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30706

/* Bison version string.  */
#define YYBISON_VERSION "3.7.6"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 439 "parser.y"

#include <stdio.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "inst.h"
#include "reg.h"
#include "mem.h"
#include "sym-tbl.h"
#include "data.h"
#include "scanner.h"
#include "parser.h"


/* return (0) */
#define LINE_PARSE_DONE YYACCEPT

/* return (1) */
#define FILE_PARSE_DONE YYABORT

typedef struct ll
{
  label *head;
  struct ll *tail;
} label_list;


/* Exported Variables: */

bool data_dir;                  /* => item in data segment */

bool text_dir;                  /* => item in text segment */

bool parse_error_occurred;      /* => parse resulted in error */


/* Local functions: */

static imm_expr *branch_offset (int n_inst);
static int cc_to_rt (int cc, int nd, int tf);
static void check_imm_range (imm_expr*, int32, int32);
static void check_uimm_range (imm_expr*, uint32, uint32);
static void clear_labels ();
static label_list *cons_label (label *head, label_list *tail);
static void div_inst (int op, int rd, int rs, int rt, int const_divisor);
static void mips32_r2_inst ();
static void mult_inst (int op, int rd, int rs, int rt);
static void nop_inst ();
static void set_eq_inst (int op, int rd, int rs, int rt);
static void set_ge_inst (int op, int rd, int rs, int rt);
static void set_gt_inst (int op, int rd, int rs, int rt);
static void set_le_inst (int op, int rd, int rs, int rt);
static void store_word_data (int value);
static void trap_inst ();
static void yywarn (char*);


/* Local variables: */

static bool null_term;		/* => string terminate by \0 */

static void (*store_op) (int); /* Function to store items in an EXPR_LST */
static void (*store_fp_op) (double*); /* Ditto FP_EXPR_LST */

static label_list *this_line_labels = NULL; /* List of label for curent line */

static bool noat_flag = 0;	/* => program can use $1 */

static char *input_file_name;	/* Name of file being parsed */


#line 144 "parser_yacc.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser_yacc.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_Y_EOF = 3,                      /* Y_EOF  */
  YYSYMBOL_Y_NL = 4,                       /* Y_NL  */
  YYSYMBOL_Y_INT = 5,                      /* Y_INT  */
  YYSYMBOL_Y_ID = 6,                       /* Y_ID  */
  YYSYMBOL_Y_REG = 7,                      /* Y_REG  */
  YYSYMBOL_Y_FP_REG = 8,                   /* Y_FP_REG  */
  YYSYMBOL_Y_STR = 9,                      /* Y_STR  */
  YYSYMBOL_Y_FP = 10,                      /* Y_FP  */
  YYSYMBOL_Y_ABS_D_OP = 11,                /* Y_ABS_D_OP  */
  YYSYMBOL_Y_ABS_PS_OP = 12,               /* Y_ABS_PS_OP  */
  YYSYMBOL_Y_ABS_S_OP = 13,                /* Y_ABS_S_OP  */
  YYSYMBOL_Y_ADD_D_OP = 14,                /* Y_ADD_D_OP  */
  YYSYMBOL_Y_ADD_OP = 15,                  /* Y_ADD_OP  */
  YYSYMBOL_Y_ADD_PS_OP = 16,               /* Y_ADD_PS_OP  */
  YYSYMBOL_Y_ADD_S_OP = 17,                /* Y_ADD_S_OP  */
  YYSYMBOL_Y_ADDI_OP = 18,                 /* Y_ADDI_OP  */
  YYSYMBOL_Y_ADDIU_OP = 19,                /* Y_ADDIU_OP  */
  YYSYMBOL_Y_ADDU_OP = 20,                 /* Y_ADDU_OP  */
  YYSYMBOL_Y_ALNV_PS_OP = 21,              /* Y_ALNV_PS_OP  */
  YYSYMBOL_Y_AND_OP = 22,                  /* Y_AND_OP  */
  YYSYMBOL_Y_ANDI_OP = 23,                 /* Y_ANDI_OP  */
  YYSYMBOL_Y_BC1F_OP = 24,                 /* Y_BC1F_OP  */
  YYSYMBOL_Y_BC1FL_OP = 25,                /* Y_BC1FL_OP  */
  YYSYMBOL_Y_BC1T_OP = 26,                 /* Y_BC1T_OP  */
  YYSYMBOL_Y_BC1TL_OP = 27,                /* Y_BC1TL_OP  */
  YYSYMBOL_Y_BC2F_OP = 28,                 /* Y_BC2F_OP  */
  YYSYMBOL_Y_BC2FL_OP = 29,                /* Y_BC2FL_OP  */
  YYSYMBOL_Y_BC2T_OP = 30,                 /* Y_BC2T_OP  */
  YYSYMBOL_Y_BC2TL_OP = 31,                /* Y_BC2TL_OP  */
  YYSYMBOL_Y_BEQ_OP = 32,                  /* Y_BEQ_OP  */
  YYSYMBOL_Y_BEQL_OP = 33,                 /* Y_BEQL_OP  */
  YYSYMBOL_Y_BGEZ_OP = 34,                 /* Y_BGEZ_OP  */
  YYSYMBOL_Y_BGEZAL_OP = 35,               /* Y_BGEZAL_OP  */
  YYSYMBOL_Y_BGEZALL_OP = 36,              /* Y_BGEZALL_OP  */
  YYSYMBOL_Y_BGEZL_OP = 37,                /* Y_BGEZL_OP  */
  YYSYMBOL_Y_BGTZ_OP = 38,                 /* Y_BGTZ_OP  */
  YYSYMBOL_Y_BGTZL_OP = 39,                /* Y_BGTZL_OP  */
  YYSYMBOL_Y_BLEZ_OP = 40,                 /* Y_BLEZ_OP  */
  YYSYMBOL_Y_BLEZL_OP = 41,                /* Y_BLEZL_OP  */
  YYSYMBOL_Y_BLTZ_OP = 42,                 /* Y_BLTZ_OP  */
  YYSYMBOL_Y_BLTZAL_OP = 43,               /* Y_BLTZAL_OP  */
  YYSYMBOL_Y_BLTZALL_OP = 44,              /* Y_BLTZALL_OP  */
  YYSYMBOL_Y_BLTZL_OP = 45,                /* Y_BLTZL_OP  */
  YYSYMBOL_Y_BNE_OP = 46,                  /* Y_BNE_OP  */
  YYSYMBOL_Y_BNEL_OP = 47,                 /* Y_BNEL_OP  */
  YYSYMBOL_Y_BREAK_OP = 48,                /* Y_BREAK_OP  */
  YYSYMBOL_Y_C_EQ_D_OP = 49,               /* Y_C_EQ_D_OP  */
  YYSYMBOL_Y_C_EQ_PS_OP = 50,              /* Y_C_EQ_PS_OP  */
  YYSYMBOL_Y_C_EQ_S_OP = 51,               /* Y_C_EQ_S_OP  */
  YYSYMBOL_Y_C_F_D_OP = 52,                /* Y_C_F_D_OP  */
  YYSYMBOL_Y_C_F_PS_OP = 53,               /* Y_C_F_PS_OP  */
  YYSYMBOL_Y_C_F_S_OP = 54,                /* Y_C_F_S_OP  */
  YYSYMBOL_Y_C_LE_D_OP = 55,               /* Y_C_LE_D_OP  */
  YYSYMBOL_Y_C_LE_PS_OP = 56,              /* Y_C_LE_PS_OP  */
  YYSYMBOL_Y_C_LE_S_OP = 57,               /* Y_C_LE_S_OP  */
  YYSYMBOL_Y_C_LT_D_OP = 58,               /* Y_C_LT_D_OP  */
  YYSYMBOL_Y_C_LT_PS_OP = 59,              /* Y_C_LT_PS_OP  */
  YYSYMBOL_Y_C_LT_S_OP = 60,               /* Y_C_LT_S_OP  */
  YYSYMBOL_Y_C_NGE_D_OP = 61,              /* Y_C_NGE_D_OP  */
  YYSYMBOL_Y_C_NGE_PS_OP = 62,             /* Y_C_NGE_PS_OP  */
  YYSYMBOL_Y_C_NGE_S_OP = 63,              /* Y_C_NGE_S_OP  */
  YYSYMBOL_Y_C_NGL_D_OP = 64,              /* Y_C_NGL_D_OP  */
  YYSYMBOL_Y_C_NGL_PS_OP = 65,             /* Y_C_NGL_PS_OP  */
  YYSYMBOL_Y_C_NGL_S_OP = 66,              /* Y_C_NGL_S_OP  */
  YYSYMBOL_Y_C_NGLE_D_OP = 67,             /* Y_C_NGLE_D_OP  */
  YYSYMBOL_Y_C_NGLE_PS_OP = 68,            /* Y_C_NGLE_PS_OP  */
  YYSYMBOL_Y_C_NGLE_S_OP = 69,             /* Y_C_NGLE_S_OP  */
  YYSYMBOL_Y_C_NGT_D_OP = 70,              /* Y_C_NGT_D_OP  */
  YYSYMBOL_Y_C_NGT_PS_OP = 71,             /* Y_C_NGT_PS_OP  */
  YYSYMBOL_Y_C_NGT_S_OP = 72,              /* Y_C_NGT_S_OP  */
  YYSYMBOL_Y_C_OLE_D_OP = 73,              /* Y_C_OLE_D_OP  */
  YYSYMBOL_Y_C_OLE_PS_OP = 74,             /* Y_C_OLE_PS_OP  */
  YYSYMBOL_Y_C_OLE_S_OP = 75,              /* Y_C_OLE_S_OP  */
  YYSYMBOL_Y_C_OLT_D_OP = 76,              /* Y_C_OLT_D_OP  */
  YYSYMBOL_Y_C_OLT_PS_OP = 77,             /* Y_C_OLT_PS_OP  */
  YYSYMBOL_Y_C_OLT_S_OP = 78,              /* Y_C_OLT_S_OP  */
  YYSYMBOL_Y_C_SEQ_D_OP = 79,              /* Y_C_SEQ_D_OP  */
  YYSYMBOL_Y_C_SEQ_PS_OP = 80,             /* Y_C_SEQ_PS_OP  */
  YYSYMBOL_Y_C_SEQ_S_OP = 81,              /* Y_C_SEQ_S_OP  */
  YYSYMBOL_Y_C_SF_D_OP = 82,               /* Y_C_SF_D_OP  */
  YYSYMBOL_Y_C_SF_PS_OP = 83,              /* Y_C_SF_PS_OP  */
  YYSYMBOL_Y_C_SF_S_OP = 84,               /* Y_C_SF_S_OP  */
  YYSYMBOL_Y_C_UEQ_D_OP = 85,              /* Y_C_UEQ_D_OP  */
  YYSYMBOL_Y_C_UEQ_PS_OP = 86,             /* Y_C_UEQ_PS_OP  */
  YYSYMBOL_Y_C_UEQ_S_OP = 87,              /* Y_C_UEQ_S_OP  */
  YYSYMBOL_Y_C_ULE_D_OP = 88,              /* Y_C_ULE_D_OP  */
  YYSYMBOL_Y_C_ULE_PS_OP = 89,             /* Y_C_ULE_PS_OP  */
  YYSYMBOL_Y_C_ULE_S_OP = 90,              /* Y_C_ULE_S_OP  */
  YYSYMBOL_Y_C_ULT_D_OP = 91,              /* Y_C_ULT_D_OP  */
  YYSYMBOL_Y_C_ULT_PS_OP = 92,             /* Y_C_ULT_PS_OP  */
  YYSYMBOL_Y_C_ULT_S_OP = 93,              /* Y_C_ULT_S_OP  */
  YYSYMBOL_Y_C_UN_D_OP = 94,               /* Y_C_UN_D_OP  */
  YYSYMBOL_Y_C_UN_PS_OP = 95,              /* Y_C_UN_PS_OP  */
  YYSYMBOL_Y_C_UN_S_OP = 96,               /* Y_C_UN_S_OP  */
  YYSYMBOL_Y_CACHE_OP = 97,                /* Y_CACHE_OP  */
  YYSYMBOL_Y_CEIL_L_D_OP = 98,             /* Y_CEIL_L_D_OP  */
  YYSYMBOL_Y_CEIL_L_S_OP = 99,             /* Y_CEIL_L_S_OP  */
  YYSYMBOL_Y_CEIL_W_D_OP = 100,            /* Y_CEIL_W_D_OP  */
  YYSYMBOL_Y_CEIL_W_S_OP = 101,            /* Y_CEIL_W_S_OP  */
  YYSYMBOL_Y_CFC0_OP = 102,                /* Y_CFC0_OP  */
  YYSYMBOL_Y_CFC1_OP = 103,                /* Y_CFC1_OP  */
  YYSYMBOL_Y_CFC2_OP = 104,                /* Y_CFC2_OP  */
  YYSYMBOL_Y_CLO_OP = 105,                 /* Y_CLO_OP  */
  YYSYMBOL_Y_CLZ_OP = 106,                 /* Y_CLZ_OP  */
  YYSYMBOL_Y_COP2_OP = 107,                /* Y_COP2_OP  */
  YYSYMBOL_Y_CTC0_OP = 108,                /* Y_CTC0_OP  */
  YYSYMBOL_Y_CTC1_OP = 109,                /* Y_CTC1_OP  */
  YYSYMBOL_Y_CTC2_OP = 110,                /* Y_CTC2_OP  */
  YYSYMBOL_Y_CVT_D_L_OP = 111,             /* Y_CVT_D_L_OP  */
  YYSYMBOL_Y_CVT_D_S_OP = 112,             /* Y_CVT_D_S_OP  */
  YYSYMBOL_Y_CVT_D_W_OP = 113,             /* Y_CVT_D_W_OP  */
  YYSYMBOL_Y_CVT_L_D_OP = 114,             /* Y_CVT_L_D_OP  */
  YYSYMBOL_Y_CVT_L_S_OP = 115,             /* Y_CVT_L_S_OP  */
  YYSYMBOL_Y_CVT_PS_S_OP = 116,            /* Y_CVT_PS_S_OP  */
  YYSYMBOL_Y_CVT_S_D_OP = 117,             /* Y_CVT_S_D_OP  */
  YYSYMBOL_Y_CVT_S_L_OP = 118,             /* Y_CVT_S_L_OP  */
  YYSYMBOL_Y_CVT_S_PL_OP = 119,            /* Y_CVT_S_PL_OP  */
  YYSYMBOL_Y_CVT_S_PU_OP = 120,            /* Y_CVT_S_PU_OP  */
  YYSYMBOL_Y_CVT_S_W_OP = 121,             /* Y_CVT_S_W_OP  */
  YYSYMBOL_Y_CVT_W_D_OP = 122,             /* Y_CVT_W_D_OP  */
  YYSYMBOL_Y_CVT_W_S_OP = 123,             /* Y_CVT_W_S_OP  */
  YYSYMBOL_Y_DERET_OP = 124,               /* Y_DERET_OP  */
  YYSYMBOL_Y_DI_OP = 125,                  /* Y_DI_OP  */
  YYSYMBOL_Y_DIV_D_OP = 126,               /* Y_DIV_D_OP  */
  YYSYMBOL_Y_DIV_OP = 127,                 /* Y_DIV_OP  */
  YYSYMBOL_Y_DIV_S_OP = 128,               /* Y_DIV_S_OP  */
  YYSYMBOL_Y_DIVU_OP = 129,                /* Y_DIVU_OP  */
  YYSYMBOL_Y_EHB_OP = 130,                 /* Y_EHB_OP  */
  YYSYMBOL_Y_EI_OP = 131,                  /* Y_EI_OP  */
  YYSYMBOL_Y_ERET_OP = 132,                /* Y_ERET_OP  */
  YYSYMBOL_Y_EXT_OP = 133,                 /* Y_EXT_OP  */
  YYSYMBOL_Y_FLOOR_L_D_OP = 134,           /* Y_FLOOR_L_D_OP  */
  YYSYMBOL_Y_FLOOR_L_S_OP = 135,           /* Y_FLOOR_L_S_OP  */
  YYSYMBOL_Y_FLOOR_W_D_OP = 136,           /* Y_FLOOR_W_D_OP  */
  YYSYMBOL_Y_FLOOR_W_S_OP = 137,           /* Y_FLOOR_W_S_OP  */
  YYSYMBOL_Y_INS_OP = 138,                 /* Y_INS_OP  */
  YYSYMBOL_Y_J_OP = 139,                   /* Y_J_OP  */
  YYSYMBOL_Y_JAL_OP = 140,                 /* Y_JAL_OP  */
  YYSYMBOL_Y_JALR_HB_OP = 141,             /* Y_JALR_HB_OP  */
  YYSYMBOL_Y_JALR_OP = 142,                /* Y_JALR_OP  */
  YYSYMBOL_Y_JR_HB_OP = 143,               /* Y_JR_HB_OP  */
  YYSYMBOL_Y_JR_OP = 144,                  /* Y_JR_OP  */
  YYSYMBOL_Y_LB_OP = 145,                  /* Y_LB_OP  */
  YYSYMBOL_Y_LBU_OP = 146,                 /* Y_LBU_OP  */
  YYSYMBOL_Y_LDC1_OP = 147,                /* Y_LDC1_OP  */
  YYSYMBOL_Y_LDC2_OP = 148,                /* Y_LDC2_OP  */
  YYSYMBOL_Y_LDXC1_OP = 149,               /* Y_LDXC1_OP  */
  YYSYMBOL_Y_LH_OP = 150,                  /* Y_LH_OP  */
  YYSYMBOL_Y_LHU_OP = 151,                 /* Y_LHU_OP  */
  YYSYMBOL_Y_LL_OP = 152,                  /* Y_LL_OP  */
  YYSYMBOL_Y_LUI_OP = 153,                 /* Y_LUI_OP  */
  YYSYMBOL_Y_LUXC1_OP = 154,               /* Y_LUXC1_OP  */
  YYSYMBOL_Y_LW_OP = 155,                  /* Y_LW_OP  */
  YYSYMBOL_Y_LWC1_OP = 156,                /* Y_LWC1_OP  */
  YYSYMBOL_Y_LWC2_OP = 157,                /* Y_LWC2_OP  */
  YYSYMBOL_Y_LWL_OP = 158,                 /* Y_LWL_OP  */
  YYSYMBOL_Y_LWR_OP = 159,                 /* Y_LWR_OP  */
  YYSYMBOL_Y_LWXC1_OP = 160,               /* Y_LWXC1_OP  */
  YYSYMBOL_Y_MADD_D_OP = 161,              /* Y_MADD_D_OP  */
  YYSYMBOL_Y_MADD_OP = 162,                /* Y_MADD_OP  */
  YYSYMBOL_Y_MADD_PS_OP = 163,             /* Y_MADD_PS_OP  */
  YYSYMBOL_Y_MADD_S_OP = 164,              /* Y_MADD_S_OP  */
  YYSYMBOL_Y_MADDU_OP = 165,               /* Y_MADDU_OP  */
  YYSYMBOL_Y_MFC0_OP = 166,                /* Y_MFC0_OP  */
  YYSYMBOL_Y_MFC1_OP = 167,                /* Y_MFC1_OP  */
  YYSYMBOL_Y_MFC2_OP = 168,                /* Y_MFC2_OP  */
  YYSYMBOL_Y_MFHC1_OP = 169,               /* Y_MFHC1_OP  */
  YYSYMBOL_Y_MFHC2_OP = 170,               /* Y_MFHC2_OP  */
  YYSYMBOL_Y_MFHI_OP = 171,                /* Y_MFHI_OP  */
  YYSYMBOL_Y_MFLO_OP = 172,                /* Y_MFLO_OP  */
  YYSYMBOL_Y_MOV_D_OP = 173,               /* Y_MOV_D_OP  */
  YYSYMBOL_Y_MOV_PS_OP = 174,              /* Y_MOV_PS_OP  */
  YYSYMBOL_Y_MOV_S_OP = 175,               /* Y_MOV_S_OP  */
  YYSYMBOL_Y_MOVF_D_OP = 176,              /* Y_MOVF_D_OP  */
  YYSYMBOL_Y_MOVF_OP = 177,                /* Y_MOVF_OP  */
  YYSYMBOL_Y_MOVF_PS_OP = 178,             /* Y_MOVF_PS_OP  */
  YYSYMBOL_Y_MOVF_S_OP = 179,              /* Y_MOVF_S_OP  */
  YYSYMBOL_Y_MOVN_D_OP = 180,              /* Y_MOVN_D_OP  */
  YYSYMBOL_Y_MOVN_OP = 181,                /* Y_MOVN_OP  */
  YYSYMBOL_Y_MOVN_PS_OP = 182,             /* Y_MOVN_PS_OP  */
  YYSYMBOL_Y_MOVN_S_OP = 183,              /* Y_MOVN_S_OP  */
  YYSYMBOL_Y_MOVT_D_OP = 184,              /* Y_MOVT_D_OP  */
  YYSYMBOL_Y_MOVT_OP = 185,                /* Y_MOVT_OP  */
  YYSYMBOL_Y_MOVT_PS_OP = 186,             /* Y_MOVT_PS_OP  */
  YYSYMBOL_Y_MOVT_S_OP = 187,              /* Y_MOVT_S_OP  */
  YYSYMBOL_Y_MOVZ_D_OP = 188,              /* Y_MOVZ_D_OP  */
  YYSYMBOL_Y_MOVZ_OP = 189,                /* Y_MOVZ_OP  */
  YYSYMBOL_Y_MOVZ_PS_OP = 190,             /* Y_MOVZ_PS_OP  */
  YYSYMBOL_Y_MOVZ_S_OP = 191,              /* Y_MOVZ_S_OP  */
  YYSYMBOL_Y_MSUB_D_OP = 192,              /* Y_MSUB_D_OP  */
  YYSYMBOL_Y_MSUB_OP = 193,                /* Y_MSUB_OP  */
  YYSYMBOL_Y_MSUB_PS_OP = 194,             /* Y_MSUB_PS_OP  */
  YYSYMBOL_Y_MSUB_S_OP = 195,              /* Y_MSUB_S_OP  */
  YYSYMBOL_Y_MSUBU_OP = 196,               /* Y_MSUBU_OP  */
  YYSYMBOL_Y_MTC0_OP = 197,                /* Y_MTC0_OP  */
  YYSYMBOL_Y_MTC1_OP = 198,                /* Y_MTC1_OP  */
  YYSYMBOL_Y_MTC2_OP = 199,                /* Y_MTC2_OP  */
  YYSYMBOL_Y_MTHC1_OP = 200,               /* Y_MTHC1_OP  */
  YYSYMBOL_Y_MTHC2_OP = 201,               /* Y_MTHC2_OP  */
  YYSYMBOL_Y_MTHI_OP = 202,                /* Y_MTHI_OP  */
  YYSYMBOL_Y_MTLO_OP = 203,                /* Y_MTLO_OP  */
  YYSYMBOL_Y_MUL_D_OP = 204,               /* Y_MUL_D_OP  */
  YYSYMBOL_Y_MUL_PS_OP = 205,              /* Y_MUL_PS_OP  */
  YYSYMBOL_Y_MUL_S_OP = 206,               /* Y_MUL_S_OP  */
  YYSYMBOL_Y_MUL_OP = 207,                 /* Y_MUL_OP  */
  YYSYMBOL_Y_MULT_OP = 208,                /* Y_MULT_OP  */
  YYSYMBOL_Y_MULTU_OP = 209,               /* Y_MULTU_OP  */
  YYSYMBOL_Y_NEG_D_OP = 210,               /* Y_NEG_D_OP  */
  YYSYMBOL_Y_NEG_PS_OP = 211,              /* Y_NEG_PS_OP  */
  YYSYMBOL_Y_NEG_S_OP = 212,               /* Y_NEG_S_OP  */
  YYSYMBOL_Y_NMADD_D_OP = 213,             /* Y_NMADD_D_OP  */
  YYSYMBOL_Y_NMADD_PS_OP = 214,            /* Y_NMADD_PS_OP  */
  YYSYMBOL_Y_NMADD_S_OP = 215,             /* Y_NMADD_S_OP  */
  YYSYMBOL_Y_NMSUB_D_OP = 216,             /* Y_NMSUB_D_OP  */
  YYSYMBOL_Y_NMSUB_PS_OP = 217,            /* Y_NMSUB_PS_OP  */
  YYSYMBOL_Y_NMSUB_S_OP = 218,             /* Y_NMSUB_S_OP  */
  YYSYMBOL_Y_NOR_OP = 219,                 /* Y_NOR_OP  */
  YYSYMBOL_Y_OR_OP = 220,                  /* Y_OR_OP  */
  YYSYMBOL_Y_ORI_OP = 221,                 /* Y_ORI_OP  */
  YYSYMBOL_Y_PFW_OP = 222,                 /* Y_PFW_OP  */
  YYSYMBOL_Y_PLL_PS_OP = 223,              /* Y_PLL_PS_OP  */
  YYSYMBOL_Y_PLU_PS_OP = 224,              /* Y_PLU_PS_OP  */
  YYSYMBOL_Y_PREF_OP = 225,                /* Y_PREF_OP  */
  YYSYMBOL_Y_PREFX_OP = 226,               /* Y_PREFX_OP  */
  YYSYMBOL_Y_PUL_PS_OP = 227,              /* Y_PUL_PS_OP  */
  YYSYMBOL_Y_PUU_PS_OP = 228,              /* Y_PUU_PS_OP  */
  YYSYMBOL_Y_RDHWR_OP = 229,               /* Y_RDHWR_OP  */
  YYSYMBOL_Y_RDPGPR_OP = 230,              /* Y_RDPGPR_OP  */
  YYSYMBOL_Y_RECIP_D_OP = 231,             /* Y_RECIP_D_OP  */
  YYSYMBOL_Y_RECIP_S_OP = 232,             /* Y_RECIP_S_OP  */
  YYSYMBOL_Y_RFE_OP = 233,                 /* Y_RFE_OP  */
  YYSYMBOL_Y_ROTR_OP = 234,                /* Y_ROTR_OP  */
  YYSYMBOL_Y_ROTRV_OP = 235,               /* Y_ROTRV_OP  */
  YYSYMBOL_Y_ROUND_L_D_OP = 236,           /* Y_ROUND_L_D_OP  */
  YYSYMBOL_Y_ROUND_L_S_OP = 237,           /* Y_ROUND_L_S_OP  */
  YYSYMBOL_Y_ROUND_W_D_OP = 238,           /* Y_ROUND_W_D_OP  */
  YYSYMBOL_Y_ROUND_W_S_OP = 239,           /* Y_ROUND_W_S_OP  */
  YYSYMBOL_Y_RSQRT_D_OP = 240,             /* Y_RSQRT_D_OP  */
  YYSYMBOL_Y_RSQRT_S_OP = 241,             /* Y_RSQRT_S_OP  */
  YYSYMBOL_Y_SB_OP = 242,                  /* Y_SB_OP  */
  YYSYMBOL_Y_SC_OP = 243,                  /* Y_SC_OP  */
  YYSYMBOL_Y_SDBBP_OP = 244,               /* Y_SDBBP_OP  */
  YYSYMBOL_Y_SDC1_OP = 245,                /* Y_SDC1_OP  */
  YYSYMBOL_Y_SDC2_OP = 246,                /* Y_SDC2_OP  */
  YYSYMBOL_Y_SDXC1_OP = 247,               /* Y_SDXC1_OP  */
  YYSYMBOL_Y_SEB_OP = 248,                 /* Y_SEB_OP  */
  YYSYMBOL_Y_SEH_OP = 249,                 /* Y_SEH_OP  */
  YYSYMBOL_Y_SH_OP = 250,                  /* Y_SH_OP  */
  YYSYMBOL_Y_SLL_OP = 251,                 /* Y_SLL_OP  */
  YYSYMBOL_Y_SLLV_OP = 252,                /* Y_SLLV_OP  */
  YYSYMBOL_Y_SLT_OP = 253,                 /* Y_SLT_OP  */
  YYSYMBOL_Y_SLTI_OP = 254,                /* Y_SLTI_OP  */
  YYSYMBOL_Y_SLTIU_OP = 255,               /* Y_SLTIU_OP  */
  YYSYMBOL_Y_SLTU_OP = 256,                /* Y_SLTU_OP  */
  YYSYMBOL_Y_SQRT_D_OP = 257,              /* Y_SQRT_D_OP  */
  YYSYMBOL_Y_SQRT_S_OP = 258,              /* Y_SQRT_S_OP  */
  YYSYMBOL_Y_SRA_OP = 259,                 /* Y_SRA_OP  */
  YYSYMBOL_Y_SRAV_OP = 260,                /* Y_SRAV_OP  */
  YYSYMBOL_Y_SRL_OP = 261,                 /* Y_SRL_OP  */
  YYSYMBOL_Y_SRLV_OP = 262,                /* Y_SRLV_OP  */
  YYSYMBOL_Y_SSNOP_OP = 263,               /* Y_SSNOP_OP  */
  YYSYMBOL_Y_SUB_D_OP = 264,               /* Y_SUB_D_OP  */
  YYSYMBOL_Y_SUB_OP = 265,                 /* Y_SUB_OP  */
  YYSYMBOL_Y_SUB_PS_OP = 266,              /* Y_SUB_PS_OP  */
  YYSYMBOL_Y_SUB_S_OP = 267,               /* Y_SUB_S_OP  */
  YYSYMBOL_Y_SUBU_OP = 268,                /* Y_SUBU_OP  */
  YYSYMBOL_Y_SUXC1_OP = 269,               /* Y_SUXC1_OP  */
  YYSYMBOL_Y_SW_OP = 270,                  /* Y_SW_OP  */
  YYSYMBOL_Y_SWC1_OP = 271,                /* Y_SWC1_OP  */
  YYSYMBOL_Y_SWC2_OP = 272,                /* Y_SWC2_OP  */
  YYSYMBOL_Y_SWL_OP = 273,                 /* Y_SWL_OP  */
  YYSYMBOL_Y_SWR_OP = 274,                 /* Y_SWR_OP  */
  YYSYMBOL_Y_SWXC1_OP = 275,               /* Y_SWXC1_OP  */
  YYSYMBOL_Y_SYNC_OP = 276,                /* Y_SYNC_OP  */
  YYSYMBOL_Y_SYNCI_OP = 277,               /* Y_SYNCI_OP  */
  YYSYMBOL_Y_SYSCALL_OP = 278,             /* Y_SYSCALL_OP  */
  YYSYMBOL_Y_TEQ_OP = 279,                 /* Y_TEQ_OP  */
  YYSYMBOL_Y_TEQI_OP = 280,                /* Y_TEQI_OP  */
  YYSYMBOL_Y_TGE_OP = 281,                 /* Y_TGE_OP  */
  YYSYMBOL_Y_TGEI_OP = 282,                /* Y_TGEI_OP  */
  YYSYMBOL_Y_TGEIU_OP = 283,               /* Y_TGEIU_OP  */
  YYSYMBOL_Y_TGEU_OP = 284,                /* Y_TGEU_OP  */
  YYSYMBOL_Y_TLBP_OP = 285,                /* Y_TLBP_OP  */
  YYSYMBOL_Y_TLBR_OP = 286,                /* Y_TLBR_OP  */
  YYSYMBOL_Y_TLBWI_OP = 287,               /* Y_TLBWI_OP  */
  YYSYMBOL_Y_TLBWR_OP = 288,               /* Y_TLBWR_OP  */
  YYSYMBOL_Y_TLT_OP = 289,                 /* Y_TLT_OP  */
  YYSYMBOL_Y_TLTI_OP = 290,                /* Y_TLTI_OP  */
  YYSYMBOL_Y_TLTIU_OP = 291,               /* Y_TLTIU_OP  */
  YYSYMBOL_Y_TLTU_OP = 292,                /* Y_TLTU_OP  */
  YYSYMBOL_Y_TNE_OP = 293,                 /* Y_TNE_OP  */
  YYSYMBOL_Y_TNEI_OP = 294,                /* Y_TNEI_OP  */
  YYSYMBOL_Y_TRUNC_L_D_OP = 295,           /* Y_TRUNC_L_D_OP  */
  YYSYMBOL_Y_TRUNC_L_S_OP = 296,           /* Y_TRUNC_L_S_OP  */
  YYSYMBOL_Y_TRUNC_W_D_OP = 297,           /* Y_TRUNC_W_D_OP  */
  YYSYMBOL_Y_TRUNC_W_S_OP = 298,           /* Y_TRUNC_W_S_OP  */
  YYSYMBOL_Y_WRPGPR_OP = 299,              /* Y_WRPGPR_OP  */
  YYSYMBOL_Y_WSBH_OP = 300,                /* Y_WSBH_OP  */
  YYSYMBOL_Y_XOR_OP = 301,                 /* Y_XOR_OP  */
  YYSYMBOL_Y_XORI_OP = 302,                /* Y_XORI_OP  */
  YYSYMBOL_Y_ABS_POP = 303,                /* Y_ABS_POP  */
  YYSYMBOL_Y_B_POP = 304,                  /* Y_B_POP  */
  YYSYMBOL_Y_BAL_POP = 305,                /* Y_BAL_POP  */
  YYSYMBOL_Y_BEQZ_POP = 306,               /* Y_BEQZ_POP  */
  YYSYMBOL_Y_BGE_POP = 307,                /* Y_BGE_POP  */
  YYSYMBOL_Y_BGEU_POP = 308,               /* Y_BGEU_POP  */
  YYSYMBOL_Y_BGT_POP = 309,                /* Y_BGT_POP  */
  YYSYMBOL_Y_BGTU_POP = 310,               /* Y_BGTU_POP  */
  YYSYMBOL_Y_BLE_POP = 311,                /* Y_BLE_POP  */
  YYSYMBOL_Y_BLEU_POP = 312,               /* Y_BLEU_POP  */
  YYSYMBOL_Y_BLT_POP = 313,                /* Y_BLT_POP  */
  YYSYMBOL_Y_BLTU_POP = 314,               /* Y_BLTU_POP  */
  YYSYMBOL_Y_BNEZ_POP = 315,               /* Y_BNEZ_POP  */
  YYSYMBOL_Y_LA_POP = 316,                 /* Y_LA_POP  */
  YYSYMBOL_Y_LD_POP = 317,                 /* Y_LD_POP  */
  YYSYMBOL_Y_L_D_POP = 318,                /* Y_L_D_POP  */
  YYSYMBOL_Y_L_S_POP = 319,                /* Y_L_S_POP  */
  YYSYMBOL_Y_LI_D_POP = 320,               /* Y_LI_D_POP  */
  YYSYMBOL_Y_LI_POP = 321,                 /* Y_LI_POP  */
  YYSYMBOL_Y_LI_S_POP = 322,               /* Y_LI_S_POP  */
  YYSYMBOL_Y_MFC1_D_POP = 323,             /* Y_MFC1_D_POP  */
  YYSYMBOL_Y_MOVE_POP = 324,               /* Y_MOVE_POP  */
  YYSYMBOL_Y_MTC1_D_POP = 325,             /* Y_MTC1_D_POP  */
  YYSYMBOL_Y_MULO_POP = 326,               /* Y_MULO_POP  */
  YYSYMBOL_Y_MULOU_POP = 327,              /* Y_MULOU_POP  */
  YYSYMBOL_Y_NEG_POP = 328,                /* Y_NEG_POP  */
  YYSYMBOL_Y_NEGU_POP = 329,               /* Y_NEGU_POP  */
  YYSYMBOL_Y_NOP_POP = 330,                /* Y_NOP_POP  */
  YYSYMBOL_Y_NOT_POP = 331,                /* Y_NOT_POP  */
  YYSYMBOL_Y_REM_POP = 332,                /* Y_REM_POP  */
  YYSYMBOL_Y_REMU_POP = 333,               /* Y_REMU_POP  */
  YYSYMBOL_Y_ROL_POP = 334,                /* Y_ROL_POP  */
  YYSYMBOL_Y_ROR_POP = 335,                /* Y_ROR_POP  */
  YYSYMBOL_Y_S_D_POP = 336,                /* Y_S_D_POP  */
  YYSYMBOL_Y_S_S_POP = 337,                /* Y_S_S_POP  */
  YYSYMBOL_Y_SD_POP = 338,                 /* Y_SD_POP  */
  YYSYMBOL_Y_SEQ_POP = 339,                /* Y_SEQ_POP  */
  YYSYMBOL_Y_SGE_POP = 340,                /* Y_SGE_POP  */
  YYSYMBOL_Y_SGEU_POP = 341,               /* Y_SGEU_POP  */
  YYSYMBOL_Y_SGT_POP = 342,                /* Y_SGT_POP  */
  YYSYMBOL_Y_SGTU_POP = 343,               /* Y_SGTU_POP  */
  YYSYMBOL_Y_SLE_POP = 344,                /* Y_SLE_POP  */
  YYSYMBOL_Y_SLEU_POP = 345,               /* Y_SLEU_POP  */
  YYSYMBOL_Y_SNE_POP = 346,                /* Y_SNE_POP  */
  YYSYMBOL_Y_ULH_POP = 347,                /* Y_ULH_POP  */
  YYSYMBOL_Y_ULHU_POP = 348,               /* Y_ULHU_POP  */
  YYSYMBOL_Y_ULW_POP = 349,                /* Y_ULW_POP  */
  YYSYMBOL_Y_USH_POP = 350,                /* Y_USH_POP  */
  YYSYMBOL_Y_USW_POP = 351,                /* Y_USW_POP  */
  YYSYMBOL_Y_ALIAS_DIR = 352,              /* Y_ALIAS_DIR  */
  YYSYMBOL_Y_ALIGN_DIR = 353,              /* Y_ALIGN_DIR  */
  YYSYMBOL_Y_ASCII_DIR = 354,              /* Y_ASCII_DIR  */
  YYSYMBOL_Y_ASCIIZ_DIR = 355,             /* Y_ASCIIZ_DIR  */
  YYSYMBOL_Y_ASM0_DIR = 356,               /* Y_ASM0_DIR  */
  YYSYMBOL_Y_BGNB_DIR = 357,               /* Y_BGNB_DIR  */
  YYSYMBOL_Y_BYTE_DIR = 358,               /* Y_BYTE_DIR  */
  YYSYMBOL_Y_COMM_DIR = 359,               /* Y_COMM_DIR  */
  YYSYMBOL_Y_DATA_DIR = 360,               /* Y_DATA_DIR  */
  YYSYMBOL_Y_DOUBLE_DIR = 361,             /* Y_DOUBLE_DIR  */
  YYSYMBOL_Y_END_DIR = 362,                /* Y_END_DIR  */
  YYSYMBOL_Y_ENDB_DIR = 363,               /* Y_ENDB_DIR  */
  YYSYMBOL_Y_ENDR_DIR = 364,               /* Y_ENDR_DIR  */
  YYSYMBOL_Y_ENT_DIR = 365,                /* Y_ENT_DIR  */
  YYSYMBOL_Y_ERR_DIR = 366,                /* Y_ERR_DIR  */
  YYSYMBOL_Y_EXTERN_DIR = 367,             /* Y_EXTERN_DIR  */
  YYSYMBOL_Y_FILE_DIR = 368,               /* Y_FILE_DIR  */
  YYSYMBOL_Y_FLOAT_DIR = 369,              /* Y_FLOAT_DIR  */
  YYSYMBOL_Y_FMASK_DIR = 370,              /* Y_FMASK_DIR  */
  YYSYMBOL_Y_FRAME_DIR = 371,              /* Y_FRAME_DIR  */
  YYSYMBOL_Y_GLOBAL_DIR = 372,             /* Y_GLOBAL_DIR  */
  YYSYMBOL_Y_HALF_DIR = 373,               /* Y_HALF_DIR  */
  YYSYMBOL_Y_K_DATA_DIR = 374,             /* Y_K_DATA_DIR  */
  YYSYMBOL_Y_K_TEXT_DIR = 375,             /* Y_K_TEXT_DIR  */
  YYSYMBOL_Y_LABEL_DIR = 376,              /* Y_LABEL_DIR  */
  YYSYMBOL_Y_LCOMM_DIR = 377,              /* Y_LCOMM_DIR  */
  YYSYMBOL_Y_LIVEREG_DIR = 378,            /* Y_LIVEREG_DIR  */
  YYSYMBOL_Y_LOC_DIR = 379,                /* Y_LOC_DIR  */
  YYSYMBOL_Y_MASK_DIR = 380,               /* Y_MASK_DIR  */
  YYSYMBOL_Y_NOALIAS_DIR = 381,            /* Y_NOALIAS_DIR  */
  YYSYMBOL_Y_OPTIONS_DIR = 382,            /* Y_OPTIONS_DIR  */
  YYSYMBOL_Y_RDATA_DIR = 383,              /* Y_RDATA_DIR  */
  YYSYMBOL_Y_REPEAT_DIR = 384,             /* Y_REPEAT_DIR  */
  YYSYMBOL_Y_SDATA_DIR = 385,              /* Y_SDATA_DIR  */
  YYSYMBOL_Y_SET_DIR = 386,                /* Y_SET_DIR  */
  YYSYMBOL_Y_SPACE_DIR = 387,              /* Y_SPACE_DIR  */
  YYSYMBOL_Y_STRUCT_DIR = 388,             /* Y_STRUCT_DIR  */
  YYSYMBOL_Y_TEXT_DIR = 389,               /* Y_TEXT_DIR  */
  YYSYMBOL_Y_VERSTAMP_DIR = 390,           /* Y_VERSTAMP_DIR  */
  YYSYMBOL_Y_VREG_DIR = 391,               /* Y_VREG_DIR  */
  YYSYMBOL_Y_WORD_DIR = 392,               /* Y_WORD_DIR  */
  YYSYMBOL_393_ = 393,                     /* ':'  */
  YYSYMBOL_394_ = 394,                     /* '='  */
  YYSYMBOL_395_ = 395,                     /* '('  */
  YYSYMBOL_396_ = 396,                     /* ')'  */
  YYSYMBOL_397_ = 397,                     /* '+'  */
  YYSYMBOL_398_ = 398,                     /* '-'  */
  YYSYMBOL_399_ = 399,                     /* '>'  */
  YYSYMBOL_400_ = 400,                     /* '*'  */
  YYSYMBOL_401_ = 401,                     /* '/'  */
  YYSYMBOL_YYACCEPT = 402,                 /* $accept  */
  YYSYMBOL_LINE = 403,                     /* LINE  */
  YYSYMBOL_404_1 = 404,                    /* $@1  */
  YYSYMBOL_LBL_CMD = 405,                  /* LBL_CMD  */
  YYSYMBOL_OPT_LBL = 406,                  /* OPT_LBL  */
  YYSYMBOL_CMD = 407,                      /* CMD  */
  YYSYMBOL_408_2 = 408,                    /* $@2  */
  YYSYMBOL_409_3 = 409,                    /* $@3  */
  YYSYMBOL_TERM = 410,                     /* TERM  */
  YYSYMBOL_ASM_CODE = 411,                 /* ASM_CODE  */
  YYSYMBOL_LOAD_OPS = 412,                 /* LOAD_OPS  */
  YYSYMBOL_LOADI_OPS = 413,                /* LOADI_OPS  */
  YYSYMBOL_ULOADH_POPS = 414,              /* ULOADH_POPS  */
  YYSYMBOL_LOADC_OPS = 415,                /* LOADC_OPS  */
  YYSYMBOL_LOADFP_OPS = 416,               /* LOADFP_OPS  */
  YYSYMBOL_LOADFP_INDEX_OPS = 417,         /* LOADFP_INDEX_OPS  */
  YYSYMBOL_STORE_OPS = 418,                /* STORE_OPS  */
  YYSYMBOL_STOREC_OPS = 419,               /* STOREC_OPS  */
  YYSYMBOL_STOREFP_OPS = 420,              /* STOREFP_OPS  */
  YYSYMBOL_STOREFP_INDEX_OPS = 421,        /* STOREFP_INDEX_OPS  */
  YYSYMBOL_SYS_OPS = 422,                  /* SYS_OPS  */
  YYSYMBOL_PREFETCH_OPS = 423,             /* PREFETCH_OPS  */
  YYSYMBOL_CACHE_OPS = 424,                /* CACHE_OPS  */
  YYSYMBOL_TLB_OPS = 425,                  /* TLB_OPS  */
  YYSYMBOL_NULLARY_OPS = 426,              /* NULLARY_OPS  */
  YYSYMBOL_NULLARY_OPS_REV2 = 427,         /* NULLARY_OPS_REV2  */
  YYSYMBOL_COUNT_LEADING_OPS = 428,        /* COUNT_LEADING_OPS  */
  YYSYMBOL_UNARY_OPS_REV2 = 429,           /* UNARY_OPS_REV2  */
  YYSYMBOL_BINARYI_OPS = 430,              /* BINARYI_OPS  */
  YYSYMBOL_BINARYIR_OPS = 431,             /* BINARYIR_OPS  */
  YYSYMBOL_BINARY_ARITHI_OPS = 432,        /* BINARY_ARITHI_OPS  */
  YYSYMBOL_BINARY_LOGICALI_OPS = 433,      /* BINARY_LOGICALI_OPS  */
  YYSYMBOL_SHIFT_OPS = 434,                /* SHIFT_OPS  */
  YYSYMBOL_SHIFT_OPS_REV2 = 435,           /* SHIFT_OPS_REV2  */
  YYSYMBOL_SHIFTV_OPS_REV2 = 436,          /* SHIFTV_OPS_REV2  */
  YYSYMBOL_BINARY_OPS = 437,               /* BINARY_OPS  */
  YYSYMBOL_BINARY_OPS_REV2 = 438,          /* BINARY_OPS_REV2  */
  YYSYMBOL_SUB_OPS = 439,                  /* SUB_OPS  */
  YYSYMBOL_DIV_POPS = 440,                 /* DIV_POPS  */
  YYSYMBOL_MUL_POPS = 441,                 /* MUL_POPS  */
  YYSYMBOL_SET_LE_POPS = 442,              /* SET_LE_POPS  */
  YYSYMBOL_SET_GT_POPS = 443,              /* SET_GT_POPS  */
  YYSYMBOL_SET_GE_POPS = 444,              /* SET_GE_POPS  */
  YYSYMBOL_SET_EQ_POPS = 445,              /* SET_EQ_POPS  */
  YYSYMBOL_MULT_OPS = 446,                 /* MULT_OPS  */
  YYSYMBOL_MULT_OPS3 = 447,                /* MULT_OPS3  */
  YYSYMBOL_BF_OPS_REV2 = 448,              /* BF_OPS_REV2  */
  YYSYMBOL_BR_COP_OPS = 449,               /* BR_COP_OPS  */
  YYSYMBOL_UNARY_BR_OPS = 450,             /* UNARY_BR_OPS  */
  YYSYMBOL_UNARY_BR_POPS = 451,            /* UNARY_BR_POPS  */
  YYSYMBOL_BINARY_BR_OPS = 452,            /* BINARY_BR_OPS  */
  YYSYMBOL_BR_GT_POPS = 453,               /* BR_GT_POPS  */
  YYSYMBOL_BR_GE_POPS = 454,               /* BR_GE_POPS  */
  YYSYMBOL_BR_LT_POPS = 455,               /* BR_LT_POPS  */
  YYSYMBOL_BR_LE_POPS = 456,               /* BR_LE_POPS  */
  YYSYMBOL_J_OPS = 457,                    /* J_OPS  */
  YYSYMBOL_B_OPS = 458,                    /* B_OPS  */
  YYSYMBOL_BINARYI_TRAP_OPS = 459,         /* BINARYI_TRAP_OPS  */
  YYSYMBOL_BINARY_TRAP_OPS = 460,          /* BINARY_TRAP_OPS  */
  YYSYMBOL_MOVE_FROM_HILO_OPS = 461,       /* MOVE_FROM_HILO_OPS  */
  YYSYMBOL_MOVE_TO_HILO_OPS = 462,         /* MOVE_TO_HILO_OPS  */
  YYSYMBOL_MOVEC_OPS = 463,                /* MOVEC_OPS  */
  YYSYMBOL_MOVE_COP_OPS = 464,             /* MOVE_COP_OPS  */
  YYSYMBOL_MOVE_COP_OPS_REV2 = 465,        /* MOVE_COP_OPS_REV2  */
  YYSYMBOL_CTL_COP_OPS = 466,              /* CTL_COP_OPS  */
  YYSYMBOL_FP_MOVE_OPS = 467,              /* FP_MOVE_OPS  */
  YYSYMBOL_FP_MOVE_OPS_REV2 = 468,         /* FP_MOVE_OPS_REV2  */
  YYSYMBOL_MOVECC_OPS = 469,               /* MOVECC_OPS  */
  YYSYMBOL_FP_MOVEC_OPS = 470,             /* FP_MOVEC_OPS  */
  YYSYMBOL_FP_MOVEC_OPS_REV2 = 471,        /* FP_MOVEC_OPS_REV2  */
  YYSYMBOL_FP_MOVECC_OPS = 472,            /* FP_MOVECC_OPS  */
  YYSYMBOL_FP_MOVECC_OPS_REV2 = 473,       /* FP_MOVECC_OPS_REV2  */
  YYSYMBOL_FP_UNARY_OPS = 474,             /* FP_UNARY_OPS  */
  YYSYMBOL_FP_UNARY_OPS_REV2 = 475,        /* FP_UNARY_OPS_REV2  */
  YYSYMBOL_FP_BINARY_OPS = 476,            /* FP_BINARY_OPS  */
  YYSYMBOL_FP_BINARY_OPS_REV2 = 477,       /* FP_BINARY_OPS_REV2  */
  YYSYMBOL_FP_TERNARY_OPS_REV2 = 478,      /* FP_TERNARY_OPS_REV2  */
  YYSYMBOL_FP_CMP_OPS = 479,               /* FP_CMP_OPS  */
  YYSYMBOL_FP_CMP_OPS_REV2 = 480,          /* FP_CMP_OPS_REV2  */
  YYSYMBOL_ASM_DIRECTIVE = 481,            /* ASM_DIRECTIVE  */
  YYSYMBOL_482_4 = 482,                    /* $@4  */
  YYSYMBOL_483_5 = 483,                    /* $@5  */
  YYSYMBOL_484_6 = 484,                    /* $@6  */
  YYSYMBOL_485_7 = 485,                    /* $@7  */
  YYSYMBOL_486_8 = 486,                    /* $@8  */
  YYSYMBOL_487_9 = 487,                    /* $@9  */
  YYSYMBOL_488_10 = 488,                   /* $@10  */
  YYSYMBOL_ADDRESS = 489,                  /* ADDRESS  */
  YYSYMBOL_490_11 = 490,                   /* $@11  */
  YYSYMBOL_ADDR = 491,                     /* ADDR  */
  YYSYMBOL_BR_IMM32 = 492,                 /* BR_IMM32  */
  YYSYMBOL_493_12 = 493,                   /* $@12  */
  YYSYMBOL_IMM16 = 494,                    /* IMM16  */
  YYSYMBOL_UIMM16 = 495,                   /* UIMM16  */
  YYSYMBOL_IMM32 = 496,                    /* IMM32  */
  YYSYMBOL_ABS_ADDR = 497,                 /* ABS_ADDR  */
  YYSYMBOL_SRC1 = 498,                     /* SRC1  */
  YYSYMBOL_SRC2 = 499,                     /* SRC2  */
  YYSYMBOL_DEST = 500,                     /* DEST  */
  YYSYMBOL_REG = 501,                      /* REG  */
  YYSYMBOL_REGISTER = 502,                 /* REGISTER  */
  YYSYMBOL_F_DEST = 503,                   /* F_DEST  */
  YYSYMBOL_F_SRC1 = 504,                   /* F_SRC1  */
  YYSYMBOL_F_SRC2 = 505,                   /* F_SRC2  */
  YYSYMBOL_FP_REGISTER = 506,              /* FP_REGISTER  */
  YYSYMBOL_CC_REG = 507,                   /* CC_REG  */
  YYSYMBOL_COP_REG = 508,                  /* COP_REG  */
  YYSYMBOL_LABEL = 509,                    /* LABEL  */
  YYSYMBOL_STR_LST = 510,                  /* STR_LST  */
  YYSYMBOL_STR = 511,                      /* STR  */
  YYSYMBOL_EXPRESSION = 512,               /* EXPRESSION  */
  YYSYMBOL_513_13 = 513,                   /* $@13  */
  YYSYMBOL_EXPR = 514,                     /* EXPR  */
  YYSYMBOL_TRM = 515,                      /* TRM  */
  YYSYMBOL_FACTOR = 516,                   /* FACTOR  */
  YYSYMBOL_EXPR_LST = 517,                 /* EXPR_LST  */
  YYSYMBOL_FP_EXPR_LST = 518,              /* FP_EXPR_LST  */
  YYSYMBOL_OPTIONAL_ID = 519,              /* OPTIONAL_ID  */
  YYSYMBOL_520_14 = 520,                   /* $@14  */
  YYSYMBOL_OPT_ID = 521,                   /* OPT_ID  */
  YYSYMBOL_ID = 522,                       /* ID  */
  YYSYMBOL_523_15 = 523                    /* $@15  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1417

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  402
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  122
/* YYNRULES -- Number of rules.  */
#define YYNRULES  573
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  852

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   647


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int16 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     395,   396,   400,   397,     2,   398,     2,   401,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   393,     2,
       2,   394,   399,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   516,   516,   516,   518,   519,   523,   532,   544,   543,
     550,   549,   555,   559,   564,   573,   589,   599,   609,   615,
     629,   635,   646,   656,   680,   708,   714,   730,   741,   765,
     790,   801,   807,   813,   819,   825,   831,   836,   842,   850,
     856,   862,   872,   878,   884,   890,   896,   902,   908,   915,
     921,   926,   932,   939,   944,   949,   955,   960,   966,   971,
     977,   984,   990,   995,  1001,  1006,  1023,  1042,  1048,  1053,
    1069,  1086,  1095,  1101,  1114,  1119,  1133,  1139,  1144,  1153,
    1162,  1171,  1183,  1195,  1201,  1206,  1217,  1222,  1234,  1239,
    1250,  1255,  1266,  1277,  1289,  1295,  1302,  1307,  1331,  1338,
    1361,  1368,  1377,  1384,  1393,  1400,  1423,  1432,  1440,  1449,
    1456,  1462,  1468,  1474,  1480,  1486,  1495,  1501,  1507,  1513,
    1519,  1525,  1531,  1538,  1555,  1561,  1567,  1573,  1579,  1585,
    1591,  1597,  1603,  1609,  1615,  1623,  1624,  1625,  1626,  1627,
    1628,  1629,  1630,  1631,  1632,  1635,  1638,  1639,  1642,  1643,
    1646,  1647,  1648,  1649,  1652,  1653,  1654,  1657,  1658,  1659,
    1660,  1661,  1662,  1663,  1666,  1667,  1668,  1669,  1672,  1673,
    1676,  1677,  1678,  1681,  1687,  1690,  1691,  1694,  1695,  1698,
    1699,  1700,  1701,  1704,  1712,  1713,  1714,  1717,  1718,  1721,
    1722,  1727,  1728,  1729,  1730,  1731,  1732,  1733,  1736,  1737,
    1738,  1741,  1742,  1743,  1744,  1747,  1748,  1749,  1752,  1753,
    1754,  1757,  1760,  1766,  1769,  1770,  1771,  1772,  1773,  1774,
    1777,  1778,  1781,  1782,  1783,  1784,  1787,  1788,  1791,  1792,
    1795,  1796,  1799,  1800,  1803,  1804,  1807,  1808,  1809,  1810,
    1811,  1812,  1815,  1818,  1819,  1822,  1823,  1824,  1825,  1826,
    1827,  1828,  1829,  1832,  1833,  1834,  1835,  1836,  1837,  1838,
    1839,  1840,  1841,  1842,  1843,  1846,  1847,  1850,  1851,  1852,
    1853,  1856,  1857,  1859,  1860,  1862,  1863,  1865,  1866,  1869,
    1870,  1871,  1872,  1873,  1874,  1877,  1878,  1882,  1883,  1884,
    1885,  1886,  1887,  1890,  1891,  1892,  1893,  1894,  1895,  1899,
    1900,  1903,  1904,  1907,  1908,  1911,  1912,  1913,  1914,  1915,
    1916,  1917,  1918,  1921,  1922,  1923,  1924,  1927,  1928,  1929,
    1930,  1931,  1932,  1937,  1938,  1941,  1945,  1946,  1950,  1951,
    1952,  1953,  1956,  1957,  1961,  1962,  1963,  1964,  1967,  1968,
    1971,  1972,  1973,  1974,  1975,  1976,  1977,  1978,  1979,  1980,
    1981,  1982,  1983,  1984,  1985,  1986,  1987,  1988,  1989,  1990,
    1993,  1994,  1995,  1996,  1997,  1998,  1999,  2000,  2001,  2002,
    2003,  2004,  2005,  2006,  2007,  2008,  2009,  2010,  2011,  2012,
    2013,  2016,  2017,  2018,  2019,  2020,  2021,  2022,  2023,  2026,
    2027,  2028,  2029,  2030,  2031,  2034,  2035,  2036,  2037,  2038,
    2039,  2040,  2041,  2042,  2043,  2044,  2045,  2046,  2049,  2050,
    2051,  2052,  2053,  2054,  2055,  2056,  2057,  2058,  2059,  2060,
    2061,  2062,  2063,  2064,  2065,  2066,  2067,  2068,  2069,  2070,
    2071,  2072,  2073,  2074,  2075,  2076,  2077,  2078,  2079,  2080,
    2083,  2084,  2085,  2086,  2087,  2088,  2089,  2090,  2091,  2092,
    2093,  2094,  2095,  2096,  2097,  2098,  2103,  2105,  2110,  2110,
    2116,  2116,  2123,  2125,  2129,  2128,  2137,  2149,  2155,  2164,
    2171,  2181,  2180,  2192,  2194,  2196,  2198,  2200,  2203,  2209,
    2215,  2219,  2218,  2230,  2232,  2235,  2243,  2242,  2254,  2263,
    2270,  2273,  2275,  2277,  2279,  2281,  2287,  2294,  2303,  2310,
    2319,  2328,  2337,  2343,  2350,  2359,  2366,  2375,  2377,  2381,
    2380,  2391,  2391,  2393,  2398,  2403,  2408,  2414,  2420,  2426,
    2431,  2437,  2443,  2451,  2451,  2453,  2459,  2466,  2471,  2476,
    2481,  2487,  2495,  2497,  2500,  2510,  2512,  2514,  2516,  2518,
    2527,  2529,  2531,  2533,  2541,  2549,  2551,  2556,  2562,  2563,
    2567,  2572,  2583,  2583,  2586,  2588,  2591,  2596,  2598,  2601,
    2605,  2607,  2610,  2623,  2627,  2631,  2641,  2645,  2652,  2652,
    2654,  2655,  2659,  2659
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "Y_EOF", "Y_NL",
  "Y_INT", "Y_ID", "Y_REG", "Y_FP_REG", "Y_STR", "Y_FP", "Y_ABS_D_OP",
  "Y_ABS_PS_OP", "Y_ABS_S_OP", "Y_ADD_D_OP", "Y_ADD_OP", "Y_ADD_PS_OP",
  "Y_ADD_S_OP", "Y_ADDI_OP", "Y_ADDIU_OP", "Y_ADDU_OP", "Y_ALNV_PS_OP",
  "Y_AND_OP", "Y_ANDI_OP", "Y_BC1F_OP", "Y_BC1FL_OP", "Y_BC1T_OP",
  "Y_BC1TL_OP", "Y_BC2F_OP", "Y_BC2FL_OP", "Y_BC2T_OP", "Y_BC2TL_OP",
  "Y_BEQ_OP", "Y_BEQL_OP", "Y_BGEZ_OP", "Y_BGEZAL_OP", "Y_BGEZALL_OP",
  "Y_BGEZL_OP", "Y_BGTZ_OP", "Y_BGTZL_OP", "Y_BLEZ_OP", "Y_BLEZL_OP",
  "Y_BLTZ_OP", "Y_BLTZAL_OP", "Y_BLTZALL_OP", "Y_BLTZL_OP", "Y_BNE_OP",
  "Y_BNEL_OP", "Y_BREAK_OP", "Y_C_EQ_D_OP", "Y_C_EQ_PS_OP", "Y_C_EQ_S_OP",
  "Y_C_F_D_OP", "Y_C_F_PS_OP", "Y_C_F_S_OP", "Y_C_LE_D_OP", "Y_C_LE_PS_OP",
  "Y_C_LE_S_OP", "Y_C_LT_D_OP", "Y_C_LT_PS_OP", "Y_C_LT_S_OP",
  "Y_C_NGE_D_OP", "Y_C_NGE_PS_OP", "Y_C_NGE_S_OP", "Y_C_NGL_D_OP",
  "Y_C_NGL_PS_OP", "Y_C_NGL_S_OP", "Y_C_NGLE_D_OP", "Y_C_NGLE_PS_OP",
  "Y_C_NGLE_S_OP", "Y_C_NGT_D_OP", "Y_C_NGT_PS_OP", "Y_C_NGT_S_OP",
  "Y_C_OLE_D_OP", "Y_C_OLE_PS_OP", "Y_C_OLE_S_OP", "Y_C_OLT_D_OP",
  "Y_C_OLT_PS_OP", "Y_C_OLT_S_OP", "Y_C_SEQ_D_OP", "Y_C_SEQ_PS_OP",
  "Y_C_SEQ_S_OP", "Y_C_SF_D_OP", "Y_C_SF_PS_OP", "Y_C_SF_S_OP",
  "Y_C_UEQ_D_OP", "Y_C_UEQ_PS_OP", "Y_C_UEQ_S_OP", "Y_C_ULE_D_OP",
  "Y_C_ULE_PS_OP", "Y_C_ULE_S_OP", "Y_C_ULT_D_OP", "Y_C_ULT_PS_OP",
  "Y_C_ULT_S_OP", "Y_C_UN_D_OP", "Y_C_UN_PS_OP", "Y_C_UN_S_OP",
  "Y_CACHE_OP", "Y_CEIL_L_D_OP", "Y_CEIL_L_S_OP", "Y_CEIL_W_D_OP",
  "Y_CEIL_W_S_OP", "Y_CFC0_OP", "Y_CFC1_OP", "Y_CFC2_OP", "Y_CLO_OP",
  "Y_CLZ_OP", "Y_COP2_OP", "Y_CTC0_OP", "Y_CTC1_OP", "Y_CTC2_OP",
  "Y_CVT_D_L_OP", "Y_CVT_D_S_OP", "Y_CVT_D_W_OP", "Y_CVT_L_D_OP",
  "Y_CVT_L_S_OP", "Y_CVT_PS_S_OP", "Y_CVT_S_D_OP", "Y_CVT_S_L_OP",
  "Y_CVT_S_PL_OP", "Y_CVT_S_PU_OP", "Y_CVT_S_W_OP", "Y_CVT_W_D_OP",
  "Y_CVT_W_S_OP", "Y_DERET_OP", "Y_DI_OP", "Y_DIV_D_OP", "Y_DIV_OP",
  "Y_DIV_S_OP", "Y_DIVU_OP", "Y_EHB_OP", "Y_EI_OP", "Y_ERET_OP",
  "Y_EXT_OP", "Y_FLOOR_L_D_OP", "Y_FLOOR_L_S_OP", "Y_FLOOR_W_D_OP",
  "Y_FLOOR_W_S_OP", "Y_INS_OP", "Y_J_OP", "Y_JAL_OP", "Y_JALR_HB_OP",
  "Y_JALR_OP", "Y_JR_HB_OP", "Y_JR_OP", "Y_LB_OP", "Y_LBU_OP", "Y_LDC1_OP",
  "Y_LDC2_OP", "Y_LDXC1_OP", "Y_LH_OP", "Y_LHU_OP", "Y_LL_OP", "Y_LUI_OP",
  "Y_LUXC1_OP", "Y_LW_OP", "Y_LWC1_OP", "Y_LWC2_OP", "Y_LWL_OP",
  "Y_LWR_OP", "Y_LWXC1_OP", "Y_MADD_D_OP", "Y_MADD_OP", "Y_MADD_PS_OP",
  "Y_MADD_S_OP", "Y_MADDU_OP", "Y_MFC0_OP", "Y_MFC1_OP", "Y_MFC2_OP",
  "Y_MFHC1_OP", "Y_MFHC2_OP", "Y_MFHI_OP", "Y_MFLO_OP", "Y_MOV_D_OP",
  "Y_MOV_PS_OP", "Y_MOV_S_OP", "Y_MOVF_D_OP", "Y_MOVF_OP", "Y_MOVF_PS_OP",
  "Y_MOVF_S_OP", "Y_MOVN_D_OP", "Y_MOVN_OP", "Y_MOVN_PS_OP", "Y_MOVN_S_OP",
  "Y_MOVT_D_OP", "Y_MOVT_OP", "Y_MOVT_PS_OP", "Y_MOVT_S_OP", "Y_MOVZ_D_OP",
  "Y_MOVZ_OP", "Y_MOVZ_PS_OP", "Y_MOVZ_S_OP", "Y_MSUB_D_OP", "Y_MSUB_OP",
  "Y_MSUB_PS_OP", "Y_MSUB_S_OP", "Y_MSUBU_OP", "Y_MTC0_OP", "Y_MTC1_OP",
  "Y_MTC2_OP", "Y_MTHC1_OP", "Y_MTHC2_OP", "Y_MTHI_OP", "Y_MTLO_OP",
  "Y_MUL_D_OP", "Y_MUL_PS_OP", "Y_MUL_S_OP", "Y_MUL_OP", "Y_MULT_OP",
  "Y_MULTU_OP", "Y_NEG_D_OP", "Y_NEG_PS_OP", "Y_NEG_S_OP", "Y_NMADD_D_OP",
  "Y_NMADD_PS_OP", "Y_NMADD_S_OP", "Y_NMSUB_D_OP", "Y_NMSUB_PS_OP",
  "Y_NMSUB_S_OP", "Y_NOR_OP", "Y_OR_OP", "Y_ORI_OP", "Y_PFW_OP",
  "Y_PLL_PS_OP", "Y_PLU_PS_OP", "Y_PREF_OP", "Y_PREFX_OP", "Y_PUL_PS_OP",
  "Y_PUU_PS_OP", "Y_RDHWR_OP", "Y_RDPGPR_OP", "Y_RECIP_D_OP",
  "Y_RECIP_S_OP", "Y_RFE_OP", "Y_ROTR_OP", "Y_ROTRV_OP", "Y_ROUND_L_D_OP",
  "Y_ROUND_L_S_OP", "Y_ROUND_W_D_OP", "Y_ROUND_W_S_OP", "Y_RSQRT_D_OP",
  "Y_RSQRT_S_OP", "Y_SB_OP", "Y_SC_OP", "Y_SDBBP_OP", "Y_SDC1_OP",
  "Y_SDC2_OP", "Y_SDXC1_OP", "Y_SEB_OP", "Y_SEH_OP", "Y_SH_OP", "Y_SLL_OP",
  "Y_SLLV_OP", "Y_SLT_OP", "Y_SLTI_OP", "Y_SLTIU_OP", "Y_SLTU_OP",
  "Y_SQRT_D_OP", "Y_SQRT_S_OP", "Y_SRA_OP", "Y_SRAV_OP", "Y_SRL_OP",
  "Y_SRLV_OP", "Y_SSNOP_OP", "Y_SUB_D_OP", "Y_SUB_OP", "Y_SUB_PS_OP",
  "Y_SUB_S_OP", "Y_SUBU_OP", "Y_SUXC1_OP", "Y_SW_OP", "Y_SWC1_OP",
  "Y_SWC2_OP", "Y_SWL_OP", "Y_SWR_OP", "Y_SWXC1_OP", "Y_SYNC_OP",
  "Y_SYNCI_OP", "Y_SYSCALL_OP", "Y_TEQ_OP", "Y_TEQI_OP", "Y_TGE_OP",
  "Y_TGEI_OP", "Y_TGEIU_OP", "Y_TGEU_OP", "Y_TLBP_OP", "Y_TLBR_OP",
  "Y_TLBWI_OP", "Y_TLBWR_OP", "Y_TLT_OP", "Y_TLTI_OP", "Y_TLTIU_OP",
  "Y_TLTU_OP", "Y_TNE_OP", "Y_TNEI_OP", "Y_TRUNC_L_D_OP", "Y_TRUNC_L_S_OP",
  "Y_TRUNC_W_D_OP", "Y_TRUNC_W_S_OP", "Y_WRPGPR_OP", "Y_WSBH_OP",
  "Y_XOR_OP", "Y_XORI_OP", "Y_ABS_POP", "Y_B_POP", "Y_BAL_POP",
  "Y_BEQZ_POP", "Y_BGE_POP", "Y_BGEU_POP", "Y_BGT_POP", "Y_BGTU_POP",
  "Y_BLE_POP", "Y_BLEU_POP", "Y_BLT_POP", "Y_BLTU_POP", "Y_BNEZ_POP",
  "Y_LA_POP", "Y_LD_POP", "Y_L_D_POP", "Y_L_S_POP", "Y_LI_D_POP",
  "Y_LI_POP", "Y_LI_S_POP", "Y_MFC1_D_POP", "Y_MOVE_POP", "Y_MTC1_D_POP",
  "Y_MULO_POP", "Y_MULOU_POP", "Y_NEG_POP", "Y_NEGU_POP", "Y_NOP_POP",
  "Y_NOT_POP", "Y_REM_POP", "Y_REMU_POP", "Y_ROL_POP", "Y_ROR_POP",
  "Y_S_D_POP", "Y_S_S_POP", "Y_SD_POP", "Y_SEQ_POP", "Y_SGE_POP",
  "Y_SGEU_POP", "Y_SGT_POP", "Y_SGTU_POP", "Y_SLE_POP", "Y_SLEU_POP",
  "Y_SNE_POP", "Y_ULH_POP", "Y_ULHU_POP", "Y_ULW_POP", "Y_USH_POP",
  "Y_USW_POP", "Y_ALIAS_DIR", "Y_ALIGN_DIR", "Y_ASCII_DIR", "Y_ASCIIZ_DIR",
  "Y_ASM0_DIR", "Y_BGNB_DIR", "Y_BYTE_DIR", "Y_COMM_DIR", "Y_DATA_DIR",
  "Y_DOUBLE_DIR", "Y_END_DIR", "Y_ENDB_DIR", "Y_ENDR_DIR", "Y_ENT_DIR",
  "Y_ERR_DIR", "Y_EXTERN_DIR", "Y_FILE_DIR", "Y_FLOAT_DIR", "Y_FMASK_DIR",
  "Y_FRAME_DIR", "Y_GLOBAL_DIR", "Y_HALF_DIR", "Y_K_DATA_DIR",
  "Y_K_TEXT_DIR", "Y_LABEL_DIR", "Y_LCOMM_DIR", "Y_LIVEREG_DIR",
  "Y_LOC_DIR", "Y_MASK_DIR", "Y_NOALIAS_DIR", "Y_OPTIONS_DIR",
  "Y_RDATA_DIR", "Y_REPEAT_DIR", "Y_SDATA_DIR", "Y_SET_DIR", "Y_SPACE_DIR",
  "Y_STRUCT_DIR", "Y_TEXT_DIR", "Y_VERSTAMP_DIR", "Y_VREG_DIR",
  "Y_WORD_DIR", "':'", "'='", "'('", "')'", "'+'", "'-'", "'>'", "'*'",
  "'/'", "$accept", "LINE", "$@1", "LBL_CMD", "OPT_LBL", "CMD", "$@2",
  "$@3", "TERM", "ASM_CODE", "LOAD_OPS", "LOADI_OPS", "ULOADH_POPS",
  "LOADC_OPS", "LOADFP_OPS", "LOADFP_INDEX_OPS", "STORE_OPS", "STOREC_OPS",
  "STOREFP_OPS", "STOREFP_INDEX_OPS", "SYS_OPS", "PREFETCH_OPS",
  "CACHE_OPS", "TLB_OPS", "NULLARY_OPS", "NULLARY_OPS_REV2",
  "COUNT_LEADING_OPS", "UNARY_OPS_REV2", "BINARYI_OPS", "BINARYIR_OPS",
  "BINARY_ARITHI_OPS", "BINARY_LOGICALI_OPS", "SHIFT_OPS",
  "SHIFT_OPS_REV2", "SHIFTV_OPS_REV2", "BINARY_OPS", "BINARY_OPS_REV2",
  "SUB_OPS", "DIV_POPS", "MUL_POPS", "SET_LE_POPS", "SET_GT_POPS",
  "SET_GE_POPS", "SET_EQ_POPS", "MULT_OPS", "MULT_OPS3", "BF_OPS_REV2",
  "BR_COP_OPS", "UNARY_BR_OPS", "UNARY_BR_POPS", "BINARY_BR_OPS",
  "BR_GT_POPS", "BR_GE_POPS", "BR_LT_POPS", "BR_LE_POPS", "J_OPS", "B_OPS",
  "BINARYI_TRAP_OPS", "BINARY_TRAP_OPS", "MOVE_FROM_HILO_OPS",
  "MOVE_TO_HILO_OPS", "MOVEC_OPS", "MOVE_COP_OPS", "MOVE_COP_OPS_REV2",
  "CTL_COP_OPS", "FP_MOVE_OPS", "FP_MOVE_OPS_REV2", "MOVECC_OPS",
  "FP_MOVEC_OPS", "FP_MOVEC_OPS_REV2", "FP_MOVECC_OPS",
  "FP_MOVECC_OPS_REV2", "FP_UNARY_OPS", "FP_UNARY_OPS_REV2",
  "FP_BINARY_OPS", "FP_BINARY_OPS_REV2", "FP_TERNARY_OPS_REV2",
  "FP_CMP_OPS", "FP_CMP_OPS_REV2", "ASM_DIRECTIVE", "$@4", "$@5", "$@6",
  "$@7", "$@8", "$@9", "$@10", "ADDRESS", "$@11", "ADDR", "BR_IMM32",
  "$@12", "IMM16", "UIMM16", "IMM32", "ABS_ADDR", "SRC1", "SRC2", "DEST",
  "REG", "REGISTER", "F_DEST", "F_SRC1", "F_SRC2", "FP_REGISTER", "CC_REG",
  "COP_REG", "LABEL", "STR_LST", "STR", "EXPRESSION", "$@13", "EXPR",
  "TRM", "FACTOR", "EXPR_LST", "FP_EXPR_LST", "OPTIONAL_ID", "$@14",
  "OPT_ID", "ID", "$@15", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   439,   440,   441,   442,   443,   444,
     445,   446,   447,   448,   449,   450,   451,   452,   453,   454,
     455,   456,   457,   458,   459,   460,   461,   462,   463,   464,
     465,   466,   467,   468,   469,   470,   471,   472,   473,   474,
     475,   476,   477,   478,   479,   480,   481,   482,   483,   484,
     485,   486,   487,   488,   489,   490,   491,   492,   493,   494,
     495,   496,   497,   498,   499,   500,   501,   502,   503,   504,
     505,   506,   507,   508,   509,   510,   511,   512,   513,   514,
     515,   516,   517,   518,   519,   520,   521,   522,   523,   524,
     525,   526,   527,   528,   529,   530,   531,   532,   533,   534,
     535,   536,   537,   538,   539,   540,   541,   542,   543,   544,
     545,   546,   547,   548,   549,   550,   551,   552,   553,   554,
     555,   556,   557,   558,   559,   560,   561,   562,   563,   564,
     565,   566,   567,   568,   569,   570,   571,   572,   573,   574,
     575,   576,   577,   578,   579,   580,   581,   582,   583,   584,
     585,   586,   587,   588,   589,   590,   591,   592,   593,   594,
     595,   596,   597,   598,   599,   600,   601,   602,   603,   604,
     605,   606,   607,   608,   609,   610,   611,   612,   613,   614,
     615,   616,   617,   618,   619,   620,   621,   622,   623,   624,
     625,   626,   627,   628,   629,   630,   631,   632,   633,   634,
     635,   636,   637,   638,   639,   640,   641,   642,   643,   644,
     645,   646,   647,    58,    61,    40,    41,    43,    45,    62,
      42,    47
};
#endif

#define YYPACT_NINF (-631)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-573)

#define yytable_value_is_error(Yyn) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -631,    18,   768,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,    21,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,     3,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
      33,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,    40,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
      40,  -631,  -631,  -631,    45,    40,    45,  -631,    40,  -631,
    -631,  -631,    40,    40,  -631,    40,  -631,  -631,    40,    40,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,    40,    40,    40,    55,     8,  -631,  -631,
    -631,    63,  -631,  -631,    78,  -631,  -631,    81,  -631,  -631,
    -631,  -631,    88,  -631,   107,    40,  -631,  -631,   126,   131,
    -631,  -631,   132,   133,   134,    70,  -631,   135,     8,   136,
    -631,     8,     8,   138,   139,    40,  -631,  -631,   768,  -631,
    -631,  -631,    40,    40,    40,    72,    45,    45,    40,    72,
      45,    45,  -631,  -631,   140,  -631,  -631,  -631,    40,    40,
      40,    40,    40,    40,    40,    40,    40,    40,    40,    40,
      40,    40,    40,    40,    40,    40,    40,    40,    45,   148,
      40,    40,    40,    40,    40,    40,    40,    40,  -631,    40,
      40,    40,    40,    40,    40,    40,    40,    45,    45,    40,
      45,    45,    45,    45,    45,    45,    45,    45,    45,    47,
      45,  -631,  -312,   152,  -631,    -4,  -309,   154,  -631,  -631,
    -631,  -631,  -631,    40,  -631,  -631,  -631,   150,  -631,     3,
     153,    40,    40,    40,    40,    40,    40,  -631,  -631,  -631,
    -631,   155,  -631,     8,  -306,  -305,  -631,  -631,   156,   156,
    -631,  -631,     8,  -631,   157,  -631,   158,  -631,   161,     8,
     160,   157,   165,   166,  -631,  -631,  -631,  -631,  -631,     8,
     167,   169,   170,   172,  -631,  -631,  -306,  -631,  -631,  -306,
    -306,  -631,   175,   176,  -631,  -631,    97,  -631,     3,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,     5,  -631,    40,  -631,     0,    17,     0,     0,    40,
      40,    40,     0,    40,     0,    40,    40,    40,    40,    40,
      40,    40,    40,    45,  -631,  -631,  -631,  -631,  -631,  -631,
      40,    40,    40,    40,    40,  -631,    40,   177,  -631,  -631,
       3,    40,  -631,  -631,  -631,    40,    72,    72,    72,    45,
      45,    40,    45,    45,    45,    45,    45,    45,    45,    45,
      45,    45,    45,    45,    97,  -631,     8,  -631,  -631,   178,
     154,   154,  -214,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,     0,     0,  -631,  -631,  -631,  -631,  -331,     8,
       8,     8,     8,  -208,   156,  -631,   156,  -207,     8,   101,
    -306,  -631,   184,  -631,  -631,  -631,  -306,  -631,   184,  -631,
      40,   103,  -306,  -631,  -631,  -631,  -631,  -631,   182,   105,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -334,    40,  -631,  -326,  -631,  -631,  -631,     0,
    -631,    67,  -631,  -631,     3,  -631,     3,    68,   183,    40,
    -631,     0,  -631,  -631,     0,     0,     0,     0,     0,     0,
       0,  -631,  -631,     0,   190,  -631,  -631,  -631,  -631,  -631,
       3,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,    40,  -631,  -631,  -631,  -631,  -631,   191,
      40,    40,   148,   148,  -631,  -631,    45,    45,    45,  -631,
      45,  -631,  -631,  -306,  -631,  -631,  -631,  -202,  -631,  -631,
    -631,  -631,  -631,  -305,  -305,  -631,  -631,   194,  -631,     8,
    -306,  -631,  -631,  -631,  -631,    40,   154,   154,  -196,    40,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,   196,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,    45,
    -631,  -197,  -631,  -306,  -193,  -189,  -187,  -631,  -184,  -631,
    -631,  -631,   208,  -631,    40,    40,  -631,  -631,  -182,  -181,
    -631,  -631
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       2,     0,   572,     1,    14,    13,   341,   360,   340,   382,
     191,   389,   381,   201,   202,   192,   395,   193,   205,   245,
     246,   247,   248,   249,   250,   251,   252,   267,   268,   253,
     255,   256,   254,   257,   258,   259,   260,   261,   263,   264,
     262,   269,   270,     0,   426,   440,   410,   424,   441,   408,
     438,   443,   422,   436,   442,   420,   437,   444,   421,   435,
     445,   419,   433,   446,   417,   439,   447,   423,   429,   448,
     413,   428,   449,   412,   434,   450,   418,   432,   451,   416,
     427,   452,   411,   431,   453,   415,   430,   454,   414,   425,
     455,   409,   177,   361,   362,   342,   343,   317,   318,   319,
     187,   188,     0,   320,   321,   322,   363,   344,   345,   364,
     365,   366,   346,   367,   368,   369,   347,   348,   349,   184,
     189,   384,   222,   383,   223,   185,   190,   183,   243,   370,
     371,   350,   351,   244,   279,   282,   284,   283,   281,   280,
     135,   136,   150,   148,   154,   137,   138,   139,   145,   155,
     140,   151,   149,   141,   142,   156,   396,   238,   397,   398,
     239,   305,   306,   308,   313,   314,   299,   300,   324,   325,
     323,   334,   326,   338,   335,   328,   303,   332,   329,   336,
     327,   339,   337,   330,   304,   333,   331,   399,   240,   400,
     401,   241,   309,   310,   312,   315,   316,   301,   302,   386,
     390,   385,   242,   236,   237,   353,   372,   352,   402,   403,
     404,   405,   406,   407,   213,   195,   206,   143,   391,   392,
     178,   175,   393,   394,   214,   215,   373,   374,   173,   211,
     212,   375,   376,   354,   355,   377,   378,   157,   158,   186,
     169,   165,   170,   216,   217,   159,   208,   198,   196,   203,
     204,   197,   356,   357,   209,   199,   210,   200,    40,   388,
     220,   387,   221,   171,   160,   168,   164,   161,   162,   172,
      36,   176,   174,   293,   287,   294,   288,   289,   295,   179,
     180,   181,   182,   296,   290,   291,   297,   298,   292,   379,
     380,   358,   359,   218,   219,   194,   207,     0,   285,   286,
     265,   273,   274,   271,   272,   277,   278,   275,   276,   266,
       0,   144,   152,   153,     0,     0,     0,   307,     0,   311,
     226,   227,     0,     0,    39,     0,   224,   225,     0,     0,
     166,   167,   163,   234,   232,   233,   230,   231,   228,   229,
     235,   146,   147,     0,     0,     0,     0,   572,   458,   460,
     462,     0,   464,   572,   467,   471,   568,     0,   475,   572,
     479,   572,     0,   481,     0,     0,   572,   486,   469,   505,
     572,   572,     0,     0,     0,     0,   572,   496,   572,   498,
     572,   572,   572,   503,     0,     0,   509,     3,     0,     5,
      12,     8,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    32,   511,     0,    35,    46,    47,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   572,
       0,     0,     0,     0,     0,     0,     0,   572,   572,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    10,     0,     0,    38,   532,     0,     0,   134,   527,
     529,    37,   539,     0,   537,   511,   543,     0,   540,     0,
       0,     0,     0,     0,     0,     0,     0,   511,   511,   535,
     511,     0,   560,   572,   457,   554,   557,   562,     0,     0,
     463,   552,   572,   468,     0,   473,   571,   474,   476,   572,
       0,     0,     0,     0,   485,   552,   470,   506,   488,   572,
       0,     0,     0,     0,   494,   497,   495,   499,   500,   501,
     502,   504,     0,     0,   552,     4,     0,   511,     0,   511,
     545,   546,   511,   511,   541,   511,   511,   511,   511,   511,
      33,     0,   511,     0,    49,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   544,   572,    92,   547,   572,   572,
     523,   523,   523,   523,   523,   107,     0,   535,   106,   109,
       0,     0,   121,   538,   122,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     6,   572,   573,   534,     0,
       0,     0,     0,    41,    19,    21,    20,    22,    45,    42,
      43,    44,     0,     0,    23,    29,    28,   456,     0,   572,
     572,   572,   572,   550,   459,   549,   461,   564,   572,   552,
     466,   567,   472,   569,   570,   477,   478,   480,   482,   483,
       0,   552,   489,   490,   491,   492,   493,   507,     0,   552,
       9,    15,    18,   526,    24,    16,    17,    25,    26,    27,
      30,    31,   516,     0,   512,   514,    34,    48,    52,     0,
      55,     0,    57,   525,     0,    59,     0,     0,     0,     0,
      66,     0,    67,    70,     0,    71,     0,     0,     0,     0,
       0,    76,   536,     0,     0,   542,    93,    94,    95,   572,
       0,   572,   572,   572,   572,   572,   572,   572,   572,   572,
     108,   110,   111,     0,   123,   124,   125,   112,   113,     0,
       0,     0,   118,     0,   126,   127,     0,     0,     0,   131,
       0,   133,    11,     7,   533,   530,   531,     0,    82,    80,
      81,    79,   561,   555,   556,   558,   559,     0,   548,   572,
     553,   563,   566,   484,   508,     0,     0,     0,     0,     0,
     572,    51,    50,    54,    53,    56,    58,    60,    61,    62,
      63,    65,    64,    69,    68,    73,    72,    75,    74,    85,
      84,    87,    86,    89,    88,    91,    90,    78,    77,     0,
      97,   524,    96,    99,    98,   101,   100,   103,   102,   105,
     104,   114,   115,   116,   117,   119,   120,   128,   129,     0,
     132,     0,   551,   565,     0,   518,   520,   513,     0,   519,
      83,   130,     0,   517,     0,     0,   515,   528,     0,     0,
     521,   522
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -631,  -631,  -631,  -631,  -631,  -172,  -631,  -631,  -520,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,  -631,
    -631,  -631,  -631,  -631,  -631,  -631,  -631,  -283,  -631,  -631,
    -524,  -631,  -561,  -541,   -98,  -464,   816,   625,   241,  -400,
    -295,    20,  -357,  -557,  -375,  -445,  -387,  -402,  -282,  -568,
    -630,  -631,  -341,  -529,  -507,  -483,  -293,  -631,  -631,  -631,
      -2,  -631
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,     2,   387,   388,   389,   536,   614,   390,   391,
     392,   393,   394,   395,   396,   397,   398,   399,   400,   401,
     402,   403,   404,   405,   406,   407,   408,   409,   410,   411,
     412,   413,   414,   415,   416,   417,   418,   419,   420,   421,
     422,   423,   424,   425,   426,   427,   428,   429,   430,   431,
     432,   433,   434,   435,   436,   437,   438,   439,   440,   441,
     442,   443,   444,   445,   446,   447,   448,   449,   450,   451,
     452,   453,   454,   455,   456,   457,   458,   459,   460,   461,
     498,   499,   501,   504,   511,   515,   534,   550,   551,   684,
     719,   720,   692,   672,   673,   469,   488,   711,   473,   592,
     489,   477,   543,   714,   478,   575,   542,   576,   644,   645,
     647,   648,   494,   495,   496,   649,   652,   505,   506,   653,
     470,   463
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     462,   618,   474,   622,   468,   465,   466,   472,   465,   466,
     465,   682,   547,   492,   612,   474,   670,   695,     3,   771,
     474,   544,   690,   474,   472,   544,   464,   474,   474,   731,
     474,   771,   661,   474,   474,   588,   589,   526,   471,   771,
     529,   530,   594,   548,   596,   597,   598,   472,   474,   744,
     745,   669,   574,   476,   749,   476,   751,   722,   724,   726,
     728,   775,   491,   776,   777,   762,   639,   640,   500,   779,
     513,   780,   783,   787,   472,   472,   768,   523,   768,   540,
     541,   615,   616,   503,   544,   544,   507,   685,   620,   621,
     533,   639,   640,   510,   752,   641,   642,   474,   474,   474,
       4,     5,   611,   613,  -465,  -465,  -487,  -487,  -510,  -510,
     763,   764,   512,   474,   474,   474,   474,   474,   474,   474,
     474,   474,   474,   474,   474,   474,   474,   474,   474,   474,
     474,   516,   474,   785,   765,   766,   517,   520,   521,   522,
     525,   527,   587,   531,   532,   552,   593,   593,   474,   593,
     593,   593,   638,   574,   474,   786,   755,   756,   617,   465,
     625,   650,   637,   627,  -572,   643,   655,   651,   656,   657,
     659,   660,   663,   716,   664,   665,   717,   718,   662,   666,
     667,   668,   757,   754,  -537,   767,   769,   774,   789,   827,
     828,   829,   624,   830,   772,   809,   822,   831,   715,   832,
     837,   840,   842,   843,   634,   635,   844,   636,   845,   734,
     735,   736,   846,   847,   850,   851,   535,   646,   658,     0,
       0,     0,     0,     0,   544,   544,     0,   544,   544,   544,
     544,   715,   715,   544,   544,   544,   715,   544,   715,     0,
       0,     0,   737,   738,     0,   740,   741,   742,   743,     0,
       0,   746,   747,   748,   671,   750,   674,     0,     0,   675,
     676,     0,   677,   678,   679,   680,   681,     0,     0,   686,
       0,     0,     0,     0,     0,   753,   712,     0,     0,     0,
       0,     0,     0,     0,     0,   712,   712,   712,   712,   712,
       0,     0,     0,     0,     0,     0,   712,   825,   826,     0,
       0,     0,     0,     0,     0,     0,     0,   770,     0,     0,
       0,     0,   835,   836,     0,     0,     0,   810,     0,   812,
     813,   814,   815,   816,   817,   818,   819,   820,     0,     0,
       0,     0,     0,   821,     0,     0,   480,   712,   712,     0,
     823,   824,     0,     0,     0,   497,     0,     0,     0,     0,
       0,   502,     0,     0,     0,     0,     0,   508,     0,   509,
       0,     0,     0,     0,   514,   773,     0,     0,   518,   519,
       0,   715,   715,   715,   524,   715,   497,     0,   528,   497,
     497,   626,     0,     0,     0,     0,     0,     0,   778,     0,
       0,     0,     0,   619,   712,   467,   712,     0,   467,     0,
     683,     0,   712,   493,   712,     0,   712,     0,     0,   712,
     712,   712,   712,   712,   712,   712,     0,   545,   712,     0,
       0,   549,     0,     0,     0,     0,     0,   577,   833,     0,
       0,     0,     0,     0,     0,   577,   577,     0,   593,     0,
       0,     0,     0,     0,     0,   593,   593,     0,   573,     0,
       0,     0,     0,     0,   841,     0,     0,   688,     0,   693,
       0,     0,     0,     0,   700,     0,   703,   599,   600,     0,
     602,   603,   604,   605,   606,   607,   608,   609,   610,     0,
     834,     0,     0,     0,   838,     0,     0,     0,     0,     0,
       0,   497,   693,     0,     0,     0,     0,     0,     0,     0,
     497,     0,     0,     0,   654,     0,     0,   497,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   497,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   758,   760,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   848,
     849,   475,     0,     0,     0,     0,   479,     0,     0,   481,
       0,     0,     0,   482,   483,     0,   484,     0,     0,   485,
     486,     0,     0,   577,     0,     0,   577,   577,     0,     0,
       0,     0,     0,     0,   487,     0,     0,     0,     0,     0,
       0,   781,     0,     0,     0,     0,   693,     0,     0,     0,
       0,     0,     0,   791,     0,     0,   793,   795,   797,   799,
     801,   803,   805,     0,   497,   807,     0,     0,     0,     0,
       0,     0,   811,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   537,   538,   539,     0,   497,   497,   497,
     497,     0,     0,     0,     0,     0,   497,     0,     0,   553,
     554,   555,   556,   557,   558,   559,   560,   561,   562,   563,
     564,   565,   566,   567,   568,   569,   570,     0,   572,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   586,     0,
       0,     0,     0,     0,   595,     0,     0,     0,     0,     0,
     601,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   577,     0,   577,
     577,   577,   577,   577,   577,   577,   577,   577,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   497,     0,     0,
       0,     4,     5,     0,     0,     0,     0,     0,   839,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   179,   180,   181,   182,   183,   184,   185,   186,
     187,   188,   189,   190,   191,   192,   193,   194,   195,   196,
     197,   198,   199,   200,   201,   202,   203,   204,   205,   206,
     207,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,   241,   242,   243,   244,   245,   246,
     247,   248,   249,   250,   251,   252,   253,   254,   255,   256,
     257,   258,   259,   260,     0,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   313,   314,   315,
     316,   317,   318,   319,   320,   321,   322,   323,   324,   325,
     326,   327,   328,   329,   330,   331,   332,   333,   334,   335,
     336,   337,   338,   339,   340,   341,   342,   343,   344,   345,
     346,   347,   348,   349,   350,   351,   352,   353,   354,   355,
     356,   357,   358,   359,   360,   361,   362,   363,   364,   365,
     366,   367,   368,   369,   370,   371,   372,   373,   374,   375,
     376,   377,   378,   379,   380,   381,   382,   383,   384,   385,
     386,   490,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   721,   723,   725,   727,   729,
       0,     0,     0,     0,   546,     0,   732,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   571,     0,     0,     0,   578,   579,   580,   581,
     582,   583,   584,   585,     0,   590,   591,   759,   761,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   623,
       0,     0,     0,     0,     0,     0,     0,   628,   629,   630,
     631,   632,   633,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   782,     0,   784,     0,     0,     0,
       0,     0,   788,     0,   790,     0,   792,     0,     0,   794,
     796,   798,   800,   802,   804,   806,     0,     0,   808,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   687,
       0,   689,   691,   694,   696,   697,   698,   699,   701,   702,
     704,   705,   706,   707,   708,   709,   710,     0,   713,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   730,     0,     0,     0,     0,     0,     0,     0,
       0,   733,     0,     0,     0,     0,     0,   739
};

static const yytype_int16 yycheck[] =
{
       2,     5,   297,   467,   102,     5,     6,     7,     5,     6,
       5,     6,   399,     5,   459,   310,   536,   558,     0,   649,
     315,   396,     5,   318,     7,   400,     5,   322,   323,   590,
     325,   661,   515,   328,   329,   437,   438,   378,     5,   669,
     381,   382,   442,   400,   444,   445,   446,     7,   343,   606,
     607,   534,     5,     8,   611,     8,   613,   581,   582,   583,
     584,   395,     7,   397,   398,   396,   397,   398,     5,   395,
     365,   397,     5,     5,     7,     7,   644,     7,   646,     7,
       8,   393,   394,     5,   459,   460,     5,   551,   397,   398,
     385,   397,   398,     5,   614,   400,   401,   392,   393,   394,
       3,     4,   459,   460,     3,     4,     3,     4,     3,     4,
     639,   640,     5,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,     5,   427,   694,   641,   642,     5,     5,     5,     5,
       5,     5,   437,     5,     5,     5,   441,   442,   443,   444,
     445,   446,   493,     5,   449,   696,   620,   621,     6,     5,
      10,   502,     7,    10,     6,     9,     5,    10,   509,     9,
       5,     5,     5,   575,     5,     5,   578,   579,   519,     7,
       5,     5,   396,     5,     7,   393,   393,     5,     5,   746,
     747,   748,   475,   750,    10,     5,     5,   399,   573,     5,
     396,     5,   399,   396,   487,   488,   395,   490,   395,   596,
     597,   598,   396,     5,   396,   396,   388,   499,   511,    -1,
      -1,    -1,    -1,    -1,   599,   600,    -1,   602,   603,   604,
     605,   606,   607,   608,   609,   610,   611,   612,   613,    -1,
      -1,    -1,   599,   600,    -1,   602,   603,   604,   605,    -1,
      -1,   608,   609,   610,   537,   612,   539,    -1,    -1,   542,
     543,    -1,   545,   546,   547,   548,   549,    -1,    -1,   552,
      -1,    -1,    -1,    -1,    -1,   616,   571,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   580,   581,   582,   583,   584,
      -1,    -1,    -1,    -1,    -1,    -1,   591,   742,   743,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   648,    -1,    -1,
      -1,    -1,   776,   777,    -1,    -1,    -1,   719,    -1,   721,
     722,   723,   724,   725,   726,   727,   728,   729,    -1,    -1,
      -1,    -1,    -1,   733,    -1,    -1,   316,   632,   633,    -1,
     740,   741,    -1,    -1,    -1,   347,    -1,    -1,    -1,    -1,
      -1,   353,    -1,    -1,    -1,    -1,    -1,   359,    -1,   361,
      -1,    -1,    -1,    -1,   366,   660,    -1,    -1,   370,   371,
      -1,   746,   747,   748,   376,   750,   378,    -1,   380,   381,
     382,   479,    -1,    -1,    -1,    -1,    -1,    -1,   683,    -1,
      -1,    -1,    -1,   397,   689,   395,   691,    -1,   395,    -1,
     395,    -1,   697,   395,   699,    -1,   701,    -1,    -1,   704,
     705,   706,   707,   708,   709,   710,    -1,   397,   713,    -1,
      -1,   401,    -1,    -1,    -1,    -1,    -1,   429,   769,    -1,
      -1,    -1,    -1,    -1,    -1,   437,   438,    -1,   733,    -1,
      -1,    -1,    -1,    -1,    -1,   740,   741,    -1,   428,    -1,
      -1,    -1,    -1,    -1,   829,    -1,    -1,   555,    -1,   557,
      -1,    -1,    -1,    -1,   562,    -1,   564,   447,   448,    -1,
     450,   451,   452,   453,   454,   455,   456,   457,   458,    -1,
     775,    -1,    -1,    -1,   779,    -1,    -1,    -1,    -1,    -1,
      -1,   493,   590,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     502,    -1,    -1,    -1,   506,    -1,    -1,   509,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   519,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   632,   633,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   844,
     845,   310,    -1,    -1,    -1,    -1,   315,    -1,    -1,   318,
      -1,    -1,    -1,   322,   323,    -1,   325,    -1,    -1,   328,
     329,    -1,    -1,   575,    -1,    -1,   578,   579,    -1,    -1,
      -1,    -1,    -1,    -1,   343,    -1,    -1,    -1,    -1,    -1,
      -1,   689,    -1,    -1,    -1,    -1,   694,    -1,    -1,    -1,
      -1,    -1,    -1,   701,    -1,    -1,   704,   705,   706,   707,
     708,   709,   710,    -1,   616,   713,    -1,    -1,    -1,    -1,
      -1,    -1,   720,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   392,   393,   394,    -1,   639,   640,   641,
     642,    -1,    -1,    -1,    -1,    -1,   648,    -1,    -1,   408,
     409,   410,   411,   412,   413,   414,   415,   416,   417,   418,
     419,   420,   421,   422,   423,   424,   425,    -1,   427,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   437,    -1,
      -1,    -1,    -1,    -1,   443,    -1,    -1,    -1,    -1,    -1,
     449,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   719,    -1,   721,
     722,   723,   724,   725,   726,   727,   728,   729,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   769,    -1,    -1,
      -1,     3,     4,    -1,    -1,    -1,    -1,    -1,   780,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   194,   195,   196,   197,   198,   199,   200,   201,
     202,   203,   204,   205,   206,   207,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   235,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,   251,
     252,   253,   254,   255,   256,   257,   258,   259,   260,   261,
     262,   263,   264,   265,    -1,   267,   268,   269,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   307,   308,   309,   310,   311,
     312,   313,   314,   315,   316,   317,   318,   319,   320,   321,
     322,   323,   324,   325,   326,   327,   328,   329,   330,   331,
     332,   333,   334,   335,   336,   337,   338,   339,   340,   341,
     342,   343,   344,   345,   346,   347,   348,   349,   350,   351,
     352,   353,   354,   355,   356,   357,   358,   359,   360,   361,
     362,   363,   364,   365,   366,   367,   368,   369,   370,   371,
     372,   373,   374,   375,   376,   377,   378,   379,   380,   381,
     382,   383,   384,   385,   386,   387,   388,   389,   390,   391,
     392,   345,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   580,   581,   582,   583,   584,
      -1,    -1,    -1,    -1,   398,    -1,   591,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   426,    -1,    -1,    -1,   430,   431,   432,   433,
     434,   435,   436,   437,    -1,   439,   440,   632,   633,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   473,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   481,   482,   483,
     484,   485,   486,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   689,    -1,   691,    -1,    -1,    -1,
      -1,    -1,   697,    -1,   699,    -1,   701,    -1,    -1,   704,
     705,   706,   707,   708,   709,   710,    -1,    -1,   713,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   553,
      -1,   555,   556,   557,   558,   559,   560,   561,   562,   563,
     564,   565,   566,   567,   568,   569,   570,    -1,   572,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   586,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   595,    -1,    -1,    -1,    -1,    -1,   601
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_int16 yystos[] =
{
       0,   403,   404,     0,     3,     4,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   313,   314,   315,
     316,   317,   318,   319,   320,   321,   322,   323,   324,   325,
     326,   327,   328,   329,   330,   331,   332,   333,   334,   335,
     336,   337,   338,   339,   340,   341,   342,   343,   344,   345,
     346,   347,   348,   349,   350,   351,   352,   353,   354,   355,
     356,   357,   358,   359,   360,   361,   362,   363,   364,   365,
     366,   367,   368,   369,   370,   371,   372,   373,   374,   375,
     376,   377,   378,   379,   380,   381,   382,   383,   384,   385,
     386,   387,   388,   389,   390,   391,   392,   405,   406,   407,
     410,   411,   412,   413,   414,   415,   416,   417,   418,   419,
     420,   421,   422,   423,   424,   425,   426,   427,   428,   429,
     430,   431,   432,   433,   434,   435,   436,   437,   438,   439,
     440,   441,   442,   443,   444,   445,   446,   447,   448,   449,
     450,   451,   452,   453,   454,   455,   456,   457,   458,   459,
     460,   461,   462,   463,   464,   465,   466,   467,   468,   469,
     470,   471,   472,   473,   474,   475,   476,   477,   478,   479,
     480,   481,   522,   523,     5,     5,     6,   395,   496,   497,
     522,     5,     7,   500,   502,   500,     8,   503,   506,   500,
     503,   500,   500,   500,   500,   500,   500,   500,   498,   502,
     498,     7,     5,   395,   514,   515,   516,   522,   482,   483,
       5,   484,   522,     5,   485,   519,   520,     5,   522,   522,
       5,   486,     5,   502,   522,   487,     5,     5,   522,   522,
       5,     5,     5,     7,   522,     5,   514,     5,   522,   514,
     514,     5,     5,   502,   488,   407,   408,   500,   500,   500,
       7,     8,   508,   504,   506,   503,   498,   508,   504,   503,
     489,   490,     5,   500,   500,   500,   500,   500,   500,   500,
     500,   500,   500,   500,   500,   500,   500,   500,   500,   500,
     500,   498,   500,   503,     5,   507,   509,   522,   498,   498,
     498,   498,   498,   498,   498,   498,   500,   502,   509,   509,
     498,   498,   501,   502,   501,   500,   501,   501,   501,   503,
     503,   500,   503,   503,   503,   503,   503,   503,   503,   503,
     503,   504,   507,   504,   409,   393,   394,     6,     5,   397,
     397,   398,   497,   498,   489,    10,   496,    10,   498,   498,
     498,   498,   498,   498,   489,   489,   489,     7,   514,   397,
     398,   400,   401,     9,   510,   511,   510,   512,   513,   517,
     514,    10,   518,   521,   522,     5,   514,     9,   518,     5,
       5,   517,   514,     5,     5,     5,     7,     5,     5,   517,
     410,   489,   495,   496,   489,   489,   489,   489,   489,   489,
     489,   489,     6,   395,   491,   497,   489,   498,   496,   498,
       5,   498,   494,   496,   498,   495,   498,   498,   498,   498,
     496,   498,   498,   496,   498,   498,   498,   498,   498,   498,
     498,   499,   502,   498,   505,   506,   509,   509,   509,   492,
     493,   499,   492,   499,   492,   499,   492,   499,   492,   499,
     498,   494,   499,   498,   508,   508,   508,   504,   504,   498,
     504,   504,   504,   504,   505,   505,   504,   504,   504,   505,
     504,   505,   410,   514,     5,   497,   497,   396,   496,   499,
     496,   499,   396,   515,   515,   516,   516,   393,   511,   393,
     514,   512,    10,   502,     5,   395,   397,   398,   502,   395,
     397,   496,   499,     5,   499,   494,   495,     5,   499,     5,
     499,   496,   499,   496,   499,   496,   499,   496,   499,   496,
     499,   496,   499,   496,   499,   496,   499,   496,   499,     5,
     509,   496,   509,   509,   509,   509,   509,   509,   509,   509,
     509,   501,     5,   501,   501,   507,   507,   505,   505,   505,
     505,   399,     5,   514,   502,   497,   497,   396,   502,   522,
       5,   506,   399,   396,   395,   395,   396,     5,   502,   502,
     396,   396
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_int16 yyr1[] =
{
       0,   402,   404,   403,   405,   405,   406,   406,   408,   407,
     409,   407,   407,   410,   410,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   411,   411,   411,   411,   411,
     411,   411,   411,   411,   411,   412,   412,   412,   412,   412,
     412,   412,   412,   412,   412,   413,   414,   414,   415,   415,
     416,   416,   416,   416,   417,   417,   417,   418,   418,   418,
     418,   418,   418,   418,   419,   419,   419,   419,   420,   420,
     421,   421,   421,   422,   422,   423,   423,   424,   424,   425,
     425,   425,   425,   426,   427,   427,   427,   428,   428,   429,
     429,   430,   430,   430,   430,   430,   430,   430,   431,   431,
     431,   432,   432,   432,   432,   433,   433,   433,   434,   434,
     434,   435,   436,   437,   438,   438,   438,   438,   438,   438,
     439,   439,   440,   440,   440,   440,   441,   441,   442,   442,
     443,   443,   444,   444,   445,   445,   446,   446,   446,   446,
     446,   446,   447,   448,   448,   449,   449,   449,   449,   449,
     449,   449,   449,   450,   450,   450,   450,   450,   450,   450,
     450,   450,   450,   450,   450,   451,   451,   452,   452,   452,
     452,   453,   453,   454,   454,   455,   455,   456,   456,   457,
     457,   457,   457,   457,   457,   458,   458,   459,   459,   459,
     459,   459,   459,   460,   460,   460,   460,   460,   460,   461,
     461,   462,   462,   463,   463,   464,   464,   464,   464,   464,
     464,   464,   464,   465,   465,   465,   465,   466,   466,   466,
     466,   466,   466,   467,   467,   468,   469,   469,   470,   470,
     470,   470,   471,   471,   472,   472,   472,   472,   473,   473,
     474,   474,   474,   474,   474,   474,   474,   474,   474,   474,
     474,   474,   474,   474,   474,   474,   474,   474,   474,   474,
     475,   475,   475,   475,   475,   475,   475,   475,   475,   475,
     475,   475,   475,   475,   475,   475,   475,   475,   475,   475,
     475,   476,   476,   476,   476,   476,   476,   476,   476,   477,
     477,   477,   477,   477,   477,   478,   478,   478,   478,   478,
     478,   478,   478,   478,   478,   478,   478,   478,   479,   479,
     479,   479,   479,   479,   479,   479,   479,   479,   479,   479,
     479,   479,   479,   479,   479,   479,   479,   479,   479,   479,
     479,   479,   479,   479,   479,   479,   479,   479,   479,   479,
     480,   480,   480,   480,   480,   480,   480,   480,   480,   480,
     480,   480,   480,   480,   480,   480,   481,   481,   482,   481,
     483,   481,   481,   481,   484,   481,   481,   481,   481,   481,
     481,   485,   481,   481,   481,   481,   481,   481,   481,   481,
     481,   486,   481,   481,   481,   481,   487,   481,   481,   481,
     481,   481,   481,   481,   481,   481,   481,   481,   481,   481,
     481,   481,   481,   481,   481,   481,   481,   481,   481,   488,
     481,   490,   489,   491,   491,   491,   491,   491,   491,   491,
     491,   491,   491,   493,   492,   494,   495,   496,   496,   496,
     496,   496,   497,   497,   497,   498,   499,   500,   501,   502,
     503,   504,   505,   506,   507,   508,   508,   509,   510,   510,
     511,   511,   513,   512,   514,   514,   514,   515,   515,   515,
     516,   516,   516,   517,   517,   517,   518,   518,   520,   519,
     521,   521,   523,   522
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     2,     2,     1,     2,     3,     0,     3,
       0,     3,     1,     1,     1,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     1,     2,     3,     1,     1,     2,     2,     1,
       1,     3,     3,     3,     3,     3,     1,     1,     3,     2,
       4,     4,     3,     4,     4,     3,     4,     3,     4,     3,
       4,     4,     4,     4,     4,     4,     3,     3,     4,     4,
       3,     3,     4,     4,     4,     4,     3,     4,     4,     4,
       4,     4,     4,     5,     4,     4,     4,     4,     4,     4,
       4,     4,     2,     3,     3,     3,     4,     4,     4,     4,
       4,     4,     4,     4,     4,     4,     2,     2,     3,     2,
       3,     3,     3,     3,     4,     4,     4,     4,     3,     4,
       4,     2,     2,     3,     3,     3,     3,     3,     4,     4,
       5,     3,     4,     3,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     2,     0,     3,
       0,     3,     1,     2,     0,     3,     3,     1,     2,     1,
       2,     0,     3,     2,     2,     1,     2,     3,     3,     1,
       3,     0,     3,     3,     4,     2,     0,     3,     2,     3,
       3,     3,     3,     3,     2,     2,     1,     2,     1,     2,
       2,     2,     2,     1,     2,     1,     2,     3,     4,     0,
       3,     0,     2,     3,     1,     4,     1,     4,     3,     3,
       3,     6,     6,     0,     2,     1,     1,     1,     6,     1,
       3,     3,     1,     3,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       1,     3,     0,     2,     1,     3,     3,     1,     3,     3,
       1,     3,     1,     2,     1,     3,     2,     1,     0,     2,
       1,     0,     0,     2
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
# ifndef YY_LOCATION_PRINT
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yykind < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yykind], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* $@1: %empty  */
#line 516 "parser.y"
                {parse_error_occurred = false; scanner_start_line (); }
#line 2564 "parser_yacc.cpp"
    break;

  case 6: /* OPT_LBL: ID ':'  */
#line 523 "parser.y"
                {
		  /* Call outside of cons_label, since an error sets that variable to NULL. */
		  label* l = record_label ((char*)yyvsp[-1].p,
					   text_dir ? current_text_pc () : current_data_pc (),
					   0);
		  this_line_labels = cons_label (l, this_line_labels);
		  free ((char*)yyvsp[-1].p);
		}
#line 2577 "parser_yacc.cpp"
    break;

  case 7: /* OPT_LBL: ID '=' EXPR  */
#line 533 "parser.y"
                {
		  label *l = record_label ((char*)yyvsp[-2].p, (mem_addr)yyvsp[0].i, 1);
		  free ((char*)yyvsp[-2].p);

		  l->const_flag = 1;
		  clear_labels ();
		}
#line 2589 "parser_yacc.cpp"
    break;

  case 8: /* $@2: %empty  */
#line 544 "parser.y"
                {
		  clear_labels ();
		}
#line 2597 "parser_yacc.cpp"
    break;

  case 10: /* $@3: %empty  */
#line 550 "parser.y"
                {
		  clear_labels ();
		}
#line 2605 "parser_yacc.cpp"
    break;

  case 13: /* TERM: Y_NL  */
#line 560 "parser.y"
                {
			LINE_PARSE_DONE;
		}
#line 2613 "parser_yacc.cpp"
    break;

  case 14: /* TERM: Y_EOF  */
#line 565 "parser.y"
                {
		  clear_labels ();
		  FILE_PARSE_DONE;
		}
#line 2622 "parser_yacc.cpp"
    break;

  case 15: /* ASM_CODE: LOAD_OPS DEST ADDRESS  */
#line 574 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i == Y_LD_POP ? Y_LW_OP : yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  if (yyvsp[-2].i == Y_LD_POP)
		    i_type_inst_free (Y_LW_OP,
				      yyvsp[-1].i + 1,
				      addr_expr_reg ((addr_expr *)yyvsp[0].p),
				      incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
							4));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2641 "parser_yacc.cpp"
    break;

  case 16: /* ASM_CODE: LOADC_OPS COP_REG ADDRESS  */
#line 590 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2654 "parser_yacc.cpp"
    break;

  case 17: /* ASM_CODE: LOADFP_OPS F_SRC1 ADDRESS  */
#line 600 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2667 "parser_yacc.cpp"
    break;

  case 18: /* ASM_CODE: LOADI_OPS DEST UIMM16  */
#line 610 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, yyvsp[-1].i, 0, (imm_expr *)yyvsp[0].p);
		}
#line 2675 "parser_yacc.cpp"
    break;

  case 19: /* ASM_CODE: Y_LA_POP DEST ADDRESS  */
#line 616 "parser.y"
                {
		  if (addr_expr_reg ((addr_expr *)yyvsp[0].p))
		    i_type_inst (Y_ADDI_OP, yyvsp[-1].i,
				 addr_expr_reg ((addr_expr *)yyvsp[0].p),
				 addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  else
		    i_type_inst (Y_ORI_OP, yyvsp[-1].i, 0,
				 addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2691 "parser_yacc.cpp"
    break;

  case 20: /* ASM_CODE: Y_LI_POP DEST IMM32  */
#line 630 "parser.y"
                {
		  i_type_inst_free (Y_ORI_OP, yyvsp[-1].i, 0, (imm_expr *)yyvsp[0].p);
		}
#line 2699 "parser_yacc.cpp"
    break;

  case 21: /* ASM_CODE: Y_LI_D_POP F_DEST Y_FP  */
#line 636 "parser.y"
                {
		  int *x = (int *) yyvsp[0].p;

		  i_type_inst (Y_ORI_OP, 1, 0, const_imm_expr (*x));
		  r_co_type_inst (Y_MTC1_OP, 0, yyvsp[-1].i, 1);
		  i_type_inst (Y_ORI_OP, 1, 0, const_imm_expr (*(x+1)));
		  r_co_type_inst (Y_MTC1_OP, 0, yyvsp[-1].i + 1, 1);
		}
#line 2712 "parser_yacc.cpp"
    break;

  case 22: /* ASM_CODE: Y_LI_S_POP F_DEST Y_FP  */
#line 647 "parser.y"
                {
		  float x = (float) *((double *) yyvsp[0].p);
		  int *y = (int *) &x;

		  i_type_inst (Y_ORI_OP, 1, 0, const_imm_expr (*y));
		  r_co_type_inst (Y_MTC1_OP, 0, yyvsp[-1].i, 1);
		}
#line 2724 "parser_yacc.cpp"
    break;

  case 23: /* ASM_CODE: Y_ULW_POP DEST ADDRESS  */
#line 657 "parser.y"
                {
#ifdef SPIM_BIGENDIAN
		  i_type_inst (Y_LWL_OP, yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  i_type_inst_free (Y_LWR_OP, yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      3));
#else
		  i_type_inst_free (Y_LWL_OP, yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      3));
		  i_type_inst (Y_LWR_OP, yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
#endif
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2750 "parser_yacc.cpp"
    break;

  case 24: /* ASM_CODE: ULOADH_POPS DEST ADDRESS  */
#line 681 "parser.y"
                {
#ifdef SPIM_BIGENDIAN
		  i_type_inst ((yyvsp[-2].i == Y_ULH_POP ? Y_LB_OP : Y_LBU_OP),
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  i_type_inst_free (Y_LBU_OP, 1,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      1));
#else
		  i_type_inst_free ((yyvsp[-2].i == Y_ULH_POP ? Y_LB_OP : Y_LBU_OP),
				    yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      1));
		  i_type_inst (Y_LBU_OP, 1,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
#endif
		  r_sh_type_inst (Y_SLL_OP, yyvsp[-1].i, yyvsp[-1].i, 8);
		  r_type_inst (Y_OR_OP, yyvsp[-1].i, yyvsp[-1].i, 1);
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2780 "parser_yacc.cpp"
    break;

  case 25: /* ASM_CODE: LOADFP_INDEX_OPS F_DEST ADDRESS  */
#line 709 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 2788 "parser_yacc.cpp"
    break;

  case 26: /* ASM_CODE: STORE_OPS SRC1 ADDRESS  */
#line 715 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i == Y_SD_POP ? Y_SW_OP : yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  if (yyvsp[-2].i == Y_SD_POP)
		    i_type_inst_free (Y_SW_OP, yyvsp[-1].i + 1,
				      addr_expr_reg ((addr_expr *)yyvsp[0].p),
				      incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
							4));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2806 "parser_yacc.cpp"
    break;

  case 27: /* ASM_CODE: STOREC_OPS COP_REG ADDRESS  */
#line 731 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2819 "parser_yacc.cpp"
    break;

  case 28: /* ASM_CODE: Y_USW_POP SRC1 ADDRESS  */
#line 742 "parser.y"
                {
#ifdef SPIM_BIGENDIAN
		  i_type_inst (Y_SWL_OP, yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  i_type_inst_free (Y_SWR_OP, yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      3));
#else
		  i_type_inst_free (Y_SWL_OP, yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      3));
		  i_type_inst (Y_SWR_OP, yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
#endif
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2845 "parser_yacc.cpp"
    break;

  case 29: /* ASM_CODE: Y_USH_POP SRC1 ADDRESS  */
#line 766 "parser.y"
                {
		  i_type_inst (Y_SB_OP, yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));

		  /* ROL SRC, SRC, 8 */
		  r_sh_type_inst (Y_SLL_OP, 1, yyvsp[-1].i, 24);
		  r_sh_type_inst (Y_SRL_OP, yyvsp[-1].i, yyvsp[-1].i, 8);
		  r_type_inst (Y_OR_OP, yyvsp[-1].i, yyvsp[-1].i, 1);

		  i_type_inst_free (Y_SB_OP, yyvsp[-1].i,
				    addr_expr_reg ((addr_expr *)yyvsp[0].p),
				    incr_expr_offset (addr_expr_imm ((addr_expr *)yyvsp[0].p),
						      1));
		  /* ROR SRC, SRC, 8 */
		  r_sh_type_inst (Y_SRL_OP, 1, yyvsp[-1].i, 24);
		  r_sh_type_inst (Y_SLL_OP, yyvsp[-1].i, yyvsp[-1].i, 8);
		  r_type_inst (Y_OR_OP, yyvsp[-1].i, yyvsp[-1].i, 1);

		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2872 "parser_yacc.cpp"
    break;

  case 30: /* ASM_CODE: STOREFP_OPS F_SRC1 ADDRESS  */
#line 791 "parser.y"
                {
		  i_type_inst (yyvsp[-2].i,
			       yyvsp[-1].i,
			       addr_expr_reg ((addr_expr *)yyvsp[0].p),
			       addr_expr_imm ((addr_expr *)yyvsp[0].p));
		  free (((addr_expr *)yyvsp[0].p)->imm);
		  free ((addr_expr *)yyvsp[0].p);
		}
#line 2885 "parser_yacc.cpp"
    break;

  case 31: /* ASM_CODE: STOREFP_INDEX_OPS F_DEST ADDRESS  */
#line 802 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 2893 "parser_yacc.cpp"
    break;

  case 32: /* ASM_CODE: SYS_OPS  */
#line 808 "parser.y"
                {
		  r_type_inst (yyvsp[0].i, 0, 0, 0);
		}
#line 2901 "parser_yacc.cpp"
    break;

  case 33: /* ASM_CODE: PREFETCH_OPS ADDRESS  */
#line 814 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 2909 "parser_yacc.cpp"
    break;

  case 34: /* ASM_CODE: CACHE_OPS Y_INT ADDRESS  */
#line 820 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, yyvsp[-1].i, 0, (imm_expr *)yyvsp[0].p);
		}
#line 2917 "parser_yacc.cpp"
    break;

  case 35: /* ASM_CODE: TLB_OPS  */
#line 826 "parser.y"
                {
		  r_type_inst (yyvsp[0].i, 0, 0, 0);
		}
#line 2925 "parser_yacc.cpp"
    break;

  case 36: /* ASM_CODE: Y_SYNC_OP  */
#line 832 "parser.y"
                {
		  r_type_inst (yyvsp[0].i, 0, 0, 0);
		}
#line 2933 "parser_yacc.cpp"
    break;

  case 37: /* ASM_CODE: Y_SYNC_OP Y_INT  */
#line 837 "parser.y"
                {
		  r_type_inst (yyvsp[-1].i, yyvsp[0].i, 0, 0);
		}
#line 2941 "parser_yacc.cpp"
    break;

  case 38: /* ASM_CODE: Y_BREAK_OP Y_INT  */
#line 843 "parser.y"
                {
		  if (yyvsp[0].i == 1)
		    yyerror ("Breakpoint 1 is reserved for debugger");
		  r_type_inst (yyvsp[-1].i, yyvsp[0].i, 0, 0);
		}
#line 2951 "parser_yacc.cpp"
    break;

  case 39: /* ASM_CODE: Y_NOP_POP  */
#line 851 "parser.y"
                {
		  nop_inst ();
		}
#line 2959 "parser_yacc.cpp"
    break;

  case 40: /* ASM_CODE: Y_SSNOP_OP  */
#line 857 "parser.y"
                {
		  r_sh_type_inst (Y_SLL_OP, 0, 0, 1); /* SLL r0 r0 1 */
		}
#line 2967 "parser_yacc.cpp"
    break;

  case 41: /* ASM_CODE: Y_ABS_POP DEST SRC1  */
#line 863 "parser.y"
                {
		  if (yyvsp[-1].i != yyvsp[0].i)
		    r_type_inst (Y_ADDU_OP, yyvsp[-1].i, 0, yyvsp[0].i);

		  i_type_inst_free (Y_BGEZ_OP, 0, yyvsp[0].i, branch_offset (2));
		  r_type_inst (Y_SUB_OP, yyvsp[-1].i, 0, yyvsp[0].i);
		}
#line 2979 "parser_yacc.cpp"
    break;

  case 42: /* ASM_CODE: Y_NEG_POP DEST SRC1  */
#line 873 "parser.y"
                {
		  r_type_inst (Y_SUB_OP, yyvsp[-1].i, 0, yyvsp[0].i);
		}
#line 2987 "parser_yacc.cpp"
    break;

  case 43: /* ASM_CODE: Y_NEGU_POP DEST SRC1  */
#line 879 "parser.y"
                {
		  r_type_inst (Y_SUBU_OP, yyvsp[-1].i, 0, yyvsp[0].i);
		}
#line 2995 "parser_yacc.cpp"
    break;

  case 44: /* ASM_CODE: Y_NOT_POP DEST SRC1  */
#line 885 "parser.y"
                {
		  r_type_inst (Y_NOR_OP, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3003 "parser_yacc.cpp"
    break;

  case 45: /* ASM_CODE: Y_MOVE_POP DEST SRC1  */
#line 891 "parser.y"
                {
		  r_type_inst (Y_ADDU_OP, yyvsp[-1].i, 0, yyvsp[0].i);
		}
#line 3011 "parser_yacc.cpp"
    break;

  case 46: /* ASM_CODE: NULLARY_OPS  */
#line 897 "parser.y"
                {
		  r_type_inst (yyvsp[0].i, 0, 0, 0);
		}
#line 3019 "parser_yacc.cpp"
    break;

  case 47: /* ASM_CODE: NULLARY_OPS_REV2  */
#line 903 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3027 "parser_yacc.cpp"
    break;

  case 48: /* ASM_CODE: COUNT_LEADING_OPS DEST SRC1  */
#line 909 "parser.y"
                {
		  /* RT must be equal to RD */
		  r_type_inst (yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, yyvsp[-1].i);
		}
#line 3036 "parser_yacc.cpp"
    break;

  case 49: /* ASM_CODE: UNARY_OPS_REV2 DEST  */
#line 916 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3044 "parser_yacc.cpp"
    break;

  case 50: /* ASM_CODE: BINARYI_OPS DEST SRC1 SRC2  */
#line 922 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3052 "parser_yacc.cpp"
    break;

  case 51: /* ASM_CODE: BINARYI_OPS DEST SRC1 IMM32  */
#line 927 "parser.y"
                {
		  i_type_inst_free (op_to_imm_op (yyvsp[-3].i), yyvsp[-2].i, yyvsp[-1].i,
				    (imm_expr *)yyvsp[0].p);
		}
#line 3061 "parser_yacc.cpp"
    break;

  case 52: /* ASM_CODE: BINARYI_OPS DEST IMM32  */
#line 933 "parser.y"
                {
		  i_type_inst_free (op_to_imm_op (yyvsp[-2].i), yyvsp[-1].i, yyvsp[-1].i,
				    (imm_expr *)yyvsp[0].p);
		}
#line 3070 "parser_yacc.cpp"
    break;

  case 53: /* ASM_CODE: BINARYIR_OPS DEST SRC1 SRC2  */
#line 940 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[0].i, yyvsp[-1].i);
		}
#line 3078 "parser_yacc.cpp"
    break;

  case 54: /* ASM_CODE: BINARYIR_OPS DEST SRC1 Y_INT  */
#line 945 "parser.y"
                {
		  r_sh_type_inst (op_to_imm_op (yyvsp[-3].i), yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3086 "parser_yacc.cpp"
    break;

  case 55: /* ASM_CODE: BINARYIR_OPS DEST Y_INT  */
#line 950 "parser.y"
                {
		  r_sh_type_inst (op_to_imm_op (yyvsp[-2].i), yyvsp[-1].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3094 "parser_yacc.cpp"
    break;

  case 56: /* ASM_CODE: BINARY_ARITHI_OPS DEST SRC1 IMM16  */
#line 956 "parser.y"
                {
		  i_type_inst_free (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3102 "parser_yacc.cpp"
    break;

  case 57: /* ASM_CODE: BINARY_ARITHI_OPS DEST IMM16  */
#line 961 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, yyvsp[-1].i, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3110 "parser_yacc.cpp"
    break;

  case 58: /* ASM_CODE: BINARY_LOGICALI_OPS DEST SRC1 UIMM16  */
#line 967 "parser.y"
                {
		  i_type_inst_free (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3118 "parser_yacc.cpp"
    break;

  case 59: /* ASM_CODE: BINARY_LOGICALI_OPS DEST UIMM16  */
#line 972 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, yyvsp[-1].i, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3126 "parser_yacc.cpp"
    break;

  case 60: /* ASM_CODE: SHIFT_OPS DEST SRC1 Y_INT  */
#line 978 "parser.y"
                {
		  if ((yyvsp[0].i < 0) || (31 < yyvsp[0].i))
		    yywarn ("Shift distance can only be in the range 0..31");
		  r_sh_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3136 "parser_yacc.cpp"
    break;

  case 61: /* ASM_CODE: SHIFT_OPS DEST SRC1 SRC2  */
#line 985 "parser.y"
                {
		  r_type_inst (imm_op_to_op (yyvsp[-3].i), yyvsp[-2].i, yyvsp[0].i, yyvsp[-1].i);
		}
#line 3144 "parser_yacc.cpp"
    break;

  case 62: /* ASM_CODE: SHIFT_OPS_REV2 DEST SRC1 Y_INT  */
#line 991 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3152 "parser_yacc.cpp"
    break;

  case 63: /* ASM_CODE: SHIFTV_OPS_REV2 DEST SRC1 SRC2  */
#line 996 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3160 "parser_yacc.cpp"
    break;

  case 64: /* ASM_CODE: BINARY_OPS DEST SRC1 SRC2  */
#line 1002 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3168 "parser_yacc.cpp"
    break;

  case 65: /* ASM_CODE: BINARY_OPS DEST SRC1 IMM32  */
#line 1007 "parser.y"
                {
		  if (bare_machine && !accept_pseudo_insts)
		    yyerror ("Immediate form not allowed in bare machine");
		  else
		    {
		      if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
			/* Use $at */
			i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		      r_type_inst (yyvsp[-3].i,
				   yyvsp[-2].i,
				   yyvsp[-1].i,
				   (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		    }
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3188 "parser_yacc.cpp"
    break;

  case 66: /* ASM_CODE: BINARY_OPS DEST IMM32  */
#line 1024 "parser.y"
                {
		  check_uimm_range ((imm_expr *)yyvsp[0].p, UIMM_MIN, UIMM_MAX);
		  if (bare_machine && !accept_pseudo_insts)
		    yyerror ("Immediate form not allowed in bare machine");
		  else
		    {
		      if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
			/* Use $at */
			i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		      r_type_inst (yyvsp[-2].i,
				   yyvsp[-1].i,
				   yyvsp[-1].i,
				   (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		    }
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3209 "parser_yacc.cpp"
    break;

  case 67: /* ASM_CODE: BINARY_OPS_REV2 DEST SRC1  */
#line 1043 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3217 "parser_yacc.cpp"
    break;

  case 68: /* ASM_CODE: SUB_OPS DEST SRC1 SRC2  */
#line 1049 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3225 "parser_yacc.cpp"
    break;

  case 69: /* ASM_CODE: SUB_OPS DEST SRC1 IMM32  */
#line 1054 "parser.y"
                {
		  int val = eval_imm_expr ((imm_expr *)yyvsp[0].p);

		  if (bare_machine && !accept_pseudo_insts)
		    yyerror ("Immediate form not allowed in bare machine");
		  else
		    i_type_inst (yyvsp[-3].i == Y_SUB_OP ? Y_ADDI_OP
				 : yyvsp[-3].i == Y_SUBU_OP ? Y_ADDIU_OP
				 : (fatal_error ("Bad SUB_OP\n"), 0),
				 yyvsp[-2].i,
				 yyvsp[-1].i,
				 make_imm_expr (-val, NULL, false));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3244 "parser_yacc.cpp"
    break;

  case 70: /* ASM_CODE: SUB_OPS DEST IMM32  */
#line 1070 "parser.y"
                {
		  int val = eval_imm_expr ((imm_expr *)yyvsp[0].p);

		  if (bare_machine && !accept_pseudo_insts)
		    yyerror ("Immediate form not allowed in bare machine");
		  else
		    i_type_inst (yyvsp[-2].i == Y_SUB_OP ? Y_ADDI_OP
				 : yyvsp[-2].i == Y_SUBU_OP ? Y_ADDIU_OP
				 : (fatal_error ("Bad SUB_OP\n"), 0),
				 yyvsp[-1].i,
				 yyvsp[-1].i,
				 make_imm_expr (-val, NULL, false));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3263 "parser_yacc.cpp"
    break;

  case 71: /* ASM_CODE: DIV_POPS DEST SRC1  */
#line 1087 "parser.y"
                {
		  /* The hardware divide operation (ignore 1st arg) */
		  if (yyvsp[-2].i != Y_DIV_OP && yyvsp[-2].i != Y_DIVU_OP)
		    yyerror ("REM requires 3 arguments");
		  else
		    r_type_inst (yyvsp[-2].i, 0, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3275 "parser_yacc.cpp"
    break;

  case 72: /* ASM_CODE: DIV_POPS DEST SRC1 SRC2  */
#line 1096 "parser.y"
                {
		  /* Pseudo divide operations */
		  div_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3284 "parser_yacc.cpp"
    break;

  case 73: /* ASM_CODE: DIV_POPS DEST SRC1 IMM32  */
#line 1102 "parser.y"
                {
		  if (is_zero_imm ((imm_expr *)yyvsp[0].p))
		    yyerror ("Divide by zero");
		  else
		    {
		      /* Use $at */
		      i_type_inst_free (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		      div_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, 1, 1);
		    }
		}
#line 3299 "parser_yacc.cpp"
    break;

  case 74: /* ASM_CODE: MUL_POPS DEST SRC1 SRC2  */
#line 1115 "parser.y"
                {
		  mult_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3307 "parser_yacc.cpp"
    break;

  case 75: /* ASM_CODE: MUL_POPS DEST SRC1 IMM32  */
#line 1120 "parser.y"
                {
		  if (is_zero_imm ((imm_expr *)yyvsp[0].p))
		    /* Optimize: n * 0 == 0 */
		    i_type_inst_free (Y_ORI_OP, yyvsp[-2].i, 0, (imm_expr *)yyvsp[0].p);
		  else
		    {
		      /* Use $at */
		      i_type_inst_free (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		      mult_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, 1);
		    }
		}
#line 3323 "parser_yacc.cpp"
    break;

  case 76: /* ASM_CODE: MULT_OPS SRC1 SRC2  */
#line 1134 "parser.y"
                {
		  r_type_inst (yyvsp[-2].i, 0, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3331 "parser_yacc.cpp"
    break;

  case 77: /* ASM_CODE: MULT_OPS3 DEST SRC1 SRC2  */
#line 1140 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3339 "parser_yacc.cpp"
    break;

  case 78: /* ASM_CODE: MULT_OPS3 DEST SRC1 IMM32  */
#line 1145 "parser.y"
                {
		  /* Special case, for backward compatibility with pseudo-op
		     MULT instruction */
		  i_type_inst_free (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p); /* Use $at */
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, 1);
		}
#line 3350 "parser_yacc.cpp"
    break;

  case 79: /* ASM_CODE: Y_ROR_POP DEST SRC1 SRC2  */
#line 1154 "parser.y"
                {
		  r_type_inst (Y_SUBU_OP, 1, 0, yyvsp[0].i);
		  r_type_inst (Y_SLLV_OP, 1, 1, yyvsp[-1].i);
		  r_type_inst (Y_SRLV_OP, yyvsp[-2].i, yyvsp[0].i, yyvsp[-1].i);
		  r_type_inst (Y_OR_OP, yyvsp[-2].i, yyvsp[-2].i, 1);
		}
#line 3361 "parser_yacc.cpp"
    break;

  case 80: /* ASM_CODE: Y_ROL_POP DEST SRC1 SRC2  */
#line 1163 "parser.y"
                {
		  r_type_inst (Y_SUBU_OP, 1, 0, yyvsp[0].i);
		  r_type_inst (Y_SRLV_OP, 1, 1, yyvsp[-1].i);
		  r_type_inst (Y_SLLV_OP, yyvsp[-2].i, yyvsp[0].i, yyvsp[-1].i);
		  r_type_inst (Y_OR_OP, yyvsp[-2].i, yyvsp[-2].i, 1);
		}
#line 3372 "parser_yacc.cpp"
    break;

  case 81: /* ASM_CODE: Y_ROR_POP DEST SRC1 IMM32  */
#line 1172 "parser.y"
                {
		  long dist = eval_imm_expr ((imm_expr *)yyvsp[0].p);

		  check_imm_range ((imm_expr *)yyvsp[0].p, 0, 31);
		  r_sh_type_inst (Y_SLL_OP, 1, yyvsp[-1].i, -dist);
		  r_sh_type_inst (Y_SRL_OP, yyvsp[-2].i, yyvsp[-1].i, dist);
		  r_type_inst (Y_OR_OP, yyvsp[-2].i, yyvsp[-2].i, 1);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3386 "parser_yacc.cpp"
    break;

  case 82: /* ASM_CODE: Y_ROL_POP DEST SRC1 IMM32  */
#line 1184 "parser.y"
                {
		  long dist = eval_imm_expr ((imm_expr *)yyvsp[0].p);

		  check_imm_range ((imm_expr *)yyvsp[0].p, 0, 31);
		  r_sh_type_inst (Y_SRL_OP, 1, yyvsp[-1].i, -dist);
		  r_sh_type_inst (Y_SLL_OP, yyvsp[-2].i, yyvsp[-1].i, dist);
		  r_type_inst (Y_OR_OP, yyvsp[-2].i, yyvsp[-2].i, 1);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3400 "parser_yacc.cpp"
    break;

  case 83: /* ASM_CODE: BF_OPS_REV2 F_DEST F_SRC2 Y_INT Y_INT  */
#line 1196 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3408 "parser_yacc.cpp"
    break;

  case 84: /* ASM_CODE: SET_LE_POPS DEST SRC1 SRC2  */
#line 1202 "parser.y"
                {
		  set_le_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3416 "parser_yacc.cpp"
    break;

  case 85: /* ASM_CODE: SET_LE_POPS DEST SRC1 IMM32  */
#line 1207 "parser.y"
                {
		  if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
		    /* Use $at */
		    i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		  set_le_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i,
			       (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3429 "parser_yacc.cpp"
    break;

  case 86: /* ASM_CODE: SET_GT_POPS DEST SRC1 SRC2  */
#line 1218 "parser.y"
                {
		  set_gt_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3437 "parser_yacc.cpp"
    break;

  case 87: /* ASM_CODE: SET_GT_POPS DEST SRC1 IMM32  */
#line 1223 "parser.y"
                {
		  if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
		    /* Use $at */
		    i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		  set_gt_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i,
			       (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3450 "parser_yacc.cpp"
    break;

  case 88: /* ASM_CODE: SET_GE_POPS DEST SRC1 SRC2  */
#line 1235 "parser.y"
                {
		  set_ge_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3458 "parser_yacc.cpp"
    break;

  case 89: /* ASM_CODE: SET_GE_POPS DEST SRC1 IMM32  */
#line 1240 "parser.y"
                {
		  if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
		    /* Use $at */
		    i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		  set_ge_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i,
			       (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3471 "parser_yacc.cpp"
    break;

  case 90: /* ASM_CODE: SET_EQ_POPS DEST SRC1 SRC2  */
#line 1251 "parser.y"
                {
		  set_eq_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3479 "parser_yacc.cpp"
    break;

  case 91: /* ASM_CODE: SET_EQ_POPS DEST SRC1 IMM32  */
#line 1256 "parser.y"
                {
		  if (!is_zero_imm ((imm_expr *)yyvsp[0].p))
		    /* Use $at */
		    i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[0].p);
		  set_eq_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i,
			       (is_zero_imm ((imm_expr *)yyvsp[0].p) ? 0 : 1));
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3492 "parser_yacc.cpp"
    break;

  case 92: /* ASM_CODE: BR_COP_OPS LABEL  */
#line 1267 "parser.y"
                {
		  /* RS and RT fields contain information on test */
                  int nd = opcode_is_nullified_branch (yyvsp[-1].i) ? 1 : 0;
                  int tf = opcode_is_true_branch (yyvsp[-1].i) ? 1 : 0;
		  i_type_inst_free (yyvsp[-1].i,
				    cc_to_rt (0, nd, tf),
				    BIN_RS (yyvsp[-1].i),
				    (imm_expr *)yyvsp[0].p);
		}
#line 3506 "parser_yacc.cpp"
    break;

  case 93: /* ASM_CODE: BR_COP_OPS CC_REG LABEL  */
#line 1278 "parser.y"
                {
		  /* RS and RT fields contain information on test */
                  int nd = opcode_is_nullified_branch (yyvsp[-2].i) ? 1 : 0;
                  int tf = opcode_is_true_branch (yyvsp[-2].i) ? 1 : 0;
		  i_type_inst_free (yyvsp[-2].i,
				    cc_to_rt (yyvsp[-1].i, nd, tf),
				    BIN_RS (yyvsp[-2].i),
				    (imm_expr *)yyvsp[0].p);
		}
#line 3520 "parser_yacc.cpp"
    break;

  case 94: /* ASM_CODE: UNARY_BR_OPS SRC1 LABEL  */
#line 1290 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, 0, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3528 "parser_yacc.cpp"
    break;

  case 95: /* ASM_CODE: UNARY_BR_POPS SRC1 LABEL  */
#line 1296 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i == Y_BEQZ_POP ? Y_BEQ_OP : Y_BNE_OP,
			       0, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3537 "parser_yacc.cpp"
    break;

  case 96: /* ASM_CODE: BINARY_BR_OPS SRC1 SRC2 LABEL  */
#line 1303 "parser.y"
                {
		  i_type_inst_free (yyvsp[-3].i, yyvsp[-1].i, yyvsp[-2].i, (imm_expr *)yyvsp[0].p);
		}
#line 3545 "parser_yacc.cpp"
    break;

  case 97: /* ASM_CODE: BINARY_BR_OPS SRC1 BR_IMM32 LABEL  */
#line 1308 "parser.y"
                {
		  if (bare_machine && !accept_pseudo_insts)
		    yyerror ("Immediate form not allowed in bare machine");
		  else
		    {
		      if (is_zero_imm ((imm_expr *)yyvsp[-1].p))
			i_type_inst (yyvsp[-3].i, yyvsp[-2].i,
				     (is_zero_imm ((imm_expr *)yyvsp[-1].p) ? 0 : 1),
				     (imm_expr *)yyvsp[0].p);
		      else
			{
			  /* Use $at */
			  i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[-1].p);
			  i_type_inst (yyvsp[-3].i, yyvsp[-2].i,
				       (is_zero_imm ((imm_expr *)yyvsp[-1].p) ? 0 : 1),
				       (imm_expr *)yyvsp[0].p);
			}
		    }
		  free ((imm_expr *)yyvsp[-1].p);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3571 "parser_yacc.cpp"
    break;

  case 98: /* ASM_CODE: BR_GT_POPS SRC1 SRC2 LABEL  */
#line 1332 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i == Y_BGT_POP ? Y_SLT_OP : Y_SLTU_OP,
			       1, yyvsp[-1].i, yyvsp[-2].i); /* Use $at */
		  i_type_inst_free (Y_BNE_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		}
#line 3581 "parser_yacc.cpp"
    break;

  case 99: /* ASM_CODE: BR_GT_POPS SRC1 BR_IMM32 LABEL  */
#line 1339 "parser.y"
                {
		  if (yyvsp[-3].i == Y_BGT_POP)
		    {
		      /* Use $at */
		      i_type_inst_free (Y_SLTI_OP, 1, yyvsp[-2].i,
					incr_expr_offset ((imm_expr *)yyvsp[-1].p, 1));
		      i_type_inst (Y_BEQ_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		    }
		  else
		    {
		      /* Use $at */
		      /* Can't add 1 to immediate since 0xffffffff+1 = 0 < 1 */
		      i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[-1].p);
		      i_type_inst_free (Y_BEQ_OP, yyvsp[-2].i, 1, branch_offset (3));
		      r_type_inst (Y_SLTU_OP, 1, yyvsp[-2].i, 1);
		      i_type_inst (Y_BEQ_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		    }
		  free ((imm_expr *)yyvsp[-1].p);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3606 "parser_yacc.cpp"
    break;

  case 100: /* ASM_CODE: BR_GE_POPS SRC1 SRC2 LABEL  */
#line 1362 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i == Y_BGE_POP ? Y_SLT_OP : Y_SLTU_OP,
			       1, yyvsp[-2].i, yyvsp[-1].i); /* Use $at */
		  i_type_inst_free (Y_BEQ_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		}
#line 3616 "parser_yacc.cpp"
    break;

  case 101: /* ASM_CODE: BR_GE_POPS SRC1 BR_IMM32 LABEL  */
#line 1369 "parser.y"
                {
		  i_type_inst (yyvsp[-3].i == Y_BGE_POP ? Y_SLTI_OP : Y_SLTIU_OP,
			       1, yyvsp[-2].i, (imm_expr *)yyvsp[-1].p); /* Use $at */
		  i_type_inst_free (Y_BEQ_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		  free ((imm_expr *)yyvsp[-1].p);
		}
#line 3627 "parser_yacc.cpp"
    break;

  case 102: /* ASM_CODE: BR_LT_POPS SRC1 SRC2 LABEL  */
#line 1378 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i == Y_BLT_POP ? Y_SLT_OP : Y_SLTU_OP,
			       1, yyvsp[-2].i, yyvsp[-1].i); /* Use $at */
		  i_type_inst_free (Y_BNE_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		}
#line 3637 "parser_yacc.cpp"
    break;

  case 103: /* ASM_CODE: BR_LT_POPS SRC1 BR_IMM32 LABEL  */
#line 1385 "parser.y"
                {
		  i_type_inst (yyvsp[-3].i == Y_BLT_POP ? Y_SLTI_OP : Y_SLTIU_OP,
			       1, yyvsp[-2].i, (imm_expr *)yyvsp[-1].p); /* Use $at */
		  i_type_inst_free (Y_BNE_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		  free ((imm_expr *)yyvsp[-1].p);
		}
#line 3648 "parser_yacc.cpp"
    break;

  case 104: /* ASM_CODE: BR_LE_POPS SRC1 SRC2 LABEL  */
#line 1394 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i == Y_BLE_POP ? Y_SLT_OP : Y_SLTU_OP,
			       1, yyvsp[-1].i, yyvsp[-2].i); /* Use $at */
		  i_type_inst_free (Y_BEQ_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		}
#line 3658 "parser_yacc.cpp"
    break;

  case 105: /* ASM_CODE: BR_LE_POPS SRC1 BR_IMM32 LABEL  */
#line 1401 "parser.y"
                {
		  if (yyvsp[-3].i == Y_BLE_POP)
		    {
		      /* Use $at */
		      i_type_inst_free (Y_SLTI_OP, 1, yyvsp[-2].i,
					incr_expr_offset ((imm_expr *)yyvsp[-1].p, 1));
		      i_type_inst (Y_BNE_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		    }
		  else
		    {
		      /* Use $at */
		      /* Can't add 1 to immediate since 0xffffffff+1 = 0 < 1 */
		      i_type_inst (Y_ORI_OP, 1, 0, (imm_expr *)yyvsp[-1].p);
		      i_type_inst (Y_BEQ_OP, yyvsp[-2].i, 1, (imm_expr *)yyvsp[0].p);
		      r_type_inst (Y_SLTU_OP, 1, yyvsp[-2].i, 1);
		      i_type_inst (Y_BNE_OP, 0, 1, (imm_expr *)yyvsp[0].p);
		    }
		  free ((imm_expr *)yyvsp[-1].p);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3683 "parser_yacc.cpp"
    break;

  case 106: /* ASM_CODE: J_OPS LABEL  */
#line 1424 "parser.y"
                {
		  if ((yyvsp[-1].i == Y_J_OP) || (yyvsp[-1].i == Y_JR_OP))
		    j_type_inst (Y_J_OP, (imm_expr *)yyvsp[0].p);
		  else if ((yyvsp[-1].i == Y_JAL_OP) || (yyvsp[-1].i == Y_JALR_OP))
		    j_type_inst (Y_JAL_OP, (imm_expr *)yyvsp[0].p);
		  free ((imm_expr *)yyvsp[0].p);
		}
#line 3695 "parser_yacc.cpp"
    break;

  case 107: /* ASM_CODE: J_OPS SRC1  */
#line 1433 "parser.y"
                {
		  if ((yyvsp[-1].i == Y_J_OP) || (yyvsp[-1].i == Y_JR_OP))
		    r_type_inst (Y_JR_OP, 0, yyvsp[0].i, 0);
		  else if ((yyvsp[-1].i == Y_JAL_OP) || (yyvsp[-1].i == Y_JALR_OP))
		    r_type_inst (Y_JALR_OP, 31, yyvsp[0].i, 0);
		}
#line 3706 "parser_yacc.cpp"
    break;

  case 108: /* ASM_CODE: J_OPS DEST SRC1  */
#line 1441 "parser.y"
                {
		  if ((yyvsp[-2].i == Y_J_OP) || (yyvsp[-2].i == Y_JR_OP))
		    r_type_inst (Y_JR_OP, 0, yyvsp[0].i, 0);
		  else if ((yyvsp[-2].i == Y_JAL_OP) || (yyvsp[-2].i == Y_JALR_OP))
		    r_type_inst (Y_JALR_OP, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3717 "parser_yacc.cpp"
    break;

  case 109: /* ASM_CODE: B_OPS LABEL  */
#line 1450 "parser.y"
                {
		  i_type_inst_free ((yyvsp[-1].i == Y_BAL_POP ? Y_BGEZAL_OP : Y_BGEZ_OP),
				    0, 0, (imm_expr *)yyvsp[0].p);
		}
#line 3726 "parser_yacc.cpp"
    break;

  case 110: /* ASM_CODE: BINARYI_TRAP_OPS SRC1 IMM16  */
#line 1457 "parser.y"
                {
		  i_type_inst_free (yyvsp[-2].i, 0, yyvsp[-1].i, (imm_expr *)yyvsp[0].p);
		}
#line 3734 "parser_yacc.cpp"
    break;

  case 111: /* ASM_CODE: BINARY_TRAP_OPS SRC1 SRC2  */
#line 1463 "parser.y"
                {
		  r_type_inst (yyvsp[-2].i, 0, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3742 "parser_yacc.cpp"
    break;

  case 112: /* ASM_CODE: FP_MOVE_OPS F_DEST F_SRC1  */
#line 1469 "parser.y"
                {
		  r_co_type_inst (yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3750 "parser_yacc.cpp"
    break;

  case 113: /* ASM_CODE: FP_MOVE_OPS_REV2 F_DEST F_SRC1  */
#line 1475 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3758 "parser_yacc.cpp"
    break;

  case 114: /* ASM_CODE: MOVEC_OPS DEST SRC1 REG  */
#line 1481 "parser.y"
                {
		  r_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3766 "parser_yacc.cpp"
    break;

  case 115: /* ASM_CODE: MOVECC_OPS DEST SRC1 Y_INT  */
#line 1487 "parser.y"
                {
                    r_type_inst (yyvsp[-3].i,
                                 yyvsp[-2].i,
                                 yyvsp[-1].i,
                                 ((yyvsp[0].i & 0x7) << 2));
		}
#line 3777 "parser_yacc.cpp"
    break;

  case 116: /* ASM_CODE: FP_MOVEC_OPS F_DEST F_SRC1 REG  */
#line 1496 "parser.y"
                {
		  r_co_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3785 "parser_yacc.cpp"
    break;

  case 117: /* ASM_CODE: FP_MOVEC_OPS_REV2 F_DEST F_SRC1 REG  */
#line 1502 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3793 "parser_yacc.cpp"
    break;

  case 118: /* ASM_CODE: FP_MOVECC_OPS F_DEST F_SRC1  */
#line 1508 "parser.y"
                {
		  r_co_type_inst (yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, cc_to_rt (0, 0, 0));
		}
#line 3801 "parser_yacc.cpp"
    break;

  case 119: /* ASM_CODE: FP_MOVECC_OPS F_DEST F_SRC1 CC_REG  */
#line 1514 "parser.y"
                {
		  r_co_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, cc_to_rt (yyvsp[0].i, 0, 0));
		}
#line 3809 "parser_yacc.cpp"
    break;

  case 120: /* ASM_CODE: FP_MOVECC_OPS_REV2 F_DEST F_SRC1 CC_REG  */
#line 1520 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3817 "parser_yacc.cpp"
    break;

  case 121: /* ASM_CODE: MOVE_FROM_HILO_OPS REG  */
#line 1526 "parser.y"
                {
		  r_type_inst (yyvsp[-1].i, yyvsp[0].i, 0, 0);
		}
#line 3825 "parser_yacc.cpp"
    break;

  case 122: /* ASM_CODE: MOVE_TO_HILO_OPS REG  */
#line 1532 "parser.y"
                {
		  r_type_inst (yyvsp[-1].i, 0, yyvsp[0].i, 0);
		}
#line 3833 "parser_yacc.cpp"
    break;

  case 123: /* ASM_CODE: MOVE_COP_OPS REG COP_REG  */
#line 1539 "parser.y"
                {
		  if (yyvsp[-2].i == Y_MFC1_D_POP)
		    {
		      r_co_type_inst (Y_MFC1_OP, 0, yyvsp[0].i, yyvsp[-1].i);
		      r_co_type_inst (Y_MFC1_OP, 0, yyvsp[0].i + 1, yyvsp[-1].i + 1);
		    }
		  else if (yyvsp[-2].i == Y_MTC1_D_POP)
		    {
		      r_co_type_inst (Y_MTC1_OP, 0, yyvsp[0].i, yyvsp[-1].i);
		      r_co_type_inst (Y_MTC1_OP, 0, yyvsp[0].i + 1, yyvsp[-1].i + 1);
		    }
		  else
		    r_co_type_inst (yyvsp[-2].i, 0, yyvsp[0].i, yyvsp[-1].i);
		}
#line 3852 "parser_yacc.cpp"
    break;

  case 124: /* ASM_CODE: MOVE_COP_OPS_REV2 REG COP_REG  */
#line 1556 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3860 "parser_yacc.cpp"
    break;

  case 125: /* ASM_CODE: CTL_COP_OPS REG COP_REG  */
#line 1562 "parser.y"
                {
		  r_co_type_inst (yyvsp[-2].i, 0, yyvsp[0].i, yyvsp[-1].i);
		}
#line 3868 "parser_yacc.cpp"
    break;

  case 126: /* ASM_CODE: FP_UNARY_OPS F_DEST F_SRC2  */
#line 1568 "parser.y"
                {
		  r_co_type_inst (yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3876 "parser_yacc.cpp"
    break;

  case 127: /* ASM_CODE: FP_UNARY_OPS_REV2 F_DEST F_SRC2  */
#line 1574 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3884 "parser_yacc.cpp"
    break;

  case 128: /* ASM_CODE: FP_BINARY_OPS F_DEST F_SRC1 F_SRC2  */
#line 1580 "parser.y"
                {
		  r_co_type_inst (yyvsp[-3].i, yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i);
		}
#line 3892 "parser_yacc.cpp"
    break;

  case 129: /* ASM_CODE: FP_BINARY_OPS_REV2 F_DEST F_SRC1 F_SRC2  */
#line 1586 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3900 "parser_yacc.cpp"
    break;

  case 130: /* ASM_CODE: FP_TERNARY_OPS_REV2 F_DEST F_SRC1 F_SRC2 FP_REGISTER  */
#line 1592 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3908 "parser_yacc.cpp"
    break;

  case 131: /* ASM_CODE: FP_CMP_OPS F_SRC1 F_SRC2  */
#line 1598 "parser.y"
                {
		  r_cond_type_inst (yyvsp[-2].i, yyvsp[-1].i, yyvsp[0].i, 0);
		}
#line 3916 "parser_yacc.cpp"
    break;

  case 132: /* ASM_CODE: FP_CMP_OPS CC_REG F_SRC1 F_SRC2  */
#line 1604 "parser.y"
                {
		  r_cond_type_inst (yyvsp[-3].i, yyvsp[-1].i, yyvsp[0].i, yyvsp[-2].i);
		}
#line 3924 "parser_yacc.cpp"
    break;

  case 133: /* ASM_CODE: FP_CMP_OPS_REV2 F_SRC1 F_SRC2  */
#line 1610 "parser.y"
                {
		  mips32_r2_inst ();
		}
#line 3932 "parser_yacc.cpp"
    break;

  case 134: /* ASM_CODE: Y_COP2_OP IMM32  */
#line 1616 "parser.y"
                {
		  i_type_inst_free (yyvsp[-1].i, 0, 0, (imm_expr *)yyvsp[0].p);
		}
#line 3940 "parser_yacc.cpp"
    break;

  case 152: /* LOADFP_OPS: Y_L_D_POP  */
#line 1648 "parser.y"
                          { yyval.i = Y_LDC1_OP; }
#line 3946 "parser_yacc.cpp"
    break;

  case 153: /* LOADFP_OPS: Y_L_S_POP  */
#line 1649 "parser.y"
                          { yyval.i = Y_LWC1_OP; }
#line 3952 "parser_yacc.cpp"
    break;

  case 166: /* STOREC_OPS: Y_S_D_POP  */
#line 1668 "parser.y"
                          { yyval.i = Y_SDC1_OP; }
#line 3958 "parser_yacc.cpp"
    break;

  case 167: /* STOREC_OPS: Y_S_S_POP  */
#line 1669 "parser.y"
                          { yyval.i = Y_SWC1_OP; }
#line 3964 "parser_yacc.cpp"
    break;

  case 173: /* SYS_OPS: Y_RFE_OP  */
#line 1682 "parser.y"
                {
#ifdef MIPS1
			yywarn ("RFE should only be used when SPIM is compiled as a MIPS-I processor");
#endif
		}
#line 3974 "parser_yacc.cpp"
    break;

  case 183: /* NULLARY_OPS: Y_ERET_OP  */
#line 1705 "parser.y"
                {
#ifdef MIPS1
			yywarn ("ERET should only be used when SPIM is compiled as a MIPS32 processor");
#endif
		}
#line 3984 "parser_yacc.cpp"
    break;

  case 281: /* J_OPS: Y_JR_HB_OP  */
#line 1871 "parser.y"
                           { yywarn ("Warning:IPS32 Rev 2 '.HB' extension is not implemented and is ignored"); }
#line 3990 "parser_yacc.cpp"
    break;

  case 284: /* J_OPS: Y_JALR_HB_OP  */
#line 1874 "parser.y"
                             { yywarn ("Warning:IPS32 Rev 2 '.HB' extension is not implemented and is ignored"); }
#line 3996 "parser_yacc.cpp"
    break;

  case 457: /* ASM_DIRECTIVE: Y_ALIGN_DIR EXPR  */
#line 2106 "parser.y"
                {
		  align_data (yyvsp[0].i);
		}
#line 4004 "parser_yacc.cpp"
    break;

  case 458: /* $@4: %empty  */
#line 2110 "parser.y"
                            {null_term = false;}
#line 4010 "parser_yacc.cpp"
    break;

  case 459: /* ASM_DIRECTIVE: Y_ASCII_DIR $@4 STR_LST  */
#line 2111 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4019 "parser_yacc.cpp"
    break;

  case 460: /* $@5: %empty  */
#line 2116 "parser.y"
                             {null_term = true;}
#line 4025 "parser_yacc.cpp"
    break;

  case 461: /* ASM_DIRECTIVE: Y_ASCIIZ_DIR $@5 STR_LST  */
#line 2117 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4034 "parser_yacc.cpp"
    break;

  case 464: /* $@6: %empty  */
#line 2129 "parser.y"
                {store_op = store_byte;}
#line 4040 "parser_yacc.cpp"
    break;

  case 465: /* ASM_DIRECTIVE: Y_BYTE_DIR $@6 EXPR_LST  */
#line 2131 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4049 "parser_yacc.cpp"
    break;

  case 466: /* ASM_DIRECTIVE: Y_COMM_DIR ID EXPR  */
#line 2138 "parser.y"
                {
		  align_data (2);
		  if (lookup_label ((char*)yyvsp[-1].p)->addr == 0)
		  {
		    (void)record_label ((char*)yyvsp[-1].p, current_data_pc (), 1);
		    free ((char*)yyvsp[-1].p);
		  }
		  increment_data_pc (yyvsp[0].i);
		}
#line 4063 "parser_yacc.cpp"
    break;

  case 467: /* ASM_DIRECTIVE: Y_DATA_DIR  */
#line 2150 "parser.y"
                {user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		}
#line 4072 "parser_yacc.cpp"
    break;

  case 468: /* ASM_DIRECTIVE: Y_DATA_DIR Y_INT  */
#line 2156 "parser.y"
                {
		  user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		  set_data_pc (yyvsp[0].i);
		}
#line 4083 "parser_yacc.cpp"
    break;

  case 469: /* ASM_DIRECTIVE: Y_K_DATA_DIR  */
#line 2165 "parser.y"
                {
                    user_kernel_data_segment (true);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		}
#line 4093 "parser_yacc.cpp"
    break;

  case 470: /* ASM_DIRECTIVE: Y_K_DATA_DIR Y_INT  */
#line 2172 "parser.y"
                {
                    user_kernel_data_segment (true);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		  set_data_pc (yyvsp[0].i);
		}
#line 4104 "parser_yacc.cpp"
    break;

  case 471: /* $@7: %empty  */
#line 2181 "parser.y"
                {
		  store_fp_op = store_double;
		  if (data_dir) set_data_alignment (3);
		}
#line 4113 "parser_yacc.cpp"
    break;

  case 472: /* ASM_DIRECTIVE: Y_DOUBLE_DIR $@7 FP_EXPR_LST  */
#line 2186 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4122 "parser_yacc.cpp"
    break;

  case 478: /* ASM_DIRECTIVE: Y_EXTERN_DIR ID EXPR  */
#line 2204 "parser.y"
                {
		  extern_directive ((char*)yyvsp[-1].p, yyvsp[0].i);
		}
#line 4130 "parser_yacc.cpp"
    break;

  case 479: /* ASM_DIRECTIVE: Y_ERR_DIR  */
#line 2210 "parser.y"
                {
		  fatal_error ("File contains an .err directive\n");
		}
#line 4138 "parser_yacc.cpp"
    break;

  case 481: /* $@8: %empty  */
#line 2219 "parser.y"
                {
		  store_fp_op = store_float;
		  if (data_dir) set_data_alignment (2);
		}
#line 4147 "parser_yacc.cpp"
    break;

  case 482: /* ASM_DIRECTIVE: Y_FLOAT_DIR $@8 FP_EXPR_LST  */
#line 2224 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4156 "parser_yacc.cpp"
    break;

  case 485: /* ASM_DIRECTIVE: Y_GLOBAL_DIR ID  */
#line 2236 "parser.y"
                {
		  (void)make_label_global ((char*)yyvsp[0].p);
		  free ((char*)yyvsp[0].p);
		}
#line 4165 "parser_yacc.cpp"
    break;

  case 486: /* $@9: %empty  */
#line 2243 "parser.y"
                {
		  store_op = store_half;
		  if (data_dir) set_data_alignment (1);
		}
#line 4174 "parser_yacc.cpp"
    break;

  case 487: /* ASM_DIRECTIVE: Y_HALF_DIR $@9 EXPR_LST  */
#line 2248 "parser.y"
                {
		  if (text_dir)
		    yyerror ("Can't put data in text segment");
		}
#line 4183 "parser_yacc.cpp"
    break;

  case 488: /* ASM_DIRECTIVE: Y_LABEL_DIR ID  */
#line 2255 "parser.y"
                {
		  (void)record_label ((char*)yyvsp[0].p,
				      text_dir ? current_text_pc () : current_data_pc (),
				      1);
		  free ((char*)yyvsp[0].p);
		}
#line 4194 "parser_yacc.cpp"
    break;

  case 489: /* ASM_DIRECTIVE: Y_LCOMM_DIR ID EXPR  */
#line 2264 "parser.y"
                {
		  lcomm_directive ((char*)yyvsp[-1].p, yyvsp[0].i);
		}
#line 4202 "parser_yacc.cpp"
    break;

  case 495: /* ASM_DIRECTIVE: Y_REPEAT_DIR EXPR  */
#line 2282 "parser.y"
                {
		  yyerror ("Warning: repeat directive ignored");
		}
#line 4210 "parser_yacc.cpp"
    break;

  case 496: /* ASM_DIRECTIVE: Y_RDATA_DIR  */
#line 2288 "parser.y"
                {
		  user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		}
#line 4220 "parser_yacc.cpp"
    break;

  case 497: /* ASM_DIRECTIVE: Y_RDATA_DIR Y_INT  */
#line 2295 "parser.y"
                {
		  user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		  set_data_pc (yyvsp[0].i);
		}
#line 4231 "parser_yacc.cpp"
    break;

  case 498: /* ASM_DIRECTIVE: Y_SDATA_DIR  */
#line 2304 "parser.y"
                {
		  user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		}
#line 4241 "parser_yacc.cpp"
    break;

  case 499: /* ASM_DIRECTIVE: Y_SDATA_DIR Y_INT  */
#line 2311 "parser.y"
                {
		  user_kernel_data_segment (false);
		  data_dir = true; text_dir = false;
		  enable_data_alignment ();
		  set_data_pc (yyvsp[0].i);
		}
#line 4252 "parser_yacc.cpp"
    break;

  case 500: /* ASM_DIRECTIVE: Y_SET_DIR ID  */
#line 2320 "parser.y"
                {
		  if (streq ((char*)yyvsp[0].p, "noat"))
		    noat_flag = true;
		  else if (streq ((char*)yyvsp[0].p, "at"))
		    noat_flag = false;
		}
#line 4263 "parser_yacc.cpp"
    break;

  case 501: /* ASM_DIRECTIVE: Y_SPACE_DIR EXPR  */
#line 2329 "parser.y"
                {
		  if (data_dir)
		    increment_data_pc (yyvsp[0].i);
		  else if (text_dir)
		    increment_text_pc (yyvsp[0].i);
		}
#line 4274 "parser_yacc.cpp"
    break;

  case 502: /* ASM_DIRECTIVE: Y_STRUCT_DIR EXPR  */
#line 2338 "parser.y"
                {
		  yyerror ("Warning: struct directive ignored");
		}
#line 4282 "parser_yacc.cpp"
    break;

  case 503: /* ASM_DIRECTIVE: Y_TEXT_DIR  */
#line 2344 "parser.y"
                {
		  user_kernel_text_segment (false);
		  data_dir = false; text_dir = true;
		  enable_data_alignment ();
		}
#line 4292 "parser_yacc.cpp"
    break;

  case 504: /* ASM_DIRECTIVE: Y_TEXT_DIR Y_INT  */
#line 2351 "parser.y"
                {
		  user_kernel_text_segment (false);
		  data_dir = false; text_dir = true;
		  enable_data_alignment ();
		  set_text_pc (yyvsp[0].i);
		}
#line 4303 "parser_yacc.cpp"
    break;

  case 505: /* ASM_DIRECTIVE: Y_K_TEXT_DIR  */
#line 2360 "parser.y"
                {
		  user_kernel_text_segment (true);
		  data_dir = false; text_dir = true;
		  enable_data_alignment ();
		}
#line 4313 "parser_yacc.cpp"
    break;

  case 506: /* ASM_DIRECTIVE: Y_K_TEXT_DIR Y_INT  */
#line 2367 "parser.y"
                {
		  user_kernel_text_segment (true);
		  data_dir = false; text_dir = true;
		  enable_data_alignment ();
		  set_text_pc (yyvsp[0].i);
		}
#line 4324 "parser_yacc.cpp"
    break;

  case 509: /* $@10: %empty  */
#line 2381 "parser.y"
                {
		  store_op = store_word_data;
		  if (data_dir) set_data_alignment (2);
		}
#line 4333 "parser_yacc.cpp"
    break;

  case 511: /* $@11: %empty  */
#line 2391 "parser.y"
                {only_id = 1;}
#line 4339 "parser_yacc.cpp"
    break;

  case 512: /* ADDRESS: $@11 ADDR  */
#line 2391 "parser.y"
                                    {only_id = 0; yyval = yyvsp[0];}
#line 4345 "parser_yacc.cpp"
    break;

  case 513: /* ADDR: '(' REGISTER ')'  */
#line 2394 "parser.y"
                {
		  yyval.p = make_addr_expr (0, NULL, yyvsp[-1].i);
		}
#line 4353 "parser_yacc.cpp"
    break;

  case 514: /* ADDR: ABS_ADDR  */
#line 2399 "parser.y"
                {
		  yyval.p = make_addr_expr (yyvsp[0].i, NULL, 0);
		}
#line 4361 "parser_yacc.cpp"
    break;

  case 515: /* ADDR: ABS_ADDR '(' REGISTER ')'  */
#line 2404 "parser.y"
                {
		  yyval.p = make_addr_expr (yyvsp[-3].i, NULL, yyvsp[-1].i);
		}
#line 4369 "parser_yacc.cpp"
    break;

  case 516: /* ADDR: Y_ID  */
#line 2409 "parser.y"
                {
		  yyval.p = make_addr_expr (0, (char*)yyvsp[0].p, 0);
		  free ((char*)yyvsp[0].p);
		}
#line 4378 "parser_yacc.cpp"
    break;

  case 517: /* ADDR: Y_ID '(' REGISTER ')'  */
#line 2415 "parser.y"
                {
		  yyval.p = make_addr_expr (0, (char*)yyvsp[-3].p, yyvsp[-1].i);
		  free ((char*)yyvsp[-3].p);
		}
#line 4387 "parser_yacc.cpp"
    break;

  case 518: /* ADDR: Y_ID '+' ABS_ADDR  */
#line 2421 "parser.y"
                {
		  yyval.p = make_addr_expr (yyvsp[0].i, (char*)yyvsp[-2].p, 0);
		  free ((char*)yyvsp[-2].p);
		}
#line 4396 "parser_yacc.cpp"
    break;

  case 519: /* ADDR: ABS_ADDR '+' ID  */
#line 2427 "parser.y"
                {
		  yyval.p = make_addr_expr (yyvsp[-2].i, (char*)yyvsp[0].p, 0);
		}
#line 4404 "parser_yacc.cpp"
    break;

  case 520: /* ADDR: Y_ID '-' ABS_ADDR  */
#line 2432 "parser.y"
                {
		  yyval.p = make_addr_expr (- yyvsp[0].i, (char*)yyvsp[-2].p, 0);
		  free ((char*)yyvsp[-2].p);
		}
#line 4413 "parser_yacc.cpp"
    break;

  case 521: /* ADDR: Y_ID '+' ABS_ADDR '(' REGISTER ')'  */
#line 2438 "parser.y"
                {
		  yyval.p = make_addr_expr (yyvsp[-3].i, (char*)yyvsp[-5].p, yyvsp[-1].i);
		  free ((char*)yyvsp[-5].p);
		}
#line 4422 "parser_yacc.cpp"
    break;

  case 522: /* ADDR: Y_ID '-' ABS_ADDR '(' REGISTER ')'  */
#line 2444 "parser.y"
                {
		  yyval.p = make_addr_expr (- yyvsp[-3].i, (char*)yyvsp[-5].p, yyvsp[-1].i);
		  free ((char*)yyvsp[-5].p);
		}
#line 4431 "parser_yacc.cpp"
    break;

  case 523: /* $@12: %empty  */
#line 2451 "parser.y"
                {only_id = 1;}
#line 4437 "parser_yacc.cpp"
    break;

  case 524: /* BR_IMM32: $@12 IMM32  */
#line 2451 "parser.y"
                                     {only_id = 0; yyval = yyvsp[0];}
#line 4443 "parser_yacc.cpp"
    break;

  case 525: /* IMM16: IMM32  */
#line 2454 "parser.y"
                {
                  check_imm_range ((imm_expr*)yyvsp[0].p, IMM_MIN, IMM_MAX);
		  yyval = yyvsp[0];
		}
#line 4452 "parser_yacc.cpp"
    break;

  case 526: /* UIMM16: IMM32  */
#line 2460 "parser.y"
                {
                  check_uimm_range ((imm_expr*)yyvsp[0].p, UIMM_MIN, UIMM_MAX);
		  yyval = yyvsp[0];
		}
#line 4461 "parser_yacc.cpp"
    break;

  case 527: /* IMM32: ABS_ADDR  */
#line 2467 "parser.y"
                {
		  yyval.p = make_imm_expr (yyvsp[0].i, NULL, false);
		}
#line 4469 "parser_yacc.cpp"
    break;

  case 528: /* IMM32: '(' ABS_ADDR ')' '>' '>' Y_INT  */
#line 2472 "parser.y"
                {
		  yyval.p = make_imm_expr (yyvsp[-4].i >> yyvsp[0].i, NULL, false);
		}
#line 4477 "parser_yacc.cpp"
    break;

  case 529: /* IMM32: ID  */
#line 2477 "parser.y"
                {
		  yyval.p = make_imm_expr (0, (char*)yyvsp[0].p, false);
		}
#line 4485 "parser_yacc.cpp"
    break;

  case 530: /* IMM32: Y_ID '+' ABS_ADDR  */
#line 2482 "parser.y"
                {
		  yyval.p = make_imm_expr (yyvsp[0].i, (char*)yyvsp[-2].p, false);
		  free ((char*)yyvsp[-2].p);
		}
#line 4494 "parser_yacc.cpp"
    break;

  case 531: /* IMM32: Y_ID '-' ABS_ADDR  */
#line 2488 "parser.y"
                {
		  yyval.p = make_imm_expr (- yyvsp[0].i, (char*)yyvsp[-2].p, false);
		  free ((char*)yyvsp[-2].p);
		}
#line 4503 "parser_yacc.cpp"
    break;

  case 533: /* ABS_ADDR: Y_INT '+' Y_INT  */
#line 2498 "parser.y"
                {yyval.i = yyvsp[-2].i + yyvsp[0].i;}
#line 4509 "parser_yacc.cpp"
    break;

  case 534: /* ABS_ADDR: Y_INT Y_INT  */
#line 2501 "parser.y"
                {
		  /* This is actually: Y_INT '-' Y_INT, since the binary
		     subtract operator gets scanned as a unary negation
		     operator. */
		  if (yyvsp[0].i >= 0) yyerror ("Syntax error");
		  yyval.i = yyvsp[-1].i - -yyvsp[0].i;
		}
#line 4521 "parser_yacc.cpp"
    break;

  case 539: /* REGISTER: Y_REG  */
#line 2519 "parser.y"
                {
		  if (yyvsp[0].i < 0 || yyvsp[0].i > 31)
		    yyerror ("Register number out of range");
		  if (yyvsp[0].i == 1 && !bare_machine && !noat_flag)
		    yyerror ("Register 1 is reserved for assembler");
		  yyval = yyvsp[0];
		}
#line 4533 "parser_yacc.cpp"
    break;

  case 543: /* FP_REGISTER: Y_FP_REG  */
#line 2534 "parser.y"
                {
		  if (yyvsp[0].i < 0 || yyvsp[0].i > 31)
		    yyerror ("FP register number out of range");
		  yyval = yyvsp[0];
		}
#line 4543 "parser_yacc.cpp"
    break;

  case 544: /* CC_REG: Y_INT  */
#line 2542 "parser.y"
                {
		  if (yyvsp[0].i < 0 || yyvsp[0].i > 7)
		    yyerror ("CC register number out of range");
		  yyval = yyvsp[0];
		}
#line 4553 "parser_yacc.cpp"
    break;

  case 547: /* LABEL: ID  */
#line 2557 "parser.y"
                {
		  yyval.p = make_imm_expr (-(int)current_text_pc (), (char*)yyvsp[0].p, true);
		}
#line 4561 "parser_yacc.cpp"
    break;

  case 550: /* STR: Y_STR  */
#line 2568 "parser.y"
                {
		  store_string ((char*)yyvsp[0].p, strlen((char*)yyvsp[0].p), null_term);
		  free ((char*)yyvsp[0].p);
		}
#line 4570 "parser_yacc.cpp"
    break;

  case 551: /* STR: Y_STR ':' Y_INT  */
#line 2573 "parser.y"
                {
		  int i;

		  for (i = 0; i < yyvsp[0].i; i ++)
		    store_string ((char*)yyvsp[-2].p, strlen((char*)yyvsp[-2].p), null_term);
		  free ((char*)yyvsp[-2].p);
		}
#line 4582 "parser_yacc.cpp"
    break;

  case 552: /* $@13: %empty  */
#line 2583 "parser.y"
                {only_id = 1;}
#line 4588 "parser_yacc.cpp"
    break;

  case 553: /* EXPRESSION: $@13 EXPR  */
#line 2583 "parser.y"
                                    {only_id = 0; yyval = yyvsp[0];}
#line 4594 "parser_yacc.cpp"
    break;

  case 555: /* EXPR: EXPR '+' TRM  */
#line 2589 "parser.y"
                { yyval.i =  yyvsp[-2].i + yyvsp[0].i; }
#line 4600 "parser_yacc.cpp"
    break;

  case 556: /* EXPR: EXPR '-' TRM  */
#line 2592 "parser.y"
                { yyval.i =  yyvsp[-2].i - yyvsp[0].i; }
#line 4606 "parser_yacc.cpp"
    break;

  case 558: /* TRM: TRM '*' FACTOR  */
#line 2599 "parser.y"
                { yyval.i = yyvsp[-2].i * yyvsp[0].i; }
#line 4612 "parser_yacc.cpp"
    break;

  case 559: /* TRM: TRM '/' FACTOR  */
#line 2602 "parser.y"
                { yyval.i = yyvsp[-2].i / yyvsp[0].i; }
#line 4618 "parser_yacc.cpp"
    break;

  case 561: /* FACTOR: '(' EXPR ')'  */
#line 2608 "parser.y"
                { yyval.i = yyvsp[-1].i; }
#line 4624 "parser_yacc.cpp"
    break;

  case 562: /* FACTOR: ID  */
#line 2611 "parser.y"
                {
		  label *l = lookup_label ((char*)yyvsp[0].p);
  		  if (l->addr == 0)
                    {
                      record_data_uses_symbol (current_data_pc (), l);
                      yyval.p = NULL;
                    }
                  else
                    yyval.i = l->addr;
		}
#line 4639 "parser_yacc.cpp"
    break;

  case 563: /* EXPR_LST: EXPR_LST EXPRESSION  */
#line 2624 "parser.y"
                {
		  store_op (yyvsp[0].i);
		}
#line 4647 "parser_yacc.cpp"
    break;

  case 564: /* EXPR_LST: EXPRESSION  */
#line 2628 "parser.y"
                {
		  store_op (yyvsp[0].i);
		}
#line 4655 "parser_yacc.cpp"
    break;

  case 565: /* EXPR_LST: EXPRESSION ':' EXPR  */
#line 2632 "parser.y"
                {
		  int i;

		  for (i = 0; i < yyvsp[0].i; i ++)
		    store_op (yyvsp[-2].i);
		}
#line 4666 "parser_yacc.cpp"
    break;

  case 566: /* FP_EXPR_LST: FP_EXPR_LST Y_FP  */
#line 2642 "parser.y"
                {
		  store_fp_op ((double*)yyvsp[0].p);
		}
#line 4674 "parser_yacc.cpp"
    break;

  case 567: /* FP_EXPR_LST: Y_FP  */
#line 2646 "parser.y"
                {
		  store_fp_op ((double*)yyvsp[0].p);
		}
#line 4682 "parser_yacc.cpp"
    break;

  case 568: /* $@14: %empty  */
#line 2652 "parser.y"
                {only_id = 1;}
#line 4688 "parser_yacc.cpp"
    break;

  case 569: /* OPTIONAL_ID: $@14 OPT_ID  */
#line 2652 "parser.y"
                                      {only_id = 0; yyval = yyvsp[0];}
#line 4694 "parser_yacc.cpp"
    break;

  case 571: /* OPT_ID: %empty  */
#line 2655 "parser.y"
                {yyval.p = (void*)NULL;}
#line 4700 "parser_yacc.cpp"
    break;

  case 572: /* $@15: %empty  */
#line 2659 "parser.y"
                {only_id = 1;}
#line 4706 "parser_yacc.cpp"
    break;

  case 573: /* ID: $@15 Y_ID  */
#line 2659 "parser.y"
                                    {only_id = 0; yyval = yyvsp[0];}
#line 4712 "parser_yacc.cpp"
    break;


#line 4716 "parser_yacc.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturn;
#endif


/*-------------------------------------------------------.
| yyreturn -- parsing is finished, clean up and return.  |
`-------------------------------------------------------*/
yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 2662 "parser.y"


/* Maintain and update the address of labels for the current line. */

void
fix_current_label_address (mem_addr new_addr)
{
  label_list *l;

  for (l = this_line_labels; l != NULL; l = l->tail)
    {
      l->head->addr = new_addr;
    }
  clear_labels ();
}


static label_list *
cons_label (label *head, label_list *tail)
{
  label_list *c = (label_list *) malloc (sizeof (label_list));

  c->head = head;
  c->tail = tail;
  return (c);
}


static void
clear_labels ()
{
  label_list *n;

  for ( ; this_line_labels != NULL; this_line_labels = n)
    {
      resolve_label_uses (this_line_labels->head);
      n = this_line_labels->tail;
      free (this_line_labels);
    }
    this_line_labels = NULL;
}


/* Operations on op codes. */

int
op_to_imm_op (int opcode)
{
  switch (opcode)
    {
    case Y_ADD_OP: return (Y_ADDI_OP);
    case Y_ADDU_OP: return (Y_ADDIU_OP);
    case Y_AND_OP: return (Y_ANDI_OP);
    case Y_OR_OP: return (Y_ORI_OP);
    case Y_XOR_OP: return (Y_XORI_OP);
    case Y_SLT_OP: return (Y_SLTI_OP);
    case Y_SLTU_OP: return (Y_SLTIU_OP);
    case Y_SLLV_OP: return (Y_SLL_OP);
    case Y_SRAV_OP: return (Y_SRA_OP);
    case Y_SRLV_OP: return (Y_SRL_OP);
    default: fatal_error ("Can't convert op to immediate op\n"); return (0);
    }
}


int
imm_op_to_op (int opcode)
{
  switch (opcode)
    {
    case Y_ADDI_OP: return (Y_ADD_OP);
    case Y_ADDIU_OP: return (Y_ADDU_OP);
    case Y_ANDI_OP: return (Y_AND_OP);
    case Y_ORI_OP: return (Y_OR_OP);
    case Y_XORI_OP: return (Y_XOR_OP);
    case Y_SLTI_OP: return (Y_SLT_OP);
    case Y_SLTIU_OP: return (Y_SLTU_OP);
    case Y_J_OP: return (Y_JR_OP);
    case Y_LUI_OP: return (Y_ADDU_OP);
    case Y_SLL_OP: return (Y_SLLV_OP);
    case Y_SRA_OP: return (Y_SRAV_OP);
    case Y_SRL_OP: return (Y_SRLV_OP);
    default: fatal_error ("Can't convert immediate op to op\n"); return (0);
    }
}


static void
nop_inst ()
{
  r_type_inst (Y_SLL_OP, 0, 0, 0); /* = 0 */
}


static void
trap_inst ()
{
  r_type_inst (Y_BREAK_OP, 0, 0, 0);
}


static imm_expr *
branch_offset (int n_inst)
{
  return (const_imm_expr (n_inst << 2)); /* Later shifted right 2 places */
}


static void
div_inst (int op, int rd, int rs, int rt, int const_divisor)
{
  if (rd != 0 && !const_divisor)
    {
      i_type_inst_free (Y_BNE_OP, 0, rt, branch_offset (2));
      trap_inst ();
    }

  if (op == Y_DIV_OP || op == Y_REM_POP)
    r_type_inst (Y_DIV_OP, 0, rs, rt);
  else
    r_type_inst (Y_DIVU_OP, 0, rs, rt);

  if (rd != 0)
    {
      if (op == Y_DIV_OP || op == Y_DIVU_OP)
	/* Quotient */
	r_type_inst (Y_MFLO_OP, rd, 0, 0);
      else
	/* Remainder */
	r_type_inst (Y_MFHI_OP, rd, 0, 0);
    }
}


static void
mult_inst (int op, int rd, int rs, int rt)
{
  if (op == Y_MULOU_POP)
    r_type_inst (Y_MULTU_OP, 0, rs, rt);
  else
    r_type_inst (Y_MULT_OP, 0, rs, rt);
  if (op == Y_MULOU_POP && rd != 0)
    {
      r_type_inst (Y_MFHI_OP, 1, 0, 0);	/* Use $at */
      i_type_inst_free (Y_BEQ_OP, 0, 1, branch_offset (2));
      trap_inst ();
    }
  else if (op == Y_MULO_POP && rd != 0)
    {
      r_type_inst (Y_MFHI_OP, 1, 0, 0); /* use $at */
      r_type_inst (Y_MFLO_OP, rd, 0, 0);
      r_sh_type_inst (Y_SRA_OP, rd, rd, 31);
      i_type_inst_free (Y_BEQ_OP, rd, 1, branch_offset (2));
      trap_inst ();
    }
  if (rd != 0)
    r_type_inst (Y_MFLO_OP, rd, 0, 0);
}


static void
set_le_inst (int op, int rd, int rs, int rt)
{
  i_type_inst_free (Y_BNE_OP, rs, rt, branch_offset (3));
  i_type_inst_free (Y_ORI_OP, rd, 0, const_imm_expr (1));
  i_type_inst_free (Y_BEQ_OP, 0, 0, branch_offset (2));
  r_type_inst ((op == Y_SLE_POP ? Y_SLT_OP : Y_SLTU_OP), rd, rs, rt);
}


static void
set_gt_inst (int op, int rd, int rs, int rt)
{
  r_type_inst (op == Y_SGT_POP ? Y_SLT_OP : Y_SLTU_OP, rd, rt, rs);
}


static void
set_ge_inst (int op, int rd, int rs, int rt)
{
  i_type_inst_free (Y_BNE_OP, rs, rt, branch_offset (3));
  i_type_inst_free (Y_ORI_OP, rd, 0, const_imm_expr (1));
  i_type_inst_free (Y_BEQ_OP, 0, 0, branch_offset (2));
  r_type_inst (op == Y_SGE_POP ? Y_SLT_OP : Y_SLTU_OP, rd, rt, rs);
}


static void
set_eq_inst (int op, int rd, int rs, int rt)
{
  imm_expr *if_eq, *if_neq;

  if (op == Y_SEQ_POP)
    if_eq = const_imm_expr (1), if_neq = const_imm_expr (0);
  else
    if_eq = const_imm_expr (0), if_neq = const_imm_expr (1);

  i_type_inst_free (Y_BEQ_OP, rs, rt, branch_offset (3));
  /* RD <- 0 (if not equal) */
  i_type_inst_free (Y_ORI_OP, rd, 0, if_neq);
  i_type_inst_free (Y_BEQ_OP, 0, 0, branch_offset (2)); /* Branch always */
  /* RD <- 1 */
  i_type_inst_free (Y_ORI_OP, rd, 0, if_eq);
}


/* Store the value either as a datum or instruction. */

static void
store_word_data (int value)
{
  if (data_dir)
    store_word (value);
  else if (text_dir)
    store_instruction (inst_decode (value));
}



void
initialize_parser (char *file_name)
{
  input_file_name = file_name;
  only_id = 0;
  data_dir = false;
  text_dir = true;
}


static void
check_imm_range (imm_expr* expr, int32 min, int32 max)
{
  if (expr->symbol == NULL || SYMBOL_IS_DEFINED (expr->symbol))
    {
      /* If expression can be evaluated, compare its value against the limits
	 and complain if the value is out of bounds. */
      int32 value = eval_imm_expr (expr);

      if (value < min || max < value)
	{
	  char str[200];
	  sprintf (str, "immediate value (%d) out of range (%d .. %d)",
		   value, min, max);
	  yywarn (str);
	}
    }
}


static void
check_uimm_range (imm_expr* expr, uint32 min, uint32 max)
{
  if (expr->symbol == NULL || SYMBOL_IS_DEFINED (expr->symbol))
    {
      /* If expression can be evaluated, compare its value against the limits
	     and complain if the value is out of bounds. */
      uint32 value = (uint32)eval_imm_expr (expr);

      if (value < min || max < value)
	{
	  char str[200];
	  sprintf (str, "immediate value (%d) out of range (%d .. %d)",
		   (int32)value, (int32)min, (int32)max);
	  yywarn (str);
	}
    }
}

void
yyerror (char *s)
{
  parse_error_occurred = true;
  clear_labels ();
  yywarn (s);
}


void
yywarn (char *s)
{
  error ("spim: (parser) %s on line %d of file %s\n%s", s, line_no, input_file_name, erroneous_line ());
}


static void
mips32_r2_inst ()
{
	yyerror ("Warning: MIPS32 Rev 2 instruction is not implemented. Instruction ignored.");
}


static int
cc_to_rt (int cc, int nd, int tf)
{
  return (cc << 2) | (nd << 1) | tf;
}
