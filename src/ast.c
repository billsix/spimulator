/* SPIM S20 MIPS simulator.
   AST constructors, recursive free, indented text printer, JSON
   printer.  Node shapes and ownership rules live in include/ast.h.

   The constructors snapshot the scanner's current line_no onto each
   new node; callers that build nodes off the parser's current line
   (e.g. multi-line data directives) override node->source_line by
   hand after construction.

   ast_free is recursive over both the .next sibling chain and any
   .child list (AST_FILE, AST_PSEUDO).  Free is idempotent on NULL.

   ast_print and ast_print_json walk the tree in source order.  The
   text printer is for direct human reading (-print-ast,
   -show-expansion); the JSON printer is the data surface for
   external tooling.

   SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "ast.h"
#include "spim-utils.h" /* xmalloc, str_copy           */
#include "scanner.h"    /* line_no                      */
#include "sym-tbl.h"    /* struct label definition      */
#include "tokens.h"     /* TOK_*_OP names for ast_print */

/* ------------------------------------------------------------------ */
/* Internal node allocator                                             */
/* ------------------------------------------------------------------ */

static ast_node* new_node(ast_kind kind) {
  ast_node* n = (ast_node*)xmalloc(sizeof(ast_node));
  /* xmalloc returns uninitialized memory; zero the whole payload so
     callers don't have to remember to clear unused union fields. */
  memset(n, 0, sizeof(*n));
  n->kind = kind;
  n->source_line = line_no;
  n->next = nullptr;
  return n;
}

/* ------------------------------------------------------------------ */
/* Instruction constructors                                            */
/* ------------------------------------------------------------------ */

ast_node* ast_make_inst_r(int op, int rd, int rs, int rt) {
  ast_node* n = new_node(AST_INST_R);
  n->u.inst_r.op = op;
  n->u.inst_r.rd = rd;
  n->u.inst_r.rs = rs;
  n->u.inst_r.rt = rt;
  return n;
}

ast_node* ast_make_inst_r_shift(int op, int rd, int rt, int shamt) {
  ast_node* n = new_node(AST_INST_R_SHIFT);
  n->u.inst_r_shift.op = op;
  n->u.inst_r_shift.rd = rd;
  n->u.inst_r_shift.rt = rt;
  n->u.inst_r_shift.shamt = shamt;
  return n;
}

ast_node* ast_make_inst_i(int op, int rt, int rs, imm_expr* imm) {
  ast_node* n = new_node(AST_INST_I);
  n->u.inst_i.op = op;
  n->u.inst_i.rt = rt;
  n->u.inst_i.rs = rs;
  n->u.inst_i.imm = imm;
  return n;
}

ast_node* ast_make_inst_j(int op, imm_expr* target) {
  ast_node* n = new_node(AST_INST_J);
  n->u.inst_j.op = op;
  n->u.inst_j.target = target;
  return n;
}

ast_node* ast_make_inst_fp_r(int op, int fd, int fs, int ft) {
  ast_node* n = new_node(AST_INST_FP_R);
  n->u.inst_fp_r.op = op;
  n->u.inst_fp_r.fd = fd;
  n->u.inst_fp_r.fs = fs;
  n->u.inst_fp_r.ft = ft;
  return n;
}

ast_node* ast_make_inst_fp_compare(int op, int fs, int ft, int cc) {
  ast_node* n = new_node(AST_INST_FP_COMPARE);
  n->u.inst_fp_compare.op = op;
  n->u.inst_fp_compare.fs = fs;
  n->u.inst_fp_compare.ft = ft;
  n->u.inst_fp_compare.cc = cc;
  return n;
}

ast_node* ast_make_pseudo(const char* mnemonic) {
  ast_node* n = new_node(AST_PSEUDO);
  n->u.pseudo.mnemonic = str_copy(mnemonic);
  n->u.pseudo.child = nullptr;
  return n;
}

/* ------------------------------------------------------------------ */
/* Data constructors                                                   */
/* ------------------------------------------------------------------ */

static ast_node* make_data_int(ast_kind kind, int count, imm_expr** exprs) {
  ast_node* n = new_node(kind);
  n->u.data_int.count = count;
  n->u.data_int.exprs = exprs;
  return n;
}

ast_node* ast_make_data_byte(int count, imm_expr** exprs) {
  return make_data_int(AST_DATA_BYTE, count, exprs);
}

