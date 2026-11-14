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
#include "spim-utils.h"   /* xmalloc                    */
#include "scanner.h"      /* line_no                      */
#include "spim.h"         /* fatal_error                  */
#include "symbol-table.h" /* struct label definition      */
#include "tokens.h"       /* TOKEN_*_OPCODE names for ast_print */

/* ------------------------------------------------------------------ */
/* Internal node allocator                                             */
/* ------------------------------------------------------------------ */

static ast_node* new_node(ast_node initialize) {
  ast_node* n = (ast_node*)allocate_or_exit(sizeof(ast_node));
  /* The caller's compound literal zero-initializes every member it does
     not name (including the sibling link).  Stamp the source position
     now, while the scanner is still on this node's line; emit_ast
     replays source_text at emit time (see ast_node.source_text). */
  initialize.source_line = line_no;
  initialize.source_text = source_line();
  *n = initialize;
  return n;
}

/* ------------------------------------------------------------------ */
/* Instruction constructors                                            */
/* ------------------------------------------------------------------ */

ast_node* ast_make_instruction_r(int opcode, int destination_register,
                                 int source_register, int target_register) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_R,
      .payload.instruction_r = {.opcode = opcode,
                                .destination_register = destination_register,
                                .source_register = source_register,
                                .target_register = target_register},
  });
}

ast_node* ast_make_instruction_r_shift(int opcode, int destination_register,
                                       int target_register, int shift_amount) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_R_SHIFT,
      .payload.instruction_r_shift = {.opcode = opcode,
                                      .destination_register =
                                          destination_register,
                                      .target_register = target_register,
                                      .shift_amount = shift_amount},
  });
}

ast_node* ast_make_instruction_i(int opcode, int target_register,
                                 int source_register,
                                 immediate_expression* immediate) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_I,
      .payload.instruction_i = {.opcode = opcode,
                                .target_register = target_register,
                                .source_register = source_register,
                                .immediate = immediate},
  });
}

ast_node* ast_make_instruction_j(int opcode, immediate_expression* target) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_J,
      .payload.instruction_j = {.opcode = opcode, .target = target},
  });
}

ast_node* ast_make_instruction_floating_point_r(int opcode,
                                                int floating_point_destination,
                                                int floating_point_source,
                                                int floating_point_target) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_FLOATING_POINT_R,
      .payload.instruction_floating_point_r =
          {.opcode = opcode,
           .floating_point_destination = floating_point_destination,
           .floating_point_source = floating_point_source,
           .floating_point_target = floating_point_target},
  });
}

ast_node* ast_make_instruction_floating_point_compare(int opcode,
                                                      int floating_point_source,
                                                      int floating_point_target,
                                                      int condition_code) {
  return new_node((ast_node){
      .kind = AST_INSTRUCTION_FLOATING_POINT_COMPARE,
      .payload.instruction_floating_point_compare =
          {.opcode = opcode,
           .floating_point_source = floating_point_source,
           .floating_point_target = floating_point_target,
           .condition_code = condition_code},
  });
}

ast_node* ast_make_pseudo(const char* mnemonic) {
  return new_node((ast_node){
      .kind = AST_PSEUDO,
      .payload.pseudo = {.mnemonic = strdup(mnemonic)},
  });
}

/* ------------------------------------------------------------------ */
/* Data constructors                                                   */
/* ------------------------------------------------------------------ */

static ast_node* make_data_integer(ast_kind kind, int count,
                                   immediate_expression** expressions) {
  return new_node((ast_node){
      .kind = kind,
      .payload.data_integer = {.count = count, .expressions = expressions},
  });
}

ast_node* ast_make_data_byte(int count, immediate_expression** expressions) {
  return make_data_integer(AST_DATA_BYTE, count, expressions);
}

ast_node* ast_make_data_half(int count, immediate_expression** expressions) {
  return make_data_integer(AST_DATA_HALF, count, expressions);
}

ast_node* ast_make_data_word(int count, immediate_expression** expressions) {
  return make_data_integer(AST_DATA_WORD, count, expressions);
}

static ast_node* make_data_floating_point(ast_kind kind, int count,
                                          double* values) {
  return new_node((ast_node){
      .kind = kind,
      .payload.data_floating_point = {.count = count, .values = values},
  });
}

