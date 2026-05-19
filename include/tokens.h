/* Y_* token enum for the hand-written parser path.
   Replaces the bison-generated parser_yacc.h that used to live in the
   build directory.  All runtime code (sym-tbl.c, inst.c, run.c,
   explain.c, etc.) and the hand parser/scanner include this header
   for the Y_* symbol set.

   The 381 keyword tokens come from op.h via its X-macro pattern; the
   handful of structural/syntactic tokens (Y_INT, Y_ID, Y_REG, ...)
   are listed explicitly below.

   Distinct integer values are all that's required — these tokens
   never cross a parser/scanner ABI boundary now that bison is gone,
   so the specific numeric assignments don't matter. */

#ifndef TOKENS_H
#define TOKENS_H

enum {
  /* Structural / literal tokens (parser.y's %token directives that
     don't have a corresponding op.h entry).  Start at 256 so they
     don't collide with single-character punctuation tokens like
     '+', ',', '(' which the scanner returns as their own ASCII value. */
  Y_EOF = 256,
  Y_NL,
  Y_INT,
  Y_ID,
  Y_REG,
  Y_FP_REG,
  Y_STR,
  Y_FP,

#define OP(_name, sym, _kind, _opcode) sym,
#include "op.h"
#undef OP
};

#endif /* TOKENS_H */
