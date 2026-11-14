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

static void ss_lazy_init(str_stream* stream) {
  if (stream->stream == nullptr) {
    *stream = (str_stream){};
    stream->stream = open_memstream(&stream->buffer, &stream->size);
    if (stream->stream == nullptr) fatal_error("open_memstream failed\n");
  }
}

void ss_init(str_stream* stream) {
  /* Historical contract: unconditionally start a fresh stream.  Local
     (stack) str_streams are ss_init'ed without being zeroed first, so
     no prior field value may be read here. */
  *stream = (str_stream){};
  ss_lazy_init(stream);
}

void ss_clear(str_stream* stream) {
  /* Start over with an empty stream.  Rewinding is not enough: a memstream
     keeps what was written past the new position, so a shorter text written
     after the rewind would end in the old text's tail. */
  if (stream->stream != nullptr) {
    fclose(stream->stream);
    free(stream->buffer);
  }
  *stream = (str_stream){};
  ss_lazy_init(stream);
}

void ss_erase(str_stream* stream, int n) {
  ss_lazy_init(stream);
  long position = ftell(stream->stream);
  if (position < 0) position = 0;
  fseek(stream->stream, (position > n) ? position - n : 0, SEEK_SET);
}

int ss_length(str_stream* stream) {
  ss_lazy_init(stream);
  fflush(stream->stream);
  return (int)stream->size;
}

/* Borrow the current contents.  The pointer remains owned by the
   stream: valid until the next ss_* call, do not free.  fflush
   NUL-terminates the buffer at the current position. */

char* ss_to_string(str_stream* stream) {
  ss_lazy_init(stream);
  fflush(stream->stream);
  return stream->buffer;
}

/* Take ownership of the contents: closes the stream and returns the
   malloc'd, NUL-terminated buffer.  The caller frees it.  The
   str_stream itself is reset and may be reused (a fresh stream is
   created on its next use). */

char* ss_take_string(str_stream* stream) {
  ss_lazy_init(stream);
  fclose(stream->stream);
  stream->stream = nullptr;
  char* const buffer = stream->buffer;
  stream->buffer = nullptr;
  stream->size = 0;
  return buffer;
}

void ss_printf(str_stream* stream, const char* format, ...) {
  va_list arguments;

  va_start(arguments, format);
  ss_lazy_init(stream);
  vfprintf(stream->stream, format, arguments);
  va_end(arguments);
}
