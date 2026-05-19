/* SPIM S20 MIPS simulator.
   Hand-written parser — public API.

   Coexists with the flex+bison parser during Phases 2-4 of the
   migration.  Selected at runtime by the `-parser=hand` flag.

   The `hp_` prefix is "hand-written parser".  After Phase 5
   when bison is removed, the prefix is dropped and these
   functions take over as the canonical names.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause (same terms as the rest of spim).
*/

#ifndef HP_PARSER_H
#define HP_PARSER_H

#include <stdio.h>

/* Parse the file currently bound to the hand-written scanner.
   Returns the number of parse errors encountered (0 on success).
   Drives the scanner until EOF.  Sets parse_errors_seen as a
   side effect (consistent with the bison path). */
int hp_parse_file(void);

/* Initialize scanner + parser state for a fresh assembly file.
   Mirrors the bison path's `initialize_scanner` +
   `initialize_parser` pair.  Call before hp_parse_file(). */
void hp_initialize_parser(FILE* in, char* file_name);

#endif /* HP_PARSER_H */