ast_node* ast_make_data_half(int count, imm_expr** exprs) {
  return make_data_int(AST_DATA_HALF, count, exprs);
}

ast_node* ast_make_data_word(int count, imm_expr** exprs) {
  return make_data_int(AST_DATA_WORD, count, exprs);
}

static ast_node* make_data_fp(ast_kind kind, int count, double* values) {
  ast_node* n = new_node(kind);
  n->u.data_fp.count = count;
  n->u.data_fp.values = values;
  return n;
}

ast_node* ast_make_data_float(int count, double* values) {
  return make_data_fp(AST_DATA_FLOAT, count, values);
}

ast_node* ast_make_data_double(int count, double* values) {
  return make_data_fp(AST_DATA_DOUBLE, count, values);
}

ast_node* ast_make_data_string(const char* bytes, int length, bool null_term) {
  ast_node* n = new_node(AST_DATA_STRING);
  /* Copy the bytes verbatim (may contain NULs); +1 so callers can also
     read it as a C-string when no embedded NULs are present. */
  char* buf = (char*)xmalloc(length + 1);
  memcpy(buf, bytes, length);
  buf[length] = '\0';
  n->u.data_string.bytes = buf;
  n->u.data_string.length = length;
  n->u.data_string.null_terminate = null_term;
  return n;
}

/* ------------------------------------------------------------------ */
/* Directive constructors                                              */
/* ------------------------------------------------------------------ */

static ast_node* make_dir_seg(ast_kind kind, bool has, mem_addr addr) {
  ast_node* n = new_node(kind);
  n->u.dir_seg.has_start_addr = has;
  n->u.dir_seg.start_addr = addr;
  return n;
}

ast_node* ast_make_dir_text(bool has_start_addr, mem_addr start_addr) {
  return make_dir_seg(AST_DIR_TEXT, has_start_addr, start_addr);
}

ast_node* ast_make_dir_data(bool has_start_addr, mem_addr start_addr) {
  return make_dir_seg(AST_DIR_DATA, has_start_addr, start_addr);
}

ast_node* ast_make_dir_ktext(bool has_start_addr, mem_addr start_addr) {
  return make_dir_seg(AST_DIR_KTEXT, has_start_addr, start_addr);
}

ast_node* ast_make_dir_kdata(bool has_start_addr, mem_addr start_addr) {
  return make_dir_seg(AST_DIR_KDATA, has_start_addr, start_addr);
}

ast_node* ast_make_dir_align(int n_) {
  ast_node* n = new_node(AST_DIR_ALIGN);
  n->u.dir_align.n = n_;
  return n;
}

ast_node* ast_make_dir_space(int size) {
  ast_node* n = new_node(AST_DIR_SPACE);
  n->u.dir_space.size = size;
  return n;
}

ast_node* ast_make_dir_globl(const char* name) {
  ast_node* n = new_node(AST_DIR_GLOBL);
  n->u.dir_globl.name = str_copy(name);
  return n;
}

static ast_node* make_named_size(ast_kind kind, const char* name, int size) {
  ast_node* n = new_node(kind);
  n->u.dir_named_size.name = str_copy(name);
  n->u.dir_named_size.size = size;
  return n;
}

ast_node* ast_make_dir_extern(const char* name, int size) {
  return make_named_size(AST_DIR_EXTERN, name, size);
}

ast_node* ast_make_dir_comm(const char* name, int size) {
  return make_named_size(AST_DIR_COMM, name, size);
}

/* ------------------------------------------------------------------ */
/* Label constructors                                                  */
/* ------------------------------------------------------------------ */

ast_node* ast_make_label_normal(const char* name) {
  ast_node* n = new_node(AST_LABEL_DEF);
  n->u.label_def.kind = AST_LABEL_NORMAL;
  n->u.label_def.name = str_copy(name);
  n->u.label_def.value = 0;
  return n;
}

ast_node* ast_make_label_const(const char* name, int32_t value) {
  ast_node* n = new_node(AST_LABEL_DEF);
  n->u.label_def.kind = AST_LABEL_CONST;
  n->u.label_def.name = str_copy(name);
  n->u.label_def.value = value;
  return n;
}

/* ------------------------------------------------------------------ */
/* File + error                                                        */
/* ------------------------------------------------------------------ */