ast_node* ast_make_data_float(int count, double* values) {
  return make_data_floating_point(AST_DATA_FLOAT, count, values);
}

ast_node* ast_make_data_double(int count, double* values) {
  return make_data_floating_point(AST_DATA_DOUBLE, count, values);
}

ast_node* ast_make_data_string(const char* bytes, int length, bool null_term) {
  /* Copy the bytes verbatim (may contain NULs); +1 so callers can also
     read it as a C-string when no embedded NULs are present. */
  char* buffer = (char*)allocate_or_exit((size_t)length + 1);
  memcpy(buffer, bytes, (size_t)length);
  buffer[length] = '\0';
  return new_node((ast_node){
      .kind = AST_DATA_STRING,
      .payload.data_string = {.bytes = buffer,
                              .length = length,
                              .null_terminate = null_term},
  });
}

/* ------------------------------------------------------------------ */
/* Directive constructors                                              */
/* ------------------------------------------------------------------ */

static ast_node* make_directive_segment(ast_kind kind, bool has,
                                        memory_address address) {
  return new_node((ast_node){
      .kind = kind,
      .payload.directive_segment = {.has_start_address = has,
                                    .start_address = address},
  });
}

ast_node* ast_make_directive_text(bool has_start_address,
                                  memory_address start_address) {
  return make_directive_segment(AST_DIRECTIVE_TEXT, has_start_address,
                                start_address);
}

ast_node* ast_make_directive_data(bool has_start_address,
                                  memory_address start_address) {
  return make_directive_segment(AST_DIRECTIVE_DATA, has_start_address,
                                start_address);
}

ast_node* ast_make_directive_ktext(bool has_start_address,
                                   memory_address start_address) {
  return make_directive_segment(AST_DIRECTIVE_KTEXT, has_start_address,
                                start_address);
}

ast_node* ast_make_directive_kdata(bool has_start_address,
                                   memory_address start_address) {
  return make_directive_segment(AST_DIRECTIVE_KDATA, has_start_address,
                                start_address);
}

ast_node* ast_make_directive_align(int n_) {
  return new_node((ast_node){
      .kind = AST_DIRECTIVE_ALIGN,
      .payload.directive_align = {.alignment_power = n_},
  });
}

ast_node* ast_make_directive_space(int size) {
  return new_node((ast_node){
      .kind = AST_DIRECTIVE_SPACE,
      .payload.directive_space = {.size = size},
  });
}

ast_node* ast_make_directive_globl(const char* name) {
  return new_node((ast_node){
      .kind = AST_DIRECTIVE_GLOBL,
      .payload.directive_globl = {.name = strdup(name)},
  });
}

static ast_node* make_named_size(ast_kind kind, const char* name, int size) {
  return new_node((ast_node){
      .kind = kind,
      .payload.directive_named_size = {.name = strdup(name), .size = size},
  });
}

ast_node* ast_make_directive_extern(const char* name, int size) {
  return make_named_size(AST_DIRECTIVE_EXTERN, name, size);
}

ast_node* ast_make_directive_comm(const char* name, int size) {
  return make_named_size(AST_DIRECTIVE_COMM, name, size);
}

/* ------------------------------------------------------------------ */
/* Label constructors                                                  */
/* ------------------------------------------------------------------ */

ast_node* ast_make_label_normal(const char* name) {
  return new_node((ast_node){
      .kind = AST_LABEL_DEFINITION,
      .payload.label_definition = {.kind = AST_LABEL_NORMAL,
                                   .name = strdup(name)},
  });
}

ast_node* ast_make_label_constant(const char* name, int32_t value) {
  return new_node((ast_node){
      .kind = AST_LABEL_DEFINITION,
      .payload.label_definition = {.kind = AST_LABEL_CONSTANT,
                                   .name = strdup(name),
                                   .value = value},
  });
}

/* ------------------------------------------------------------------ */
/* File + error                                                        */
/* ------------------------------------------------------------------ */

ast_node* ast_make_file(const char* source_file) {
  return new_node((ast_node){
      .kind = AST_FILE,
      .payload.file = {.source_file =
                           source_file ? strdup(source_file) : nullptr},
  });
}

ast_node* ast_make_error(const char* message) {
  return new_node((ast_node){
      .kind = AST_ERROR,
      .payload.error = {.message = message ? strdup(message) : nullptr},
  });
}

