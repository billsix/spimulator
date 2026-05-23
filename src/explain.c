/* Teaching mode for spimulator.

   When explain_mode is true, the run loop calls explain_before() before each
   instruction executes (to narrate what's about to happen) and explain_after()
   after (to show what changed). All output goes through write_output() so it
   honors the simulator's console redirection. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "symbol-table.h"
#include "tokens.h"
#include "explain.h"

int explain_level = 0;

/* Snapshot of architectural state at explain_before() time. The narration
   itself runs from explain_after() (so every line reads in past tense by
   the time the user sees it), which means input-side reads come from
   snap_R / snap_HI / snap_LO / snap_PC and result-side reads come from
   the live gpr / HI / LO / PC. Stores also need a pre-execute memory
   snapshot, since by the time explain_after runs the write has happened
   and peek_word would return the new value. */
static reg_word snap_R[R_LENGTH];
static reg_word snap_HI, snap_LO;
static mem_addr snap_PC;
static bool snap_has_mem;
static mem_addr snap_mem_addr;
static reg_word snap_mem_val;

/* Per-instruction "did the template name this write?" tracking. The
   fallback block at the end of explain_after prints any register that
   changed but isn't here as a "Side effect:" line — protects against
   template/op mismatch and surfaces unexpected mutations. Reset at
   each explain_after entry. */
static bool touched_reg[R_LENGTH];
static bool touched_hi, touched_lo;

/* Once-per-session flag: explain_after emits a short labeled legend
   the first time it runs, explaining what each piece of the upcoming
   "Stepped" header line means. Spares the novice from having to infer
   the header's layout, and avoids cluttering every subsequent step. */
static bool legend_emitted = false;

/* Forward declarations needed by explain_print_step_header, which is
   defined below but called from explain_after (and from run.c, via the
   public extern in explain.h). */
static void hr(void);

static void emit_legend(void) {
  write_output(
      message_out,
      "\n"
      "══════════════════════════════════════════════════════════\n"
      "About the output\n"
      "══════════════════════════════════════════════════════════\n"
      "\n"
      "Every step begins with a header like:\n"
      "\n"
      "    Stepped at PC = 0x00400128:\n"
      "        memory[0x00400128] = 0x214affff   →   addi $t2, $t2, -1\n"
      "        source line 118: addi $t2, $t2, -1\n"
      "\n"
      "What each part means:\n"
      "\n"
      "  PC = 0xADDR\n"
      "        The program counter — the address in memory of the\n"
      "        instruction that just executed.\n"
      "\n"
      "  memory[0xADDR] = 0xWORD\n"
      "        The 32-bit machine code stored at that address. This\n"
      "        is what the CPU's decoder reads.\n"
      "\n"
      "  →  real-instruction\n"
      "        The disassembled form — what the CPU actually ran,\n"
      "        with registers shown in ABI names ($t2, $sp, $a0)\n"
      "        rather than register numbers.\n"
      "\n"
      "  source line N: ...\n"
      "        The line from your assembly file the assembler read\n"
      "        for this instruction. If it doesn't match the real\n"
      "        instruction on the line above, you wrote a pseudo-\n"
      "        instruction (e.g. `la $t7, dst` expands to `lui` +\n"
      "        `ori` — the source line appears once but two real\n"
      "        instructions get narrated).\n"
      "\n"
      "(This header is shown once per session. Higher `-explain`\n"
      "levels add per-instruction narration after the header.)\n");
}

/* Emit one step's header (legend on first call, then a horizontal
   separator, "Stepped at PC = X:", labeled machine-view line, optional
   source line). Public — called both from explain_after (for the
   labeled lead-in to the per-instruction narration) and from the
   default step display in run.c so the no-explain step output has the
   same shape. Level-independent. */
void explain_print_step_header(mem_addr pc, mips_instruction* instruction) {
  if (!legend_emitted) {
    emit_legend();
    legend_emitted = true;
  }

  write_output(message_out, "\n");
  hr();
  write_output(message_out, "Stepped at PC = 0x%08x:\n", pc);

  /* Disassembled form. inst_to_string returns one of:
       "[0xADDR]\t0xENC  DISASSEMBLY              ; LINE: SOURCE"
       "[0xADDR]\t0xENC  DISASSEMBLY"
     Split at the `;` so we can emit the source line under its own
     label, which makes pseudo-op divergence (source != real) visually
     obvious. */
  char* dis = inst_to_string(pc);
  size_t dn = strlen(dis);
  while (dn > 0 &&
         (dis[dn - 1] == '\n' || dis[dn - 1] == ' ' || dis[dn - 1] == '\t'))
    dis[--dn] = '\0';

  char* p = strchr(dis, '\t');
  if (p)
    p++;
  else
    p = dis;
  char* semi = strchr(p, ';');
  char* source_part = nullptr;
  if (semi) {
    char* end = semi;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = '\0';
    source_part = semi + 1;
    while (*source_part == ' ') source_part++;
  }
  /* p now starts with "0xENCODING  DISASSEMBLY". Strip the encoding
     (10 chars: "0x" + 8 hex) so we can label it explicitly. */
  char* dasm = p;
  if (strlen(p) >= 10 && p[0] == '0' && p[1] == 'x') {
    dasm = p + 10;
    while (*dasm == ' ' || *dasm == '\t') dasm++;
  }

  uint32_t enc = (uint32_t)inst_encode(instruction);
  write_output(message_out, "    memory[0x%08x] = 0x%08x   →   %s\n", pc, enc,
               dasm);
  if (source_part && *source_part) {
    write_output(message_out, "    source line %s\n", source_part);
  }
  free(dis);
}

/* ------------ Tab-completion suggestions ------------
 *
 * Each call to explain_before clears this list. Each "Try it yourself"
 * line emitted by a template adds its bare command (no trailing comment)
 * to the list. spim.c queries explain_suggestion_count / explain_suggestion
 * from its libedit completion callback so a student can recall a hint
 * with Tab.
 *
 * Lifetime: strings are str_copy'd at add time and freed by
 * explain_clear_suggestions. Storage is a fixed-cap array — 16 is a
 * comfortable ceiling (templates emit at most 3-4 hints).
 */
#define MAX_SUGGESTIONS 16
static char* suggestions[MAX_SUGGESTIONS];
static size_t n_suggestions;

void explain_clear_suggestions(void) {
  for (size_t i = 0; i < n_suggestions; i++) {
    free(suggestions[i]);
    suggestions[i] = nullptr;
  }
  n_suggestions = 0;
}

static void add_suggestion(const char* cmd) {
  if (n_suggestions >= MAX_SUGGESTIONS) return;
  char* copy = str_copy(cmd);
  if (copy != nullptr) suggestions[n_suggestions++] = copy;
}

size_t explain_suggestion_count(void) { return n_suggestions; }

const char* explain_suggestion(size_t i) {
  if (i >= n_suggestions) return nullptr;
  return suggestions[i];
}

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
    {nullptr, nullptr, false},
};

/* Continuation state. Set when a pseudo-op's first real instruction is
 * narrated; consulted on the next instruction to decide whether to emit a
 * "(continuation of `X` expansion above)" hint. */
static const char* pending_pseudo_name = nullptr;
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
 * nullptr. */
static const struct pseudo_info* find_pseudo_in_source(const char* src) {
  if (!src) return nullptr;
  const char* s = skip_source_prefix(src);
  for (int i = 0; pseudo_ops[i].name != nullptr; i++) {
    size_t n = strlen(pseudo_ops[i].name);
    if (strncmp(s, pseudo_ops[i].name, n) != 0) continue;
    char after = s[n];
    if (after == ' ' || after == '\t' || after == '\0' || after == '\n' ||
        after == '#')
      return &pseudo_ops[i];
  }
  return nullptr;
}

/* read_mem_* will raise a real CPU exception on bad addresses (e.g. an
   uninitialized base register). The narrator must not mutate
   exception_occurred / CP0 state out from under the real dispatch, so wrap
   peeks in save/restore. */
