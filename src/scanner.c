/* SPIM S20 MIPS simulator.
   Hand-written lexical scanner.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause.
*/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "spim.h"
#include "spim-utils.h"    /* name_value_value, allocate_or_exit */
#include "registers.h"     /* GENERAL_PURPOSE_REGISTER_COUNT */
#include "symbol-table.h"  /* label_is_defined */
#include "scanner.h"       /* scan_value, line_no */
#include "tokens.h"        /* TOKEN_* token values */
#include "opcode-types.h"  /* opcode_type tag enumerators */
#include "opcode-tables.h" /* opcode_table: keyword -> {token, opcode_type} */

/* Runtime-visible globals. */
int line_no = 1;
scan_value_t scan_value = {};

/* Register-name → register-number lookup.  Public because instruction.c and
   explain.c also call register_name_to_number, independent of the
   scanner's internal keyword table. */
static const name_value_value register_table[] = {
    {.name = "a0", .value1 = 4},   {.name = "a1", .value1 = 5},
    {.name = "a2", .value1 = 6},   {.name = "a3", .value1 = 7},
    {.name = "at", .value1 = 1},   {.name = "fp", .value1 = 30},
    {.name = "gp", .value1 = 28},  {.name = "k0", .value1 = 26},
    {.name = "k1", .value1 = 27},  {.name = "kt0", .value1 = 26},
    {.name = "kt1", .value1 = 27}, {.name = "ra", .value1 = 31},
    {.name = "s0", .value1 = 16},  {.name = "s1", .value1 = 17},
    {.name = "s2", .value1 = 18},  {.name = "s3", .value1 = 19},
    {.name = "s4", .value1 = 20},  {.name = "s5", .value1 = 21},
    {.name = "s6", .value1 = 22},  {.name = "s7", .value1 = 23},
    {.name = "s8", .value1 = 30},  {.name = "sp", .value1 = 29},
    {.name = "t0", .value1 = 8},   {.name = "t1", .value1 = 9},
    {.name = "t2", .value1 = 10},  {.name = "t3", .value1 = 11},
    {.name = "t4", .value1 = 12},  {.name = "t5", .value1 = 13},
    {.name = "t6", .value1 = 14},  {.name = "t7", .value1 = 15},
    {.name = "t8", .value1 = 24},  {.name = "t9", .value1 = 25},
    {.name = "v0", .value1 = 2},   {.name = "v1", .value1 = 3},
    {.name = "zero", .value1 = 0}};

int register_name_to_number(const char* name) {
  const int c1 = *name, c2 = *(name + 1);
  if ('0' <= c1 && c1 <= '9' &&
      (c2 == '\0' || (('0' <= c2 && c2 <= '9') && *(name + 2) == '\0'))) {
    return atoi(name);
  } else if (c1 == 'f' && c2 >= '0' && c2 <= '9') {
    /* $f0..$f31; a larger number is not a register (and atoi would
       overflow on a long one). */
    const long floating_point_register = strtol(name + 1, nullptr, 10);
    return floating_point_register <= 31 ? (int)floating_point_register : -1;
  } else {
    const name_value_value* const entry = map_string_to_name_value_value(
        register_table, sizeof(register_table) / sizeof(name_value_value),
        name);
    return entry ? entry->value1 : -1;
  }
}

/* --- keyword table ---------------------------------------- */
/* Keywords are looked up in opcode_table (opcode-tables.h), generated from
   include/opcodes.tsv and sorted by name: {name, token, opcode_type}. */

static int check_keyword(const char* identifier, bool allow_pseudo_ops) {
  const name_value_value* const entry =
      map_string_to_name_value_value(opcode_table, OPCODE_COUNT, identifier);
  if (entry == nullptr)
    return 0;
  else if (!allow_pseudo_ops && entry->value2 == PSEUDO_OP)
    return 0;
  else
    return entry->value1;
}

/* --- input buffering -------------------------------------- */
/* The scanner reads one line at a time from `input_file` into
   `line_buffer`.  Tokens are scanned by character-walking
   `line_position` within the buffer.  When `line_position` reaches the
   end (or NUL), the next line is read on demand. */

