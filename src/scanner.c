/* SPIM S20 MIPS simulator.
   Hand-written lexical scanner.

   Copyright (c) 2026, William Emerison Six.
   BSD 3-Clause.
*/

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "spim.h"
#include "spim-utils.h"     /* name_val_val, str_copy, xmalloc */
#include "reg.h"            /* R_LENGTH */
#include "sym-tbl.h"        /* label_is_defined */
#include "scanner.h"        /* scan_value, line_no */
#include "tokens.h"    /* TOK_* token values */

/* Runtime-visible globals. */
int          line_no    = 1;
scan_value_t scan_value = {0};

/* Register-name → register-number lookup.  Public because inst.c and
   explain.c also call register_name_to_number, independent of the
   scanner's internal keyword table. */
static name_val_val register_tbl[] = {
    {"a0", 4, 0},   {"a1", 5, 0},   {"a2", 6, 0},   {"a3", 7, 0},
    {"at", 1, 0},   {"fp", 30, 0},  {"gp", 28, 0},  {"k0", 26, 0},
    {"k1", 27, 0},  {"kt0", 26, 0}, {"kt1", 27, 0}, {"ra", 31, 0},
    {"s0", 16, 0},  {"s1", 17, 0},  {"s2", 18, 0},  {"s3", 19, 0},
    {"s4", 20, 0},  {"s5", 21, 0},  {"s6", 22, 0},  {"s7", 23, 0},
    {"s8", 30, 0},  {"sp", 29, 0},  {"t0", 8, 0},   {"t1", 9, 0},
    {"t2", 10, 0},  {"t3", 11, 0},  {"t4", 12, 0},  {"t5", 13, 0},
    {"t6", 14, 0},  {"t7", 15, 0},  {"t8", 24, 0},  {"t9", 25, 0},
    {"v0", 2, 0},   {"v1", 3, 0},   {"zero", 0, 0}};

int register_name_to_number(char* name) {
  int c1 = *name, c2 = *(name + 1);
  if ('0' <= c1 && c1 <= '9'
      && (c2 == '\0' || (('0' <= c2 && c2 <= '9') && *(name + 2) == '\0'))) {
    return atoi(name);
  } else if (c1 == 'f' && c2 >= '0' && c2 <= '9') {
    return atoi(name + 1);
  } else {
    name_val_val* entry = map_string_to_name_val_val(
        register_tbl, sizeof(register_tbl) / sizeof(name_val_val), name);
    return entry ? entry->value1 : -1;
  }
}

/* --- keyword table ---------------------------------------- */
/* Built from op.h's X-macro pattern. */

static name_val_val keyword_tbl[] = {
#undef OP
#define OP(NAME, OPCODE, TYPE, R_OPCODE) {NAME, OPCODE, TYPE},
#include "op.h"
};

static int check_keyword(char* id, int allow_pseudo_ops) {
  name_val_val* entry =
    map_string_to_name_val_val(keyword_tbl,
                               sizeof(keyword_tbl) / sizeof(name_val_val),
                               id);
  if (entry == NULL) return 0;
  if (!allow_pseudo_ops && entry->value2 == PSEUDO_OP) return 0;
  return entry->value1;
}

/* --- input buffering -------------------------------------- */
/* The scanner reads one line at a time from `input_file` into
   `line_buf`.  Tokens are scanned by character-walking
   `line_pos` within the buffer.  When `line_pos` reaches the
   end (or NUL), the next line is read on demand. */

#define LINE_BUF_MAX 4096

static FILE* input_file = NULL;
static char  line_buf[LINE_BUF_MAX];
static int   line_pos = 0;
static int   line_len = 0;
static bool  at_eof   = false;

/* The text recorded as "current_line" for erroneous_line() —
   a snapshot of the line being parsed when the FIRST token of
   that line was returned.  Reset by scanner_start_line(). */
static char  current_line_buf[LINE_BUF_MAX];
static int   current_line_no_saved = 0;
static bool  current_line_saved = false;

/* --- lookahead buffer ------------------------------------- */
/* Three-slot token cache for peek / peek2 / advance.  See the
   "Token lifecycle" section of the design doc. */

typedef struct {
  int      type;
  scan_value_t val;
  bool     present;
} scan_token;

static scan_token tok_buf[3];

