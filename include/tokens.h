/* Y_* token enum shared by the scanner, parser, and runtime.

   The 381 keyword tokens come from op.h via its X-macro pattern; the
   handful of structural/syntactic tokens (Y_INT, Y_ID, Y_REG, ...)
   are listed explicitly below.

   Distinct integer values are all that's required; the specific
   numeric assignments don't matter to any external ABI. */

#ifndef TOKENS_H
#define TOKENS_H

enum {
  /* Structural / literal tokens.  Start at 256 so they don't collide
     with single-character punctuation tokens like '+', ',', '(' which
     the scanner returns as their own ASCII value. */
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