constexpr int LINE_BUFFER_MAX = 4096;

static FILE* input_file = nullptr;
static char line_buffer[LINE_BUFFER_MAX];
static int line_position = 0;
static int line_length = 0;
static bool at_eof = false;

/* The text recorded as "current_line" for erroneous_line() —
   a snapshot of the line being parsed when the FIRST token of
   that line was returned.  Reset by scanner_start_line(). */
static char current_line_buffer[LINE_BUFFER_MAX];
static int current_line_no_saved = 0;
static bool current_line_saved = false;

/* --- lookahead buffer ------------------------------------- */
/* Three-slot token cache for peek / peek2 / advance.  See the
   "Token lifecycle" section of the design doc. */

typedef struct {
  int type;
  scan_value_t value;
  bool present;
} scan_token;

static scan_token token_buffer[3];

/* Self-clearing flag: when set, the NEXT identifier token is
   classified as TOKEN_IDENTIFIER even if it would otherwise be a keyword. */
static bool force_identifier_next = false;

/* --- public API ------------------------------------------- */

void scanner_initialize(FILE* input) {
  input_file = input;
  line_position = 0;
  line_length = 0;
  at_eof = false;
  line_no = 1;
  current_line_saved = false;
  force_identifier_next = false;
  for (size_t i = 0; i < sizeof token_buffer / sizeof token_buffer[0]; i++)
    token_buffer[i] = (scan_token){};
}

void scanner_start_line(void) { current_line_saved = false; }

void scanner_force_identifier(void) { force_identifier_next = true; }

/* --- low-level character access --------------------------- */

/* Read the next line from input_file into line_buffer.  Sets at_eof
   if the read returns 0 bytes.  Includes the trailing newline
   (if present) so newline-tokenization sees it. */
static void fill_line_buffer(void) {
  if (at_eof) {
    return;
  } else if (fgets(line_buffer, LINE_BUFFER_MAX, input_file) == nullptr) {
    at_eof = true;
    line_length = 0;
    line_position = 0;
    return;
  } else {
    line_length = (int)strlen(line_buffer);
    line_position = 0;
  }
}

/* Look at the current character without consuming.  Returns 0
   if at end of buffer (caller should refill if more input is
   expected). */
static int peek_char(void) {
  if (line_position >= line_length) return 0;
  return (unsigned char)line_buffer[line_position];
}

static int peek_char2(void) {
  if (line_position + 1 >= line_length) return 0;
  return (unsigned char)line_buffer[line_position + 1];
}

static int next_char(void) {
  if (line_position >= line_length) return 0;
  return (unsigned char)line_buffer[line_position++];
}

/* Snapshot the current line into current_line_buffer the first
   time a non-whitespace token is encountered on a given line. */
static void maybe_snapshot_line(void) {
  if (!current_line_saved) {
    /* Copy up to (but not including) the trailing newline */
    int length = line_length;
    if (length > 0 && line_buffer[length - 1] == '\n') length--;
    if (length >= LINE_BUFFER_MAX) length = LINE_BUFFER_MAX - 1;
    memcpy(current_line_buffer, line_buffer, (size_t)length);
    current_line_buffer[length] = '\0';
    current_line_no_saved = line_no;
    current_line_saved = true;
  }
}

/* --- token scanners --------------------------------------- */

/* The value of hex digit C; the caller has checked isxdigit(C). */
static inline int hex_digit_value(int c) {
  return c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
}

/* Look-ahead: starting from line_position, decide if the next token
   is a floating-point literal (matches `[0-9]+[.][0-9]+(e[+-]?[0-9]+)?`)
   vs a plain integer.  Returns the position past the FP-literal
   on success; line_position on no-match.  Does NOT mutate line_position. */
