/* SPIM S20 MIPS simulator.
   Abstract Syntax Tree node definitions.

   The parser running in PARSE_AST mode (the default) produces a tree
   of these nodes rather than calling the action helpers directly.
   `emit_ast` in parser.c walks the tree in source order and calls
   the action helpers (r_type_inst, store_word, record_label, ...)
   that commit each node's effect to the simulator's memory and
   symbol table.

   Three teaching surfaces consume the tree:
     -print-ast      indented text dump of every node
     -print-ast-json same as JSON for external tooling
     -show-expansion just the AST_PSEUDO wrappers and their children,
                     showing what each pseudo-op rewrites into

   Layout: tagged union (one ast_node struct with a discriminant +
   per-kind payload).  Trades a little wasted space per node for a
   uniform tree-walk shape.  At ~50-100 bytes/node and O(10k) nodes
   for a typical file, the overhead is irrelevant.

   Ownership: each ast_node owns
     - its own malloc'd struct
     - any "name" fields (free'd by ast_free)
     - any imm_expr* / addr_expr* it holds
     - the .next chain (recursively)
     - the .child chain (for AST_FILE and AST_PSEUDO)

   SPDX-License-Identifier: BSD-3-Clause */

#ifndef AST_H
#define AST_H

#include <stdio.h>

#include "spim.h"
#include "inst.h" /* for imm_expr, addr_expr */

/* ------------------------------------------------------------------ */
/* Node kinds                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
  /* Instructions */
  AST_INST_R,          /* op rd, rs, rt                     */
  AST_INST_R_SHIFT,    /* op rd, rt, shamt                  */
  AST_INST_I,          /* op rt, rs, imm                    */
  AST_INST_J,          /* op target                         */
  AST_INST_FP_R,       /* op fd, fs, ft                     */
  AST_INST_FP_COMPARE, /* op fs, ft, cc                     */
  AST_PSEUDO,          /* wrapper for a parser-level pseudo-op
                          rewrite (la, li, move, neg, bge, ...).
                          Children are the real instructions the
                          parser emits for that pseudo-op. */

  /* Data directives */
  AST_DATA_BYTE,   /* .byte  EXPR[, EXPR]*              */
  AST_DATA_HALF,   /* .half  EXPR[, EXPR]*              */
  AST_DATA_WORD,   /* .word  EXPR[, EXPR]*              */
  AST_DATA_FLOAT,  /* .float FP[, FP]*                  */
  AST_DATA_DOUBLE, /* .double FP[, FP]*                 */
  AST_DATA_STRING, /* .ascii STR  or  .asciiz STR       */

  /* Segment / layout directives */
  AST_DIR_TEXT,   /* .text [ADDR]                      */
  AST_DIR_DATA,   /* .data [ADDR]                      */
  AST_DIR_KTEXT,  /* .ktext [ADDR]                     */
  AST_DIR_KDATA,  /* .kdata [ADDR]                     */
  AST_DIR_ALIGN,  /* .align N                          */
  AST_DIR_SPACE,  /* .space N                          */
  AST_DIR_GLOBL,  /* .globl NAME                       */
  AST_DIR_EXTERN, /* .extern NAME SIZE                 */
  AST_DIR_COMM,   /* .comm NAME SIZE                   */

  /* Labels */
  AST_LABEL_DEF, /* NAME:    or    NAME = EXPR        */

  /* Structural */
  AST_FILE, /* root — owns the .child chain      */

  /* Sentinel for error recovery */
  AST_ERROR, /* held position of a sync'd line    */
} ast_kind;

typedef enum {
  AST_LABEL_NORMAL, /* NAME : (placement at current PC)     */
  AST_LABEL_CONST,  /* NAME = EXPR (compile-time constant)  */
} ast_label_kind;

/* ------------------------------------------------------------------ */
/* Node                                                                */
/* ------------------------------------------------------------------ */

typedef struct ast_node ast_node;

struct ast_node {
  ast_kind kind;
  int source_line; /* 1-based, 0 if unknown */
  ast_node* next;  /* sibling chain inside a parent's child list */

  union {
    /* ---------- instructions ---------- */

    struct {
      int op;
      int rd, rs, rt;
    } inst_r;

    struct {
      int op;
      int rd, rt, shamt;
    } inst_r_shift;

    struct {
      int op;
      int rt, rs;
      imm_expr* imm; /* owned */
    } inst_i;

    struct {
      int op;
      imm_expr* target; /* owned */
    } inst_j;

    struct {
      int op;
      int fd, fs, ft;
    } inst_fp_r;

    struct {
      int op;
      int fs, ft;
      int cc;
    } inst_fp_compare;

    struct {
      char* mnemonic;  /* owned — e.g., "la", "li", "rol"        */
      ast_node* child; /* chain of expanded AST_INST_* children   */
    } pseudo;

    /* ---------- data (lists of values) ---------- */

    struct {
      int count;
      imm_expr** exprs; /* array of count imm_expr*, each owned */
    } data_int;         /* used by BYTE / HALF / WORD */

    struct {
      int count;
      double* values; /* malloc'd array */
    } data_fp;        /* used by FLOAT / DOUBLE */