static reg_word peek_word(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = mem_read_word(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

static reg_word peek_half(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = mem_read_half(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

static reg_word peek_byte(mem_addr addr) {
  int se = exception_occurred;
  reg_word sbv = CP0_BadVAddr;
  reg_word v = mem_read_byte(addr);
  exception_occurred = se;
  CP0_BadVAddr = sbv;
  return v;
}

/* ------------ small printing helpers ------------ */

static void hr(void) {
  write_output(message_out,
               "──────────────────────────────────────────────────────────\n");
}

/* Input-side reads: always from snap_R, since templates run after the
   dispatch and gpr[] now holds post-execute values. */
static void say_input_reg(int r) {
  if (r == 0) return; /* $zero is always 0; saying so is noise */
  write_output(message_out, "    $%s = 0x%08x  (decimal %d)\n",
               int_reg_names[r], snap_R[r], snap_R[r]);
}

static void say_input_imm(int imm_signed) {
  write_output(message_out, "    immediate = %d  (0x%04x)\n", imm_signed,
               imm_signed & 0xffff);
}

/* Result-side: print "$X: before -> after (decimal N)" using the
   snapshot for the before value and the live register for the after.
   Marks the register as touched so the side-effect fallback doesn't
   duplicate it. */
static void say_wrote_reg(int r) {
  if (r == 0) return; /* writes to $zero are discarded; nothing to show */
  write_output(message_out, "    $%s:  0x%08x  →  0x%08x   (decimal %d)\n",
               int_reg_names[r], snap_R[r], gpr[r], gpr[r]);
  touched_reg[r] = true;
}

static void say_wrote_hi(void) {
  write_output(message_out, "    HI:   0x%08x  →  0x%08x\n", snap_HI, HI);
  touched_hi = true;
}
static void say_wrote_lo(void) {
  write_output(message_out, "    LO:   0x%08x  →  0x%08x\n", snap_LO, LO);
  touched_lo = true;
}

/* For stores: print "0x<addr>: prior -> current". width selects the
   masking applied to both ends so byte/halfword stores display
   sensibly. Only valid when snap_has_mem is true. */
static void say_wrote_mem(int width) {
  if (!snap_has_mem) return;
  uint32_t mask = (width == 1) ? 0xff : (width == 2) ? 0xffff : 0xffffffffu;
  reg_word now;
  if (width == 1)
    now = peek_byte(snap_mem_addr) & 0xff;
  else if (width == 2)
    now = peek_half(snap_mem_addr) & 0xffff;
  else
    now = peek_word(snap_mem_addr);
  write_output(message_out, "  Wrote to memory:\n");
  write_output(message_out, "    0x%08x:  0x%08x  →  0x%08x\n", snap_mem_addr,
               ((uint32_t)snap_mem_val) & mask, ((uint32_t)now) & mask);
}

/* Emit "Try it yourself" lines for up to four registers. Pass 0 (=$zero) to
   skip a slot; duplicates are skipped automatically. Each emitted line is
   also added to the tab-completion suggestions list. */
static void say_try_regs(int a, int b, int c, int d) {
  int regs[4] = {a, b, c, d};
  write_output(message_out, "  Try it yourself:\n");
  bool any = false;
  char cmd[64];
  for (int i = 0; i < 4; i++) {
    int r = regs[i];
    if (r == 0) continue;
    bool dup = false;
    for (int j = 0; j < i; j++)
      if (regs[j] == r) dup = true;
    if (dup) continue;
    snprintf(cmd, sizeof cmd, "print $%s", int_reg_names[r]);
    write_output(message_out, "    %s\n", cmd);
    add_suggestion(cmd);
    any = true;
  }
  if (!any) {
    write_output(message_out, "    print_all_regs   # show every register\n");
    add_suggestion("print_all_regs");
  }
}

/* One-off "Try it yourself" line for hints that aren't a bare $reg —
   memory addresses, base+offset forms, etc. Writes the line aligned with
   a trailing # comment, and adds the bare cmd to suggestions. */
static void say_try(const char* cmd, const char* why) {
  write_output(message_out, "    %-22s # %s\n", cmd, why);
  add_suggestion(cmd);
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

static void tpl_r3_arith(int level, mips_instruction* instruction,
                         const char* op_label, const char* op_text) {
  int rs = RS(instruction), rt = RT(instruction), rd = RD(instruction);
  char buf[512];
  subst_field_regs(buf, sizeof(buf), op_text, rs, rt, rd);
  write_output(message_out, "  What it did:\n");
  write_output(message_out, "    %s — %s; the result was placed in $%s.\n",
               op_label, buf, int_reg_names[rd]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    if (rs == 0 && rt == 0)
      write_output(message_out, "    (both source registers are $zero)\n");
    say_input_reg(rs);
    say_input_reg(rt);
    write_output(message_out, "  Wrote:\n");
    say_wrote_reg(rd);
    say_try_regs(rs, rt, rd, 0);
  }
}

static void tpl_i2_arith(int level, mips_instruction* instruction,
                         const char* op_label, const char* op_text) {
  int rs = RS(instruction), rt = RT(instruction);
  short imm = (short)IMM(instruction);
  char buf[512];
  /* For I-type, "$rt" in the op_text refers to the source field; the
     destination is also rt architecturally but we always describe it
     by its concrete name in the surrounding "result was placed in" line.
     We map $rs and $rt to their concrete names; $rd is unused. */
  subst_field_regs(buf, sizeof(buf), op_text, rs, rt, -1);
  write_output(message_out, "  What it did:\n");
  write_output(message_out, "    %s — %s; the result was placed in $%s.\n",
               op_label, buf, int_reg_names[rt]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(rs);
    say_input_imm(imm);
    write_output(message_out, "  Wrote:\n");
    say_wrote_reg(rt);
    say_try_regs(rs, rt, 0, 0);
  }
}

static void tpl_shift(int level, mips_instruction* instruction,
                      const char* op_label, const char* op_text) {
  int rt = RT(instruction), rd = RD(instruction), sh = SHAMT(instruction);
  char buf[512];
  subst_field_regs(buf, sizeof(buf), op_text, -1, rt, rd);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — %s by %d bit%s; the result was placed in $%s.\n",
               op_label, buf, sh, sh == 1 ? "" : "s", int_reg_names[rd]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(rt);
    write_output(message_out, "    shift amount = %d\n", sh);
    write_output(message_out, "  Wrote:\n");
    say_wrote_reg(rd);
    say_try_regs(rt, rd, 0, 0);
  }
}

static void tpl_load(int level, mips_instruction* instruction,
                     const char* op_label, const char* op_text, int width) {
  int rt = RT(instruction), base = BASE(instruction);
  short off = (short)IOFFSET(instruction);
  /* Effective address uses snapshot base — the value the instruction
     actually used. If this load's rt happens to equal base (rare but
     legal), the post-execute gpr[base] is the loaded value, not the
     pre-execute base. */
  mem_addr ea = (mem_addr)(snap_R[base] + off);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — read %s from memory at the effective address; "
               "placed the result in $%s.\n",
               op_label, op_text, int_reg_names[rt]);
  write_output(message_out,
               "    Effective address = $%s + %d  =  0x%08x + %d  =  0x%08x.\n",
               int_reg_names[base], off, snap_R[base], off, ea);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(base);
    if (off != 0)
      write_output(message_out, "    offset = %d  (0x%04x)\n", off,
                   off & 0xffff);
    /* Loads don't modify memory, so peek_word post-execute returns the
       same value the instruction read. */
    reg_word cur = 0;
    if (width == 1)
      cur = peek_byte(ea) & 0xff;
    else if (width == 2)
      cur = peek_half(ea) & 0xffff;
    else
      cur = peek_word(ea);
    write_output(message_out, "    memory at 0x%08x = 0x%08x\n", ea,
                 (uint32_t)cur);
    write_output(message_out, "  Wrote:\n");
    say_wrote_reg(rt);
    say_try_regs(base, rt, 0, 0);
    char buf2[64];
    snprintf(buf2, sizeof buf2, "print 0x%08x", ea);
    say_try(buf2, "inspect that memory");
    snprintf(buf2, sizeof buf2, "print %d($%s)", off, int_reg_names[base]);
    say_try(buf2, "same memory, using the N($reg) form you wrote");
  }
}

static void tpl_store(int level, mips_instruction* instruction,
                      const char* op_label, const char* op_text, int width) {
  int rt = RT(instruction), base = BASE(instruction);
  short off = (short)IOFFSET(instruction);
  mem_addr ea = (mem_addr)(snap_R[base] + off);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — wrote %s from $%s into memory at the effective "
               "address.\n",
               op_label, op_text, int_reg_names[rt]);
  write_output(message_out,
               "    Effective address = $%s + %d  =  0x%08x + %d  =  0x%08x.\n",
               int_reg_names[base], off, snap_R[base], off, ea);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(base);
    say_input_reg(rt);
    if (off != 0)
      write_output(message_out, "    offset = %d  (0x%04x)\n", off,
                   off & 0xffff);
    say_wrote_mem(width);
    say_try_regs(base, rt, 0, 0);
    char buf2[64];
    snprintf(buf2, sizeof buf2, "print 0x%08x", ea);
    say_try(buf2, "inspect that memory");
    snprintf(buf2, sizeof buf2, "print %d($%s)", off, int_reg_names[base]);
    say_try(buf2, "same memory, using the N($reg) form you wrote");
  }
}

