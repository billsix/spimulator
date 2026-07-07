/* SPIM S20 MIPS simulator.
  Append-only output stream convertable to a string.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "spim.h"
#include "string-stream.h"

/* Thin wrappers over POSIX open_memstream, which standardized (in
   POSIX.1-2008) exactly what the previous hand-rolled implementation
   did: an in-memory FILE* backed by a growing, NUL-terminated
   malloc'd buffer.  fflush publishes the buffer/size; fclose hands
   the buffer to the caller. */

static void ss_lazy_init(str_stream* ss) {
  if (ss->stream == nullptr) {
    ss->buf = nullptr;
    ss->size = 0;
    ss->stream = open_memstream(&ss->buf, &ss->size);
    if (ss->stream == nullptr) fatal_error("open_memstream failed\n");
  }
}

void ss_init(str_stream* ss) {
  /* Historical contract: unconditionally start a fresh stream.  Local
     (stack) str_streams are ss_init'ed without being zeroed first, so
     no prior field value may be read here. */
  ss->stream = nullptr;
  ss_lazy_init(ss);
}

void ss_clear(str_stream* ss) {
  ss_lazy_init(ss);
  rewind(ss->stream);
}

void ss_erase(str_stream* ss, int n) {
  ss_lazy_init(ss);
  long pos = ftell(ss->stream);
  if (pos < 0) pos = 0;
  fseek(ss->stream, (pos > n) ? pos - n : 0, SEEK_SET);
}

int ss_length(str_stream* ss) {
  ss_lazy_init(ss);
  fflush(ss->stream);
  return (int)ss->size;
}

/* Borrow the current contents.  The pointer remains owned by the
   stream: valid until the next ss_* call, do not free.  fflush
   NUL-terminates the buffer at the current position. */

char* ss_to_string(str_stream* ss) {
  ss_lazy_init(ss);
  fflush(ss->stream);
  return ss->buf;
}

/* Take ownership of the contents: closes the stream and returns the
   malloc'd, NUL-terminated buffer.  The caller frees it.  The
   str_stream itself is reset and may be reused (a fresh stream is
   created on its next use). */

char* ss_take_string(str_stream* ss) {
  ss_lazy_init(ss);
  fclose(ss->stream);
  ss->stream = nullptr;
  char* buf = ss->buf;
  ss->buf = nullptr;
  ss->size = 0;
  return buf;
}

void ss_printf(str_stream* ss, char* fmt, ...) {
  va_list args;

  va_start(args, fmt);
  ss_lazy_init(ss);
  vfprintf(ss->stream, fmt, args);
  va_end(args);
}