static int scan_floating_point_check(void) {
  int p = line_position;
  /* digits */
  if (p >= line_length || !isdigit((unsigned char)line_buffer[p]))
    return line_position;
  while (p < line_length && isdigit((unsigned char)line_buffer[p])) p++;
  /* the decimal point; `,` separates operands, so `.word 1,2` is two words */
  if (p >= line_length || line_buffer[p] != '.') return line_position;
  p++;
  /* more digits */
  if (p >= line_length || !isdigit((unsigned char)line_buffer[p]))
    return line_position;
  while (p < line_length && isdigit((unsigned char)line_buffer[p])) p++;
  /* optional exponent */
  if (p < line_length && (line_buffer[p] == 'e' || line_buffer[p] == 'E')) {
    p++;
    if (p < line_length && (line_buffer[p] == '+' || line_buffer[p] == '-'))
      p++;
    while (p < line_length && isdigit((unsigned char)line_buffer[p])) p++;
  }
  return p;
}

static int scan_floating_point(int sign, int end_position, scan_token* out) {
  static double scratch;
  /* Copy the literal text into a temp buffer for atof. */
  char buffer[64];
  int length = end_position - line_position;
  if (length >= (int)sizeof(buffer)) length = sizeof(buffer) - 1;
  memcpy(buffer, line_buffer + line_position, (size_t)length);
  buffer[length] = '\0';
  scratch = sign * atof(buffer);
  line_position = end_position;
  *out = (scan_token){.type = TOKEN_FLOATING_POINT,
                      .value.pointer = (void*)&scratch,
                      .present = true};
  return TOKEN_FLOATING_POINT;
}

/* Scan an integer literal (decimal or hex).  Leading optional
   '-' has already been consumed if signed.  Returns TOKEN_INTEGER with
   scan_value.i set. */
static int scan_integer(int sign, scan_token* out) {
  /* Accumulate in uint32_t: left-shifting/multiplying a signed int past
     its range is UB (e.g. 0x80000000, or a long decimal run).  Unsigned
     arithmetic is defined to wrap; convert to the signed value at the end. */
  uint32_t value = 0;
  if (peek_char() == '0' && (peek_char2() == 'x' || peek_char2() == 'X')) {
    next_char();
    next_char(); /* consume "0x" */
    while (isxdigit(peek_char())) {
      const int c = next_char();
      value = (value << 4) | (uint32_t)hex_digit_value(c);
    }
  } else {
    while (isdigit(peek_char())) {
      value = value * 10 + (uint32_t)(next_char() - '0');
    }
  }
  *out = (scan_token){.type = TOKEN_INTEGER,
                      .value.integer = (int32_t)((uint32_t)sign * value),
                      .present = true};
  return TOKEN_INTEGER;
}

/* Decode a backslash escape in a char literal.  `pp` points to the
   char AFTER the backslash; it's advanced past the consumed escape. */
static int scan_escape_char(const char** cursor) {
  const char first = **cursor;
  (*cursor)++;
  switch (first) {
    case 'a':
      return '\a';
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    case '\\':
      return '\\';
    case '"':
      return '"';
    case '\'':
      return '\'';
    case 'X':
    case 'x': {
      const int high = **cursor, low = *(*cursor + 1);
      int v = 0;
      if (isxdigit(high))
        v = hex_digit_value(high) << 4;
      else {
        error("Bad character in \\X construct in char literal\n");
      }
      if (isxdigit(low))
        v |= hex_digit_value(low);
      else {
        error("Bad character in \\X construct in char literal\n");
      }
      *cursor += 2;
      return v;
    }
    default:
      error("Bad escape character \\%c in char literal\n", first);
      return '\0';
  }
}

/* Decode a string literal — produces a fresh heap-allocated
   NUL-terminated decoded buffer. */
