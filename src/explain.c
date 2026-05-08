/* Teaching mode for spimulator.

   When explain_mode is true, the run loop calls explain_before() before each
   instruction executes (to narrate what's about to happen) and explain_after()
   after (to show what changed). All output goes through write_output() so it
   honors the simulator's console redirection. */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "inst.h"
#include "reg.h"
#include "mem.h"
#include "sym-tbl.h"
#include "parser_yacc.h"
#include "explain.h"

bool explain_mode = false;

/* Snapshot of architectural state at explain_before() time. */
static reg_word snap_R[R_LENGTH];
static reg_word snap_HI, snap_LO;
static mem_addr snap_PC;

/* ------------ pseudo-op narration ------------
 *
 * The assembler expands pseudo-instructions (la, li, move, b, bge, ...) into
 * one-or-more real instructions before they reach the run loop. By the time
 * explain_before() sees an instruction, the pseudo-op name is gone — only
 * the expanded real instruction is left. We recover the pseudo-op by parsing
 * the SOURCE() line attached to the instruction, then emit a short
 * pedagogical header explaining what the pseudo-op means and that what
 * follows is the assembler's expansion. For multi-instruction expansions,
 * the second (and later) emitted instructions have no source line of their
 * own — we mark them as continuations.
 *
 * Listing is alphabetical for easy maintenance. `may_be_multi` enables the
 * continuation hint when the next instruction arrives with no source line.
 */
struct pseudo_info {
  const char* name;
  const char* what_it_means;
  bool may_be_multi;
};

static const struct pseudo_info pseudo_ops[] = {
    {"abs",
     "Absolute Value — set $rd to |$rs|. Multi-instruction expansion using\n"
     "    arithmetic-shift / xor / sub to avoid a conditional branch.",
     true},
    {"b",
     "Branch — unconditional jump to a label. Encoded as `beq $0, $0, label`\n"
     "    ($0 always equals $0, so the branch is always taken).",
     false},
    {"bal",
     "Branch-and-Link — unconditional call to a label, saving the return\n"
     "    address in $ra. Encoded as `bgezal $0, label`.",
     false},
    {"beqz",
     "Branch if Equal to Zero — branch if $rs == 0. Encoded as\n"
     "    `beq $rs, $0, label`.",
     false},
    {"bnez",
     "Branch if Not Equal to Zero — branch if $rs != 0. Encoded as\n"
     "    `bne $rs, $0, label`.",
     false},
    {"bge",
     "Branch if Greater or Equal (signed) — branch if $rs >= $rt. Expands\n"
     "    to `slt $at, $rs, $rt` + `beq $at, $0, label`.",
     true},
    {"bgeu",
     "Branch if Greater or Equal (unsigned) — branch if $rs >= $rt unsigned.\n"
     "    Expands to `sltu $at, $rs, $rt` + `beq $at, $0, label`.",
     true},
    {"bgt",
     "Branch if Greater Than (signed) — branch if $rs > $rt. Expands to\n"
     "    `slt $at, $rt, $rs` + `bne $at, $0, label`.",
     true},
    {"bgtu",
     "Branch if Greater Than (unsigned) — branch if $rs > $rt unsigned.\n"
     "    Expands to `sltu $at, $rt, $rs` + `bne $at, $0, label`.",
     true},
    {"ble",
     "Branch if Less or Equal (signed) — branch if $rs <= $rt. Expands to\n"
     "    `slt $at, $rt, $rs` + `beq $at, $0, label`.",
     true},
    {"bleu",
     "Branch if Less or Equal (unsigned) — branch if $rs <= $rt unsigned.\n"
     "    Expands to `sltu $at, $rt, $rs` + `beq $at, $0, label`.",
     true},
    {"blt",
     "Branch if Less Than (signed) — branch if $rs < $rt. Expands to\n"
     "    `slt $at, $rs, $rt` + `bne $at, $0, label`.",
     true},
    {"bltu",
     "Branch if Less Than (unsigned) — branch if $rs < $rt unsigned.\n"
     "    Expands to `sltu $at, $rs, $rt` + `bne $at, $0, label`.",
     true},
    {"la",
     "Load Address — set $rd to the 32-bit address of a label. Typically\n"
     "    expands to `lui $rd, hi(addr)` + `ori $rd, $rd, lo(addr)` (or just\n"
     "    `addiu` for relocations that fit in 16 bits).",
     true},
    {"ld",
     "Load Doubleword — load 64 bits into a register pair. Expands to two\n"
     "    `lw` instructions covering offsets 0 and 4.",
     true},
    {"li",
     "Load Immediate — set $rd to a 32-bit constant. Small constants emit\n"
     "    one `ori` or `addiu`; larger ones expand to `lui` + `ori`.",
     true},
    {"move",
     "Move — copy $rs into $rd. Encoded as `addu $rd, $0, $rs` (adding $zero\n"
     "    is the simplest way to copy a register).",
     false},
    {"mulo",
     "Multiply with Overflow check — signed multiply that traps on overflow.\n"
     "    Expands to `mult` plus an overflow check plus `mflo`.",
     true},
    {"mulou",
     "Multiply Unsigned with Overflow check — unsigned variant of `mulo`.",
     true},
    {"neg",
     "Negate (signed; traps on overflow) — set $rd = -$rs. Encoded as\n"
     "    `sub $rd, $0, $rs`.",
     false},
    {"negu",
     "Negate Unsigned — set $rd = -$rs with no overflow trap. Encoded as\n"
     "    `subu $rd, $0, $rs`.",
     false},
    {"nop",
     "No Operation — do nothing for one cycle. Encoded as `sll $0, $0, 0`\n"
     "    (a shift writing to $zero, which is hardwired to 0 and discards\n"
     "    the result).",
     false},
    {"not",
     "Bitwise NOT — set $rd to the one's complement of $rs. Encoded as\n"
     "    `nor $rd, $rs, $0` (NOR with $zero is logical NOT).",
     false},
    {"rem",
     "Signed Remainder — set $rd = $rs % $rt. Expands to `div $rs, $rt`\n"
     "    + `mfhi $rd`.",
     true},
    {"remu",
     "Unsigned Remainder — set $rd = $rs % $rt unsigned. Expands to\n"
     "    `divu $rs, $rt` + `mfhi $rd`.",
     true},
    {"rol",
     "Rotate Left — multi-instruction expansion using sll, srl, and or to\n"
     "    rotate the bits of $rs left.",
     true},
    {"ror",
     "Rotate Right — multi-instruction expansion using srl, sll, and or.",
     true},
    {"sd",
     "Store Doubleword — store 64 bits from a register pair. Expands to two\n"
     "    `sw` instructions at offsets 0 and 4.",
     true},
    {"seq",
     "Set if Equal — set $rd = ($rs == $rt) ? 1 : 0. Expands to `xor` +\n"
     "    `sltiu`.",
     true},
    {"sne",
     "Set if Not Equal — set $rd = ($rs != $rt) ? 1 : 0. Expands to `xor` +\n"
     "    `sltu` against $zero.",
     true},
    {"sge", "Set if Greater or Equal (signed) — $rd = ($rs >= $rt) ? 1 : 0.",
     true},
    {"sgeu", "Set if Greater or Equal (unsigned).", true},
    {"sgt", "Set if Greater Than (signed) — $rd = ($rs > $rt) ? 1 : 0.", true},
    {"sgtu", "Set if Greater Than (unsigned).", true},
    {"sle", "Set if Less or Equal (signed) — $rd = ($rs <= $rt) ? 1 : 0.",
     true},
    {"sleu", "Set if Less or Equal (unsigned).", true},
    {"ulh",
     "Unaligned Load Halfword (signed) — read 16 bits at a possibly-unaligned\n"
     "    address. Expands to byte loads + shifts.",
     true},
    {"ulhu",
     "Unaligned Load Halfword Unsigned — zero-extending variant of `ulh`.",
     true},
    {"ulw",
     "Unaligned Load Word — read 32 bits at a possibly-unaligned address.\n"
     "    Expands to `lwl` + `lwr` (or a sequence of byte loads).",
     true},
    {"ush",
     "Unaligned Store Halfword — write 16 bits at a possibly-unaligned "
     "address.",
     true},
    {"usw",
     "Unaligned Store Word — write 32 bits at a possibly-unaligned address.\n"
     "    Expands to `swl` + `swr`.",
     true},
    {NULL, NULL, false},
};

