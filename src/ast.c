/* SPIM S20 MIPS simulator.
   AST constructors, free, and debug print.

   Pure additions for Phase 2b/2c.  Nothing in the simulator calls
   these yet — Phase 2d wires the parser to start producing nodes
   behind the -parser=ast flag.

   SPDX-License-Identifier: BSD-3-Clause */

#include <string.h>

#include "ast.h"
#include "spim-utils.h"   /* xmalloc, str_copy           */
#include "scanner.h"      /* line_no                      */
#include "sym-tbl.h"      /* struct lab definition        */
#include "tokens.h"       /* TOK_*_OP names for ast_print */

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
    if (e->offset < 0) fprintf(out, "%d", e->offset); /* the - is in the number */
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
      fprintf(out, "INST_R op=%d rd=%d rs=%d rt=%d\n",
              node->u.inst_r.op, node->u.inst_r.rd, node->u.inst_r.rs,
              node->u.inst_r.rt);
      return;

    case AST_INST_R_SHIFT:
      fprintf(out, "INST_R_SHIFT op=%d rd=%d rt=%d shamt=%d\n",
              node->u.inst_r_shift.op, node->u.inst_r_shift.rd,
              node->u.inst_r_shift.rt, node->u.inst_r_shift.shamt);
      return;

    case AST_INST_I:
      fprintf(out, "INST_I op=%d rt=%d rs=%d imm=",
              node->u.inst_i.op, node->u.inst_i.rt, node->u.inst_i.rs);
      print_imm(node->u.inst_i.imm, out);
      fputc('\n', out);
      return;

    case AST_INST_J:
      fprintf(out, "INST_J op=%d target=", node->u.inst_j.op);
      print_imm(node->u.inst_j.target, out);
      fputc('\n', out);
      return;

    case AST_INST_FP_R:
      fprintf(out, "INST_FP_R op=%d fd=%d fs=%d ft=%d\n",
              node->u.inst_fp_r.op, node->u.inst_fp_r.fd,
              node->u.inst_fp_r.fs, node->u.inst_fp_r.ft);
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
        fprintf(out, "LABEL_DEF name=%s (placement)\n",
                node->u.label_def.name);
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

void ast_print(const ast_node* node, FILE* out) {
  print_node(node, out, 0);
}