    struct {
      char* bytes; /* not necessarily NUL-terminated; len fields below */
      int length;
      bool null_terminate;
    } data_string;

    /* ---------- directives ---------- */

    struct {
      bool has_start_addr;
      mem_addr start_addr;
    } dir_seg; /* covers TEXT / DATA / KTEXT / KDATA */

    struct {
      int n; /* alignment power */
    } dir_align;

    struct {
      int size; /* bytes */
    } dir_space;

    struct {
      char* name; /* owned */
    } dir_globl;

    struct {
      char* name; /* owned */
      int size;
    } dir_named_size; /* covers EXTERN and COMM */

    /* ---------- labels ---------- */

    struct {
      char* name; /* owned */
      ast_label_kind kind;
      int32_t value; /* for AST_LABEL_CONST; ignored otherwise */
    } label_def;

    /* ---------- file ---------- */

    struct {
      ast_node* child;   /* head of the statement chain */
      char* source_file; /* owned; may be null */
    } file;

    /* ---------- error ---------- */

    struct {
      char* message; /* owned; may be null */
    } error;
  } u;
};

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */
/* Each one allocates an ast_node, fills it, and returns the pointer. */
/* source_line is captured from the scanner's global at call time.    */

/* Instructions */
[[nodiscard]] ast_node* ast_make_inst_r(int op, int rd, int rs, int rt);
[[nodiscard]] ast_node* ast_make_inst_r_shift(int op, int rd, int rt, int shamt);
[[nodiscard]] ast_node* ast_make_inst_i(int op, int rt, int rs, imm_expr* imm);
[[nodiscard]] ast_node* ast_make_inst_j(int op, imm_expr* target);
[[nodiscard]] ast_node* ast_make_inst_fp_r(int op, int fd, int fs, int ft);
[[nodiscard]] ast_node* ast_make_inst_fp_compare(int op, int fs, int ft, int cc);
[[nodiscard]] ast_node* ast_make_pseudo(const char* mnemonic); /* mnemonic dup'd */

/* Data */
[[nodiscard]] ast_node* ast_make_data_byte(int count, imm_expr** exprs); /* takes ownership */
[[nodiscard]] ast_node* ast_make_data_half(int count, imm_expr** exprs);
[[nodiscard]] ast_node* ast_make_data_word(int count, imm_expr** exprs);
[[nodiscard]] ast_node* ast_make_data_float(int count, double* values); /* takes ownership */
[[nodiscard]] ast_node* ast_make_data_double(int count, double* values);
[[nodiscard]] ast_node* ast_make_data_string(const char* bytes, int length, bool null_term);

/* Segment + layout */
[[nodiscard]] ast_node* ast_make_dir_text(bool has_start_addr, mem_addr start_addr);
[[nodiscard]] ast_node* ast_make_dir_data(bool has_start_addr, mem_addr start_addr);
[[nodiscard]] ast_node* ast_make_dir_ktext(bool has_start_addr, mem_addr start_addr);
[[nodiscard]] ast_node* ast_make_dir_kdata(bool has_start_addr, mem_addr start_addr);
[[nodiscard]] ast_node* ast_make_dir_align(int n);
[[nodiscard]] ast_node* ast_make_dir_space(int size);
[[nodiscard]] ast_node* ast_make_dir_globl(const char* name); /* name dup'd */
[[nodiscard]] ast_node* ast_make_dir_extern(const char* name, int size);
[[nodiscard]] ast_node* ast_make_dir_comm(const char* name, int size);

/* Labels */
[[nodiscard]] ast_node* ast_make_label_normal(const char* name);
[[nodiscard]] ast_node* ast_make_label_const(const char* name, int32_t value);

/* File root */
[[nodiscard]] ast_node* ast_make_file(const char* source_file);

/* Error recovery sentinel */
[[nodiscard]] ast_node* ast_make_error(const char* message);

/* ------------------------------------------------------------------ */
/* Tree manipulation                                                   */
/* ------------------------------------------------------------------ */

/* Append `stmt` to the end of `file`'s child chain.  O(N) per append;
   if this turns into a hotspot the parser can maintain its own tail
   pointer.  Returns `file` for chaining. */
ast_node* ast_file_append(ast_node* file, ast_node* stmt);

/* Append `child` to the end of a pseudo wrapper's child chain. */
ast_node* ast_pseudo_append(ast_node* pseudo, ast_node* child);

/* Recursively free a node and any owned strings, expressions, child
   chains, and sibling chains reachable from it.  Passing NULL is a
   no-op. */
void ast_free(ast_node* node);

/* ------------------------------------------------------------------ */
/* Debug printer                                                       */
/* ------------------------------------------------------------------ */

/* Dump the tree rooted at `node` to `out` as indented text.  Used by
   the -print-ast flag.  Does not free anything. */
void ast_print(const ast_node* node, FILE* out);

/* Dump the tree as JSON, one node per object.  Used by -print-ast-json
   as the data surface for external tooling (GUI scrubber, listing
   converter, static-analysis pass runners). */
void ast_print_json(const ast_node* node, FILE* out);

#endif