static void tpl_branch_2reg(int level, mips_instruction* instruction,
                            const char* op_label, const char* op_symbol,
                            bool taken) {
  int rs = RS(instruction), rt = RT(instruction);
  /* Target is computed from the instruction's own PC (snap_PC) plus 4 +
     displacement. Live PC has already advanced past this instruction. */
  mem_addr target = snap_PC + 4 + BRANCH_OFFSET(instruction);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — would transfer to 0x%08x if ($%s %s $%s).\n", op_label,
               target, int_reg_names[rs], op_symbol, int_reg_names[rt]);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s %d  →  %s  →  branch was %s\n",
               snap_R[rs], op_symbol, snap_R[rt], taken ? "true" : "false",
               taken ? "taken" : "NOT taken");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(rs);
    say_input_reg(rt);
    write_output(message_out, "    branch target = 0x%08x\n", target);
    say_try_regs(rs, rt, 0, 0);
  }
}

static void tpl_branch_1reg(int level, mips_instruction* instruction,
                            const char* op_label, const char* op_symbol,
                            bool taken) {
  int rs = RS(instruction);
  mem_addr target = snap_PC + 4 + BRANCH_OFFSET(instruction);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — would transfer to 0x%08x if ($%s %s 0).\n", op_label,
               target, int_reg_names[rs], op_symbol);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s 0  →  %s  →  branch was %s\n",
               snap_R[rs], op_symbol, taken ? "true" : "false",
               taken ? "taken" : "NOT taken");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_reg(rs);
    write_output(message_out, "    branch target = 0x%08x\n", target);
    say_try_regs(rs, 0, 0, 0);
  }
}

/* ------------ syscall, the special case ------------ */

static void explain_syscall(int level) {
  /* Call number is the pre-execute $v0. syscall 5 (read_int) overwrites
     $v0 with the read integer, so post-execute gpr[REG_V0] would be wrong. */
  reg_word v0 = snap_R[REG_V0];
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    System call. The call number was in $v0; arguments\n"
               "    were in $a0..$a3 (and $f12 for floats).\n");
  switch (v0) {
    case 1:
      write_output(message_out,
                   "    $v0 = 1  →  print_int: printed the integer in $a0.\n");
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Wrote the integer to the console.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 1\n");
        say_input_reg(REG_A0);
        say_try_regs(REG_V0, REG_A0, 0, 0);
      }
      break;
    case 4: {
      mem_addr a0 = (mem_addr)snap_R[REG_A0];
      write_output(message_out,
                   "    $v0 = 4  →  print_string: printed the null-terminated\n"
                   "    string whose address was in $a0.\n");
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Wrote the string to the console.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 4\n");
        write_output(message_out, "    $a0 = 0x%08x  (address of the string)\n",
                     a0);
        say_try_regs(REG_V0, REG_A0, 0, 0);
        char buf2[64];
        snprintf(buf2, sizeof buf2, "print 0x%08x", a0);
        say_try(buf2, "inspect string memory");
      }
      break;
    }
    case 5:
      write_output(
          message_out,
          "    $v0 = 5  →  read_int: read an integer from the console\n"
          "    and returned it in $v0.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 5\n");
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(REG_V0);
        say_try_regs(REG_V0, 0, 0, 0);
      }
      break;
    case 8: {
      mem_addr a0 = (mem_addr)snap_R[REG_A0];
      write_output(
          message_out,
          "    $v0 = 8  →  read_string: read a string into the buffer\n"
          "    at $a0, up to $a1 bytes.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 8\n");
        write_output(message_out, "    $a0 = 0x%08x  (buffer address)\n", a0);
        say_input_reg(REG_A1);
        write_output(message_out, "  Wrote to memory:\n");
        write_output(message_out, "    0x%08x .. 0x%08x\n", a0,
                     a0 + snap_R[REG_A1] - 1);
        say_try_regs(REG_V0, REG_A0, REG_A1, 0);
      }
      break;
    }
    case 10:
      write_output(message_out, "    $v0 = 10 →  exit: halted the program.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 10\n");
        say_try_regs(REG_V0, 0, 0, 0);
      }
      break;
    case 11:
      write_output(message_out,
                   "    $v0 = 11 →  print_char: printed the byte in $a0 as a "
                   "character.\n");
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Wrote one character to the console.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 11\n");
        say_input_reg(REG_A0);
        say_try_regs(REG_V0, REG_A0, 0, 0);
      }
      break;
    default:
      write_output(
          message_out,
          "    $v0 = %d (no detailed explanation for this syscall yet).\n", v0);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(REG_V0);
        say_try_regs(REG_V0, REG_A0, REG_A1, 0);
      }
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

static void write_bits(char* out, uint32_t val, int hi, int lo) {
  int width = hi - lo + 1;
  for (int i = 0; i < width; i++) {
    int bit_pos = hi - i;
    out[i] = (val & (1u << bit_pos)) ? '1' : '0';
  }
  out[width] = '\0';
}

static void render_r_layout(uint32_t enc, const char* mnemonic) {
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
               "    ┌────────┬───────┬───────┬───────┬───────┬────────┐\n"
               "    │ %s │ %s │ %s │ %s │ %s │ %s │\n"
               "    └────────┴───────┴───────┴───────┴───────┴────────┘\n"
               "      opcode    rs      rt      rd     shamt    funct\n"
               "       = %-2d   = $%-4s = $%-4s = $%-4s   = %-2d    = 0x%02x\n"
               "    -> opcode=0x%02x (%s) tells the CPU to look at funct;\n"
               "       funct=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_rs, b_rt, b_rd, b_sh, b_fn, op, int_reg_names[rs],
               int_reg_names[rt], int_reg_names[rd], shamt, funct, op, group,
               funct, mnemonic);
}

static void render_i_layout(uint32_t enc, const char* mnemonic) {
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
               "    ┌────────┬───────┬───────┬──────────────────┐\n"
               "    │ %s │ %s │ %s │ %s │\n"
               "    └────────┴───────┴───────┴──────────────────┘\n"
               "      opcode    rs      rt        immediate (16-bit)\n"
               "      = 0x%02x  = $%-4s = $%-4s   = %d  (signed) / 0x%04x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_rs, b_rt, b_im, op, int_reg_names[rs],
               int_reg_names[rt], (int)imm_s, imm_u, op, mnemonic);
}

static void render_j_layout(uint32_t enc, const char* mnemonic) {
  char b_op[7], b_tg[27];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_tg, enc, 25, 0);
  int op = (enc >> 26) & 0x3f;
  uint32_t target = enc & 0x03ffffff;

  write_output(message_out,
               "  Bit layout (J-type, encoding 0x%08x):\n"
               "     31    26 25                          0\n"
               "    ┌────────┬────────────────────────────┐\n"
               "    │ %s │ %s │\n"
               "    └────────┴────────────────────────────┘\n"
               "      opcode         target (26-bit word index)\n"
               "      = 0x%02x  = 0x%07x  ->  jump addr = (PC[31:28] | "
               "target<<2) = 0x%08x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               enc, b_op, b_tg, op, target,
               (snap_PC & 0xf0000000u) | (target << 2), op, mnemonic);
}

static void explain_bit_layout(mips_instruction* instruction) {
  if (instruction == nullptr) return;
  uint32_t enc = (uint32_t)inst_encode(instruction);
  /* nop (sll $0,$0,0) genuinely encodes to 0x00000000. Skip the diagram
     since "all zeros" carries no pedagogical signal of its own; the
     mnemonic-level explanation below covers it. */
  if (enc == 0) return;

  const char* mnemonic = inst_op_name(instruction);
  int op = (enc >> 26) & 0x3f;
  if (op == 0x00 || op == 0x1c || op == 0x1f)
    render_r_layout(enc, mnemonic);
  else if (op == 0x02 || op == 0x03)
    render_j_layout(enc, mnemonic);
  else
    render_i_layout(enc, mnemonic);
}

/* ------------ L4: progressive decoding walkthrough ------------
 *
 * At L4 the single L3 bit-layout box is replaced by a sequence of
 * frames — the same box redrawn step by step as the CPU's
 * hierarchical decoder resolves the word. Undecoded fields show
 * a "?" placeholder inside the box; once a field is read, its
 * bits and label appear.
 */

/* Pre-formatted "unknown" placeholders, sized to match each field's
   display width inside the box (one centered "?"). */