/* Self-clearing flag: when set, the NEXT identifier token is
   classified as TOK_ID even if it would otherwise be a keyword. */
static bool force_id_next = false;

/* --- public API ------------------------------------------- */

void scanner_init(FILE* in) {
  input_file = in;
  line_pos = 0;
  line_len = 0;
  at_eof = false;
  line_no = 1;
  current_line_saved = false;
  force_id_next = false;
  tok_buf[0].present = tok_buf[1].present = tok_buf[2].present = false;
}

void scanner_start_line(void) {
  current_line_saved = false;
}

void scanner_force_identifier(void) {
  force_id_next = true;
}

/* --- low-level character access --------------------------- */

/* Read the next line from input_file into line_buf.  Sets at_eof
   if the read returns 0 bytes.  Includes the trailing newline
   (if present) so newline-tokenization sees it. */
static void fill_line_buf(void) {
  if (at_eof) return;
  if (fgets(line_buf, LINE_BUF_MAX, input_file) == NULL) {
    at_eof = true;
    line_len = 0;
    line_pos = 0;
    return;
  }
  line_len = (int)strlen(line_buf);
  line_pos = 0;
}

/* Look at the current character without consuming.  Returns 0
   if at end of buffer (caller should refill if more input is
   expected). */
static int peek_char(void) {
  if (line_pos >= line_len) return 0;
  return (unsigned char)line_buf[line_pos];
}

static int peek_char2(void) {
  if (line_pos + 1 >= line_len) return 0;
  return (unsigned char)line_buf[line_pos + 1];
}

static int next_char(void) {
  if (line_pos >= line_len) return 0;
  return (unsigned char)line_buf[line_pos++];
}

/* Snapshot the current line into current_line_buf the first
   time a non-whitespace token is encountered on a given line. */
static void maybe_snapshot_line(void) {
  if (!current_line_saved) {
    /* Copy up to (but not including) the trailing newline */
    int len = line_len;
    if (len > 0 && line_buf[len - 1] == '\n') len--;
    if (len >= LINE_BUF_MAX) len = LINE_BUF_MAX - 1;
    memcpy(current_line_buf, line_buf, len);
    current_line_buf[len] = '\0';
    current_line_no_saved = line_no;
    current_line_saved = true;
  }
}

/* --- token scanners --------------------------------------- */

/* Look-ahead: starting from line_pos, decide if the next token
   is a floating-point literal (matches `[0-9]+[.,'][0-9]+(e[+-]?[0-9]+)?`)
   vs a plain integer.  Returns the position past the FP-literal
   on success; line_pos on no-match.  Does NOT mutate line_pos. */
static int scan_fp_check(void) {
  int p = line_pos;
  /* digits */
  if (p >= line_len || !isdigit((unsigned char)line_buf[p])) return line_pos;
  while (p < line_len && isdigit((unsigned char)line_buf[p])) p++;
  /* separator */
  if (p >= line_len) return line_pos;
  char sep = line_buf[p];
  if (sep != '.' && sep != ',' && sep != '\'') return line_pos;
  p++;
  /* more digits */
  if (p >= line_len || !isdigit((unsigned char)line_buf[p])) return line_pos;
  while (p < line_len && isdigit((unsigned char)line_buf[p])) p++;
  /* optional exponent */
  if (p < line_len && (line_buf[p] == 'e' || line_buf[p] == 'E')) {
    p++;
    if (p < line_len && (line_buf[p] == '+' || line_buf[p] == '-')) p++;
    while (p < line_len && isdigit((unsigned char)line_buf[p])) p++;
  }
  return p;
}

static int scan_fp(int sign, int end_pos, scan_token* out) {
  static double scratch;
  /* Copy literal text into a temp buffer for atof.  Replace any
     `,` or `'` separator with `.` since atof wants a `.`. */
  char buf[64];
  int len = end_pos - line_pos;
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, line_buf + line_pos, len);
  buf[len] = '\0';
  /* Normalize separator */
  for (int i = 0; i < len; i++) {
    if (buf[i] == ',' || buf[i] == '\'') { buf[i] = '.'; break; }
  }
  scratch = sign * atof(buf);
  line_pos = end_pos;
  out->type = TOK_FP;
  out->val.p = (void*)&scratch;
  out->present = true;
  return TOK_FP;
}

