// Copyright (c) 2021-2026 William Emerison Six
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/* PURPOSE: The tree-building companion to calc-sdt — the SAME TI-83-style
 * infix calculator and grammar, but the parser BUILDS AN ABSTRACT SYNTAX TREE
 * first, then a SEPARATE recursive walker (eval) walks the tree to produce the
 * value.  One expression per line of stdin; the result (or "error") is printed
 * per line.  Output is byte-for-byte identical to calc-sdt (they share a
 * golden), so a student can diff the two sources to see exactly what a tree
 * buys — and what it costs.
 *
 *     $ echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-tree.asm
 *     11.500000
 *
 * The grammar (unchanged from calc-sdt — precedence baked into the layering):
 *
 *     expr   := term   (('+' | '-') term)*
 *     term   := factor (('*' | '/') factor)*
 *     factor := NUMBER | '-' factor | '(' expr ')'
 *
 * The difference from calc-sdt is purely architectural.  There, each parse
 * function RETURNED the double it evaluated to.  Here, each parse function
 * RETURNS A NODE, and nothing is computed until eval() walks the finished tree.
 * Three node kinds mirror the grammar: NUM holds a literal's value, BINOP holds
 * an operator and its two subtrees, and NEG holds one child (the dedicated
 * unary-minus node the `factor := '-' factor` rule produces — not desugared to
 * `0 - x`).
 *
 * Nodes are allocated by a BUMP ALLOCATOR over the program break: new_node()
 * reads the current break with os_brk(0), grows it by one node, and hands back
 * the old top.  Nodes are never freed — this is the "allocate, never free" heap
 * lesson (also what the mini C compiler will do), and it mirrors calc-tree.asm,
 * which bumps the break with syscall 9 (sbrk).  A partially-built tree from a
 * malformed line is simply leaked; on error we print "error" and never walk it.
 *
 * Values are doubles (floating point, like a TI-83).  Display reuses rpn's /
 * calc-sdt's fixed deterministic printer (integral -> integer, else integer
 * part + '.' + 6 truncated fractional digits; nan/inf named), so the C
 * reference and calc-tree.asm match byte-for-byte.  A malformed line prints
 * "error" and parsing resumes at the next line; division by zero shows inf/nan,
 * not a trap.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

/* Fractional digits printed for a non-integral result. */
#define FRAC_DIGITS 6

/* AST node kinds (the numeric values match calc-tree.asm's kind word). */
enum { NODE_NUM, NODE_BINOP, NODE_NEG };

typedef struct Node {
  int kind;           /* NODE_NUM | NODE_BINOP | NODE_NEG */
  int op;             /* BINOP: the operator character '+' '-' '*' '/' */
  double value;       /* NUM: the literal's value */
  struct Node* left;  /* BINOP left operand / NEG child */
  struct Node* right; /* BINOP right operand */
} Node;

static int cur;       /* current lookahead character (-1 at EOF) */
static int had_error; /* set when the current line fails to parse */

static void advance(void) { cur = read_char(); }

/* Skip spaces and tabs WITHIN a line — a newline ends an expression, so it is
 * deliberately not skipped here. */
static void skip_blanks(void) {
  while (cur == ' ' || cur == '\t' || cur == '\r') {
    advance();
  }
}

/* Bump-allocate one node off the program break — the same allocator
 * calc-tree.asm implements with syscall 9 (sbrk).  os_brk(0) reads the current
 * break; os_brk(new) grows it.  Nodes are never freed. */
static Node* new_node(void) {
  Node* n = (Node*)os_brk(0);
  os_brk((char*)n + sizeof(Node));
  return n;
}

static Node* make_num(double value) {
  Node* n = new_node();
  n->kind = NODE_NUM;
  n->value = value;
  return n;
}

static Node* make_neg(Node* child) {
  Node* n = new_node();
  n->kind = NODE_NEG;
  n->left = child;
  return n;
}

static Node* make_binop(int op, Node* left, Node* right) {
  Node* n = new_node();
  n->kind = NODE_BINOP;
  n->op = op;
  n->left = left;
  n->right = right;
  return n;
}

/* Print `value` deterministically — identical to rpn's / calc-sdt's printer, so
 * all sides agree byte-for-byte. */