#define UK5 "  ?  "
#define UK6 "  ?   "
#define UK16 "       ?        "
#define UK26 "            ?             "

/* SPECIAL-group: opcode == 0x00, funct selects the actual op. Table
   filled out for the integer ops spimulator's existing templates
   cover; entries not listed return nullptr (rendered as "?"). */
static const char* special_funct_to_name[64] = {
    [0x00] = "sll",  [0x02] = "srl",   [0x03] = "sra",     [0x04] = "sllv",
    [0x06] = "srlv", [0x07] = "srav",  [0x08] = "jr",      [0x09] = "jalr",
    [0x0a] = "movz", [0x0b] = "movn",  [0x0c] = "syscall", [0x0d] = "break",
    [0x10] = "mfhi", [0x11] = "mthi",  [0x12] = "mflo",    [0x13] = "mtlo",
    [0x18] = "mult", [0x19] = "multu", [0x1a] = "div",     [0x1b] = "divu",
    [0x20] = "add",  [0x21] = "addu",  [0x22] = "sub",     [0x23] = "subu",
    [0x24] = "and",  [0x25] = "or",    [0x26] = "xor",     [0x27] = "nor",
    [0x2a] = "slt",  [0x2b] = "sltu",
};

/* REGIMM-group: opcode == 0x01, rt selects the actual op. */
static const char* regimm_rt_to_name[32] = {
    [0x00] = "bltz",
    [0x01] = "bgez",
    [0x10] = "bltzal",
    [0x11] = "bgezal",
};

static void render_r_decode_step(uint32_t enc, int step, const char* mnemonic,
                                 const char* group_name) {
  uint32_t op = (enc >> 26) & 0x3f;
  uint32_t rs = (enc >> 21) & 0x1f;
  uint32_t rt = (enc >> 16) & 0x1f;
  uint32_t rd = (enc >> 11) & 0x1f;
  uint32_t shamt = (enc >> 6) & 0x1f;
  uint32_t funct = enc & 0x3f;

  char b_op[7], b_rs[6], b_rt[6], b_rd[6], b_sh[6], b_fn[7];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_rs, enc, 25, 21);
  write_bits(b_rt, enc, 20, 16);
  write_bits(b_rd, enc, 15, 11);
  write_bits(b_sh, enc, 10, 6);
  write_bits(b_fn, enc, 5, 0);

  const char* fld_rs = (step >= 3) ? b_rs : UK5;
  const char* fld_rt = (step >= 3) ? b_rt : UK5;
  const char* fld_rd = (step >= 3) ? b_rd : UK5;
  const char* fld_sh = (step >= 3) ? b_sh : UK5;
  const char* fld_fn = (step >= 2) ? b_fn : UK6;

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out, "  Step 2 — read funct (bits [5:0]).\n");
      break;
    case 3:
      write_output(message_out,
                   "  Step 3 — read the remaining R-type fields.\n");
      break;
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬───────┬───────┬────────┐\n"
               "  │ %s │ %s │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴───────┴───────┴────────┘\n",
               b_op, fld_rs, fld_rt, fld_rd, fld_sh, fld_fn);
  write_output(message_out, "   opcode    %-7s %-7s %-7s %-7s %s\n",
               (step >= 3) ? "rs" : "?", (step >= 3) ? "rt" : "?",
               (step >= 3) ? "rd" : "?", (step >= 3) ? "shamt" : "?",
               (step >= 2) ? "funct" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", op);
  } else if (step == 2) {
    write_output(message_out,
                 "   = 0x%02x                                       "
                 "= 0x%02x\n",
                 op, funct);
  } else {
    bool shamt_used = mnemonic && (strcmp(mnemonic, "sll") == 0 ||
                                   strcmp(mnemonic, "srl") == 0 ||
                                   strcmp(mnemonic, "sra") == 0);
    write_output(message_out,
                 "   = 0x%02x    = $%-5s = $%-5s = $%-5s = %-5d = 0x%02x\n", op,
                 int_reg_names[rs], int_reg_names[rt], int_reg_names[rd], shamt,
                 funct);
    if (!shamt_used) {
      write_output(message_out,
                   "                                     "
                   "(unused for %s)\n",
                   mnemonic ? mnemonic : "this op");
    }
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x is the %s group. The CPU can't decide "
                 "the\n"
                 "  operation from the opcode alone — read the funct field "
                 "next.\n",
                 op, group_name);
  } else if (step == 2) {
    const char* fname =
        (op == 0x00 && funct < 64 && special_funct_to_name[funct])
            ? special_funct_to_name[funct]
            : (mnemonic ? mnemonic : "?");
    write_output(message_out,
                 "  In the %s table, funct=0x%02x is `%s`. Now we know\n"
                 "  this is R-type %s, so the remaining fields are rs, rt, "
                 "rd,\n"
                 "  shamt — read them in turn.\n",
                 group_name, funct, fname, fname);
  }
}

static void render_i_decode_step(uint32_t enc, int step, const char* mnemonic) {
  uint32_t op = (enc >> 26) & 0x3f;
  uint32_t rs = (enc >> 21) & 0x1f;
  uint32_t rt = (enc >> 16) & 0x1f;
  int16_t imm_s = (int16_t)(enc & 0xffff);
  uint16_t imm_u = (uint16_t)(enc & 0xffff);

  char b_op[7], b_rs[6], b_rt[6], b_im[17];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_rs, enc, 25, 21);
  write_bits(b_rt, enc, 20, 16);
  write_bits(b_im, enc, 15, 0);

  const char* fld_rs = (step >= 2) ? b_rs : UK5;
  const char* fld_rt = (step >= 2) ? b_rt : UK5;
  const char* fld_im = (step >= 2) ? b_im : UK16;

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out,
                   "  Step 2 — read the remaining I-type fields.\n");
      break;
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬──────────────────┐\n"
               "  │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴──────────────────┘\n",
               b_op, fld_rs, fld_rt, fld_im);
  write_output(message_out, "   opcode    %-7s %-7s %s\n",
               (step >= 2) ? "rs" : "?", (step >= 2) ? "rt" : "?",
               (step >= 2) ? "imm (16-bit, signed)" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", op);
  } else {
    write_output(message_out, "   = 0x%02x    = $%-5s = $%-5s = %d  (0x%04x)\n",
                 op, int_reg_names[rs], int_reg_names[rt], (int)imm_s, imm_u);
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x in the primary table is `%s`. This is an\n"
                 "  I-type instruction; the rest of the word is rs, rt, "
                 "and\n"
                 "  a 16-bit signed immediate.\n",
                 op, mnemonic ? mnemonic : "?");
  }
}

static void render_j_decode_step(uint32_t enc, int step, const char* mnemonic) {
  uint32_t op = (enc >> 26) & 0x3f;
  uint32_t target = enc & 0x03ffffff;

  char b_op[7], b_tg[27];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_tg, enc, 25, 0);

  const char* fld_tg = (step >= 2) ? b_tg : UK26;

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out, "  Step 2 — read the target field.\n");
      break;
  }

  write_output(message_out,
               "  ┌────────┬────────────────────────────┐\n"
               "  │ %s │ %s │\n"
               "  └────────┴────────────────────────────┘\n",
               b_op, fld_tg);
  write_output(message_out, "   opcode    %s\n",
               (step >= 2) ? "target (26-bit word index)" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", op);
  } else {
    write_output(message_out,
                 "   = 0x%02x    = 0x%07x\n"
                 "             -> actual address = (PC[31:28] | target<<2)\n"
                 "                                = 0x%08x\n",
                 op, target, (snap_PC & 0xf0000000u) | (target << 2));
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x in the primary table is `%s`. J-type —\n"
                 "  one big target field below.\n",
                 op, mnemonic ? mnemonic : "?");
  }
}