static char* decode_string(const char* source, int length) {
  char* const destination = (char*)allocate_or_exit((size_t)(length + 1));
  char* d = destination;
  const char* s = source;
  const char* const end = source + length;
  while (s < end) {
    if (*s != '\\') {
      *d++ = *s++;
      continue;
    } else if (s + 1 >= end) {
      /* A backslash with nothing after it is copied as is. */
      *d++ = *s++;
      continue;
    } else {
      /* Backslash sequence */
      const char n = *(s + 1);
      switch (n) {
        case 'n':
          *d++ = '\n';
          s += 2;
          break;
        case 't':
          *d++ = '\t';
          s += 2;
          break;
        case '"':
          *d++ = '"';
          s += 2;
          break;
        case '0':
        case '1':
        case '2':
        case '3': {
          /* Three-digit octal escape \NNN.  First digit shifts 6 bits,
             not 3 — a long-standing spim bug fixed 2026-05-19. */
          if (s + 3 >= end) {
            *d++ = *s++;
            break;
          }
          const int c2 = *(s + 2), c3 = *(s + 3);
          int b = (n - '0') << 6;
          if (c2 >= '0' && c2 <= '7')
            b += (c2 - '0') << 3;
          else {
            error("Bad character in \\ooo construct in string\n");
          }
          if (c3 >= '0' && c3 <= '7')
            b += c3 - '0';
          else {
            error("Bad character in \\ooo construct in string\n");
          }
          *d++ = (char)b;
          s += 4;
          break;
        }
        case 'X': {
          if (s + 3 >= end) {
            *d++ = *s++;
            break;
          }
          const int c2 = *(s + 2), c3 = *(s + 3);
          int b = 0;
          if (isxdigit(c2))
            b = hex_digit_value(c2);
          else {
            error("Bad character in \\X construct in string\n");
          }
          b <<= 4;
          if (isxdigit(c3))
            b |= hex_digit_value(c3);
          else {
            error("Bad character in \\X construct in string\n");
          }
          *d++ = (char)b;
          s += 4;
          break;
        }
        default:
          /* Unknown escape: copy the backslash, let next iteration
             handle the trailing char. */
          *d++ = *s++;
          break;
      }
    }
  }
  *d = '\0';
  return destination;
}

/* Scan an identifier or keyword.  Returns the appropriate token. */
static int scan_identifier(scan_token* out, bool force_identifier) {
  /* Capture the identifier text. */
  const int start = line_position;
  while (peek_char() == '_' || peek_char() == '.' || isalnum(peek_char())) {
    next_char();
  }
  const int end = line_position;

  static char identifier_buffer[256];
  int identifier_length = end - start;
  if (identifier_length >= (int)sizeof(identifier_buffer))
    identifier_length = sizeof(identifier_buffer) - 1;
  memcpy(identifier_buffer, line_buffer + start, (size_t)identifier_length);
  identifier_buffer[identifier_length] = '\0';

  /* Keyword lookup (unless force_identifier). */
  if (!force_identifier) {
    const int token = check_keyword(
        identifier_buffer, !bare_machine && accept_pseudo_instructions);
    if (token != 0) {
      *out =
          (scan_token){.type = token, .value.integer = token, .present = true};
      return token;
    }
  }

  /* Defined-constant label? */
  label* const l = label_is_defined(identifier_buffer);
  if (l != nullptr && l->constant_flag) {
    *out = (scan_token){.type = TOKEN_INTEGER,
                        .value.integer = (int)l->address,
                        .present = true};
    return TOKEN_INTEGER;
  }

  /* Otherwise an identifier. */
  *out = (scan_token){.type = TOKEN_IDENTIFIER,
                      .value.pointer = (void*)strdup(identifier_buffer),
                      .present = true};
  return TOKEN_IDENTIFIER;
}