ast_node* ast_make_file(const char* source_file) {
  ast_node* n = new_node(AST_FILE);
  n->u.file.child = nullptr;
  n->u.file.source_file = source_file ? str_copy(source_file) : nullptr;
  return n;
}

ast_node* ast_make_error(const char* message) {
  ast_node* n = new_node(AST_ERROR);
  n->u.error.message = message ? str_copy(message) : nullptr;
  return n;
}

/* ------------------------------------------------------------------ */
/* Tree manipulation                                                   */
/* ------------------------------------------------------------------ */

ast_node* ast_file_append(ast_node* file, ast_node* stmt) {
  if (file == nullptr || stmt == nullptr) return file;
  if (file->u.file.child == nullptr) {
    file->u.file.child = stmt;
    return file;
  }
  ast_node* t = file->u.file.child;
  while (t->next != nullptr) t = t->next;
  t->next = stmt;
  return file;
}

ast_node* ast_pseudo_append(ast_node* pseudo, ast_node* child) {
  if (pseudo == nullptr || child == nullptr) return pseudo;
  if (pseudo->u.pseudo.child == nullptr) {
    pseudo->u.pseudo.child = child;
    return pseudo;
  }
  ast_node* t = pseudo->u.pseudo.child;
  while (t->next != nullptr) t = t->next;
  t->next = child;
  return pseudo;
}

/* ------------------------------------------------------------------ */
/* ast_free — recursive cleanup                                        */
/* ------------------------------------------------------------------ */

static void free_imm_array(imm_expr** arr, int n) {
  if (arr == nullptr) return;
  for (int i = 0; i < n; i++)
    if (arr[i] != nullptr) free(arr[i]);
  free(arr);
}

void ast_free(ast_node* node) {
  while (node != nullptr) {
    ast_node* next_sibling = node->next;

    switch (node->kind) {
      case AST_INST_R:
      case AST_INST_R_SHIFT:
      case AST_INST_FP_R:
      case AST_INST_FP_COMPARE:
        break; /* nothing owned */

      case AST_INST_I:
        if (node->u.inst_i.imm) free(node->u.inst_i.imm);
        break;

      case AST_INST_J:
        if (node->u.inst_j.target) free(node->u.inst_j.target);
        break;

      case AST_PSEUDO:
        free(node->u.pseudo.mnemonic);
        ast_free(node->u.pseudo.child);
        break;

      case AST_DATA_BYTE:
      case AST_DATA_HALF:
      case AST_DATA_WORD:
        free_imm_array(node->u.data_int.exprs, node->u.data_int.count);
        break;

      case AST_DATA_FLOAT:
      case AST_DATA_DOUBLE:
        free(node->u.data_fp.values);
        break;

      case AST_DATA_STRING:
        free(node->u.data_string.bytes);
        break;

      case AST_DIR_TEXT:
      case AST_DIR_DATA:
      case AST_DIR_KTEXT:
      case AST_DIR_KDATA:
      case AST_DIR_ALIGN:
      case AST_DIR_SPACE:
        break; /* nothing owned */

      case AST_DIR_GLOBL:
        free(node->u.dir_globl.name);
        break;

      case AST_DIR_EXTERN:
      case AST_DIR_COMM:
        free(node->u.dir_named_size.name);
        break;

      case AST_LABEL_DEF:
        free(node->u.label_def.name);
        break;

      case AST_FILE:
        ast_free(node->u.file.child);
        free(node->u.file.source_file);
        break;

      case AST_ERROR:
        free(node->u.error.message);
        break;
    }

    free(node);
    node = next_sibling;
  }
}

/* ------------------------------------------------------------------ */
/* ast_print — indented text dump                                      */
/* ------------------------------------------------------------------ */

static void indent(FILE* out, int depth) {
  for (int i = 0; i < depth; i++) fputs("  ", out);
}

/* Render an imm_expr to a short string.  Used for both data values
   and instruction immediates. */
static void print_imm(const imm_expr* e, FILE* out) {
  if (e == nullptr) {
    fputs("(null)", out);
    return;
  }
  if (e->symbol != nullptr && e->symbol->name != nullptr) {
    fprintf(out, "%s", e->symbol->name);
    if (e->offset > 0) fprintf(out, "+%d", e->offset);
    if (e->offset < 0)
      fprintf(out, "%d", e->offset); /* the - is in the number */
    if (e->pc_relative) fputs(" (pc-rel)", out);
  } else {
    fprintf(out, "%d", e->offset);
  }
}

