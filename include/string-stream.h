/* SPIM S20 MIPS simulator.
  Append-only output stream convertable to a string.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef STRING_STREAM_H
#define STRING_STREAM_H

#include <stdio.h>

/* Backed by POSIX open_memstream (POSIX.1-2008): a FILE* whose writes
   grow a malloc'd, NUL-terminated buffer.  Zero-initialized (static)
   str_streams work without an explicit stream_initialize — every entry point
   lazily creates the stream on first use. */
typedef struct str_stm {
  FILE* stream; /* nullptr until first use */
  char* buffer;    /* owned by the stream until stream_take_string */
  size_t size;  /* bytes written, updated on flush */
} str_stream;

void stream_initialize(str_stream* stream);
void stream_clear(str_stream* stream);
void stream_erase(str_stream* stream, int n);
int stream_length(str_stream* stream);
[[nodiscard]] char* stream_to_string(str_stream* stream);
[[nodiscard]] char* stream_take_string(str_stream* stream);
[[gnu::format(printf, 2, 3)]] void stream_printf(str_stream* stream, const char* format,
                                             ...);

#endif