/* Scan a $register reference. */
static int scan_register(scan_token* out) {
  next_char(); /* skip the $ */
  const int start = line_position;
  while (peek_char() == '_' || peek_char() == '.' || peek_char() == '$' ||
         isalnum(peek_char())) {
    next_char();
  }
  const int end = line_position;

  static char register_buffer[64];
  int register_name_length = end - start;
  if (register_name_length >= (int)sizeof(register_buffer))
    register_name_length = sizeof(register_buffer) - 1;
  memcpy(register_buffer, line_buffer + start, (size_t)register_name_length);
  register_buffer[register_name_length] = '\0';

  const int register_no = register_name_to_number(register_buffer);

  /* FP register if leading is 'f' and not "fp" */
  if (register_no != -1 && register_buffer[0] == 'f' &&
      register_buffer[1] != 'p') {
    *out = (scan_token){.type = TOKEN_FLOATING_POINT_REGISTER,
                        .value.integer = register_no,
                        .present = true};
    return TOKEN_FLOATING_POINT_REGISTER;
  } else if (register_no >= 0 && register_no < GENERAL_PURPOSE_REGISTER_COUNT) {
    *out = (scan_token){
        .type = TOKEN_REGISTER, .value.integer = register_no, .present = true};
    return TOKEN_REGISTER;
  } else {
    /* Fall back to identifier-or-constant-label behavior. */
    label* const l = label_is_defined(register_buffer);
    if (l != nullptr && l->constant_flag)
      *out = (scan_token){.type = TOKEN_INTEGER,
                          .value.integer = (int)l->address,
                          .present = true};
    else
      *out = (scan_token){.type = TOKEN_IDENTIFIER,
                          .value.pointer = (void*)strdup(register_buffer),
                          .present = true};
    return out->type;
  }
}

/* Scan a string literal "...".  Returns TOKEN_STRING; scan_value.p is a
   heap-allocated decoded copy. */
static int scan_string(scan_token* out) {
  next_char(); /* skip opening " */
  const int start = line_position;
  /* Find closing quote, respecting \" escapes. */
  while (peek_char() != 0 && peek_char() != '"') {
    if (peek_char() == '\\' && peek_char2() != 0) {
      next_char(); /* the backslash */
      next_char(); /* the escaped char */
    } else {
      next_char();
    }
  }
  const int end = line_position;
  if (peek_char() == '"') next_char(); /* skip closing " */

  *out = (scan_token){
      .type = TOKEN_STRING,
      .value.pointer = (void*)decode_string(line_buffer + start, end - start),
      .present = true};
  return TOKEN_STRING;
}

/* Scan a char literal '...'.  Returns TOKEN_INTEGER with the character
   value in scan_value.i. */
static int scan_char(scan_token* out) {
  next_char(); /* skip opening ' */
  int v;
  if (peek_char() == '\\') {
    next_char(); /* skip backslash */
    /* scan_escape_char wants a pointer that advances; we'll
       use a small staging area. */
    const char* p = &line_buffer[line_position];
    const char* const p0 = p;
    v = scan_escape_char(&p);
    /* Advance line_position by however many chars scan_escape_char ate. */
    line_position += (int)(p - p0);
  } else {
    v = next_char();
  }
  if (peek_char() == '\'') next_char(); /* skip closing ' */
  *out =
      (scan_token){.type = TOKEN_INTEGER, .value.integer = v, .present = true};
  return TOKEN_INTEGER;
}

/* The main scanner: produce one token into *out.  Skips
   whitespace and comments first. */