static void print_node(const ast_node* node, FILE* out, int depth);

static void print_chain(const ast_node* head, FILE* out, int depth) {
  for (const ast_node* n = head; n != nullptr; n = n->next)
    print_node(n, out, depth);
}

static void print_node(const ast_node* node, FILE* out, int depth) {
  if (node == nullptr) return;

  indent(out, depth);
  fprintf(out, "[line %d] ", node->source_line);

  switch (node->kind) {
    case AST_FILE:
      fprintf(out, "FILE source=%s\n",
              node->u.file.source_file ? node->u.file.source_file : "(stdin)");
      print_chain(node->u.file.child, out, depth + 1);
      return;

    case AST_INST_R:
      fprintf(out, "INST_R op=%d rd=%d rs=%d rt=%d\n", node->u.inst_r.op,
              node->u.inst_r.rd, node->u.inst_r.rs, node->u.inst_r.rt);
      return;

    case AST_INST_R_SHIFT:
      fprintf(out, "INST_R_SHIFT op=%d rd=%d rt=%d shamt=%d\n",
              node->u.inst_r_shift.op, node->u.inst_r_shift.rd,
              node->u.inst_r_shift.rt, node->u.inst_r_shift.shamt);
      return;

    case AST_INST_I:
      fprintf(out, "INST_I op=%d rt=%d rs=%d imm=", node->u.inst_i.op,
              node->u.inst_i.rt, node->u.inst_i.rs);
      print_imm(node->u.inst_i.imm, out);
      fputc('\n', out);
      return;

    case AST_INST_J:
      fprintf(out, "INST_J op=%d target=", node->u.inst_j.op);
      print_imm(node->u.inst_j.target, out);
      fputc('\n', out);
      return;

    case AST_INST_FP_R:
      fprintf(out, "INST_FP_R op=%d fd=%d fs=%d ft=%d\n", node->u.inst_fp_r.op,
              node->u.inst_fp_r.fd, node->u.inst_fp_r.fs, node->u.inst_fp_r.ft);
      return;

    case AST_INST_FP_COMPARE:
      fprintf(out, "INST_FP_COMPARE op=%d fs=%d ft=%d cc=%d\n",
              node->u.inst_fp_compare.op, node->u.inst_fp_compare.fs,
              node->u.inst_fp_compare.ft, node->u.inst_fp_compare.cc);
      return;

    case AST_PSEUDO:
      fprintf(out, "PSEUDO mnemonic=%s\n", node->u.pseudo.mnemonic);
      print_chain(node->u.pseudo.child, out, depth + 1);
      return;

    case AST_DATA_BYTE:
    case AST_DATA_HALF:
    case AST_DATA_WORD: {
      const char* tag = node->kind == AST_DATA_BYTE   ? "DATA_BYTE"
                        : node->kind == AST_DATA_HALF ? "DATA_HALF"
                                                      : "DATA_WORD";
      fprintf(out, "%s count=%d values=[", tag, node->u.data_int.count);
      for (int i = 0; i < node->u.data_int.count; i++) {
        if (i > 0) fputs(", ", out);
        print_imm(node->u.data_int.exprs[i], out);
      }
      fputs("]\n", out);
      return;
    }

    case AST_DATA_FLOAT:
    case AST_DATA_DOUBLE: {
      const char* tag =
          node->kind == AST_DATA_FLOAT ? "DATA_FLOAT" : "DATA_DOUBLE";
      fprintf(out, "%s count=%d values=[", tag, node->u.data_fp.count);
      for (int i = 0; i < node->u.data_fp.count; i++) {
        if (i > 0) fputs(", ", out);
        fprintf(out, "%g", node->u.data_fp.values[i]);
      }
      fputs("]\n", out);
      return;
    }

    case AST_DATA_STRING:
      fprintf(out, "DATA_STRING len=%d null_term=%s value=\"",
              node->u.data_string.length,
              node->u.data_string.null_terminate ? "yes" : "no");
      for (int i = 0; i < node->u.data_string.length; i++) {
        unsigned char c = (unsigned char)node->u.data_string.bytes[i];
        if (c == '\n')
          fputs("\\n", out);
        else if (c == '\t')
          fputs("\\t", out);
        else if (c == '\\' || c == '"')
          fprintf(out, "\\%c", c);
        else if (c >= 0x20 && c < 0x7f)
          fputc(c, out);
        else
          fprintf(out, "\\x%02x", c);
      }
      fputs("\"\n", out);
      return;

    case AST_DIR_TEXT:
    case AST_DIR_DATA:
    case AST_DIR_KTEXT:
    case AST_DIR_KDATA: {
      const char* tag = node->kind == AST_DIR_TEXT    ? "DIR_TEXT"
                        : node->kind == AST_DIR_DATA  ? "DIR_DATA"
                        : node->kind == AST_DIR_KTEXT ? "DIR_KTEXT"
                                                      : "DIR_KDATA";
      if (node->u.dir_seg.has_start_addr)
        fprintf(out, "%s start=0x%08x\n", tag, node->u.dir_seg.start_addr);
      else
        fprintf(out, "%s\n", tag);
      return;
    }

    case AST_DIR_ALIGN:
      fprintf(out, "DIR_ALIGN n=%d\n", node->u.dir_align.n);
      return;

    case AST_DIR_SPACE:
      fprintf(out, "DIR_SPACE size=%d\n", node->u.dir_space.size);
      return;

    case AST_DIR_GLOBL:
      fprintf(out, "DIR_GLOBL name=%s\n", node->u.dir_globl.name);
      return;

    case AST_DIR_EXTERN:
    case AST_DIR_COMM: {
      const char* tag =
          node->kind == AST_DIR_EXTERN ? "DIR_EXTERN" : "DIR_COMM";
      fprintf(out, "%s name=%s size=%d\n", tag, node->u.dir_named_size.name,
              node->u.dir_named_size.size);
      return;
    }

    case AST_LABEL_DEF:
      if (node->u.label_def.kind == AST_LABEL_NORMAL)
        fprintf(out, "LABEL_DEF name=%s (placement)\n", node->u.label_def.name);
      else
        fprintf(out, "LABEL_DEF name=%s = %d (constant)\n",
                node->u.label_def.name, node->u.label_def.value);
      return;

    case AST_ERROR:
      fprintf(out, "ERROR %s\n",
              node->u.error.message ? node->u.error.message : "(no message)");
      return;
  }
}

