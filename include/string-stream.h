/* SPIM S20 MIPS simulator.
  Append-only output stream convertable to a string.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef STRING_STREAM_H
#define STRING_STREAM_H

#include <stdio.h>

/* Backed by POSIX open_memstream (POSIX.1-2008): a FILE* whose writes
   grow a malloc'd, NUL-terminated buffer.  Zero-initialized (static)
   str_streams work without an explicit ss_init — every entry point
   lazily creates the stream on first use. */
typedef struct str_stm {
  FILE* stream; /* nullptr until first use */
  char* buf;    /* owned by the stream until ss_take_string */
  size_t size;  /* bytes written, updated on flush */
} str_stream;

void ss_init(str_stream* ss);
void ss_clear(str_stream* ss);
void ss_erase(str_stream* ss, int n);
int ss_length(str_stream* ss);
[[nodiscard]] char* ss_to_string(str_stream* ss);
[[nodiscard]] char* ss_take_string(str_stream* ss);
void ss_printf(str_stream* ss, char* fmt, ...);

#endif