static void scan_one_token(scan_token* out) {
  /* Force-identifier flag is consumed by this call (one-shot). */
  const bool force_identifier = force_identifier_next;
  force_identifier_next = false;

  /* Skip whitespace, comments, and CR.  Stop at NL, ';', or a
   * real token character.  Each iteration may need to refill
   * line_buffer if we've exhausted it without finding a token. */
  while (true) {
    /* If buffer is empty / exhausted, refill — but we report
       TOKEN_NEWLINE between every line, and TOKEN_EOF/TOKEN_EOF only when the
       FILE is actually done.  We treat the trailing newline of
       a line as the line's terminating TOKEN_NEWLINE, then refill on
       the next call. */

    while (line_position < line_length) {
      const int c = peek_char();
      switch (c) {
        case ' ':
        case '\t':
        case '\r':
          next_char();
          continue;
        case '#':
          /* Comment to end of line. */
          while (peek_char() != 0 && peek_char() != '\n') next_char();
          continue;
        case ',':
          next_char();
          continue;
        default:
          goto have_char;
      }
    }

    /* Need a new line. */
    if (at_eof) {
      *out = (scan_token){.type = TOKEN_EOF, .present = true};
      return;
    }
    fill_line_buffer();
    if (at_eof && line_length == 0) {
      *out = (scan_token){.type = TOKEN_EOF, .present = true};
      return;
    }
    /* current_line snapshot will be re-taken at first token on the new line */
    current_line_saved = false;
  }

have_char:
  {
    const int c = peek_char();

    /* Newline: returns TOKEN_NEWLINE.  After we consume it, the line is
       complete; line_no has already advanced (by fgets reading
       the next chunk later).  Increment line_no here so it
       reflects the line of the NEXT token. */
    if (c == '\n') {
      next_char();
      line_no += 1;
      *out = (scan_token){.type = TOKEN_NEWLINE, .present = true};
      return;
    } else if (c == ';') {
      /* Semicolon also delimits a "line" for grammar purposes. */
      next_char();
      *out = (scan_token){.type = TOKEN_NEWLINE, .present = true};
      return;
    } else {
      /* From here, we have a real token char.  Snapshot the
         source line for erroneous_line() if we haven't yet. */
      maybe_snapshot_line();

      /* Negative integer or FP? */
      if (c == '-' && isdigit(peek_char2())) {
        next_char(); /* consume the '-' */
        const int floating_point_end = scan_floating_point_check();
        if (floating_point_end > line_position) {
          scan_floating_point(-1, floating_point_end, out);
          return;
        }
        scan_integer(-1, out);
        return;
      } else if (isdigit(c)) {
        /* Positive number — could be int or FP. */
        const int floating_point_end = scan_floating_point_check();
        if (floating_point_end > line_position) {
          scan_floating_point(1, floating_point_end, out);
          return;
        }
        scan_integer(1, out);
        return;
      } else if (c == '_' || c == '.' || isalpha(c)) {
        /* Identifier or keyword? */
        scan_identifier(out, force_identifier);
        return;
      } else if (c == '$') {
        /* Register? */
        scan_register(out);
        return;
      } else if (c == '"') {
        /* String literal? */
        scan_string(out);
        return;
      } else if (c == '\'') {
        /* Char literal? */
        scan_char(out);
        return;
      } else if (c == '?') {
        /* `?` returns TOKEN_IDENTIFIER for the REPL. */
        next_char();
        static char q[2] = "?";
        *out = (scan_token){.type = TOKEN_IDENTIFIER,
                            .value.pointer = (void*)strdup(q),
                            .present = true};
        return;
      } else if (c == '*' || c == '/' || c == ':' || c == '(' || c == ')' ||
                 c == '+' || c == '-' || c == '>' || c == '=') {
        /* Plain punctuation tokens. */
        next_char();
        *out = (scan_token){.type = c, .value.integer = c, .present = true};
        return;
      } else {
        /* Unknown character. */
        next_char();
        error("Unknown character in source: '%c' (0x%02x)\n", c, c);
        *out = (scan_token){
            .type = TOKEN_NEWLINE,
            .present = true}; /* recover by treating as line terminator */
        return;
      }
    }
  }
}

/* --- token-buffer machinery ------------------------------- */

static void ensure_filled(int n) {
  /* Make sure token_buffer[0..n-1] are populated. */
  for (int i = 0; i < n; i++) {
    if (!token_buffer[i].present) {
      scan_one_token(&token_buffer[i]);
    }
  }
}

int scanner_peek(void) {
  ensure_filled(1);
  return token_buffer[0].type;
}

int scanner_peek2(void) {
  ensure_filled(2);
  return token_buffer[1].type;
}

int scanner_advance(void) {
  ensure_filled(1);
  const int t = token_buffer[0].type;
  /* Only overwrite scan_value for tokens that actually carry a value.
     Callers like spim.c's flush_to_newline rely on scan_value.p
     surviving a TOKEN_NEWLINE read after a TOKEN_STRING. */
  switch (t) {
    case TOKEN_INTEGER:
    case TOKEN_IDENTIFIER:
    case TOKEN_REGISTER:
    case TOKEN_FLOATING_POINT_REGISTER:
    case TOKEN_STRING:
    case TOKEN_FLOATING_POINT:
      scan_value = token_buffer[0].value;
      break;
    default:
      break;
  }
  /* Shift the buffer left by one. */
  token_buffer[0] = token_buffer[1];
  token_buffer[1] = token_buffer[2];
  token_buffer[2].present = false;
  return t;
}