static void render_regimm_decode_step(uint32_t enc, int step,
                                      const char* mnemonic) {
  uint32_t op = (enc >> 26) & 0x3f;
  uint32_t rs = (enc >> 21) & 0x1f;
  uint32_t rt = (enc >> 16) & 0x1f;
  int16_t imm_s = (int16_t)(enc & 0xffff);

  char b_op[7], b_rs[6], b_rt[6], b_im[17];
  write_bits(b_op, enc, 31, 26);
  write_bits(b_rs, enc, 25, 21);
  write_bits(b_rt, enc, 20, 16);
  write_bits(b_im, enc, 15, 0);

  const char* fld_rs = (step >= 3) ? b_rs : UK5;
  const char* fld_rt = (step >= 2) ? b_rt : UK5;
  const char* fld_im = (step >= 3) ? b_im : UK16;

  const char* rtname = (rt < 32 && regimm_rt_to_name[rt])
                           ? regimm_rt_to_name[rt]
                           : (mnemonic ? mnemonic : "?");

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out,
                   "  Step 2 — read rt-as-selector (bits [20:16]).\n");
      break;
    case 3:
      write_output(message_out, "  Step 3 — read the rs and offset fields.\n");
      break;
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬──────────────────┐\n"
               "  │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴──────────────────┘\n",
               b_op, fld_rs, fld_rt, fld_im);
  write_output(message_out, "   opcode    %-7s %-7s %s\n",
               (step >= 3) ? "rs" : "?", (step >= 2) ? "rt-sel" : "?",
               (step >= 3) ? "offset (signed)" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", op);
  } else if (step == 2) {
    write_output(message_out, "   = 0x%02x            = %s\n", op, rtname);
  } else {
    write_output(
        message_out,
        "   = 0x%02x    = $%-5s = %-6s = %d  (target = PC+4 + (offset<<2))\n",
        op, int_reg_names[rs], rtname, (int)imm_s);
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x is the REGIMM group. Like SPECIAL, but the\n"
                 "  selector is in the rt field — not funct. Read rt next.\n",
                 op);
  } else if (step == 2) {
    write_output(message_out,
                 "  In the REGIMM table, rt=0x%02x is `%s`. This is I-type\n"
                 "  with a register tested against zero and a 16-bit branch\n"
                 "  displacement.\n",
                 rt, rtname);
  }
}

static void explain_decoding_steps(mips_instruction* instruction) {
  if (instruction == nullptr) return;
  uint32_t enc = (uint32_t)inst_encode(instruction);
  if (enc == 0) return; /* true nop — skip */

  const char* mnemonic = inst_op_name(instruction);
  int op = (enc >> 26) & 0x3f;

  write_output(message_out, "  Decoding 0x%08x step by step:\n\n", enc);

  if (op == 0x00 || op == 0x1c || op == 0x1f) {
    const char* group = (op == 0x00)   ? "SPECIAL"
                        : (op == 0x1c) ? "SPECIAL2"
                                       : "SPECIAL3";
    for (int s = 1; s <= 3; s++) {
      if (s > 1) write_output(message_out, "\n");
      render_r_decode_step(enc, s, mnemonic, group);
    }
  } else if (op == 0x01) {
    for (int s = 1; s <= 3; s++) {
      if (s > 1) write_output(message_out, "\n");
      render_regimm_decode_step(enc, s, mnemonic);
    }
  } else if (op == 0x02 || op == 0x03) {
    for (int s = 1; s <= 2; s++) {
      if (s > 1) write_output(message_out, "\n");
      render_j_decode_step(enc, s, mnemonic);
    }
  } else {
    for (int s = 1; s <= 2; s++) {
      if (s > 1) write_output(message_out, "\n");
      render_i_decode_step(enc, s, mnemonic);
    }
  }
}

/* ------------ main entrypoints ------------ */

/* explain_before runs immediately before the dispatch switch in run_spim.
   It captures everything the post-execute narration will need to read —
   nothing is printed here, so by the time the (spim) prompt comes back,
   every visible line describes something that has already happened. */
void explain_before(mips_instruction* instruction, mem_addr addr) {
  if (explain_level == 0) return;

  /* Tab-completion suggestions are per-instruction; clear before any
     template populates new ones. (At L1 nothing gets added, so the list
     remains empty — by design.) */
  explain_clear_suggestions();

  /* Snapshot of architectural state for the post-execute templates. */
  memcpy(snap_R, gpr, sizeof(snap_R));
  snap_HI = HI;
  snap_LO = LO;
  snap_PC = addr;

  /* For stores, also snapshot the memory word that's about to be
     overwritten. By the time explain_after runs, the write has happened
     and peek_word would return the new value. */
  snap_has_mem = false;
  switch (OPCODE(instruction)) {
    case TOK_SW_OPCODE:
    case TOK_SH_OPCODE:
    case TOK_SB_OPCODE: {
      int base = BASE(instruction);
      short off = (short)IOFFSET(instruction);
      snap_mem_addr = (mem_addr)(gpr[base] + off);
      snap_mem_val = peek_word(snap_mem_addr);
      snap_has_mem = true;
      break;
    }
  }
}

/* ------------ Category preamble (L2+) ------------
 *
 * Before each per-opcode narration, emit a short classification
 * preamble: which of the five main MIPS instruction categories this
 * op belongs to (Arithmetic, Logical, Data transfer, Conditional
 * branch, Unconditional jump — plus a sixth "System / coprocessor"
 * catch-all), and which mnemonic-suffix modifiers apply (`u`, `i`,
 * `v`, `al`, `w`, `h`, `b`). Naming and grouping follow Patterson &
 * Hennessy *Computer Organization and Design*, 4th edition, Green
 * Sheet.
 *
 * First-time-per-session each category and modifier gets the full
 * description; subsequent encounters just name it. State persists
 * across instructions; not reset on reinitialize (the categories
 * don't change, so re-explaining them would be noise).
 */

typedef enum {
  CAT_ARITHMETIC,
  CAT_LOGICAL,
  CAT_DATA_TRANSFER,
  CAT_COND_BRANCH,
  CAT_UNCOND_JUMP,
  CAT_SYSTEM,
  CAT_COUNT
} inst_category;

typedef enum {
  MOD_U_UNSIGNED_ARITH, /* `u` on arith: no overflow trap */
  MOD_U_ZERO_EXTEND,    /* `u` on narrow load: zero-extend not sign */
  MOD_I_IMMEDIATE,      /* `i` suffix: 16-bit constant in instruction */
  MOD_V_VARIABLE_SHIFT, /* `v` suffix: shift count from a register */
  MOD_AL_AND_LINK,      /* `al` suffix: write $ra (subroutine call) */
  MOD_W_WORD,           /* word-width memory access (32 bits) */
  MOD_H_HALFWORD,       /* halfword-width memory access (16 bits) */
  MOD_B_BYTE,           /* byte-width memory access (8 bits) */
  MOD_COUNT
} inst_modifier;

static const char* category_names[CAT_COUNT] = {
    [CAT_ARITHMETIC] = "Arithmetic",
    [CAT_LOGICAL] = "Logical",
    [CAT_DATA_TRANSFER] = "Data transfer",
    [CAT_COND_BRANCH] = "Conditional branch",
    [CAT_UNCOND_JUMP] = "Unconditional jump",
    [CAT_SYSTEM] = "System / coprocessor",
};

static const char* category_descriptions[CAT_COUNT] = {
    [CAT_ARITHMETIC] =
        "Integer math on registers. Includes add/sub/mul/div and\n"
        "    their immediate variants. (COD §2.2.)",
    [CAT_LOGICAL] =
        "Bitwise operations (AND, OR, XOR, NOR) and shifts (left,\n"
        "    right-logical, right-arithmetic). (COD §2.6.)",
    [CAT_DATA_TRANSFER] =
        "Move data between registers and memory. MIPS is a load/store\n"
        "    architecture — math never operates on memory directly.\n"
        "    Includes `lui` for constructing 32-bit constants and\n"
        "    addresses. (COD §2.3 and §2.9.)",
    [CAT_COND_BRANCH] =
        "Test a condition; if true, transfer PC to a labeled target.\n"
        "    Also includes the set-less-than family (slt/slti) used to\n"
        "    build compound comparisons. (COD §2.7.)",
    [CAT_UNCOND_JUMP] =
        "Transfer PC unconditionally — no condition test. Includes\n"
        "    direct jumps, register-indirect jumps, and the and-link\n"
        "    variants used for subroutine calls. (COD §2.8.)",
    [CAT_SYSTEM] =
        "Operating-system services and miscellaneous control. The\n"
        "    `syscall` instruction in particular dispatches on $v0.",
};

static const char* modifier_letters[MOD_COUNT] = {
    [MOD_U_UNSIGNED_ARITH] = "`u`", [MOD_U_ZERO_EXTEND] = "`u`",
    [MOD_I_IMMEDIATE] = "`i`",      [MOD_V_VARIABLE_SHIFT] = "`v`",
    [MOD_AL_AND_LINK] = "`al`",     [MOD_W_WORD] = "`w`",
    [MOD_H_HALFWORD] = "`h`",       [MOD_B_BYTE] = "`b`",
};

