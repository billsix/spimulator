/* SPIM S20 MIPS simulator.
   Interface to parser for instructions and assembler directives.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef PARSER_H
#define PARSER_H

#include "spim.h"

/* Exported functions. */

#include <stdio.h>

/* Parser execution mode.  Default is PARSE_DIRECT (syntax-directed
   translation — the parser calls action helpers like r_type_inst /
   store_word directly while parsing, committing to memory inline).
   PARSE_AST routes the same actions through an AST: the parser
   builds a tree, then emit_ast() walks it calling the same action
   helpers.  Phase 2d.  Set via the -parser= command-line flag. */
typedef enum {
  PARSE_DIRECT = 0,   /* default — current SDT behavior */
  PARSE_AST    = 1,
} parse_mode_t;

/* Set before parser_init / parse_file.  Defaults to PARSE_DIRECT. */
void parser_set_mode(parse_mode_t mode);
parse_mode_t parser_get_mode(void);

/* If true, after a successful parse_file in PARSE_AST mode, dump the
   AST to ast_print_out (or stderr if null) BEFORE emitting any code.
   Used by -print-ast.  Has no effect in PARSE_DIRECT mode. */
void parser_set_print_ast(bool on, FILE* out);

/* If true, after printing the AST (if -print-ast was set), skip the
   emit phase entirely.  Lets students see the tree without spim
   committing anything to memory.  Used by -print-ast. */
void parser_set_print_ast_only(bool on);
bool parser_get_print_ast_only(void);

/* Parse the file currently bound to the scanner.  Returns the number
   of parse errors encountered (0 on success).  Drives the scanner
   until EOF.  Sets parse_errors_seen as a side effect.  In PARSE_AST
   mode also emits the resulting AST unless -print-ast-only is set. */
[[nodiscard]] int parse_file(void);

/* Initialize scanner + parser state for a fresh assembly file.
   Call before parse_file(). */
void parser_init(FILE* in, char* file_name);

void fix_current_label_address(mem_addr new_addr);
int imm_op_to_op(int opcode);
void parse_error(char* s);

/* Name of the current input file (set by parser_init).  May be null
   when assembling from a non-file stream. */
char* input_file_name_get(void);

/* Exported Variables: */

extern bool data_dir; /* => item in data segment */

extern bool text_dir; /* => item in text segment */

extern bool parse_error_occurred; /* => parse resulted in error */

extern int parse_errors_seen; /* cumulative parser errors across the file */

#endif
