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

static void stream_lazy_initialize(string_stream* stream) {
  if (stream->stream == nullptr) {
    *stream = (string_stream){};
    stream->stream = open_memstream(&stream->buffer, &stream->size);
    if (stream->stream == nullptr) fatal_error("open_memstream failed\n");
  }
}

void stream_initialize(string_stream* stream) {
  /* Historical contract: unconditionally start a fresh stream.  Local
     (stack) str_streams are stream_initialize'ed without being zeroed first, so
     no prior field value may be read here. */
  *stream = (string_stream){};
  stream_lazy_initialize(stream);
}

void stream_clear(string_stream* stream) {
  /* Start over with an empty stream.  Rewinding is not enough: a memstream
     keeps what was written past the new position, so a shorter text written
     after the rewind would end in the old text's tail. */
  if (stream->stream != nullptr) {
    fclose(stream->stream);
    free(stream->buffer);
  }
  *stream = (string_stream){};
  stream_lazy_initialize(stream);
}

void stream_erase(string_stream* stream, int n) {
  stream_lazy_initialize(stream);
  long position = ftell(stream->stream);
  if (position < 0) position = 0;
  fseek(stream->stream, (position > n) ? position - n : 0, SEEK_SET);
}

int stream_length(string_stream* stream) {
  stream_lazy_initialize(stream);
  fflush(stream->stream);
  return (int)stream->size;
}

/* Borrow the current contents.  The pointer remains owned by the
   stream: valid until the next ss_* call, do not free.  fflush
   NUL-terminates the buffer at the current position. */

char* stream_to_string(string_stream* stream) {
  stream_lazy_initialize(stream);
  fflush(stream->stream);
  return stream->buffer;
}

/* Take ownership of the contents: closes the stream and returns the
   malloc'd, NUL-terminated buffer.  The caller frees it.  The
   string_stream itself is reset and may be reused (a fresh stream is
   created on its next use). */

char* stream_take_string(string_stream* stream) {
  stream_lazy_initialize(stream);
  fclose(stream->stream);
  stream->stream = nullptr;
  char* const buffer = stream->buffer;
  stream->buffer = nullptr;
  stream->size = 0;
  return buffer;
}

void stream_printf(string_stream* stream, const char* format, ...) {
  va_list arguments;

  va_start(arguments, format);
  stream_lazy_initialize(stream);
  vfprintf(stream->stream, format, arguments);
  va_end(arguments);
}