static const char* modifier_descriptions[MOD_COUNT] = {
    [MOD_U_UNSIGNED_ARITH] =
        "unsigned: no trap on overflow. The plain (no-`u`) form\n"
        "          traps on overflow; the `u` form silently wraps.\n"
        "          Note: `u` means different things in different\n"
        "          contexts — see the load modifiers.",
    [MOD_U_ZERO_EXTEND] =
        "unsigned: zero-extend the loaded value to 32 bits. The\n"
        "          plain (no-`u`) load sign-extends instead. Affects\n"
        "          how `lbu`/`lhu` interpret the high bit of the\n"
        "          byte/halfword they read.",
    [MOD_I_IMMEDIATE] =
        "immediate form: one operand is a 16-bit constant baked\n"
        "          into the instruction word, not a register. Lets\n"
        "          small constants be supplied without a separate\n"
        "          load.",
    [MOD_V_VARIABLE_SHIFT] =
        "variable shift: the shift count comes from a register\n"
        "          (rs), not the 5-bit `shamt` field. Allows the\n"
        "          count to be computed at runtime.",
    [MOD_AL_AND_LINK] =
        "and link: save the return address (PC+4, or PC+8 with\n"
        "          delayed branches) into $ra. Turns the\n"
        "          branch/jump into a subroutine call.",
    [MOD_W_WORD] = "word width: 32 bits (full register width).",
    [MOD_H_HALFWORD] = "halfword width: 16 bits.",
    [MOD_B_BYTE] = "byte width: 8 bits.",
};

static const char* modifier_short[MOD_COUNT] = {
    [MOD_U_UNSIGNED_ARITH] = "unsigned (no overflow trap)",
    [MOD_U_ZERO_EXTEND] = "unsigned (zero-extend on load)",
    [MOD_I_IMMEDIATE] = "immediate (16-bit constant)",
    [MOD_V_VARIABLE_SHIFT] = "variable shift count (from register)",
    [MOD_AL_AND_LINK] = "and link (writes $ra)",
    [MOD_W_WORD] = "word (32-bit)",
    [MOD_H_HALFWORD] = "halfword (16-bit)",
    [MOD_B_BYTE] = "byte (8-bit)",
};

static bool category_seen[CAT_COUNT];
static bool modifier_seen[MOD_COUNT];

static void lookup_classification(int op, inst_category* cat,
                                  inst_modifier mods[3], int* n_mods) {
  *n_mods = 0;
  *cat = CAT_COUNT; /* default = unknown */
  switch (op) {
    /* Arithmetic */
    case TOK_ADD_OPCODE:
    case TOK_SUB_OPCODE:
    case TOK_MUL_OPCODE:
    case TOK_MULT_OPCODE:
    case TOK_DIV_OPCODE:
    case TOK_MFHI_OPCODE:
    case TOK_MFLO_OPCODE:
      *cat = CAT_ARITHMETIC;
      break;
    case TOK_ADDU_OPCODE:
    case TOK_SUBU_OPCODE:
    case TOK_MULTU_OPCODE:
    case TOK_DIVU_OPCODE:
      *cat = CAT_ARITHMETIC;
      mods[(*n_mods)++] = MOD_U_UNSIGNED_ARITH;
      break;
    case TOK_ADDI_OPCODE:
      *cat = CAT_ARITHMETIC;
      mods[(*n_mods)++] = MOD_I_IMMEDIATE;
      break;
    case TOK_ADDIU_OPCODE:
      *cat = CAT_ARITHMETIC;
      mods[(*n_mods)++] = MOD_I_IMMEDIATE;
      mods[(*n_mods)++] = MOD_U_UNSIGNED_ARITH;
      break;

    /* Logical */
    case TOK_AND_OPCODE:
    case TOK_OR_OPCODE:
    case TOK_XOR_OPCODE:
    case TOK_NOR_OPCODE:
    case TOK_SLL_OPCODE:
    case TOK_SRL_OPCODE:
    case TOK_SRA_OPCODE:
      *cat = CAT_LOGICAL;
      break;
    case TOK_ANDI_OPCODE:
    case TOK_ORI_OPCODE:
    case TOK_XORI_OPCODE:
      *cat = CAT_LOGICAL;
      mods[(*n_mods)++] = MOD_I_IMMEDIATE;
      break;
    case TOK_SLLV_OPCODE:
    case TOK_SRLV_OPCODE:
    case TOK_SRAV_OPCODE:
      *cat = CAT_LOGICAL;
      mods[(*n_mods)++] = MOD_V_VARIABLE_SHIFT;
      break;

    /* Data transfer */
    case TOK_LW_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_W_WORD;
      break;
    case TOK_LH_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_H_HALFWORD;
      break;
    case TOK_LHU_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_H_HALFWORD;
      mods[(*n_mods)++] = MOD_U_ZERO_EXTEND;
      break;
    case TOK_LB_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_B_BYTE;
      break;
    case TOK_LBU_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_B_BYTE;
      mods[(*n_mods)++] = MOD_U_ZERO_EXTEND;
      break;
    case TOK_SW_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_W_WORD;
      break;
    case TOK_SH_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_H_HALFWORD;
      break;
    case TOK_SB_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      mods[(*n_mods)++] = MOD_B_BYTE;
      break;
    case TOK_LUI_OPCODE:
      *cat = CAT_DATA_TRANSFER;
      break;

    /* Conditional branch */
    case TOK_BEQ_OPCODE:
    case TOK_BNE_OPCODE:
    case TOK_BGEZ_OPCODE:
    case TOK_BGTZ_OPCODE:
    case TOK_BLEZ_OPCODE:
    case TOK_BLTZ_OPCODE:
    case TOK_SLT_OPCODE:
      *cat = CAT_COND_BRANCH;
      break;
    case TOK_SLTU_OPCODE:
      *cat = CAT_COND_BRANCH;
      mods[(*n_mods)++] = MOD_U_UNSIGNED_ARITH;
      break;
    case TOK_SLTI_OPCODE:
      *cat = CAT_COND_BRANCH;
      mods[(*n_mods)++] = MOD_I_IMMEDIATE;
      break;
    case TOK_SLTIU_OPCODE:
      *cat = CAT_COND_BRANCH;
      mods[(*n_mods)++] = MOD_I_IMMEDIATE;
      mods[(*n_mods)++] = MOD_U_UNSIGNED_ARITH;
      break;
    case TOK_BGEZAL_OPCODE:
    case TOK_BLTZAL_OPCODE:
      *cat = CAT_COND_BRANCH;
      mods[(*n_mods)++] = MOD_AL_AND_LINK;
      break;

    /* Unconditional jump */
    case TOK_J_OPCODE:
    case TOK_JR_OPCODE:
      *cat = CAT_UNCOND_JUMP;
      break;
    case TOK_JAL_OPCODE:
    case TOK_JALR_OPCODE:
      *cat = CAT_UNCOND_JUMP;
      mods[(*n_mods)++] = MOD_AL_AND_LINK;
      break;

    /* System */
    case TOK_SYSCALL_OPCODE:
      *cat = CAT_SYSTEM;
      break;
  }
}

static void emit_category_preamble(mips_instruction* instruction) {
  inst_category cat;
  inst_modifier mods[3];
  int n_mods;
  lookup_classification(OPCODE(instruction), &cat, mods, &n_mods);

  if (cat == CAT_COUNT) return; /* unknown opcode; skip preamble */

  write_output(message_out, "  Category: %s\n", category_names[cat]);
  if (!category_seen[cat]) {
    write_output(message_out, "    %s\n", category_descriptions[cat]);
    category_seen[cat] = true;
  }

  if (n_mods > 0) {
    const char* mnemonic = inst_op_name(instruction);
    write_output(message_out, "  Modifiers in `%s`:\n", mnemonic);
    for (int i = 0; i < n_mods; i++) {
      inst_modifier m = mods[i];
      if (!modifier_seen[m]) {
        write_output(message_out, "    %s — %s\n", modifier_letters[m],
                     modifier_descriptions[m]);
        modifier_seen[m] = true;
      } else {
        write_output(message_out, "    %s — %s\n", modifier_letters[m],
                     modifier_short[m]);
      }
    }
  }
}

/* render_dispatch: walks the opcode switch and emits each template's
   per-opcode "What it did / Inputs / Wrote / Try it yourself" content.
   Called from explain_after only. Reads inputs from snap_R[], outputs
   from live gpr[]. */