void ast_print(const ast_node* node, FILE* out) { print_node(node, out, 0); }

/* ------------------------------------------------------------------ */
/* ast_print_json — JSON dump for external tooling                     */
/* ------------------------------------------------------------------ */

/* Each node renders as one JSON object:
     {"kind":"INST_R","source_line":12,"op":315,"mnemonic":"addu",
      "rd":4,"rs":0,"rt":5}
   AST_FILE / AST_PSEUDO carry their child chain as "children":[...].
   imm_expr renders as {"value":N,"symbol":null} or
     {"value":N,"symbol":"foo","offset":4,"pc_relative":false}.
   String fields are escaped per RFC 8259 (NUL/control bytes become
   \uXXXX). */

static void json_escape_string(const char* s, int len, FILE* out) {
  fputc('"', out);
  if (s == nullptr) {
    fputc('"', out);
    return;
  }
  /* If len < 0, treat as NUL-terminated. */
  int n = (len < 0) ? (int)strlen(s) : len;
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':
        fputs("\\\"", out);
        break;
      case '\\':
        fputs("\\\\", out);
        break;
      case '\n':
        fputs("\\n", out);
        break;
      case '\r':
        fputs("\\r", out);
        break;
      case '\t':
        fputs("\\t", out);
        break;
      case '\b':
        fputs("\\b", out);
        break;
      case '\f':
        fputs("\\f", out);
        break;
      default:
        if (c < 0x20)
          fprintf(out, "\\u%04x", c);
        else
          fputc(c, out);
    }
  }
  fputc('"', out);
}

static void json_imm(const imm_expr* e, FILE* out) {
  if (e == nullptr) {
    fputs("null", out);
    return;
  }
  fputs("{\"offset\":", out);
  fprintf(out, "%d", e->offset);
  fputs(",\"symbol\":", out);
  if (e->symbol != nullptr && e->symbol->name != nullptr) {
    json_escape_string(e->symbol->name, -1, out);
  } else {
    fputs("null", out);
  }
  fputs(",\"pc_relative\":", out);
  fputs(e->pc_relative ? "true" : "false", out);
  fputc('}', out);
}