/* ------------------------------------------------------------------ */
/* Tree manipulation                                                   */
/* ------------------------------------------------------------------ */

ast_node* ast_file_append(ast_node* file, ast_node* statement) {
  if (file == nullptr || statement == nullptr) {
    return file;
  } else if (file->payload.file.child == nullptr) {
    file->payload.file.child = statement;
    return file;
  } else {
    ast_node* t = file->payload.file.child;
    while (t->next != nullptr) t = t->next;
    t->next = statement;
    return file;
  }
}

ast_node* ast_pseudo_append(ast_node* pseudo, ast_node* child) {
  if (pseudo == nullptr || child == nullptr) {
    return pseudo;
  } else if (pseudo->payload.pseudo.child == nullptr) {
    pseudo->payload.pseudo.child = child;
    return pseudo;
  } else {
    ast_node* t = pseudo->payload.pseudo.child;
    while (t->next != nullptr) t = t->next;
    t->next = child;
    return pseudo;
  }
}

/* ------------------------------------------------------------------ */
/* ast_free — recursive cleanup                                        */
/* ------------------------------------------------------------------ */

static void free_immediate_array(immediate_expression** array, int n) {
  if (array == nullptr) return;
  for (int i = 0; i < n; i++)
    if (array[i] != nullptr) free(array[i]);
  free(array);
}