static void render_dispatch(int level, mips_instruction* instruction) {
  switch (OPCODE(instruction)) {
    /* Arithmetic, R-type */
    case TOK_ADD_OPCODE:
      tpl_r3_arith(level, instruction, "Add",
                   "computed $rs + $rt as a signed sum; trapped on overflow");
      break;
    case TOK_ADDU_OPCODE:
      tpl_r3_arith(level, instruction, "Add Unsigned",
                   "computed $rs + $rt with no overflow trap");
      break;
    case TOK_SUB_OPCODE:
      tpl_r3_arith(
          level, instruction, "Subtract",
          "computed $rs - $rt as a signed difference; trapped on overflow");
      break;
    case TOK_SUBU_OPCODE:
      tpl_r3_arith(level, instruction, "Subtract Unsigned",
                   "computed $rs - $rt with no overflow trap");
      break;
    case TOK_AND_OPCODE:
      tpl_r3_arith(level, instruction, "Bitwise AND",
                   "computed the bitwise AND of $rs and $rt");
      break;
    case TOK_OR_OPCODE:
      tpl_r3_arith(level, instruction, "Bitwise OR",
                   "computed the bitwise OR of $rs and $rt");
      break;
    case TOK_XOR_OPCODE:
      tpl_r3_arith(level, instruction, "Bitwise XOR",
                   "computed the bitwise XOR of $rs and $rt");
      break;
    case TOK_NOR_OPCODE:
      tpl_r3_arith(level, instruction, "Bitwise NOR",
                   "computed NOT($rs OR $rt)");
      break;
    case TOK_SLT_OPCODE:
      tpl_r3_arith(level, instruction, "Set on Less Than (signed)",
                   "set the destination to 1 if $rs < $rt (signed), else 0");
      break;
    case TOK_SLTU_OPCODE:
      tpl_r3_arith(level, instruction, "Set on Less Than (unsigned)",
                   "set the destination to 1 if $rs < $rt (unsigned), else 0");
      break;
    case TOK_MUL_OPCODE:
      tpl_r3_arith(level, instruction, "Multiply (MIPS32)",
                   "computed the low 32 bits of $rs * $rt");
      break;

    /* Arithmetic, I-type */
    case TOK_ADDI_OPCODE:
      tpl_i2_arith(level, instruction, "Add Immediate",
                   "computed $rs + immediate; trapped on overflow");
      break;
    case TOK_ADDIU_OPCODE:
      tpl_i2_arith(level, instruction, "Add Immediate Unsigned",
                   "computed $rs + sign-extended immediate, no overflow trap");
      break;
    case TOK_ANDI_OPCODE:
      tpl_i2_arith(level, instruction, "AND Immediate",
                   "computed $rs AND zero-extended immediate");
      break;
    case TOK_ORI_OPCODE:
      tpl_i2_arith(level, instruction, "OR Immediate",
                   "computed $rs OR zero-extended immediate");
      break;
    case TOK_XORI_OPCODE:
      tpl_i2_arith(level, instruction, "XOR Immediate",
                   "computed $rs XOR zero-extended immediate");
      break;
    case TOK_SLTI_OPCODE:
      tpl_i2_arith(level, instruction, "Set Less Than Immediate (signed)",
                   "set destination to 1 if $rs < imm (signed)");
      break;
    case TOK_SLTIU_OPCODE:
      tpl_i2_arith(level, instruction, "Set Less Than Immediate (unsigned)",
                   "set destination to 1 if $rs < imm (unsigned)");
      break;

    /* LUI is special: I1t-type, only writes high bits. */
    case TOK_LUI_OPCODE: {
      int rt = RT(instruction);
      short imm = (short)IMM(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Load Upper Immediate — placed the 16-bit immediate "
                   "into the\n"
                   "    upper 16 bits of $%s, zeroing the lower 16. Often "
                   "paired with\n"
                   "    `ori` to construct a full 32-bit constant.\n",
                   int_reg_names[rt]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_imm(imm);
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(rt);
        say_try_regs(rt, 0, 0, 0);
      }
      break;
    }

    /* Shifts */
    case TOK_SLL_OPCODE:
      if (RD(instruction) == 0 && RT(instruction) == 0 &&
          SHAMT(instruction) == 0) {
        /* nop = sll $0, $0, 0 */
        write_output(message_out, "  What it did:\n");
        write_output(message_out,
                     "    No Operation. The processor took one cycle and "
                     "did nothing.\n"
                     "    (Encoded as `sll $0, $0, 0` — a shift of $zero "
                     "with no destination.)\n");
        if (level >= 2) {
          write_output(message_out,
                       "  Inputs (before this step):\n    (none)\n");
          write_output(message_out, "  Wrote:\n    (no register changes)\n");
          say_try_regs(0, 0, 0, 0);
        }
      } else {
        tpl_shift(level, instruction, "Shift Left Logical",
                  "shifted $rt left, filling with 0");
      }
      break;
    case TOK_SRL_OPCODE:
      tpl_shift(level, instruction, "Shift Right Logical",
                "shifted $rt right, filling with 0");
      break;
    case TOK_SRA_OPCODE:
      tpl_shift(level, instruction, "Shift Right Arithmetic",
                "shifted $rt right, filling with the sign bit");
      break;

    /* Loads */
    case TOK_LW_OPCODE:
      tpl_load(level, instruction, "Load Word", "a 32-bit word", 4);
      break;
    case TOK_LB_OPCODE:
      tpl_load(level, instruction, "Load Byte (signed)",
               "one byte, sign-extended", 1);
      break;
    case TOK_LBU_OPCODE:
      tpl_load(level, instruction, "Load Byte Unsigned",
               "one byte, zero-extended", 1);
      break;
    case TOK_LH_OPCODE:
      tpl_load(level, instruction, "Load Halfword (signed)",
               "16 bits, sign-extended", 2);
      break;
    case TOK_LHU_OPCODE:
      tpl_load(level, instruction, "Load Halfword Unsigned",
               "16 bits, zero-extended", 2);
      break;

    /* Stores */
    case TOK_SW_OPCODE:
      tpl_store(level, instruction, "Store Word", "the 32-bit value", 4);
      break;
    case TOK_SB_OPCODE:
      tpl_store(level, instruction, "Store Byte", "the low 8 bits", 1);
      break;
    case TOK_SH_OPCODE:
      tpl_store(level, instruction, "Store Halfword", "the low 16 bits", 2);
      break;

    /* Branches. The comparison value is read from snap_R[] since by the
       time the template runs, an instruction like `beq $t0, $t0, ...`
       has already executed (though it would not have modified rs/rt
       anyway). */
    case TOK_BEQ_OPCODE:
      tpl_branch_2reg(level, instruction, "Branch if Equal",
                      "==", snap_R[RS(instruction)] == snap_R[RT(instruction)]);
      break;
    case TOK_BNE_OPCODE:
      tpl_branch_2reg(level, instruction, "Branch if Not Equal",
                      "!=", snap_R[RS(instruction)] != snap_R[RT(instruction)]);
      break;
    case TOK_BGEZ_OPCODE:
      tpl_branch_1reg(level, instruction, "Branch if Greater or Equal Zero",
                      ">=", (reg_word)snap_R[RS(instruction)] >= 0);
      break;
    case TOK_BGTZ_OPCODE:
      tpl_branch_1reg(level, instruction, "Branch if Greater Than Zero", ">",
                      (reg_word)snap_R[RS(instruction)] > 0);
      break;
    case TOK_BLEZ_OPCODE:
      tpl_branch_1reg(level, instruction, "Branch if Less or Equal Zero",
                      "<=", (reg_word)snap_R[RS(instruction)] <= 0);
      break;
    case TOK_BLTZ_OPCODE:
      tpl_branch_1reg(level, instruction, "Branch if Less Than Zero", "<",
                      (reg_word)snap_R[RS(instruction)] < 0);
      break;

    /* Jumps. PC change is captured by the side-effect fallback at the
       end of explain_after, so per-jump templates don't repeat it. */
    case TOK_J_OPCODE: {
      mem_addr target = TARGET(instruction) << 2;
      write_output(message_out, "  What it did:\n");
      write_output(message_out, "    Jumped unconditionally to 0x%08x.\n",
                   target);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n    (none)\n");
        say_try_regs(0, 0, 0, 0);
      }
      break;
    }
    case TOK_JAL_OPCODE: {
      mem_addr target = TARGET(instruction) << 2;
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump and Link — saved the return address into "
                   "$ra,\n    then jumped to 0x%08x.\n",
                   target);
      write_output(message_out,
                   "    (Standard subroutine call: $ra holds "
                   "the return address.)\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n    (none)\n");
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(31);
        say_try_regs(31, 0, 0, 0);
      }
      break;
    }
    case TOK_JR_OPCODE: {
      int rs = RS(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump Register — jumped to the address held in $%s.\n"
                   "    (Used to return from a subroutine when $rs == $ra.)\n"
                   "    Destination = 0x%08x.\n",
                   int_reg_names[rs], (mem_addr)snap_R[rs]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        say_try_regs(rs, 0, 0, 0);
      }
      break;
    }
    case TOK_JALR_OPCODE: {
      int rd = RD(instruction), rs = RS(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump and Link Register — saved the return address "
                   "into $%s,\n    then jumped to the address in $%s "
                   "(0x%08x).\n",
                   int_reg_names[rd], int_reg_names[rs], (mem_addr)snap_R[rs]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(rd);
        say_try_regs(rs, rd, 0, 0);
      }
      break;
    }

    /* HI / LO */
    case TOK_MFHI_OPCODE: {
      int rd = RD(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Move From HI — copied the special HI register into "
                   "$%s.\n"
                   "    HI holds the high half of a multiplication, or the "
                   "remainder of a division.\n",
                   int_reg_names[rd]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    HI = 0x%08x\n", snap_HI);
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(rd);
        say_try_regs(rd, 0, 0, 0);
      }
      break;
    }
    case TOK_MFLO_OPCODE: {
      int rd = RD(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Move From LO — copied the special LO register into "
                   "$%s.\n"
                   "    LO holds the low half of a multiplication, or the "
                   "quotient of a division.\n",
                   int_reg_names[rd]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    LO = 0x%08x\n", snap_LO);
        write_output(message_out, "  Wrote:\n");
        say_wrote_reg(rd);
        say_try_regs(rd, 0, 0, 0);
      }
      break;
    }
    case TOK_MULT_OPCODE: {
      int rs = RS(instruction), rt = RT(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Multiply (signed) — computed $%s * $%s as a 64-bit "
                   "result.\n"
                   "    The high 32 bits went to HI; the low 32 bits went to "
                   "LO.\n"
                   "    Use mfhi / mflo to retrieve them.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        say_input_reg(rt);
        write_output(message_out, "  Wrote:\n");
        say_wrote_hi();
        say_wrote_lo();
        say_try_regs(rs, rt, 0, 0);
      }
      break;
    }
    case TOK_DIV_OPCODE: {
      int rs = RS(instruction), rt = RT(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Divide (signed) — computed $%s / $%s.\n"
                   "    Quotient went to LO; remainder went to HI.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        say_input_reg(rt);
        write_output(message_out, "  Wrote:\n");
        say_wrote_hi();
        say_wrote_lo();
        say_try_regs(rs, rt, 0, 0);
      }
      break;
    }
    case TOK_MULTU_OPCODE: {
      int rs = RS(instruction), rt = RT(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Multiply Unsigned — computed $%s * $%s as a 64-bit "
                   "unsigned result.\n"
                   "    The high 32 bits went to HI; the low 32 bits went to "
                   "LO.\n"
                   "    Use mfhi / mflo to retrieve them. No overflow trap.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        say_input_reg(rt);
        write_output(message_out, "  Wrote:\n");
        say_wrote_hi();
        say_wrote_lo();
        say_try_regs(rs, rt, 0, 0);
      }
      break;
    }
    case TOK_DIVU_OPCODE: {
      int rs = RS(instruction), rt = RT(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Divide Unsigned — computed $%s / $%s, treating both "
                   "operands as unsigned.\n"
                   "    Quotient went to LO; remainder went to HI.\n",
                   int_reg_names[rs], int_reg_names[rt]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_reg(rs);
        say_input_reg(rt);
        write_output(message_out, "  Wrote:\n");
        say_wrote_hi();
        say_wrote_lo();
        say_try_regs(rs, rt, 0, 0);
      }
      break;
    }

    /* Syscall */
    case TOK_SYSCALL_OPCODE:
      explain_syscall(level);
      break;

    default:
      write_output(message_out,
                   "  (no detailed explanation for this opcode yet — "
                   "see the disassembly above)\n");
      break;
  }

  write_output(message_out, "\n");
}