static void json_kind_header(const ast_node* node, FILE* out) {
  const char* kind_name = "?";
  switch (node->kind) {
    case AST_INST_R:
      kind_name = "INST_R";
      break;
    case AST_INST_R_SHIFT:
      kind_name = "INST_R_SHIFT";
      break;
    case AST_INST_I:
      kind_name = "INST_I";
      break;
    case AST_INST_J:
      kind_name = "INST_J";
      break;
    case AST_INST_FP_R:
      kind_name = "INST_FP_R";
      break;
    case AST_INST_FP_COMPARE:
      kind_name = "INST_FP_COMPARE";
      break;
    case AST_PSEUDO:
      kind_name = "PSEUDO";
      break;
    case AST_DATA_BYTE:
      kind_name = "DATA_BYTE";
      break;
    case AST_DATA_HALF:
      kind_name = "DATA_HALF";
      break;
    case AST_DATA_WORD:
      kind_name = "DATA_WORD";
      break;
    case AST_DATA_FLOAT:
      kind_name = "DATA_FLOAT";
      break;
    case AST_DATA_DOUBLE:
      kind_name = "DATA_DOUBLE";
      break;
    case AST_DATA_STRING:
      kind_name = "DATA_STRING";
      break;
    case AST_DIR_TEXT:
      kind_name = "DIR_TEXT";
      break;
    case AST_DIR_DATA:
      kind_name = "DIR_DATA";
      break;
    case AST_DIR_KTEXT:
      kind_name = "DIR_KTEXT";
      break;
    case AST_DIR_KDATA:
      kind_name = "DIR_KDATA";
      break;
    case AST_DIR_ALIGN:
      kind_name = "DIR_ALIGN";
      break;
    case AST_DIR_SPACE:
      kind_name = "DIR_SPACE";
      break;
    case AST_DIR_GLOBL:
      kind_name = "DIR_GLOBL";
      break;
    case AST_DIR_EXTERN:
      kind_name = "DIR_EXTERN";
      break;
    case AST_DIR_COMM:
      kind_name = "DIR_COMM";
      break;
    case AST_LABEL_DEF:
      kind_name = "LABEL_DEF";
      break;
    case AST_FILE:
      kind_name = "FILE";
      break;
    case AST_ERROR:
      kind_name = "ERROR";
      break;
  }
  fputs("{\"kind\":\"", out);
  fputs(kind_name, out);
  fputs("\",\"source_line\":", out);
  fprintf(out, "%d", node->source_line);
}

static void json_node(const ast_node* node, FILE* out);

static void json_chain(const ast_node* head, FILE* out) {
  fputc('[', out);
  bool first = true;
  for (const ast_node* n = head; n != nullptr; n = n->next) {
    if (!first) fputc(',', out);
    first = false;
    json_node(n, out);
  }
  fputc(']', out);
}

static void json_imm_array(imm_expr* const* arr, int n, FILE* out) {
  fputc('[', out);
  for (int i = 0; i < n; i++) {
    if (i > 0) fputc(',', out);
    json_imm(arr[i], out);
  }
  fputc(']', out);
}

