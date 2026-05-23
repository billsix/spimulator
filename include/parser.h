/* SPIM S20 MIPS simulator.
   Interface to parser for instructions and assembler directives.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef PARSER_H
#define PARSER_H

#include "spim.h"
#include "inst.h" /* for imm_expr in the emit_* signatures */

/* Exported functions. */

#include <stdio.h>

/* Parser execution mode.  Two paths are kept alive:
     PARSE_DIRECT — syntax-directed translation.  The parser calls
       action helpers like r_type_inst / store_word inline while
       parsing, committing to memory as each statement is seen.  No
       AST is built.  This is the **default** — what every spim
       invocation has used historically.
     PARSE_AST — the parser builds an abstract syntax tree first,
       then emit_ast() walks the tree calling the same action helpers
       in source order.  Enables -print-ast / -show-expansion /
       -print-ast-json.
   Both produce byte-identical memory contents for the same input.
   Set via the -parser= command-line flag (or implicitly by any of
   the AST-inspecting flags). */
typedef enum : uint8_t {
  PARSE_DIRECT = 0,
  PARSE_AST = 1,
} parse_mode_t;

/* Set before parser_init / parse_file.  Defaults to PARSE_DIRECT
   (SDT).  Auto-flipped to PARSE_AST when -print-ast,
   -show-expansion, or -print-ast-json is on the command line. */
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

/* If true, after the parse completes in PARSE_AST mode, dump just the
   pseudo-op wrappers (each AST_PSEUDO plus its expanded children) to
   ast_print_out.  Skips the rest of the tree.  Used by
   -show-expansion to give a focused view of what each pseudo-op
   actually becomes in the parser. */
void parser_set_show_expansion(bool on, FILE* out);

/* If true, after parse_file completes in PARSE_AST mode, dump the AST
   as JSON to `out` (default stderr).  Output is one line per file.
   Drives external tooling — GUI scrubber, listing-to-html converter,
   static-analysis exercise framework. */
void parser_set_print_ast_json(bool on, FILE* out);

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

/* Emit-action dispatch helpers.  Every instruction-emitting call in
   parser.c and pseudo_op.c routes through these so the parser can
   pick between inline emit (PARSE_DIRECT) and AST construction
   (PARSE_AST) without per-call-site branching.  Signatures mirror
   the underlying action helpers:
     - emit_i: caller retains ownership of `imm` (mirrors i_type_inst).
     - emit_i_free: caller transfers ownership (mirrors i_type_inst_free).
     - emit_j: caller retains ownership. */
void emit_r(int op, int rd, int rs, int rt);
void emit_r_shift(int op, int rd, int rt, int shamt);
void emit_i(int op, int rt, int rs, imm_expr* imm);
void emit_i_free(int op, int rt, int rs, imm_expr* imm);
void emit_j(int op, imm_expr* target);
void emit_fp_r(int op, int fd, int fs, int ft);
void emit_fp_compare(int op, int fs, int ft, int cc);

/* Exported Variables: */

extern bool data_dir; /* => item in data segment */

extern bool text_dir; /* => item in text segment */

extern bool parse_error_occurred; /* => parse resulted in error */

extern int parse_errors_seen; /* cumulative parser errors across the file */

#endif