/* Scan an integer literal (decimal or hex).  Leading optional
   '-' has already been consumed if signed.  Returns TOK_INT with
   scan_value.i set. */
static int scan_int(int sign, scan_token* out) {
  int value = 0;
  if (peek_char() == '0' && (peek_char2() == 'x' || peek_char2() == 'X')) {
    next_char(); next_char();  /* consume "0x" */
    while (isxdigit(peek_char())) {
      int c = next_char();
      value = (value << 4)
            | (c <= '9' ? c - '0'
               : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
    }
  } else {
    while (isdigit(peek_char())) {
      value = value * 10 + (next_char() - '0');
    }
  }
  out->type = TOK_INT;
  out->val.i = sign * value;
  out->present = true;
  return TOK_INT;
}

/* Decode a backslash escape in a char literal.  `pp` points to the
   char AFTER the backslash; it's advanced past the consumed escape. */
static int scan_escape_char(const char** pp) {
  char first = **pp;
  (*pp)++;
  switch (first) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    case 'X': case 'x': {
      int hi = **pp, lo = *(*pp + 1);
      int v = 0;
      if (isxdigit(hi)) v = (hi <= '9' ? hi - '0'
                             : (hi <= 'F' ? hi - 'A' + 10 : hi - 'a' + 10)) << 4;
      else { error("Bad character in \\X construct in char literal\n"); }
      if (isxdigit(lo)) v |= (lo <= '9' ? lo - '0'
                              : (lo <= 'F' ? lo - 'A' + 10 : lo - 'a' + 10));
      else { error("Bad character in \\X construct in char literal\n"); }
      *pp += 2;
      return v;
    }
    default:
      error("Bad escape character \\%c in char literal\n", first);
      return '\0';
  }
}

/* Decode a string literal — produces a fresh heap-allocated
   NUL-terminated decoded buffer. */