static void print_double(double value) {
  if (value != value) {
    print_string("nan");
    return;
  }
  if (value < 0.0) {
    print_char('-');
    value = -value;
  }
  if (value != 0.0 && value + value == value) {
    print_string("inf");
    return;
  }
  int integer_part = (int)value;
  double fraction = value - (double)integer_part;
  print_int(integer_part);
  if (fraction == 0.0) {
    return;
  }
  print_char('.');
  for (int i = 0; i < FRAC_DIGITS; i++) {
    fraction *= 10.0;
    int digit = (int)fraction;
    print_char((char)('0' + digit));
    fraction -= (double)digit;
  }
}

static Node* parse_expr(void); /* forward decl for the '(' expr ')' rule */

/* factor := NUMBER | '-' factor | '(' expr ')' — returns the subtree. */
static Node* parse_factor(void) {
  skip_blanks();
  if (cur == '-') {
    advance();
    return make_neg(parse_factor());
  }
  if (cur == '(') {
    advance();
    Node* node = parse_expr();
    skip_blanks();
    if (cur == ')') {
      advance();
    } else {
      had_error = 1; /* unbalanced parenthesis */
    }
    return node;
  }
  if ((cur >= '0' && cur <= '9') || cur == '.') {
    double mantissa = 0.0;
    int fractional_digits = 0;
    int seen_digit = 0;
    while (cur >= '0' && cur <= '9') {
      mantissa = mantissa * 10.0 + (double)(cur - '0');
      seen_digit = 1;
      advance();
    }
    if (cur == '.') {
      advance();
      while (cur >= '0' && cur <= '9') {
        mantissa = mantissa * 10.0 + (double)(cur - '0');
        fractional_digits++;
        seen_digit = 1;
        advance();
      }
    }
    if (!seen_digit) {
      had_error = 1; /* a lone '.' is not a number */
      return make_num(0.0);
    }
    double value = mantissa;
    for (int i = 0; i < fractional_digits; i++) {
      value = value / 10.0;
    }
    return make_num(value);
  }
  had_error = 1; /* unexpected character where a factor was expected */
  return make_num(0.0);
}

/* term := factor (('*' | '/') factor)* — left-associative BINOP nesting. */
static Node* parse_term(void) {
  Node* node = parse_factor();
  for (;;) {
    skip_blanks();
    if (cur == '*') {
      advance();
      node = make_binop('*', node, parse_factor());
    } else if (cur == '/') {
      advance();
      node = make_binop('/', node, parse_factor());
    } else {
      return node;
    }
  }
}

/* expr := term (('+' | '-') term)* — left-associative BINOP nesting. */
static Node* parse_expr(void) {
  Node* node = parse_term();
  for (;;) {
    skip_blanks();
    if (cur == '+') {
      advance();
      node = make_binop('+', node, parse_term());
    } else if (cur == '-') {
      advance();
      node = make_binop('-', node, parse_term());
    } else {
      return node;
    }
  }
}

/* Walk the finished tree, computing its value.  This is the whole point of the
 * tree architecture: parsing and evaluation are now two separate passes. */
static double eval(Node* node) {
  if (node->kind == NODE_NUM) {
    return node->value;
  }
  if (node->kind == NODE_NEG) {
    return -eval(node->left);
  }
  /* NODE_BINOP: evaluate both children, then apply the operator. */
  double left = eval(node->left);
  double right = eval(node->right);
  switch (node->op) {
    case '+':
      return left + right;
    case '-':
      return left - right;
    case '*':
      return left * right;
    case '/':
      return left / right;
    default:
      return 0.0; /* grammar admits only + - * / ; unreachable */
  }
}

int my_main(int argc, char** argv) {
  /* This demo reads stdin, not the command line, but crt0.h always calls
   * my_main(argc, argv), so the signature is fixed.  Casting the unused
   * parameters to void marks them deliberately-ignored and silences
   * -Wunused-parameter (the build enables it via warning_level=3 / -Wextra).
   * The casts evaluate-and-discard, emitting no code. */
  (void)argc;
  (void)argv;

  advance(); /* prime the first lookahead character */
  while (cur != -1) {
    skip_blanks();
    if (cur == '\n') { /* blank line */
      advance();
      continue;
    }
    if (cur == -1) {
      break;
    }
    had_error = 0;
    Node* tree = parse_expr();
    skip_blanks();
    if (cur != '\n' && cur != -1) {
      had_error = 1; /* trailing garbage after a complete expression */
    }
    if (had_error) {
      print_string("error");
      while (cur != '\n' &&
             cur != -1) { /* recover: discard the rest of the line */
        advance();
      }
    } else {
      print_double(eval(tree));
    }
    print_char('\n');
    if (cur == '\n') {
      advance();
    }
  }
  return 0;
}