/* Continuation state. Set when a pseudo-op's first real instruction is
 * narrated; consulted on the next instruction to decide whether to emit a
 * "(continuation of `X` expansion above)" hint. */
static const char* pending_pseudo_name = NULL;
static bool pending_pseudo_multi = false;

/* Walk past the "NNN: " line-number prefix that source_line() prepends. */
static const char* skip_source_prefix(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  while (*s >= '0' && *s <= '9') s++;
  if (*s == ':') s++;
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

/* Match s against a pseudo-op mnemonic, requiring the mnemonic to be followed
 * by end-of-token (whitespace, comment, end of string). Returns the entry or
 * NULL. */
static const struct pseudo_info* find_pseudo_in_source(const char* src) {
  if (!src) return NULL;
  const char* s = skip_source_prefix(src);
  for (int i = 0; pseudo_ops[i].name != NULL; i++) {
    size_t n = strlen(pseudo_ops[i].name);
    if (strncmp(s, pseudo_ops[i].name, n) != 0) continue;
    char after = s[n];
    if (after == ' ' || after == '\t' || after == '\0' || after == '\n' ||
        after == '#')
      return &pseudo_ops[i];
  }
  return NULL;
}

/* read_mem_* will raise a real CPU exception on bad addresses (e.g. an
   uninitialized base register). The narrator must not mutate
   exception_occurred / CP0 state out from under the real dispatch, so wrap
   peeks in save/restore. */
static reg_word peek_word(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = read_mem_word(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

static reg_word peek_half(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = read_mem_half(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

static reg_word peek_byte(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = read_mem_byte(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

/* ------------ small printing helpers ------------ */

static void hr(void) {
  write_output(message_out,
               "----------------------------------------------------------\n");
}

static void say_input_reg(int r) {
  if (r == 0) return; /* $zero is always 0; saying so is noise */
  write_output(message_out, "    $%s = 0x%08x  (decimal %d)\n",
               int_reg_names[r], R[r], R[r]);
}

static void say_input_imm(int imm_signed) {
  write_output(message_out, "    immediate = %d  (0x%04x)\n", imm_signed,
               imm_signed & 0xffff);
}

static void say_will_write_reg(int r) {
  write_output(message_out, "  Will write:\n");
  write_output(message_out, "    $%s  (currently 0x%08x)\n", int_reg_names[r],
               R[r]);
}

/* Emit "Try it yourself" lines for up to four registers. Pass 0 (=$zero) to
   skip a slot; duplicates are skipped automatically. */
static void say_try_regs(int a, int b, int c, int d) {
  int regs[4] = {a, b, c, d};
  write_output(message_out, "  Try it yourself:\n");
  bool any = false;
  for (int i = 0; i < 4; i++) {
    int r = regs[i];
    if (r == 0) continue;
    bool dup = false;
    for (int j = 0; j < i; j++)
      if (regs[j] == r) dup = true;
    if (dup) continue;
    write_output(message_out, "    print $%s\n", int_reg_names[r]);
    any = true;
  }
  if (!any) {
    write_output(message_out, "    print_all_regs   # show every register\n");
  }
}

/* ------------ per-opcode templates ------------

   Each template prints: "What it does", "Inputs read", "Will write",
   "Try it yourself". They share a uniform shape so a student sees the same
   layout across every instruction.

   Template op_text strings use generic field placeholders `$rs`, `$rt`,
   `$rd` for readability at the call site (e.g. "compute $rs + $rt as a
   signed sum"). `subst_field_regs()` rewrites those placeholders to the
   actual register names this particular instruction uses, so a student
   sees one coherent vocabulary ($a2, $v0, ...) instead of mixing generic
   $rs/$rt with concrete $a2 in the same sentence. Pass -1 for any field
   that doesn't apply to a given template. */

static void subst_field_regs(char* dst, size_t cap, const char* src, int rs,
                             int rt, int rd) {
  size_t i = 0;
  while (*src && i + 1 < cap) {
    if (src[0] == '$' && src[1] == 'r' &&
        (src[2] == 's' || src[2] == 't' || src[2] == 'd') &&
        !isalnum((unsigned char)src[3]) && src[3] != '_') {
      int r = -1;
      switch (src[2]) {
        case 's':
          r = rs;
          break;
        case 't':
          r = rt;
          break;
        case 'd':
          r = rd;
          break;
      }
      if (r >= 0 && r < 32) {
        int n = snprintf(dst + i, cap - i, "$%s", int_reg_names[r]);
        if (n > 0) i += (size_t)n;
        if (i >= cap) i = cap - 1;
        src += 3;
        continue;
      }
    }
    dst[i++] = *src++;
  }
  dst[i] = '\0';
}

static void tpl_r3_arith(instruction* inst, const char* op_label,
                         const char* op_text) {
  int rs = RS(inst), rt = RT(inst), rd = RD(inst);
  char buf[512];
  subst_field_regs(buf, sizeof(buf), op_text, rs, rt, rd);
  write_output(message_out, "  What it does:\n");
  write_output(message_out, "    %s — %s. Result is placed in $%s.\n", op_label,
               buf, int_reg_names[rd]);
  write_output(message_out, "  Inputs read:\n");
  if (rs == 0 && rt == 0)
    write_output(message_out, "    (both source registers are $zero)\n");
  say_input_reg(rs);
  say_input_reg(rt);
  say_will_write_reg(rd);
  say_try_regs(rs, rt, rd, 0);
}

static void tpl_i2_arith(instruction* inst, const char* op_label,
                         const char* op_text) {
  int rs = RS(inst), rt = RT(inst);
  short imm = (short)IMM(inst);
  char buf[512];
  /* For I-type, "$rt" in the op_text refers to the source field; the
     destination is also rt architecturally but we always describe it
     by its concrete name in the surrounding "Result is placed in" line.
     We map $rs and $rt to their concrete names; $rd is unused. */
  subst_field_regs(buf, sizeof(buf), op_text, rs, rt, -1);
  write_output(message_out, "  What it does:\n");
  write_output(message_out, "    %s — %s. Result is placed in $%s.\n", op_label,
               buf, int_reg_names[rt]);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(rs);
  say_input_imm(imm);
  say_will_write_reg(rt);
  say_try_regs(rs, rt, 0, 0);
}

static void tpl_shift(instruction* inst, const char* op_label,
                      const char* op_text) {
  int rt = RT(inst), rd = RD(inst), sh = SHAMT(inst);
  char buf[512];
  subst_field_regs(buf, sizeof(buf), op_text, -1, rt, rd);
  write_output(message_out, "  What it does:\n");
  write_output(message_out,
               "    %s — %s by %d bit%s. Result is placed in $%s.\n", op_label,
               buf, sh, sh == 1 ? "" : "s", int_reg_names[rd]);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(rt);
  write_output(message_out, "    shift amount = %d\n", sh);
  say_will_write_reg(rd);
  say_try_regs(rt, rd, 0, 0);
}

static void tpl_load(instruction* inst, const char* op_label,
                     const char* op_text, int width) {
  int rt = RT(inst), base = BASE(inst);
  short off = (short)IOFFSET(inst);
  mem_addr ea = (mem_addr)(R[base] + off);
  write_output(message_out, "  What it does:\n");
  write_output(message_out,
               "    %s — %s from memory at the effective address, "
               "and place the result in $%s.\n",
               op_label, op_text, int_reg_names[rt]);
  write_output(message_out,
               "    Effective address = $%s + %d  =  0x%08x + %d  =  0x%08x.\n",
               int_reg_names[base], off, R[base], off, ea);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(base);
  if (off != 0)
    write_output(message_out, "    offset = %d  (0x%04x)\n", off, off & 0xffff);
  reg_word cur = 0;
  if (width == 1)
    cur = peek_byte(ea) & 0xff;
  else if (width == 2)
    cur = peek_half(ea) & 0xffff;
  else
    cur = peek_word(ea);
  write_output(message_out, "    memory at 0x%08x currently = 0x%08x\n", ea,
               (uint32)cur);
  say_will_write_reg(rt);
  say_try_regs(base, rt, 0, 0);
  write_output(message_out, "    print 0x%08x          # inspect that memory\n",
               ea);
  write_output(message_out,
               "    print %d($%s)          # same memory, using the N($reg) "
               "form you wrote\n",
               off, int_reg_names[base]);
}

static void tpl_store(instruction* inst, const char* op_label,
                      const char* op_text, int width) {
  int rt = RT(inst), base = BASE(inst);
  short off = (short)IOFFSET(inst);
  mem_addr ea = (mem_addr)(R[base] + off);
  write_output(message_out, "  What it does:\n");
  write_output(message_out,
               "    %s — %s of $%s into memory at the effective address.\n",
               op_label, op_text, int_reg_names[rt]);
  write_output(message_out,
               "    Effective address = $%s + %d  =  0x%08x + %d  =  0x%08x.\n",
               int_reg_names[base], off, R[base], off, ea);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(base);
  say_input_reg(rt);
  if (off != 0)
    write_output(message_out, "    offset = %d  (0x%04x)\n", off, off & 0xffff);
  reg_word cur = 0;
  if (width == 1)
    cur = peek_byte(ea) & 0xff;
  else if (width == 2)
    cur = peek_half(ea) & 0xffff;
  else
    cur = peek_word(ea);
  write_output(message_out, "  Will write to memory:\n");
  write_output(message_out, "    0x%08x  (currently 0x%08x)\n", ea,
               (uint32)cur);
  say_try_regs(base, rt, 0, 0);
  write_output(message_out, "    print 0x%08x          # inspect that memory\n",
               ea);
  write_output(message_out,
               "    print %d($%s)          # same memory, using the N($reg) "
               "form you wrote\n",
               off, int_reg_names[base]);
}

static void tpl_branch_2reg(instruction* inst, const char* op_label,
                            const char* op_symbol, bool taken) {
  int rs = RS(inst), rt = RT(inst);
  mem_addr target = PC + 4 + IDISP(inst);
  mem_addr fallthrough = PC + 4;
  write_output(message_out, "  What it does:\n");
  write_output(message_out, "    %s — branch to 0x%08x if ($%s %s $%s).\n",
               op_label, target, int_reg_names[rs], op_symbol,
               int_reg_names[rt]);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(rs);
  say_input_reg(rt);
  write_output(message_out, "    branch target = 0x%08x\n", target);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s %d  →  %s  →  branch %s\n", R[rs],
               op_symbol, R[rt], taken ? "true" : "false",
               taken ? "WILL be taken" : "will NOT be taken");
  write_output(message_out, "    PC will become 0x%08x\n",
               taken ? target : fallthrough);
  say_try_regs(rs, rt, 0, 0);
}

static void tpl_branch_1reg(instruction* inst, const char* op_label,
                            const char* op_symbol, bool taken) {
  int rs = RS(inst);
  mem_addr target = PC + 4 + IDISP(inst);
  mem_addr fallthrough = PC + 4;
  write_output(message_out, "  What it does:\n");
  write_output(message_out, "    %s — branch to 0x%08x if ($%s %s 0).\n",
               op_label, target, int_reg_names[rs], op_symbol);
  write_output(message_out, "  Inputs read:\n");
  say_input_reg(rs);
  write_output(message_out, "    branch target = 0x%08x\n", target);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s 0  →  %s  →  branch %s\n", R[rs],
               op_symbol, taken ? "true" : "false",
               taken ? "WILL be taken" : "will NOT be taken");
  write_output(message_out, "    PC will become 0x%08x\n",
               taken ? target : fallthrough);
  say_try_regs(rs, 0, 0, 0);
}

/* ------------ syscall, the special case ------------ */

static void explain_syscall(void) {
  reg_word v0 = R[REG_V0];
  write_output(message_out, "  What it does:\n");
  write_output(message_out,
               "    System call. The call number lives in $v0; arguments\n"
               "    live in $a0..$a3 (and $f12 for floats).\n");
  switch (v0) {
    case 1:
      write_output(message_out,
                   "    $v0 = 1  →  print_int: print the integer in $a0.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 1\n");
      say_input_reg(REG_A0);
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Writes the integer to the console.\n");
      say_try_regs(REG_V0, REG_A0, 0, 0);
      break;
    case 4: {
      mem_addr a0 = (mem_addr)R[REG_A0];
      write_output(message_out,
                   "    $v0 = 4  →  print_string: print the null-terminated\n"
                   "    string whose address is in $a0.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 4\n");
      write_output(message_out, "    $a0 = 0x%08x  (address of the string)\n",
                   a0);
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Writes the string to the console.\n");
      say_try_regs(REG_V0, REG_A0, 0, 0);
      write_output(message_out,
                   "    print 0x%08x          # inspect string memory\n", a0);
      break;
    }
    case 5:
      write_output(
          message_out,
          "    $v0 = 5  →  read_int: read an integer from the console\n"
          "    and return it in $v0.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 5\n");
      write_output(message_out, "  Will write:\n");
      write_output(message_out,
                   "    $v0  (will hold the integer that was read)\n");
      say_try_regs(REG_V0, 0, 0, 0);
      break;
    case 8: {
      mem_addr a0 = (mem_addr)R[REG_A0];
      write_output(
          message_out,
          "    $v0 = 8  →  read_string: read a string into the buffer\n"
          "    at $a0, up to $a1 bytes.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 8\n");
      write_output(message_out, "    $a0 = 0x%08x  (buffer address)\n", a0);
      say_input_reg(REG_A1);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    memory at 0x%08x .. 0x%08x\n", a0,
                   a0 + R[REG_A1] - 1);
      say_try_regs(REG_V0, REG_A0, REG_A1, 0);
      break;
    }
    case 10:
      write_output(message_out, "    $v0 = 10 →  exit: halt the program.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 10\n");
      say_try_regs(REG_V0, 0, 0, 0);
      break;
    case 11:
      write_output(message_out,
                   "    $v0 = 11 →  print_char: print the byte in $a0 as a "
                   "character.\n");
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    $v0 = 11\n");
      say_input_reg(REG_A0);
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Writes one character to the console.\n");
      say_try_regs(REG_V0, REG_A0, 0, 0);
      break;
    default:
      write_output(
          message_out,
          "    $v0 = %d (no detailed explanation for this syscall yet).\n", v0);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(REG_V0);
      say_try_regs(REG_V0, REG_A0, REG_A1, 0);
      break;
  }
}

/* ------------ bit-layout ASCII diagram ------------
 *
 * For each instruction, render the 32-bit encoding as a labeled bit-field
 * diagram so a student can see how the assembled binary decomposes into the
 * MIPS instruction fields. Three canonical formats:
 *
 *   R-type:  opcode(6) | rs(5)  | rt(5)  | rd(5)  | shamt(5) | funct(6)
 *   I-type:  opcode(6) | rs(5)  | rt(5)  | immediate(16)
 *   J-type:  opcode(6) | target(26)
 *
 * Classification is by the top-6 opcode field:
 *   0x00 SPECIAL, 0x1c SPECIAL2, 0x1f SPECIAL3  -> R-type
 *   0x02 J, 0x03 JAL                            -> J-type
 *   everything else                             -> I-type
 *   (Coprocessor formats 0x10-0x13 are rendered as I-type — they fit the
 *    same 6/5/5/16 split closely enough for a first look.)
 */

static void write_bits(char* out, uint32 val, int hi, int lo) {
  int width = hi - lo + 1;
  for (int i = 0; i < width; i++) {
    int bit_pos = hi - i;
    out[i] = (val & (1u << bit_pos)) ? '1' : '0';
  }
  out[width] = '\0';
}

static void render_r_layout(uint32 enc, const char* mnemonic) {
  char b_op[7], b_rs[6], b_rt[6], b_rd[6], b_sh[6], b_fn[7];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_rs, enc, 25, 21);
  write_bits(b_rt, enc, 20, 16);
  write_bits(b_rd, enc, 15, 11);
  write_bits(b_sh, enc, 10, 6);
  write_bits(b_fn, enc, 5, 0);
  int op = (enc >> 26) & 0x3f;
  int rs = (enc >> 21) & 0x1f;
  int rt = (enc >> 16) & 0x1f;
  int rd = (enc >> 11) & 0x1f;
  int shamt = (enc >> 6) & 0x1f;
  int funct = enc & 0x3f;
  const char* group = (op == 0x00)   ? "SPECIAL"
                      : (op == 0x1c) ? "SPECIAL2"
                      : (op == 0x1f) ? "SPECIAL3"
                                     : "?";

  write_output(message_out,
               "  Bit layout (R-type, encoding 0x%08x):\n"
               "     31    26 25  21 20  16 15  11 10   6 5     0\n"
               "    +--------+-------+-------+-------+-------+--------+\n"
               "    | %s | %s | %s | %s | %s | %s |\n"
               "    +--------+-------+-------+-------+-------+--------+\n"
               "      opcode    rs      rt      rd     shamt    funct\n"
               "       = %-2d   = $%-4s = $%-4s = $%-4s   = %-2d    = 0x%02x\n"
               "    -> opcode=0x%02x (%s) tells the CPU to look at funct;\n"
               "       funct=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_rs, b_rt, b_rd, b_sh, b_fn, op, int_reg_names[rs],
               int_reg_names[rt], int_reg_names[rd], shamt, funct, op, group,
               funct, mnemonic);
}

static void render_i_layout(uint32 enc, const char* mnemonic) {
  char b_op[7], b_rs[6], b_rt[6], b_im[17];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_rs, enc, 25, 21);
  write_bits(b_rt, enc, 20, 16);
  write_bits(b_im, enc, 15, 0);
  int op = (enc >> 26) & 0x3f;
  int rs = (enc >> 21) & 0x1f;
  int rt = (enc >> 16) & 0x1f;
  int16_t imm_s = (int16_t)(enc & 0xffff);
  uint16_t imm_u = (uint16_t)(enc & 0xffff);

  write_output(message_out,
               "  Bit layout (I-type, encoding 0x%08x):\n"
               "     31    26 25  21 20  16 15                 0\n"
               "    +--------+-------+-------+------------------+\n"
               "    | %s | %s | %s | %s |\n"
               "    +--------+-------+-------+------------------+\n"
               "      opcode    rs      rt        immediate (16-bit)\n"
               "      = 0x%02x  = $%-4s = $%-4s   = %d  (signed) / 0x%04x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_rs, b_rt, b_im, op, int_reg_names[rs],
               int_reg_names[rt], (int)imm_s, imm_u, op, mnemonic);
}

static void render_j_layout(uint32 enc, const char* mnemonic) {
  char b_op[7], b_tg[27];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_tg, enc, 25, 0);
  int op = (enc >> 26) & 0x3f;
  uint32 target = enc & 0x03ffffff;

  write_output(message_out,
               "  Bit layout (J-type, encoding 0x%08x):\n"
               "     31    26 25                          0\n"
               "    +--------+----------------------------+\n"
               "    | %s | %s |\n"
               "    +--------+----------------------------+\n"
               "      opcode         target (26-bit word index)\n"
               "      = 0x%02x  = 0x%07x  ->  jump addr = (PC[31:28] | "
               "target<<2) = 0x%08x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_tg, op, target,
               (snap_PC & 0xf0000000u) | (target << 2), op, mnemonic);
}

static void explain_bit_layout(instruction* inst) {
  if (inst == NULL) return;
  uint32 enc = (uint32)inst_encode(inst);
  /* nop (sll $0,$0,0) genuinely encodes to 0x00000000. Skip the diagram
     since "all zeros" carries no pedagogical signal of its own; the
     mnemonic-level explanation below covers it. */
  if (enc == 0) return;

  const char* mnemonic = inst_op_name(inst);
  int op = (enc >> 26) & 0x3f;
  if (op == 0x00 || op == 0x1c || op == 0x1f)
    render_r_layout(enc, mnemonic);
  else if (op == 0x02 || op == 0x03)
    render_j_layout(enc, mnemonic);
  else
    render_i_layout(enc, mnemonic);
}

/* ------------ main entrypoints ------------ */

void explain_before(instruction* inst, mem_addr addr) {
  if (!explain_mode) return;

  /* Snapshot for the after-diff. */
  memcpy(snap_R, R, sizeof(snap_R));
  snap_HI = HI;
  snap_LO = LO;
  snap_PC = addr;

  write_output(message_out, "\n");
  hr();
  write_output(message_out, "About to execute at 0x%08x:\n", addr);

  /* Disassembled form. inst_to_string already appends "; <source line>" as a
     trailing comment when the instruction has a source_line, so we don't need
     to print it separately. */
  char* dis = inst_to_string(addr);
  write_output(message_out, "    %s", dis);
  size_t n = strlen(dis);
  if (n == 0 || dis[n - 1] != '\n') write_output(message_out, "\n");
  free(dis);

  /* ASCII bit-field diagram for the 32-bit encoding. */
  explain_bit_layout(inst);

  /* Pseudo-op header / continuation hint.
   *
   * Two paths:
   *   (a) Current instruction has a SOURCE line. Try to match it against a
   *       pseudo-op mnemonic. If matched, describe the pseudo-op and mark a
   *       continuation pending if it may expand to multiple real insts. If
   *       not matched, clear any prior pending state (we've moved to a new
   *       source line).
   *   (b) Current instruction has no SOURCE line. If we have a pending
   *       multi-instruction pseudo-op from a prior call, emit a continuation
   *       hint and leave the pending state in place (more continuations may
   *       follow).
   */
  if (accept_pseudo_insts && SOURCE(inst) != NULL) {
    const struct pseudo_info* p = find_pseudo_in_source(SOURCE(inst));
    if (p != NULL) {
      write_output(message_out,
                   "  Pseudo-instruction `%s` (as written in source):\n"
                   "    %s\n"
                   "  The line below is the %s real instruction the assembler "
                   "emitted.\n",
                   p->name, p->what_it_means,
                   p->may_be_multi ? "first" : "single");
      pending_pseudo_name = p->name;
      pending_pseudo_multi = p->may_be_multi;
    } else {
      pending_pseudo_name = NULL;
      pending_pseudo_multi = false;
    }
  } else if (pending_pseudo_name != NULL && pending_pseudo_multi) {
    write_output(message_out,
                 "  (continuation of the `%s` pseudo-op expansion above —\n"
                 "   same source line, next real instruction emitted)\n",
                 pending_pseudo_name);
  }
  write_output(message_out, "\n");

  switch (OPCODE(inst)) {
    /* Arithmetic, R-type */
    case Y_ADD_OP:
      tpl_r3_arith(inst, "Add",
                   "compute $rs + $rt as a signed sum; trap on overflow");
      break;
    case Y_ADDU_OP:
      tpl_r3_arith(inst, "Add Unsigned",
                   "compute $rs + $rt with no overflow trap");
      break;
    case Y_SUB_OP:
      tpl_r3_arith(
          inst, "Subtract",
          "compute $rs - $rt as a signed difference; trap on overflow");
      break;
    case Y_SUBU_OP:
      tpl_r3_arith(inst, "Subtract Unsigned",
                   "compute $rs - $rt with no overflow trap");
      break;
    case Y_AND_OP:
      tpl_r3_arith(inst, "Bitwise AND",
                   "compute the bitwise AND of $rs and $rt");
      break;
    case Y_OR_OP:
      tpl_r3_arith(inst, "Bitwise OR", "compute the bitwise OR of $rs and $rt");
      break;
    case Y_XOR_OP:
      tpl_r3_arith(inst, "Bitwise XOR",
                   "compute the bitwise XOR of $rs and $rt");
      break;
    case Y_NOR_OP:
      tpl_r3_arith(inst, "Bitwise NOR", "compute NOT($rs OR $rt)");
      break;
    case Y_SLT_OP:
      tpl_r3_arith(inst, "Set on Less Than (signed)",
                   "set the destination to 1 if $rs < $rt (signed), else 0");
      break;
    case Y_SLTU_OP:
      tpl_r3_arith(inst, "Set on Less Than (unsigned)",
                   "set the destination to 1 if $rs < $rt (unsigned), else 0");
      break;
    case Y_MUL_OP:
      tpl_r3_arith(inst, "Multiply (MIPS32)",
                   "compute the low 32 bits of $rs * $rt");
      break;

    /* Arithmetic, I-type */
    case Y_ADDI_OP:
      tpl_i2_arith(inst, "Add Immediate",
                   "compute $rs + immediate; trap on overflow");
      break;
    case Y_ADDIU_OP:
      tpl_i2_arith(inst, "Add Immediate Unsigned",
                   "compute $rs + sign-extended immediate, no overflow trap");
      break;
    case Y_ANDI_OP:
      tpl_i2_arith(inst, "AND Immediate",
                   "compute $rs AND zero-extended immediate");
      break;
    case Y_ORI_OP:
      tpl_i2_arith(inst, "OR Immediate",
                   "compute $rs OR zero-extended immediate");
      break;
    case Y_XORI_OP:
      tpl_i2_arith(inst, "XOR Immediate",
                   "compute $rs XOR zero-extended immediate");
      break;
    case Y_SLTI_OP:
      tpl_i2_arith(inst, "Set Less Than Immediate (signed)",
                   "set destination to 1 if $rs < imm (signed)");
      break;
    case Y_SLTIU_OP:
      tpl_i2_arith(inst, "Set Less Than Immediate (unsigned)",
                   "set destination to 1 if $rs < imm (unsigned)");
      break;

    /* LUI is special: I1t-type, only writes high bits. */
    case Y_LUI_OP: {
      int rt = RT(inst);
      short imm = (short)IMM(inst);
      uint32 result = ((uint32)(imm & 0xffff)) << 16;
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Load Upper Immediate — place the 16-bit immediate "
                   "into the\n"
                   "    upper 16 bits of $%s, zero the lower 16. Often paired "
                   "with\n"
                   "    `ori` to construct a full 32-bit constant.\n",
                   int_reg_names[rt]);
      write_output(message_out, "  Inputs read:\n");
      say_input_imm(imm);
      write_output(message_out,
                   "    will become 0x%08x (immediate << 16) in $%s.\n", result,
                   int_reg_names[rt]);
      say_will_write_reg(rt);
      say_try_regs(rt, 0, 0, 0);
      break;
    }

    /* Shifts */
    case Y_SLL_OP:
      if (RD(inst) == 0 && RT(inst) == 0 && SHAMT(inst) == 0) {
        /* nop = sll $0, $0, 0 */
        write_output(message_out, "  What it does:\n");
        write_output(message_out,
                     "    No Operation. The processor takes one cycle and "
                     "does nothing.\n"
                     "    (Encoded as `sll $0, $0, 0` — a shift of $zero "
                     "with no destination.)\n");
        write_output(message_out, "  Inputs read:\n    (none)\n");
        write_output(message_out, "  Will write:\n    (no register changes)\n");
        say_try_regs(0, 0, 0, 0);
      } else {
        tpl_shift(inst, "Shift Left Logical", "shift $rt left, filling with 0");
      }
      break;
    case Y_SRL_OP:
      tpl_shift(inst, "Shift Right Logical", "shift $rt right, filling with 0");
      break;
    case Y_SRA_OP:
      tpl_shift(inst, "Shift Right Arithmetic",
                "shift $rt right, filling with the sign bit");
      break;

    /* Loads */
    case Y_LW_OP:
      tpl_load(inst, "Load Word", "read a 32-bit word", 4);
      break;
    case Y_LB_OP:
      tpl_load(inst, "Load Byte (signed)", "read one byte and sign-extend it",
               1);
      break;
    case Y_LBU_OP:
      tpl_load(inst, "Load Byte Unsigned", "read one byte and zero-extend it",
               1);
      break;
    case Y_LH_OP:
      tpl_load(inst, "Load Halfword (signed)",
               "read 16 bits and sign-extend them", 2);
      break;
    case Y_LHU_OP:
      tpl_load(inst, "Load Halfword Unsigned",
               "read 16 bits and zero-extend them", 2);
      break;

    /* Stores */
    case Y_SW_OP:
      tpl_store(inst, "Store Word", "write the 32-bit value", 4);
      break;
    case Y_SB_OP:
      tpl_store(inst, "Store Byte", "write the low 8 bits", 1);
      break;
    case Y_SH_OP:
      tpl_store(inst, "Store Halfword", "write the low 16 bits", 2);
      break;

    /* Branches */
    case Y_BEQ_OP:
      tpl_branch_2reg(inst, "Branch if Equal",
                      "==", R[RS(inst)] == R[RT(inst)]);
      break;
    case Y_BNE_OP:
      tpl_branch_2reg(inst, "Branch if Not Equal",
                      "!=", R[RS(inst)] != R[RT(inst)]);
      break;
    case Y_BGEZ_OP:
      tpl_branch_1reg(inst, "Branch if Greater or Equal Zero",
                      ">=", (reg_word)R[RS(inst)] >= 0);
      break;
    case Y_BGTZ_OP:
      tpl_branch_1reg(inst, "Branch if Greater Than Zero", ">",
                      (reg_word)R[RS(inst)] > 0);
      break;
    case Y_BLEZ_OP:
      tpl_branch_1reg(inst, "Branch if Less or Equal Zero",
                      "<=", (reg_word)R[RS(inst)] <= 0);
      break;
    case Y_BLTZ_OP:
      tpl_branch_1reg(inst, "Branch if Less Than Zero", "<",
                      (reg_word)R[RS(inst)] < 0);
      break;

    /* Jumps */
    case Y_J_OP: {
      mem_addr target = TARGET(inst) << 2;
      write_output(message_out, "  What it does:\n");
      write_output(message_out, "    Jump unconditionally to 0x%08x.\n",
                   target);
      write_output(message_out, "  Inputs read:\n    (none)\n");
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    PC  (currently 0x%08x  ->  0x%08x)\n", PC,
                   target);
      say_try_regs(0, 0, 0, 0);
      break;
    }
    case Y_JAL_OP: {
      mem_addr target = TARGET(inst) << 2;
      mem_addr link = PC + (delayed_branches ? 8 : 4);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Jump and Link — save 0x%08x (%s) into $ra, then "
                   "jump to 0x%08x.\n",
                   link,
                   delayed_branches ? "PC+8, skipping the delay slot"
                                    : "PC+4, the next instruction",
                   target);
      write_output(message_out,
                   "    (Standard subroutine call: $ra holds "
                   "the return address.)\n");
      write_output(message_out, "  Inputs read:\n    (none)\n");
      write_output(message_out, "  Will write:\n");
      write_output(message_out,
                   "    $ra (currently 0x%08x)  →  0x%08x  (return address)\n",
                   R[31], link);
      write_output(message_out, "    PC  (currently 0x%08x)  →  0x%08x\n", PC,
                   target);
      say_try_regs(31, 0, 0, 0);
      break;
    }
    case Y_JR_OP: {
      int rs = RS(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Jump Register — jump to the address held in $%s.\n"
                   "    (Used to return from a subroutine when $rs == $ra.)\n",
                   int_reg_names[rs]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    PC  (currently 0x%08x)  →  0x%08x\n", PC,
                   (mem_addr)R[rs]);
      say_try_regs(rs, 0, 0, 0);
      break;
    }
    case Y_JALR_OP: {
      int rd = RD(inst), rs = RS(inst);
      mem_addr link = PC + (delayed_branches ? 8 : 4);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Jump and Link Register — save 0x%08x (%s) into "
                   "$%s,\n    then jump to the address in $%s.\n",
                   link,
                   delayed_branches ? "PC+8, skipping the delay slot"
                                    : "PC+4, the next instruction",
                   int_reg_names[rd], int_reg_names[rs]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    $%s  →  0x%08x\n", int_reg_names[rd],
                   link);
      write_output(message_out, "    PC   →  0x%08x\n", (mem_addr)R[rs]);
      say_try_regs(rs, rd, 0, 0);
      break;
    }

    /* HI / LO */
    case Y_MFHI_OP: {
      int rd = RD(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Move From HI — copy the special HI register into $%s.\n"
                   "    HI holds the high half of a multiplication, or the "
                   "remainder of a division.\n",
                   int_reg_names[rd]);
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    HI = 0x%08x\n", HI);
      say_will_write_reg(rd);
      say_try_regs(rd, 0, 0, 0);
      break;
    }
    case Y_MFLO_OP: {
      int rd = RD(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Move From LO — copy the special LO register into $%s.\n"
                   "    LO holds the low half of a multiplication, or the "
                   "quotient of a division.\n",
                   int_reg_names[rd]);
      write_output(message_out, "  Inputs read:\n");
      write_output(message_out, "    LO = 0x%08x\n", LO);
      say_will_write_reg(rd);
      say_try_regs(rd, 0, 0, 0);
      break;
    }
    case Y_MULT_OP: {
      int rs = RS(inst), rt = RT(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Multiply (signed) — compute $%s * $%s as a 64-bit "
                   "result.\n"
                   "    The high 32 bits go to HI; the low 32 bits go to LO.\n"
                   "    Use mfhi / mflo to retrieve them.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      say_input_reg(rt);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    HI, LO\n");
      say_try_regs(rs, rt, 0, 0);
      break;
    }
    case Y_DIV_OP: {
      int rs = RS(inst), rt = RT(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Divide (signed) — compute $%s / $%s.\n"
                   "    Quotient goes to LO; remainder goes to HI.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      say_input_reg(rt);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    HI (remainder), LO (quotient)\n");
      say_try_regs(rs, rt, 0, 0);
      break;
    }
    case Y_MULTU_OP: {
      int rs = RS(inst), rt = RT(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Multiply Unsigned — compute $%s * $%s as a 64-bit "
                   "unsigned result.\n"
                   "    The high 32 bits go to HI; the low 32 bits go to LO.\n"
                   "    Use mfhi / mflo to retrieve them. No overflow trap.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      say_input_reg(rt);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    HI, LO\n");
      say_try_regs(rs, rt, 0, 0);
      break;
    }
    case Y_DIVU_OP: {
      int rs = RS(inst), rt = RT(inst);
      write_output(message_out, "  What it does:\n");
      write_output(message_out,
                   "    Divide Unsigned — compute $%s / $%s, treating both "
                   "operands as unsigned.\n"
                   "    Quotient goes to LO; remainder goes to HI.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      write_output(message_out, "  Inputs read:\n");
      say_input_reg(rs);
      say_input_reg(rt);
      write_output(message_out, "  Will write:\n");
      write_output(message_out, "    HI (remainder), LO (quotient)\n");
      say_try_regs(rs, rt, 0, 0);
      break;
    }

    /* Syscall */
    case Y_SYSCALL_OP:
      explain_syscall();
      break;

    default:
      write_output(message_out,
                   "  (no detailed explanation for this opcode yet — "
                   "see the disassembly above)\n");
      break;
  }

  write_output(message_out, "\n");
}