int scanner_next(void) { return scanner_advance(); }

/* --- nested-input scanner stack ---------------------------- *
 *
 * spim.c's libedit REPL wraps each command line in a fmemopen FILE*
 * and pushes it; parse_spim_command consumes tokens; then pops.
 */

constexpr int SCANNER_STACK_DEPTH = 8;
typedef struct {
  FILE* input_file;
  char line_buffer[LINE_BUFFER_MAX];
  int line_position;
  int line_length;
  bool at_eof;
  int line_no;
  char current_line_buffer[LINE_BUFFER_MAX];
  int current_line_no_saved;
  bool current_line_saved;
  bool force_identifier_next;
  scan_token token_buffer[3];
} scanner_snapshot;

static scanner_snapshot scanner_stack[SCANNER_STACK_DEPTH];
static int scanner_stack_depth = 0;

void scanner_push_source(FILE* input_stream) {
  if (scanner_stack_depth >= SCANNER_STACK_DEPTH) {
    fatal_error("scanner_push_source: nested scanner stack overflow\n");
    return;
  }
  scanner_snapshot* s = &scanner_stack[scanner_stack_depth++];
  s->input_file = input_file;
  memcpy(s->line_buffer, line_buffer, sizeof(line_buffer));
  s->line_position = line_position;
  s->line_length = line_length;
  s->at_eof = at_eof;
  s->line_no = line_no;
  memcpy(s->current_line_buffer, current_line_buffer,
         sizeof(current_line_buffer));
  s->current_line_no_saved = current_line_no_saved;
  s->current_line_saved = current_line_saved;
  s->force_identifier_next = force_identifier_next;
  memcpy(s->token_buffer, token_buffer, sizeof(token_buffer));

  scanner_initialize(input_stream);
}

void scanner_pop_source(void) {
  if (scanner_stack_depth <= 0) {
    /* No saved state — silently no-op; spim.c's SIGINT unwind path
       can fire pop without a matching push if the signal lands
       before scanner_push_source ran. */
    return;
  }
  scanner_snapshot* const s = &scanner_stack[--scanner_stack_depth];
  input_file = s->input_file;
  memcpy(line_buffer, s->line_buffer, sizeof(line_buffer));
  line_position = s->line_position;
  line_length = s->line_length;
  at_eof = s->at_eof;
  line_no = s->line_no;
  memcpy(current_line_buffer, s->current_line_buffer,
         sizeof(current_line_buffer));
  current_line_no_saved = s->current_line_no_saved;
  current_line_saved = s->current_line_saved;
  force_identifier_next = s->force_identifier_next;
  memcpy(token_buffer, s->token_buffer, sizeof(token_buffer));
}

/* --- erroneous_line / source_line ------------------------- */

/* Return a heap-allocated copy of the current source line, tab-
   indented and newline-terminated, for the parse-error printer. */
char* erroneous_line(void) {
  const size_t length = strlen(current_line_buffer);
  char* const r = (char*)allocate_or_exit(length + 16);
  snprintf(r, length + 16, "\t  %s\n", current_line_buffer);
  return r;
}

/* source_line: return a heap-allocated copy of the current source
   line, prefixed with "NNN: " line number.  Used by instruction.c to
   annotate assembled instructions with their originating source. */
char* source_line(void) {
  if (!current_line_saved) return nullptr;
  const size_t length = strlen(current_line_buffer);
  /* "NNN: " prefix — up to 10 digits + ": " + line + NUL */
  char* const r = (char*)allocate_or_exit(length + 16);
  snprintf(r, length + 16, "%d: %s", current_line_no_saved,
           current_line_buffer);
  return r;
}