/* explain_after runs immediately after the dispatch switch (and PC
   advance) in run_spim. This is where the narration actually emits —
   by the time the user sees it, everything described has already
   happened, so we can use past tense throughout. */
void explain_after(mips_instruction* instruction) {
  if (explain_level == 0) return;
  int level = explain_level;

  /* Reset the "touched by template" tracking so the side-effect
     fallback below knows what's already been named. */
  memset(touched_reg, 0, sizeof touched_reg);
  touched_hi = false;
  touched_lo = false;

  explain_print_step_header(snap_PC, instruction);

  /* L3 shows a single bit-field diagram; L4 expands that into the
     CPU's hierarchical decoding sequence (multiple frames, same shape,
     fields filling in progressively). The L4 final frame equals the
     L3 box, so we don't show both. */
  if (level == 3)
    explain_bit_layout(instruction);
  else if (level >= 4)
    explain_decoding_steps(instruction);

  /* Pseudo-op header / continuation hint.
   *
   * Two paths:
   *   (a) Current instruction has a SOURCE line — try to match it
   *       against a pseudo-op mnemonic and emit a header.
   *   (b) No SOURCE line, but the previous step matched a may-be-multi
   *       pseudo-op — emit a continuation hint. */
  if (accept_pseudo_insts && SOURCE(instruction) != nullptr) {
    const struct pseudo_info* p = find_pseudo_in_source(SOURCE(instruction));
    if (p != nullptr) {
      write_output(message_out,
                   "  Pseudo-instruction `%s` (as written in source):\n"
                   "    %s\n"
                   "  This was the %s real instruction the assembler "
                   "emitted for it.\n",
                   p->name, p->what_it_means,
                   p->may_be_multi ? "first" : "single");
      pending_pseudo_name = p->name;
      pending_pseudo_multi = p->may_be_multi;
    } else {
      pending_pseudo_name = nullptr;
      pending_pseudo_multi = false;
    }
  } else if (pending_pseudo_name != nullptr && pending_pseudo_multi) {
    write_output(message_out,
                 "  (continuation of the `%s` pseudo-op expansion above —\n"
                 "   same source line, this was the next real instruction "
                 "emitted)\n",
                 pending_pseudo_name);
  }
  write_output(message_out, "\n");

  /* Everything below — per-opcode "What it did", Inputs/Wrote blocks,
     Try-it-yourself hints, PC delta, side-effect fallback — is L2+
     content. At L1 the labeled header above is the whole story (same
     four data items as the default compact one-liner, just relayed
     out). */
  if (level >= 2) {
    /* Category + modifiers preamble (first-time-per-session gets the
       full description, subsequent times get the short form). */
    emit_category_preamble(instruction);

    /* Per-opcode narration. */
    render_dispatch(level, instruction);

    /* Load destination annotation: if a load completed but its
       destination register value didn't change (loaded the same value
       already there), the say_wrote_reg line shows "0xV → 0xV" which
       is technically true but easy to misread as "nothing happened."
       Annotate explicitly. */
    switch (OPCODE(instruction)) {
      case TOK_LW_OPCODE:
      case TOK_LB_OPCODE:
      case TOK_LBU_OPCODE:
      case TOK_LH_OPCODE:
      case TOK_LHU_OPCODE: {
        int rt = RT(instruction);
        if (rt != 0 && snap_R[rt] == gpr[rt]) {
          write_output(message_out,
                       "    (the load did happen — the memory value matched "
                       "the prior register value)\n");
        }
        break;
      }
    }

    /* Side-effect fallback: anything that changed but wasn't named by
       the template. Captures unexpected mutations (e.g. exception
       paths, templates that don't yet cover all of an op's writes). */
    bool header_done = false;
    for (int i = 1; i < R_LENGTH; i++) {
      if (gpr[i] != snap_R[i] && !touched_reg[i]) {
        if (!header_done) {
          write_output(message_out,
                       "  Side effects (changed but not named above):\n");
          header_done = true;
        }
        write_output(message_out,
                     "    $%s:  0x%08x  →  0x%08x   (decimal %d)\n",
                     int_reg_names[i], snap_R[i], gpr[i], gpr[i]);
      }
    }
    if (HI != snap_HI && !touched_hi) {
      if (!header_done) {
        write_output(message_out,
                     "  Side effects (changed but not named above):\n");
        header_done = true;
      }
      write_output(message_out, "    HI:  0x%08x  →  0x%08x\n", snap_HI, HI);
    }
    if (LO != snap_LO && !touched_lo) {
      if (!header_done) {
        write_output(message_out,
                     "  Side effects (changed but not named above):\n");
        header_done = true;
      }
      write_output(message_out, "    LO:  0x%08x  →  0x%08x\n", snap_LO, LO);
    }

    /* PC always advances; print it last. */
    write_output(message_out, "  PC:  0x%08x  →  0x%08x\n", snap_PC, PC);
  }

  write_output(message_out, "\n");
}