static void json_node(const ast_node* node, FILE* out) {
  if (node == nullptr) {
    fputs("null", out);
    return;
  }
  json_kind_header(node, out);

  switch (node->kind) {
    case AST_FILE:
      if (node->u.file.source_file != nullptr) {
        fputs(",\"source_file\":", out);
        json_escape_string(node->u.file.source_file, -1, out);
      }
      fputs(",\"children\":", out);
      json_chain(node->u.file.child, out);
      break;

    case AST_INST_R:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_r.op);
      json_escape_string(op_token_name(node->u.inst_r.op), -1, out);
      fprintf(out, ",\"rd\":%d,\"rs\":%d,\"rt\":%d", node->u.inst_r.rd,
              node->u.inst_r.rs, node->u.inst_r.rt);
      break;

    case AST_INST_R_SHIFT:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_r_shift.op);
      json_escape_string(op_token_name(node->u.inst_r_shift.op), -1, out);
      fprintf(out, ",\"rd\":%d,\"rt\":%d,\"shamt\":%d", node->u.inst_r_shift.rd,
              node->u.inst_r_shift.rt, node->u.inst_r_shift.shamt);
      break;

    case AST_INST_I:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_i.op);
      json_escape_string(op_token_name(node->u.inst_i.op), -1, out);
      fprintf(out, ",\"rt\":%d,\"rs\":%d,\"imm\":", node->u.inst_i.rt,
              node->u.inst_i.rs);
      json_imm(node->u.inst_i.imm, out);
      break;

    case AST_INST_J:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_j.op);
      json_escape_string(op_token_name(node->u.inst_j.op), -1, out);
      fputs(",\"target\":", out);
      json_imm(node->u.inst_j.target, out);
      break;

    case AST_INST_FP_R:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_fp_r.op);
      json_escape_string(op_token_name(node->u.inst_fp_r.op), -1, out);
      fprintf(out, ",\"fd\":%d,\"fs\":%d,\"ft\":%d", node->u.inst_fp_r.fd,
              node->u.inst_fp_r.fs, node->u.inst_fp_r.ft);
      break;

    case AST_INST_FP_COMPARE:
      fprintf(out, ",\"op\":%d,\"mnemonic\":", node->u.inst_fp_compare.op);
      json_escape_string(op_token_name(node->u.inst_fp_compare.op), -1, out);
      fprintf(out, ",\"fs\":%d,\"ft\":%d,\"cc\":%d", node->u.inst_fp_compare.fs,
              node->u.inst_fp_compare.ft, node->u.inst_fp_compare.cc);
      break;

    case AST_PSEUDO:
      fputs(",\"mnemonic\":", out);
      json_escape_string(node->u.pseudo.mnemonic, -1, out);
      fputs(",\"children\":", out);
      json_chain(node->u.pseudo.child, out);
      break;

    case AST_DATA_BYTE:
    case AST_DATA_HALF:
    case AST_DATA_WORD:
      fprintf(out, ",\"count\":%d,\"values\":", node->u.data_int.count);
      json_imm_array(node->u.data_int.exprs, node->u.data_int.count, out);
      break;

    case AST_DATA_FLOAT:
    case AST_DATA_DOUBLE: {
      fprintf(out, ",\"count\":%d,\"values\":[", node->u.data_fp.count);
      for (int i = 0; i < node->u.data_fp.count; i++) {
        if (i > 0) fputc(',', out);
        fprintf(out, "%.17g", node->u.data_fp.values[i]);
      }
      fputc(']', out);
      break;
    }

    case AST_DATA_STRING:
      fprintf(out, ",\"length\":%d,\"null_terminate\":%s,\"value\":",
              node->u.data_string.length,
              node->u.data_string.null_terminate ? "true" : "false");
      json_escape_string(node->u.data_string.bytes, node->u.data_string.length,
                         out);
      break;

    case AST_DIR_TEXT:
    case AST_DIR_DATA:
    case AST_DIR_KTEXT:
    case AST_DIR_KDATA:
      if (node->u.dir_seg.has_start_addr) {
        fprintf(out, ",\"start_addr\":%u",
                (unsigned)node->u.dir_seg.start_addr);
      }
      break;

    case AST_DIR_ALIGN:
      fprintf(out, ",\"n\":%d", node->u.dir_align.n);
      break;

    case AST_DIR_SPACE:
      fprintf(out, ",\"size\":%d", node->u.dir_space.size);
      break;

    case AST_DIR_GLOBL:
      fputs(",\"name\":", out);
      json_escape_string(node->u.dir_globl.name, -1, out);
      break;

    case AST_DIR_EXTERN:
    case AST_DIR_COMM:
      fputs(",\"name\":", out);
      json_escape_string(node->u.dir_named_size.name, -1, out);
      fprintf(out, ",\"size\":%d", node->u.dir_named_size.size);
      break;

    case AST_LABEL_DEF:
      fputs(",\"name\":", out);
      json_escape_string(node->u.label_def.name, -1, out);
      fputs(",\"label_kind\":\"", out);
      fputs(
          node->u.label_def.kind == AST_LABEL_NORMAL ? "placement" : "constant",
          out);
      fputc('"', out);
      if (node->u.label_def.kind == AST_LABEL_CONST) {
        fprintf(out, ",\"value\":%d", node->u.label_def.value);
      }
      break;

    case AST_ERROR:
      fputs(",\"message\":", out);
      json_escape_string(node->u.error.message ? node->u.error.message : "", -1,
                         out);
      break;
  }

  fputc('}', out);
}

void ast_print_json(const ast_node* node, FILE* out) {
  json_node(node, out);
  fputc('\n', out);
}
