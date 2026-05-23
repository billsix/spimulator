/**
 * @file tree-sitter grammar for MIPS assembly, spim flavour.
 *
 * Covers the syntax accepted by /spimulator/src/parser.c — the
 * hand-written recursive-descent parser in the spim simulator
 * (https://github.com/billsix/spimulator).  This grammar is for
 * editor integration only (syntax highlighting, structural
 * navigation, folds) — it does NOT replace spim's parser.
 *
 * Keyword surface is generated from spim's include/opcodes.h via
 * scripts/extract-keywords.py; the resulting lists are pasted
 * inline here as `KEYWORDS.<kind>` for readability.
 *
 * @license BSD-3-Clause
 */

// ---------------------------------------------------------------
// Keyword inventory (extracted from spim's opcodes.h, 381 entries).
// Categories match the hand parser's dispatch table.
// ---------------------------------------------------------------

const KEYWORDS = require('./scripts/keyword-lists.js');

// Helper: a `choice(...)` of all keyword strings in a category, each
// wrapped in `alias(token(prec(N, ...)))` so they outrank IDs.  Keyword
// priority matters because instruction mnemonics like `add` would
// otherwise tokenise as identifiers.
function kw(category) {
  return choice(...category.map(s => token(prec(2, s))));
}

// ---------------------------------------------------------------