static char* decode_string(const char* src, int len) {
  char* dst = (char*)xmalloc(len + 1);
  char* d = dst;
  const char* s = src;
  const char* end = src + len;
  while (s < end) {
    if (*s != '\\') { *d++ = *s++; continue; }
    /* Backslash sequence */
    if (s + 1 >= end) { *d++ = *s++; continue; }
    char n = *(s + 1);
    switch (n) {
      case 'n':  *d++ = '\n'; s += 2; break;
      case 't':  *d++ = '\t'; s += 2; break;
      case '"':  *d++ = '"';  s += 2; break;
      case '0': case '1': case '2': case '3': {
        /* Three-digit octal escape \NNN.  First digit shifts 6 bits,
           not 3 — a long-standing spim bug fixed 2026-05-19. */
        if (s + 3 >= end) { *d++ = *s++; break; }
        int c2 = *(s + 2), c3 = *(s + 3);
        int b = (n - '0') << 6;
        if (c2 >= '0' && c2 <= '7') b += (c2 - '0') << 3;
        else { error("Bad character in \\ooo construct in string\n"); }
        if (c3 >= '0' && c3 <= '7') b += c3 - '0';
        else { error("Bad character in \\ooo construct in string\n"); }
        *d++ = (char)b;
        s += 4;
        break;
      }
      case 'X': {
        if (s + 3 >= end) { *d++ = *s++; break; }
        int c2 = *(s + 2), c3 = *(s + 3);
        int b = 0;
        if (isxdigit(c2)) b = (c2 <= '9' ? c2 - '0'
                               : (c2 <= 'F' ? c2 - 'A' + 10 : c2 - 'a' + 10));
        else { error("Bad character in \\X construct in string\n"); }
        b <<= 4;
        if (isxdigit(c3)) b |= (c3 <= '9' ? c3 - '0'
                                : (c3 <= 'F' ? c3 - 'A' + 10 : c3 - 'a' + 10));
        else { error("Bad character in \\X construct in string\n"); }
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
  *d = '\0';
  return dst;
}

/* Scan an identifier or keyword.  Returns the appropriate token. */
static int scan_identifier(scan_token* out, bool force_id) {
  /* Capture the identifier text. */
  int start = line_pos;
  while (peek_char() == '_' || peek_char() == '.'
         || isalnum(peek_char())) {
    next_char();
  }
  int end = line_pos;

  static char id_buf[256];
  int idlen = end - start;
  if (idlen >= (int)sizeof(id_buf)) idlen = sizeof(id_buf) - 1;
  memcpy(id_buf, line_buf + start, idlen);
  id_buf[idlen] = '\0';

  /* Keyword lookup (unless force_id). */
  if (!force_id) {
    int token = check_keyword(id_buf,
                                 !bare_machine && accept_pseudo_insts);
    if (token != 0) {
      out->type = token;
      out->val.i = token;
      out->present = true;
      return token;
    }
  }

  /* Defined-constant label? */
  label* l = label_is_defined(id_buf);
  if (l != NULL && l->const_flag) {
    out->type = TOK_INT;
    out->val.i = (int)l->addr;
    out->present = true;
    return TOK_INT;
  }

  /* Otherwise an identifier. */
  out->type = TOK_ID;
  out->val.p = (void*)str_copy(id_buf);
  out->present = true;
  return TOK_ID;
}

/* Scan a $register reference. */
static int scan_register(scan_token* out) {
  int start;
  next_char();  /* skip the $ */
  start = line_pos;
  while (peek_char() == '_' || peek_char() == '.' || peek_char() == '$'
         || isalnum(peek_char())) {
    next_char();
  }
  int end = line_pos;

  static char reg_buf[64];
  int reglen = end - start;
  if (reglen >= (int)sizeof(reg_buf)) reglen = sizeof(reg_buf) - 1;
  memcpy(reg_buf, line_buf + start, reglen);
  reg_buf[reglen] = '\0';

  int reg_no = register_name_to_number(reg_buf);

  /* FP register if leading is 'f' and not "fp" */
  if (reg_no != -1 && reg_buf[0] == 'f' && reg_buf[1] != 'p') {
    out->type = TOK_FP_REG;
    out->val.i = reg_no;
    out->present = true;
    return TOK_FP_REG;
  }

  if (reg_no >= 0 && reg_no < R_LENGTH) {
    out->type = TOK_REG;
    out->val.i = reg_no;
    out->present = true;
    return TOK_REG;
  }

  /* Fall back to identifier-or-constant-label behavior. */
  label* l = label_is_defined(reg_buf);
  if (l != NULL && l->const_flag) {
    out->type = TOK_INT;
    out->val.i = (int)l->addr;
  } else {
    out->type = TOK_ID;
    out->val.p = (void*)str_copy(reg_buf);
  }
  out->present = true;
  return out->type;
}

/* Scan a string literal "...".  Returns TOK_STR; scan_value.p is a
   heap-allocated decoded copy. */
static int scan_string(scan_token* out) {
  next_char();  /* skip opening " */
  int start = line_pos;
  /* Find closing quote, respecting \" escapes. */
  while (peek_char() != 0 && peek_char() != '"') {
    if (peek_char() == '\\' && peek_char2() != 0) {
      next_char();  /* the backslash */
      next_char();  /* the escaped char */
    } else {
      next_char();
    }
  }
  int end = line_pos;
  if (peek_char() == '"') next_char();  /* skip closing " */

  out->type = TOK_STR;
  out->val.p = (void*)decode_string(line_buf + start, end - start);
  out->present = true;
  return TOK_STR;
}

/* Scan a char literal '...'.  Returns TOK_INT with the character
   value in scan_value.i. */
static int scan_char(scan_token* out) {
  next_char();  /* skip opening ' */
  int v;
  if (peek_char() == '\\') {
    next_char();  /* skip backslash */
    /* scan_escape_char wants a pointer that advances; we'll
       use a small staging area. */
    const char* p = &line_buf[line_pos];
    const char* p0 = p;
    v = scan_escape_char(&p);
    /* Advance line_pos by however many chars scan_escape_char ate. */
    line_pos += (int)(p - p0);
  } else {
    v = next_char();
  }
  if (peek_char() == '\'') next_char();  /* skip closing ' */
  out->type = TOK_INT;
  out->val.i = v;
  out->present = true;
  return TOK_INT;
}

/* The main scanner: produce one token into *out.  Skips
   whitespace and comments first. */
static void scan_one_token(scan_token* out) {
  /* Force-identifier flag is consumed by this call (one-shot). */
  bool force_id = force_id_next;
  force_id_next = false;

  /* Skip whitespace, comments, and CR.  Stop at NL, ';', or a
   * real token character.  Each iteration may need to refill
   * line_buf if we've exhausted it without finding a token. */
  for (;;) {
    /* If buffer is empty / exhausted, refill — but we report
       TOK_NL between every line, and TOK_EOF/TOK_EOF only when the
       FILE is actually done.  We treat the trailing newline of
       a line as the line's terminating TOK_NL, then refill on
       the next call. */

    while (line_pos < line_len) {
      int c = peek_char();
      if (c == ' ' || c == '\t' || c == '\r') {
        next_char();
        continue;
      }
      if (c == '#') {
        /* Comment to end of line. */
        while (peek_char() != 0 && peek_char() != '\n') next_char();
        continue;
      }
      if (c == ',') {
        next_char();
        continue;
      }
      goto have_char;
    }

    /* Need a new line. */
    if (at_eof) {
      out->type = TOK_EOF;
      out->val.i = 0;
      out->present = true;
      return;
    }
    fill_line_buf();
    if (at_eof && line_len == 0) {
      out->type = TOK_EOF;
      out->val.i = 0;
      out->present = true;
      return;
    }
    /* current_line snapshot will be re-taken at first token on the new line */
    current_line_saved = false;
  }

have_char: {
    int c = peek_char();

    /* Newline: returns TOK_NL.  After we consume it, the line is
       complete; line_no has already advanced (by fgets reading
       the next chunk later).  Increment line_no here so it
       reflects the line of the NEXT token. */
    if (c == '\n') {
      next_char();
      line_no += 1;
      out->type = TOK_NL;
      out->val.i = 0;
      out->present = true;
      return;
    }

    /* Semicolon also delimits a "line" for grammar purposes. */
    if (c == ';') {
      next_char();
      out->type = TOK_NL;
      out->val.i = 0;
      out->present = true;
      return;
    }

    /* From here, we have a real token char.  Snapshot the
       source line for erroneous_line() if we haven't yet. */
    maybe_snapshot_line();

    /* Negative integer or FP? */
    if (c == '-' && isdigit(peek_char2())) {
      next_char();  /* consume the '-' */
      int fp_end = scan_fp_check();
      if (fp_end > line_pos) {
        scan_fp(-1, fp_end, out);
        return;
      }
      scan_int(-1, out);
      return;
    }

    /* Positive number — could be int or FP. */
    if (isdigit(c)) {
      int fp_end = scan_fp_check();
      if (fp_end > line_pos) {
        scan_fp(1, fp_end, out);
        return;
      }
      scan_int(1, out);
      return;
    }

    /* Identifier or keyword? */
    if (c == '_' || c == '.' || isalpha(c)) {
      scan_identifier(out, force_id);
      return;
    }

    /* Register? */
    if (c == '$') {
      scan_register(out);
      return;
    }

    /* String literal? */
    if (c == '"') {
      scan_string(out);
      return;
    }

    /* Char literal? */
    if (c == '\'') {
      scan_char(out);
      return;
    }

    /* `?` returns TOK_ID for the REPL. */
    if (c == '?') {
      next_char();
      static char q[2] = "?";
      out->type = TOK_ID;
      out->val.p = (void*)str_copy(q);
      out->present = true;
      return;
    }

    /* Plain punctuation tokens. */
    if (c == '*' || c == '/' || c == ':' || c == '(' || c == ')'
        || c == '+' || c == '-' || c == '>' || c == '=') {
      next_char();
      out->type = c;
      out->val.i = c;
      out->present = true;
      return;
    }

    /* Unknown character. */
    next_char();
    error("Unknown character in source: '%c' (0x%02x)\n", c, c);
    out->type = TOK_NL;  /* recover by treating as line terminator */
    out->val.i = 0;
    out->present = true;
    return;
  }
}

/* --- token-buffer machinery ------------------------------- */

static void ensure_filled(int n) {
  /* Make sure tok_buf[0..n-1] are populated. */
  for (int i = 0; i < n; i++) {
    if (!tok_buf[i].present) {
      scan_one_token(&tok_buf[i]);
    }
  }
}

int scanner_peek(void) {
  ensure_filled(1);
  return tok_buf[0].type;
}

int scanner_peek2(void) {
  ensure_filled(2);
  return tok_buf[1].type;
}

int scanner_advance(void) {
  ensure_filled(1);
  int t = tok_buf[0].type;
  /* Only overwrite scan_value for tokens that actually carry a value.
     Callers like spim.c's flush_to_newline rely on scan_value.p
     surviving a TOK_NL read after a TOK_STR. */
  switch (t) {
    case TOK_INT: case TOK_ID: case TOK_REG: case TOK_FP_REG: case TOK_STR: case TOK_FP:
      scan_value = tok_buf[0].val;
      break;
    default:
      break;
  }
  /* Shift the buffer left by one. */
  tok_buf[0] = tok_buf[1];
  tok_buf[1] = tok_buf[2];
  tok_buf[2].present = false;
  return t;
}

int scanner_next(void) {
  return scanner_advance();
}

/* --- nested-input scanner stack ---------------------------- *
 *
 * spim.c's libedit REPL wraps each command line in a fmemopen FILE*
 * and pushes it; parse_spim_command consumes tokens; then pops.
 */


#define SCANNER_STACK_DEPTH 8
typedef struct {
  FILE*  input_file;
  char   line_buf[LINE_BUF_MAX];
  int    line_pos;
  int    line_len;
  bool   at_eof;
  int    line_no;
  char   current_line_buf[LINE_BUF_MAX];
  int    current_line_no_saved;
  bool   current_line_saved;
  bool   force_id_next;
  scan_token tok_buf[3];
} scanner_snapshot;

static scanner_snapshot scanner_stack[SCANNER_STACK_DEPTH];
static int scanner_stack_depth = 0;

void scanner_push_source(FILE* in_file) {
  if (scanner_stack_depth >= SCANNER_STACK_DEPTH) {
    fatal_error("scanner_push_source: nested scanner stack overflow\n");
    return;
  }
  scanner_snapshot* s = &scanner_stack[scanner_stack_depth++];
  s->input_file = input_file;
  memcpy(s->line_buf, line_buf, sizeof(line_buf));
  s->line_pos = line_pos;
  s->line_len = line_len;
  s->at_eof   = at_eof;
  s->line_no  = line_no;
  memcpy(s->current_line_buf, current_line_buf, sizeof(current_line_buf));
  s->current_line_no_saved = current_line_no_saved;
  s->current_line_saved    = current_line_saved;
  s->force_id_next         = force_id_next;
  memcpy(s->tok_buf, tok_buf, sizeof(tok_buf));

  scanner_init(in_file);
}

void scanner_pop_source(void) {
  if (scanner_stack_depth <= 0) {
    /* No saved state — silently no-op; spim.c's SIGINT unwind path
       can fire pop without a matching push if the signal lands
       before scanner_push_source ran. */
    return;
  }
  scanner_snapshot* s = &scanner_stack[--scanner_stack_depth];
  input_file = s->input_file;
  memcpy(line_buf, s->line_buf, sizeof(line_buf));
  line_pos = s->line_pos;
  line_len = s->line_len;
  at_eof   = s->at_eof;
  line_no  = s->line_no;
  memcpy(current_line_buf, s->current_line_buf, sizeof(current_line_buf));
  current_line_no_saved = s->current_line_no_saved;
  current_line_saved    = s->current_line_saved;
  force_id_next         = s->force_id_next;
  memcpy(tok_buf, s->tok_buf, sizeof(tok_buf));
}

/* --- erroneous_line / source_line ------------------------- */

/* Return a heap-allocated copy of the current source line, tab-
   indented and newline-terminated, for the parse-error printer. */
char* erroneous_line(void) {
  size_t len = strlen(current_line_buf);
  char* r = (char*)xmalloc((int)(len + 16));
  snprintf(r, len + 16, "\t  %s\n", current_line_buf);
  return r;
}

/* source_line: return a heap-allocated copy of the current source
   line, prefixed with "NNN: " line number.  Used by inst.c to
   annotate assembled instructions with their originating source. */
char* source_line(void) {
  if (!current_line_saved) return NULL;
  size_t len = strlen(current_line_buf);
  /* "NNN: " prefix — up to 10 digits + ": " + line + NUL */
  char* r = (char*)xmalloc((int)(len + 16));
  snprintf(r, len + 16, "%d: %s", current_line_no_saved, current_line_buf);
  return r;
}
