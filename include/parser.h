/* SPIM S20 MIPS simulator.
   Interface to parser for instructions and assembler directives.

   Copyright (c) 1990-2010, James R. Larus.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

   Neither the name of the James R. Larus nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

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