void ast_free(ast_node* node) {
  while (node != nullptr) {
    ast_node* const next_sibling = node->next;

    switch (node->kind) {
      case AST_INSTRUCTION_R:
      case AST_INSTRUCTION_R_SHIFT:
      case AST_INSTRUCTION_FLOATING_POINT_R:
      case AST_INSTRUCTION_FLOATING_POINT_COMPARE:
        break; /* nothing owned */

      case AST_INSTRUCTION_I:
        if (node->payload.instruction_i.immediate)
          free(node->payload.instruction_i.immediate);
        break;

      case AST_INSTRUCTION_J:
        if (node->payload.instruction_j.target)
          free(node->payload.instruction_j.target);
        break;

      case AST_PSEUDO:
        free(node->payload.pseudo.mnemonic);
        ast_free(node->payload.pseudo.child);
        break;

      case AST_DATA_BYTE:
      case AST_DATA_HALF:
      case AST_DATA_WORD:
        free_immediate_array(node->payload.data_integer.expressions,
                             node->payload.data_integer.count);
        break;

      case AST_DATA_FLOAT:
      case AST_DATA_DOUBLE:
        free(node->payload.data_floating_point.values);
        break;

      case AST_DATA_STRING:
        free(node->payload.data_string.bytes);
        break;

      case AST_DIRECTIVE_TEXT:
      case AST_DIRECTIVE_DATA:
      case AST_DIRECTIVE_KTEXT:
      case AST_DIRECTIVE_KDATA:
      case AST_DIRECTIVE_ALIGN:
      case AST_DIRECTIVE_SPACE:
        break; /* nothing owned */

      case AST_DIRECTIVE_GLOBL:
        free(node->payload.directive_globl.name);
        break;

      case AST_DIRECTIVE_EXTERN:
      case AST_DIRECTIVE_COMM:
        free(node->payload.directive_named_size.name);
        break;

      case AST_LABEL_DEFINITION:
        free(node->payload.label_definition.name);
        break;

      case AST_FILE:
        ast_free(node->payload.file.child);
        free(node->payload.file.source_file);
        break;

      case AST_ERROR:
        free(node->payload.error.message);
        break;
      default:
        /* Never reached: the cases above list every ast_kind, and the build's
         * -Wswitch-enum fails if a new one is added without a case. */
        fatal_error("internal error: ast_free: unknown AST node kind %d\n",
                    (int)node->kind);
    }

    free(node->source_text);
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

/* Render an immediate_expression to a short string.  Used for both data values
   and instruction immediates. */
static void print_immediate(const immediate_expression* e, FILE* out) {
  if (e == nullptr) {
    fputs("(null)", out);
    return;
  }
  if (e->symbol != nullptr && e->symbol->name != nullptr) {
    fprintf(out, "%s", e->symbol->name);
    if (e->offset > 0) fprintf(out, "+%d", e->offset);
    if (e->offset < 0)
      fprintf(out, "%d", e->offset); /* the - is in the number */
    if (e->program_counter_relative) fputs(" (pc-rel)", out);
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
              node->payload.file.source_file ? node->payload.file.source_file
                                             : "(stdin)");
      print_chain(node->payload.file.child, out, depth + 1);
      return;

    case AST_INSTRUCTION_R:
      fprintf(out, "INST_R op=%s rd=%d rs=%d rt=%d\n",
              opcode_token_name(node->payload.instruction_r.opcode),
              node->payload.instruction_r.destination_register,
              node->payload.instruction_r.source_register,
              node->payload.instruction_r.target_register);
      return;

    case AST_INSTRUCTION_R_SHIFT:
      fprintf(out, "INST_R_SHIFT op=%s rd=%d rt=%d shamt=%d\n",
              opcode_token_name(node->payload.instruction_r_shift.opcode),
              node->payload.instruction_r_shift.destination_register,
              node->payload.instruction_r_shift.target_register,
              node->payload.instruction_r_shift.shift_amount);
      return;

    case AST_INSTRUCTION_I:
      fprintf(out, "INST_I op=%s rt=%d rs=%d imm=",
              opcode_token_name(node->payload.instruction_i.opcode),
              node->payload.instruction_i.target_register,
              node->payload.instruction_i.source_register);
      print_immediate(node->payload.instruction_i.immediate, out);
      fputc('\n', out);
      return;

    case AST_INSTRUCTION_J:
      fprintf(out, "INST_J op=%s target=",
              opcode_token_name(node->payload.instruction_j.opcode));
      print_immediate(node->payload.instruction_j.target, out);
      fputc('\n', out);
      return;

    case AST_INSTRUCTION_FLOATING_POINT_R:
      fprintf(
          out, "INST_FP_R op=%s fd=%d fs=%d ft=%d\n",
          opcode_token_name(node->payload.instruction_floating_point_r.opcode),
          node->payload.instruction_floating_point_r.floating_point_destination,
          node->payload.instruction_floating_point_r.floating_point_source,
          node->payload.instruction_floating_point_r.floating_point_target);
      return;

    case AST_INSTRUCTION_FLOATING_POINT_COMPARE:
      fprintf(out, "INST_FP_COMPARE op=%s fs=%d ft=%d cc=%d\n",
              opcode_token_name(
                  node->payload.instruction_floating_point_compare.opcode),
              node->payload.instruction_floating_point_compare
                  .floating_point_source,
              node->payload.instruction_floating_point_compare
                  .floating_point_target,
              node->payload.instruction_floating_point_compare.condition_code);
      return;

    case AST_PSEUDO:
      fprintf(out, "PSEUDO mnemonic=%s\n", node->payload.pseudo.mnemonic);
      print_chain(node->payload.pseudo.child, out, depth + 1);
      return;

    case AST_DATA_BYTE:
    case AST_DATA_HALF:
    case AST_DATA_WORD: {
      const char* const tag = node->kind == AST_DATA_BYTE   ? "DATA_BYTE"
                              : node->kind == AST_DATA_HALF ? "DATA_HALF"
                                                            : "DATA_WORD";
      fprintf(out, "%s count=%d values=[", tag,
              node->payload.data_integer.count);
      for (int i = 0; i < node->payload.data_integer.count; i++) {
        if (i > 0) fputs(", ", out);
        print_immediate(node->payload.data_integer.expressions[i], out);
      }
      fputs("]\n", out);
      return;
    }

    case AST_DATA_FLOAT:
    case AST_DATA_DOUBLE: {
      const char* const tag =
          node->kind == AST_DATA_FLOAT ? "DATA_FLOAT" : "DATA_DOUBLE";
      fprintf(out, "%s count=%d values=[", tag,
              node->payload.data_floating_point.count);
      for (int i = 0; i < node->payload.data_floating_point.count; i++) {
        if (i > 0) fputs(", ", out);
        fprintf(out, "%g", node->payload.data_floating_point.values[i]);
      }
      fputs("]\n", out);
      return;
    }

    case AST_DATA_STRING:
      fprintf(out, "DATA_STRING len=%d null_term=%s value=\"",
              node->payload.data_string.length,
              node->payload.data_string.null_terminate ? "yes" : "no");
      for (int i = 0; i < node->payload.data_string.length; i++) {
        const unsigned char c =
            (unsigned char)node->payload.data_string.bytes[i];
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

    case AST_DIRECTIVE_TEXT:
    case AST_DIRECTIVE_DATA:
    case AST_DIRECTIVE_KTEXT:
    case AST_DIRECTIVE_KDATA: {
      const char* const tag = node->kind == AST_DIRECTIVE_TEXT    ? "DIR_TEXT"
                              : node->kind == AST_DIRECTIVE_DATA  ? "DIR_DATA"
                              : node->kind == AST_DIRECTIVE_KTEXT ? "DIR_KTEXT"
                                                                  : "DIR_KDATA";
      if (node->payload.directive_segment.has_start_address)
        fprintf(out, "%s start=0x%08x\n", tag,
                node->payload.directive_segment.start_address);
      else
        fprintf(out, "%s\n", tag);
      return;
    }

    case AST_DIRECTIVE_ALIGN:
      fprintf(out, "DIR_ALIGN n=%d\n",
              node->payload.directive_align.alignment_power);
      return;

    case AST_DIRECTIVE_SPACE:
      fprintf(out, "DIR_SPACE size=%d\n", node->payload.directive_space.size);
      return;

    case AST_DIRECTIVE_GLOBL:
      fprintf(out, "DIR_GLOBL name=%s\n", node->payload.directive_globl.name);
      return;

    case AST_DIRECTIVE_EXTERN:
    case AST_DIRECTIVE_COMM: {
      const char* const tag =
          node->kind == AST_DIRECTIVE_EXTERN ? "DIR_EXTERN" : "DIR_COMM";
      fprintf(out, "%s name=%s size=%d\n", tag,
              node->payload.directive_named_size.name,
              node->payload.directive_named_size.size);
      return;
    }

    case AST_LABEL_DEFINITION:
      if (node->payload.label_definition.kind == AST_LABEL_NORMAL)
        fprintf(out, "LABEL_DEF name=%s (placement)\n",
                node->payload.label_definition.name);
      else
        fprintf(out, "LABEL_DEF name=%s = %d (constant)\n",
                node->payload.label_definition.name,
                node->payload.label_definition.value);
      return;

    case AST_ERROR:
      fprintf(out, "ERROR %s\n",
              node->payload.error.message ? node->payload.error.message
                                          : "(no message)");
      return;
    default:
      /* Never reached: the cases above list every ast_kind, and the build's
       * -Wswitch-enum fails if a new one is added without a case. */
      fatal_error("internal error: print_node: unknown AST node kind %d\n",
                  (int)node->kind);
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
   immediate_expression renders as {"value":N,"symbol":null} or
     {"value":N,"symbol":"foo","offset":4,"program_counter_relative":false}.
   String fields are escaped per RFC 8259 (NUL/control bytes become
   \uXXXX). */

static void json_escape_string(const char* s, int length, FILE* out) {
  fputc('"', out);
  if (s == nullptr) {
    fputc('"', out);
    return;
  }
  /* If len < 0, treat as NUL-terminated. */
  const int n = (length < 0) ? (int)strlen(s) : length;
  for (int i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)s[i];
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

static void json_immediate(const immediate_expression* e, FILE* out) {
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
  fputs(e->program_counter_relative ? "true" : "false", out);
  fputc('}', out);
}

static void json_kind_header(const ast_node* node, FILE* out) {
  const char* kind_name = "?";
  switch (node->kind) {
    case AST_INSTRUCTION_R:
      kind_name = "INST_R";
      break;
    case AST_INSTRUCTION_R_SHIFT:
      kind_name = "INST_R_SHIFT";
      break;
    case AST_INSTRUCTION_I:
      kind_name = "INST_I";
      break;
    case AST_INSTRUCTION_J:
      kind_name = "INST_J";
      break;
    case AST_INSTRUCTION_FLOATING_POINT_R:
      kind_name = "INST_FP_R";
      break;
    case AST_INSTRUCTION_FLOATING_POINT_COMPARE:
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
    case AST_DIRECTIVE_TEXT:
      kind_name = "DIR_TEXT";
      break;
    case AST_DIRECTIVE_DATA:
      kind_name = "DIR_DATA";
      break;
    case AST_DIRECTIVE_KTEXT:
      kind_name = "DIR_KTEXT";
      break;
    case AST_DIRECTIVE_KDATA:
      kind_name = "DIR_KDATA";
      break;
    case AST_DIRECTIVE_ALIGN:
      kind_name = "DIR_ALIGN";
      break;
    case AST_DIRECTIVE_SPACE:
      kind_name = "DIR_SPACE";
      break;
    case AST_DIRECTIVE_GLOBL:
      kind_name = "DIR_GLOBL";
      break;
    case AST_DIRECTIVE_EXTERN:
      kind_name = "DIR_EXTERN";
      break;
    case AST_DIRECTIVE_COMM:
      kind_name = "DIR_COMM";
      break;
    case AST_LABEL_DEFINITION:
      kind_name = "LABEL_DEF";
      break;
    case AST_FILE:
      kind_name = "FILE";
      break;
    case AST_ERROR:
      kind_name = "ERROR";
      break;
    default:
      /* Never reached: the cases above list every ast_kind, and the build's
       * -Wswitch-enum fails if a new one is added without a case. */
      fatal_error(
          "internal error: json_kind_header: unknown AST node kind %d\n",
          (int)node->kind);
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

static void json_immediate_array(immediate_expression* const* array, int n,
                                 FILE* out) {
  fputc('[', out);
  for (int i = 0; i < n; i++) {
    if (i > 0) fputc(',', out);
    json_immediate(array[i], out);
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
      if (node->payload.file.source_file != nullptr) {
        fputs(",\"source_file\":", out);
        json_escape_string(node->payload.file.source_file, -1, out);
      }
      fputs(",\"children\":", out);
      json_chain(node->payload.file.child, out);
      break;

    case AST_INSTRUCTION_R:
      fprintf(out,
              ",\"op\":%d,\"mnemonic\":", node->payload.instruction_r.opcode);
      json_escape_string(opcode_token_name(node->payload.instruction_r.opcode),
                         -1, out);
      fprintf(out, ",\"rd\":%d,\"rs\":%d,\"rt\":%d",
              node->payload.instruction_r.destination_register,
              node->payload.instruction_r.source_register,
              node->payload.instruction_r.target_register);
      break;

    case AST_INSTRUCTION_R_SHIFT:
      fprintf(out, ",\"op\":%d,\"mnemonic\":",
              node->payload.instruction_r_shift.opcode);
      json_escape_string(
          opcode_token_name(node->payload.instruction_r_shift.opcode), -1, out);
      fprintf(out, ",\"rd\":%d,\"rt\":%d,\"shamt\":%d",
              node->payload.instruction_r_shift.destination_register,
              node->payload.instruction_r_shift.target_register,
              node->payload.instruction_r_shift.shift_amount);
      break;

    case AST_INSTRUCTION_I:
      fprintf(out,
              ",\"op\":%d,\"mnemonic\":", node->payload.instruction_i.opcode);
      json_escape_string(opcode_token_name(node->payload.instruction_i.opcode),
                         -1, out);
      fprintf(out, ",\"rt\":%d,\"rs\":%d,\"imm\":",
              node->payload.instruction_i.target_register,
              node->payload.instruction_i.source_register);
      json_immediate(node->payload.instruction_i.immediate, out);
      break;

    case AST_INSTRUCTION_J:
      fprintf(out,
              ",\"op\":%d,\"mnemonic\":", node->payload.instruction_j.opcode);
      json_escape_string(opcode_token_name(node->payload.instruction_j.opcode),
                         -1, out);
      fputs(",\"target\":", out);
      json_immediate(node->payload.instruction_j.target, out);
      break;

    case AST_INSTRUCTION_FLOATING_POINT_R:
      fprintf(out, ",\"op\":%d,\"mnemonic\":",
              node->payload.instruction_floating_point_r.opcode);
      json_escape_string(
          opcode_token_name(node->payload.instruction_floating_point_r.opcode),
          -1, out);
      fprintf(
          out, ",\"fd\":%d,\"fs\":%d,\"ft\":%d",
          node->payload.instruction_floating_point_r.floating_point_destination,
          node->payload.instruction_floating_point_r.floating_point_source,
          node->payload.instruction_floating_point_r.floating_point_target);
      break;

    case AST_INSTRUCTION_FLOATING_POINT_COMPARE:
      fprintf(out, ",\"op\":%d,\"mnemonic\":",
              node->payload.instruction_floating_point_compare.opcode);
      json_escape_string(
          opcode_token_name(
              node->payload.instruction_floating_point_compare.opcode),
          -1, out);
      fprintf(out, ",\"fs\":%d,\"ft\":%d,\"cc\":%d",
              node->payload.instruction_floating_point_compare
                  .floating_point_source,
              node->payload.instruction_floating_point_compare
                  .floating_point_target,
              node->payload.instruction_floating_point_compare.condition_code);
      break;

    case AST_PSEUDO:
      fputs(",\"mnemonic\":", out);
      json_escape_string(node->payload.pseudo.mnemonic, -1, out);
      fputs(",\"children\":", out);
      json_chain(node->payload.pseudo.child, out);
      break;

    case AST_DATA_BYTE:
    case AST_DATA_HALF:
    case AST_DATA_WORD:
      fprintf(out,
              ",\"count\":%d,\"values\":", node->payload.data_integer.count);
      json_immediate_array(node->payload.data_integer.expressions,
                           node->payload.data_integer.count, out);
      break;

    case AST_DATA_FLOAT:
    case AST_DATA_DOUBLE: {
      fprintf(out, ",\"count\":%d,\"values\":[",
              node->payload.data_floating_point.count);
      for (int i = 0; i < node->payload.data_floating_point.count; i++) {
        if (i > 0) fputc(',', out);
        fprintf(out, "%.17g", node->payload.data_floating_point.values[i]);
      }
      fputc(']', out);
      break;
    }

    case AST_DATA_STRING:
      fprintf(out, ",\"length\":%d,\"null_terminate\":%s,\"value\":",
              node->payload.data_string.length,
              node->payload.data_string.null_terminate ? "true" : "false");
      json_escape_string(node->payload.data_string.bytes,
                         node->payload.data_string.length, out);
      break;

    case AST_DIRECTIVE_TEXT:
    case AST_DIRECTIVE_DATA:
    case AST_DIRECTIVE_KTEXT:
    case AST_DIRECTIVE_KDATA:
      if (node->payload.directive_segment.has_start_address) {
        fprintf(out, ",\"start_addr\":%u",
                (unsigned)node->payload.directive_segment.start_address);
      }
      break;

    case AST_DIRECTIVE_ALIGN:
      fprintf(out, ",\"n\":%d", node->payload.directive_align.alignment_power);
      break;

    case AST_DIRECTIVE_SPACE:
      fprintf(out, ",\"size\":%d", node->payload.directive_space.size);
      break;

    case AST_DIRECTIVE_GLOBL:
      fputs(",\"name\":", out);
      json_escape_string(node->payload.directive_globl.name, -1, out);
      break;

    case AST_DIRECTIVE_EXTERN:
    case AST_DIRECTIVE_COMM:
      fputs(",\"name\":", out);
      json_escape_string(node->payload.directive_named_size.name, -1, out);
      fprintf(out, ",\"size\":%d", node->payload.directive_named_size.size);
      break;

    case AST_LABEL_DEFINITION:
      fputs(",\"name\":", out);
      json_escape_string(node->payload.label_definition.name, -1, out);
      fputs(",\"label_kind\":\"", out);
      fputs(node->payload.label_definition.kind == AST_LABEL_NORMAL
                ? "placement"
                : "constant",
            out);
      fputc('"', out);
      if (node->payload.label_definition.kind == AST_LABEL_CONSTANT) {
        fprintf(out, ",\"value\":%d", node->payload.label_definition.value);
      }
      break;

    case AST_ERROR:
      fputs(",\"message\":", out);
      json_escape_string(
          node->payload.error.message ? node->payload.error.message : "", -1,
          out);
      break;
    default:
      /* Never reached: the cases above list every ast_kind, and the build's
       * -Wswitch-enum fails if a new one is added without a case. */
      fatal_error("internal error: json_node: unknown AST node kind %d\n",
                  (int)node->kind);
  }

  fputc('}', out);
}

void ast_print_json(const ast_node* node, FILE* out) {
  json_node(node, out);
  fputc('\n', out);
}
