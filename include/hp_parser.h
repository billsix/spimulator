/* SPIM S20 MIPS simulator.
   Hand-written parser — public API.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause (same terms as the rest of spim).
*/

#ifndef HP_PARSER_H
#define HP_PARSER_H

#include <stdio.h>

/* Parse the file currently bound to the scanner.  Returns the number
   of parse errors encountered (0 on success).  Drives the scanner
   until EOF.  Sets parse_errors_seen as a side effect. */
int hp_parse_file(void);

/* Initialize scanner + parser state for a fresh assembly file.
   Call before hp_parse_file(). */
void hp_initialize_parser(FILE* in, char* file_name);

#endif /* HP_PARSER_H */