module.exports = grammar({
  name: 'mips_spim',

  // The scanner returns a newline token; we use it as a statement
  // separator.  Whitespace (spaces, tabs) and comments are stripped.
  extras: $ => [
    /[ \t]/,
    $.comment,
  ],

  // Tree-sitter's word rule: identifiers and keywords compete for
  // the same lexer slot.  Marking `_word` lets tree-sitter give
  // keywords priority correctly.
  word: $ => $._word,

  conflicts: $ => [
    // Pseudo-op operands: an expression like `label` could be
    // either a bare _expr operand or the immediate part of an
    // address.  Tree-sitter's GLR handles both with this conflict
    // declared.
    [$._pseudo_operand, $.address],
    [$._expr, $.label_ref],
  ],

  rules: {
    // -----------------------------------------------------------
    // Top level
    // -----------------------------------------------------------

    source_file: $ => repeat($._line),

    // A line is: optional label prefix(es), optional statement, then newline.
    // An empty line is just a newline.
    _line: $ => seq(
      optional($._label_prefix),
      optional(choice(
        $.directive,
        $.instruction,
        $.constant_def,
      )),
      $._newline,
    ),

    _label_prefix: $ => prec.right(repeat1($.label_def)),

    // -----------------------------------------------------------
    // Labels + constants
    // -----------------------------------------------------------

    label_def: $ => seq(
      field('name', $.identifier),
      ':',
    ),

    // `NAME = EXPR` defines a numeric constant (alias for a value).
    constant_def: $ => seq(
      field('name', $.identifier),
      '=',
      field('value', $._expr),
    ),

    // -----------------------------------------------------------
    // Directives
    // -----------------------------------------------------------

    directive: $ => choice(
      $._directive_segment,
      $._directive_data,
      $._directive_meta,
      $._directive_set,
    ),

    // Segment-switching directives take no operands.
    _directive_segment: $ => choice(
      '.data', '.text', '.kdata', '.ktext', '.rdata', '.sdata',
    ),

    // Data-storage directives.
    _directive_data: $ => choice(
      seq('.word',    $._expr_list),
      seq('.half',    $._expr_list),
      seq('.byte',    $._expr_list),
      seq('.float',   $._fp_list),
      seq('.double',  $._fp_list),
      seq('.ascii',   $._string_list),
      seq('.asciiz',  $._string_list),
      seq('.space',   $._expr),
      seq('.align',   $._expr),
    ),

    // Symbol / scope / debug directives.
    _directive_meta: $ => choice(
      seq('.globl',  $.identifier),
      seq('.global', $.identifier),
      seq('.extern', $.identifier, $._expr),
      seq('.comm',   $.identifier, $._expr),
      seq('.lcomm',  $.identifier, $._expr),
      // The debug-info directives below are recognised but their
      // payload is consumed up to EOL without further structure.
      seq(choice('.file', '.loc', '.frame', '.mask', '.fmask',
                 '.ent', '.end', '.bgnb', '.endb', '.endr',
                 '.label', '.livereg', '.option', '.struct',
                 '.alias', '.noalias', '.verstamp', '.vreg',
                 '.repeat', '.asm0', '.err'),
          repeat(choice($.identifier, $.integer, $.string, ','))),
    ),

    // `.set noat` / `.set at` etc.
    _directive_set: $ => seq('.set', $.identifier),

    // -----------------------------------------------------------
    // Instructions — one alternative per dispatch kind in
    // parser.c's parse_asm_code().
    // -----------------------------------------------------------

    instruction: $ => choice(
      $._inst_noarg,
      $._inst_r3,
      $._inst_r2sh,
      $._inst_r1s,
      $._inst_r1d,
      $._inst_r2st,
      $._inst_r2ds,
      $._inst_r2td,
      $._inst_r3sh,
      $._inst_i2,
      $._inst_i2a,
      $._inst_i1s,
      $._inst_i1t,
      $._inst_b1,
      $._inst_b2,
      $._inst_j,
      $._inst_bc,
      $._inst_movc,
      $._inst_fp_r3,
      $._inst_fp_r2ds,
      $._inst_fp_r2ts,
      $._inst_fp_r4,
      $._inst_fp_cmp,
      $._inst_fp_movc,
      $._inst_fp_i2a,
      $._inst_pseudo,
    ),

    _inst_noarg: $ => field('op', alias(kw(KEYWORDS.NOARG), $.opcode)),

    // R-type, 3 operands.  Three accepted shapes per the hand parser:
    //   1. `op rd, rs, rt`          (canonical 3-register form)
    //   2. `op rd, rs, imm`         (3-with-immediate, uses $at + ori)
    //   3. `op rd, imm`             (2-with-immediate shorthand, rs := rd)
    _inst_r3: $ => seq(
      field('op', alias(kw(KEYWORDS.R3), $.opcode)),
      field('rd', $.register),
      ',',
      choice(
        seq(field('rs', $.register), ',', field('rt', choice($.register, $._expr))),
        field('imm', $._expr),
      ),
    ),

    // R-type shift: rd, rt, shamt
    _inst_r2sh: $ => seq(
      field('op', alias(kw(KEYWORDS.R2sh), $.opcode)),
      field('rd', $.register), ',',
      field('rt', $.register), ',',
      field('shamt', $._expr),
    ),

    _inst_r1s: $ => seq(
      field('op', alias(kw(KEYWORDS.R1s), $.opcode)),
      field('rs', $.register),
    ),

    _inst_r1d: $ => seq(
      field('op', alias(kw(KEYWORDS.R1d), $.opcode)),
      field('rd', $.register),
    ),

    // mult / multu / div / divu: 2 source regs, no destination
    // (real instructions), OR 3-operand pseudo form
    // `div $rd, $rs, $rt` that emits div + mflo.
    _inst_r2st: $ => seq(
      field('op', alias(kw(KEYWORDS.R2st), $.opcode)),
      field('rs', $.register), ',',
      field('rt', $.register),
      optional(seq(',', field('rt2', choice($.register, $._expr)))),
    ),

    _inst_r2ds: $ => seq(
      field('op', alias(kw(KEYWORDS.R2ds), $.opcode)),
      field('rd', $.register), ',',
      field('rs', $.register),
    ),

    // mfc0 / mtc0 / cfc1 / ctc1: REG + COP_REG.
    _inst_r2td: $ => seq(
      field('op', alias(kw(KEYWORDS.R2td), $.opcode)),
      field('rt', $.register), ',',
      field('rd', $.register),
    ),

    // Variable-register shifts (sllv/srav/srlv).
    _inst_r3sh: $ => seq(
      field('op', alias(kw(KEYWORDS.R3sh), $.opcode)),
      field('rd', $.register), ',',
      field('rt', $.register), ',',
      field('rs', $.register),
    ),

    // I-type with two source registers (addi, andi, ori, ...).
    _inst_i2: $ => seq(
      field('op', alias(kw(KEYWORDS.I2), $.opcode)),
      field('rt', $.register), ',',
      optional(seq(field('rs', $.register), ',')),
      field('imm', $._expr),
    ),

    // I-type address form (lw, sw, lb, sb, lh, sh, ll, sc, ...).
    _inst_i2a: $ => seq(
      field('op', alias(kw(KEYWORDS.I2a), $.opcode)),
      field('rt', $.register), ',',
      field('addr', $.address),
    ),

    // teqi / tnei / tlti / etc.: SRC + IMM16.
    _inst_i1s: $ => seq(
      field('op', alias(kw(KEYWORDS.I1s), $.opcode)),
      field('rs', $.register), ',',
      field('imm', $._expr),
    ),

    // lui: DEST + UIMM16.
    _inst_i1t: $ => seq(
      field('op', alias(kw(KEYWORDS.I1t), $.opcode)),
      field('rt', $.register), ',',
      field('imm', $._expr),
    ),

    // Single-register branches (bgez, bltz, bgtz, blez, ...).
    _inst_b1: $ => seq(
      field('op', alias(kw(KEYWORDS.B1), $.opcode)),
      field('rs', $.register), ',',
      field('label', $.label_ref),
    ),

    // Two-register branches (beq, bne, ...).
    _inst_b2: $ => seq(
      field('op', alias(kw(KEYWORDS.B2), $.opcode)),
      field('rs', $.register), ',',
      field('rt', choice($.register, $._expr)), ',',
      field('label', $.label_ref),
    ),

    // J-type: jal / j / jr / jalr.
    _inst_j: $ => seq(
      field('op', alias(kw(KEYWORDS.J), $.opcode)),
      field('target', choice($.register, $.label_ref)),
      optional(seq(',', field('source', $.register))),
    ),

    // FP branch-on-condition (bc1t, bc1f, ...).
    _inst_bc: $ => seq(
      field('op', alias(kw(KEYWORDS.BC), $.opcode)),
      optional(seq(field('cc', $.integer), ',')),
      field('label', $.label_ref),
    ),

    _inst_movc: $ => seq(
      field('op', alias(kw(KEYWORDS.MOVC), $.opcode)),
      field('rd', $.register), ',',
      field('rs', $.register),
      optional(seq(',', field('cc', $.integer))),
    ),

    // FP R-type with 3 FP registers.
    _inst_fp_r3: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_R3), $.opcode)),
      field('fd', $.fp_register), ',',
      field('fs', $.fp_register), ',',
      field('ft', $.fp_register),
    ),

    // FP R-type 2 regs (abs.s, neg.s, mov.s, cvt.*, ...).
    _inst_fp_r2ds: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_R2ds), $.opcode)),
      field('fd', $.fp_register), ',',
      field('fs', $.fp_register),
    ),

    // mfc1 / mtc1 / cfc1 / ctc1: integer reg + FP reg.
    _inst_fp_r2ts: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_R2ts), $.opcode)),
      field('rt', $.register), ',',
      field('fs', choice($.register, $.fp_register)),
    ),

    // 4-operand FP (madd.s, msub.s, ...).
    _inst_fp_r4: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_R4), $.opcode)),
      field('fd', $.fp_register), ',',
      field('fr', $.fp_register), ',',
      field('fs', $.fp_register), ',',
      field('ft', $.fp_register),
    ),

    // FP compare (c.lt.s, c.eq.d, ...).  Optional leading CC.
    _inst_fp_cmp: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_CMP), $.opcode)),
      optional(seq(field('cc', $.integer), ',')),
      field('fs', $.fp_register), ',',
      field('ft', $.fp_register),
    ),

    // FP conditional move (movf.s, movt.s, movn.s, movz.s, ...).
    _inst_fp_movc: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_MOVC), $.opcode)),
      field('fd', $.fp_register), ',',
      field('fs', $.fp_register), ',',
      field('cc_or_reg', choice($.integer, $.register, $.fp_register)),
    ),

    // FP load/store: lwc1, swc1, ldc1, sdc1.
    _inst_fp_i2a: $ => seq(
      field('op', alias(kw(KEYWORDS.FP_I2a), $.opcode)),
      field('ft', $.fp_register), ',',
      field('addr', $.address),
    ),

    // Pseudo-ops are heterogeneous — capture them as a single
    // catch-all production whose operands are a free-form list.
    // The walker (if anyone writes one) dispatches per opcode name.
    _inst_pseudo: $ => seq(
      field('op', alias(kw(KEYWORDS.PSEUDO), $.opcode)),
      optional($._pseudo_operands),
    ),

    _pseudo_operands: $ => prec.right(seq(
      $._pseudo_operand,
      repeat(seq(',', $._pseudo_operand)),
    )),

    _pseudo_operand: $ => choice(
      $.register,
      $.fp_register,
      $.address,
      $.label_ref,
      $._expr,
      $.string,
    ),

    // -----------------------------------------------------------
    // Operands
    // -----------------------------------------------------------

    // $0..$31, $a0..$a3, $t0..$t9, $s0..$s8, $v0..$v1, $k0..$k1,
    // $sp, $gp, $fp, $ra, $at, $zero.
    register: $ => /\$([0-9]+|zero|at|v[01]|a[0-3]|t[0-9]|s[0-8]|k[01]|gp|sp|fp|ra)/,

    // $f0..$f31 (FP registers — separate token to disambiguate).
    fp_register: $ => /\$f[0-9]+/,

    // Address forms: imm, (reg), imm(reg), label, label+imm,
    // label+imm(reg), label-imm(reg).
    address: $ => choice(
      seq($._expr, '(', $.register, ')'),
      seq('(', $.register, ')'),
      $._expr,
    ),

    label_ref: $ => $.identifier,

    // EXPR : TERM (+/- TERM)*
    _expr: $ => choice(
      $.integer,
      $.identifier,
      $._paren_expr,
      $._binary_expr,
      $._unary_expr,
    ),

    _paren_expr: $ => seq('(', $._expr, ')'),

    _binary_expr: $ => prec.left(1, seq(
      $._expr,
      choice('+', '-'),
      $._expr,
    )),

    _unary_expr: $ => prec(2, seq(
      choice('+', '-'),
      $._expr,
    )),

    // EXPR_LIST: comma-separated expressions (data directives).
    _expr_list: $ => prec.right(seq(
      $._expr,
      repeat(seq(',', $._expr)),
    )),

    _fp_list: $ => prec.right(seq(
      $.fp_literal,
      repeat(seq(',', $.fp_literal)),
    )),

    _string_list: $ => prec.right(seq(
      $.string,
      repeat(seq(',', $.string)),
    )),

    // -----------------------------------------------------------
    // Literals
    // -----------------------------------------------------------

    integer: $ => choice(
      /0[xX][0-9a-fA-F]+/,
      /-?[0-9]+/,
      /'([^'\\]|\\.)'/,           // char literal
    ),

    fp_literal: $ => /-?[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/,

    string: $ => /"([^"\\]|\\.)*"/,

    // Identifiers — MIPS asm allows dots in names (e.g.,
    // `label.foo`) and trailing digits.
    identifier: $ => $._word,
    _word: $ => /[a-zA-Z_.$][a-zA-Z0-9_.$]*/,

    // Comments: `# ...` to end of line.  spim doesn't recognise
    // `//` or `/* */` — only `#`.
    comment: $ => token(seq('#', /[^\n]*/)),

    _newline: $ => '\n',
  },
});
