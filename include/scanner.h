/* SPIM S20 MIPS simulator.
   Interface to lexical scanner.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef SCANNER_H
#define SCANNER_H

#include "spim.h"

/* Exported functions. */

void scanner_init(FILE* in_file);
char* erroneous_line(void);
void scanner_start_line(void);
int register_name_to_number(char* name);
char* source_line(void);

/* Exported Variables: */

typedef intptr_union scan_value_t;
extern scan_value_t
    scan_value; /* Value of the last token returned by the scanner. */

extern int line_no; /* Line number in input file*/

#endif