void explain_after(instruction* inst) {
  if (!explain_mode) return;

  /* If this was a load, force-print the destination register even when
   * the loaded value happens to equal the prior contents — otherwise the
   * student sees "(no register changes)" after a load that did execute,
   * which is pedagogically misleading. */
  int load_rt = -1;
  switch (OPCODE(inst)) {
    case Y_LW_OP:
    case Y_LB_OP:
    case Y_LBU_OP:
    case Y_LH_OP:
    case Y_LHU_OP:
      load_rt = RT(inst);
      break;
  }

  write_output(message_out, "After execution:\n");

  bool any = false;
  bool printed_load_rt = false;
  for (int i = 1; i < R_LENGTH; i++) {
    if (R[i] != snap_R[i]) {
      write_output(message_out, "    $%s:  0x%08x  ->  0x%08x   (decimal %d)\n",
                   int_reg_names[i], snap_R[i], R[i], R[i]);
      any = true;
      if (i == load_rt) printed_load_rt = true;
    }
  }
  if (load_rt > 0 && !printed_load_rt) {
    write_output(message_out,
                 "    $%s:  0x%08x  (loaded from memory; same value "
                 "as before — load still happened)\n",
                 int_reg_names[load_rt], R[load_rt]);
    any = true;
  }
  if (HI != snap_HI) {
    write_output(message_out, "    HI:   0x%08x  ->  0x%08x\n", snap_HI, HI);
    any = true;
  }
  if (LO != snap_LO) {
    write_output(message_out, "    LO:   0x%08x  ->  0x%08x\n", snap_LO, LO);
    any = true;
  }
  write_output(message_out, "    PC:   0x%08x  ->  0x%08x\n", snap_PC, PC);
  if (!any) write_output(message_out, "    (no register changes)\n");

  write_output(message_out, "\n");
}
