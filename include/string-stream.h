/* SPIM S20 MIPS simulator.
  Append-only output stream convertable to a string.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef STRING_STREAM_H
#define STRING_STREAM_H

typedef struct str_stm {
  char* buf;       /* Buffer containing output */
  int max_length;  /* Length of buffer */
  int empty_pos;   /* Index  of empty char in stream*/
  int initialized; /* Stream initialized? */
} str_stream;

void ss_init(str_stream* ss);
void ss_clear(str_stream* ss);
void ss_erase(str_stream* ss, int n);
int ss_length(str_stream* ss);
[[nodiscard]] char* ss_to_string(str_stream* ss);
void ss_printf(str_stream* ss, char* fmt, ...);

#endif
