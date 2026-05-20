/* SPIM S20 MIPS simulator.
   Interface to parser for instructions and assembler directives.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef PARSER_H
#define PARSER_H

#include "spim.h"

/* Exported functions. */

#include <stdio.h>

/* Parse the file currently bound to the scanner.  Returns the number
   of parse errors encountered (0 on success).  Drives the scanner
   until EOF.  Sets parse_errors_seen as a side effect. */
int parse_file(void);

/* Initialize scanner + parser state for a fresh assembly file.
   Call before parse_file(). */
void parser_init(FILE* in, char* file_name);

void fix_current_label_address(mem_addr new_addr);
int imm_op_to_op(int opcode);
void parse_error(char* s);

/* Exported Variables: */

extern bool data_dir; /* => item in data segment */

extern bool text_dir; /* => item in text segment */

extern bool parse_error_occurred; /* => parse resulted in error */

extern int parse_errors_seen; /* cumulative parser errors across the file */

#endif
