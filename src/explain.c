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
   snapshot_registers / snapshot_multiply_divide_high / snapshot_multiply_divide_low / snapshot_program_counter and result-side reads come from
   the live gpr / HI / LO / PC. Stores also need a pre-execute memory
   snapshot, since by the time explain_after runs the write has happened
   and peek_word would return the new value. */
static register_word snapshot_registers[GENERAL_PURPOSE_REGISTER_COUNT];
static register_word snapshot_multiply_divide_high, snapshot_multiply_divide_low;
static memory_address snapshot_program_counter;
static bool snapshot_has_memory;
static memory_address snapshot_memory_address;
static register_word snapshot_memory_value;

/* Per-instruction "did the template name this write?" tracking. The
   fallback block at the end of explain_after prints any register that
   changed but isn't here as a "Side effect:" line — protects against
   template/op mismatch and surfaces unexpected mutations. Reset at
   each explain_after entry. */
static bool touched_register[GENERAL_PURPOSE_REGISTER_COUNT];
static bool touched_multiply_divide_high, touched_multiply_divide_low;

/* Once-per-session flag: explain_after emits a short labeled legend
   the first time it runs, explaining what each piece of the upcoming
   "Stepped" header line means. Spares the novice from having to infer
   the header's layout, and avoids cluttering every subsequent step. */
static bool legend_emitted = false;

/* Once-per-session flag: before the first narrated instruction runs,
   print a full disassembly listing of the loaded program, so the
   student has a static map of what they're about to step through. */
static bool listing_shown = false;

/* Forward declarations needed by explain_print_step_header, which is
   defined below but called from explain_after (and from run.c, via the
   public extern in explain.h). */
static void horizontal_rule(void);

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
      "Placeholder notation in pseudo-op descriptions and structural\n"
      "explanations:\n"
      "\n"
      "  $rs, $rt, $rd, shamt\n"
      "        Field names from the MIPS instruction encoding —\n"
      "        $rs = first source register, $rt = second source or\n"
      "        target register, $rd = destination register, shamt =\n"
      "        shift amount.  When you see these in a pseudo-op's\n"
      "        general-form description (e.g. \"set $rd to ...\"),\n"
      "        they refer to whatever registers you wrote in YOUR\n"
      "        source line.  The disassembled \"What it did\" lines\n"
      "        substitute the concrete register names in place of\n"
      "        these placeholders.\n"
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
void explain_print_step_header(memory_address program_counter,
                               const mips_instruction* instruction) {
  if (!legend_emitted) {
    emit_legend();
    legend_emitted = true;
  }

  write_output(message_out, "\n");
  horizontal_rule();
  write_output(message_out, "Stepped at PC = 0x%08x:\n", program_counter);

  /* Disassembled form. instruction_to_string returns one of:
       "[0xADDR]\t0xENC  DISASSEMBLY              ; LINE: SOURCE"
       "[0xADDR]\t0xENC  DISASSEMBLY"
     Split at the `;` so we can emit the source line under its own
     label, which makes pseudo-op divergence (source != real) visually
     obvious. */
  char* disassembly = instruction_to_string(program_counter);
  size_t disassembly_length = strlen(disassembly);
  while (disassembly_length > 0 &&
         (disassembly[disassembly_length - 1] == '\n' || disassembly[disassembly_length - 1] == ' ' || disassembly[disassembly_length - 1] == '\t'))
    disassembly[--disassembly_length] = '\0';

  char* p = strchr(disassembly, '\t');
  if (p)
    p++;
  else
    p = disassembly;
  char* const semi = strchr(p, ';');
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
  char* disassembly_text = p;
  if (strlen(p) >= 10 && p[0] == '0' && p[1] == 'x') {
    disassembly_text = p + 10;
    while (*disassembly_text == ' ' || *disassembly_text == '\t') disassembly_text++;
  }

  const uint32_t encoding = (uint32_t)instruction_encode(instruction);
  write_output(message_out, "    memory[0x%08x] = 0x%08x   →   %s\n", program_counter, encoding,
               disassembly_text);
  if (source_part && *source_part) {
    write_output(message_out, "    source line %s\n", source_part);
  }
  free(disassembly);
}

/* ------------ Tab-completion suggestions ------------
 *
 * Each call to explain_before clears this list. Each "Try it yourself"
 * line emitted by a template adds its bare command (no trailing comment)
 * to the list. spim.c queries explain_suggestion_count / explain_suggestion
 * from its libedit completion callback so a student can recall a hint
 * with Tab.
 *
 * Lifetime: strings are strdup'd at add time and freed by
 * explain_clear_suggestions. Storage is a fixed-cap array — 16 is a
 * comfortable ceiling (templates emit at most 3-4 hints).
 */
constexpr int MAX_SUGGESTIONS = 16;
static char* suggestions[MAX_SUGGESTIONS];
static size_t suggestion_count;

void explain_clear_suggestions(void) {
  for (size_t i = 0; i < suggestion_count; i++) {
    free(suggestions[i]);
    suggestions[i] = nullptr;
  }
  suggestion_count = 0;
}

static void add_suggestion(const char* command) {
  if (suggestion_count >= MAX_SUGGESTIONS) return;
  char* const copy = strdup(command);
  if (copy != nullptr) suggestions[suggestion_count++] = copy;
}

size_t explain_suggestion_count(void) { return suggestion_count; }

const char* explain_suggestion(size_t i) {
  if (i >= suggestion_count) return nullptr;
  return suggestions[i];
}

/* ------------ pseudo-op narration ------------
 *
 * The assembler expands pseudo-instructions (la, li, move, b, bge, ...) into
 * one-or-more real instructions before they reach the run loop, and expands
 * a real instruction whose operand does not fit (lw from a label, an
 * immediate wider than 16 bits) the same way.  It records on each emitted
 * instruction the mnemonic written and the instruction's place in the
 * expansion (mips_instruction's expansion_* fields), and the narration
 * names them: a header on the first instruction, a continuation hint on
 * the rest.  This table describes each pseudo-op; it is alphabetical for
 * easy maintenance.
 */
struct pseudo_info {
  const char* name;
  const char* what_it_means;
};

static const struct pseudo_info pseudo_ops[] = {
    {.name = "abs",
     .what_it_means =
         "Absolute Value — set $rd to |$rs|. "
         "Multi-instruction expansion using\n"
         "    arithmetic-shift / xor / sub to avoid a conditional branch."},
    {.name = "b",
     .what_it_means =
         "Branch — unconditional jump to a label. Encoded as `bgez $0, label`\n"
         "    ($0 is never negative, so the branch is always taken)."},
    {.name = "bal",
     .what_it_means =
         "Branch-and-Link — unconditional call to a label, saving the return\n"
         "    address in $ra. Encoded as `bgezal $0, label`."},
    {.name = "beqz",
     .what_it_means =
         "Branch if Equal to Zero — branch if $rs == 0. Encoded as\n"
         "    `beq $rs, $0, label`."},
    {.name = "bnez",
     .what_it_means =
         "Branch if Not Equal to Zero — branch if $rs != 0. Encoded as\n"
         "    `bne $rs, $0, label`."},
    {.name = "bge",
     .what_it_means =
         "Branch if Greater or Equal (signed) — branch if $rs >= $rt. Expands\n"
         "    to `slt $at, $rs, $rt` + `beq "
         "$at, $0, label` (with an immediate,\n"
         "    `slti $at, $rs, imm` + `beq`)."},
    {.name = "bgeu",
     .what_it_means =
         "Branch if Greater or Equal (unsigned) "
         "— branch if $rs >= $rt unsigned.\n"
         "    Expands to `sltu $at, $rs, $rt` + `beq $at, $0, label`."},
    {.name = "bgt",
     .what_it_means =
         "Branch if Greater Than (signed) — branch if $rs > $rt. Expands to\n"
         "    `slt $at, $rt, $rs` + `bne $at, $0, label` (with an immediate,\n"
         "    `slti $at, $rs, imm+1` + `beq $at, $0, label`)."},
    {.name = "bgtu",
     .what_it_means =
         "Branch if Greater Than (unsigned) — branch if $rs > $rt unsigned.\n"
         "    Expands to `sltu $at, $rt, $rs` + `bne $at, $0, label`."},
    {.name = "ble",
     .what_it_means =
         "Branch if Less or Equal (signed) — branch if $rs <= $rt. Expands to\n"
         "    `slt $at, $rt, $rs` + `beq $at, $0, label` (with an immediate,\n"
         "    `slti $at, $rs, imm+1` + `bne $at, $0, label`)."},
    {.name = "bleu",
     .what_it_means =
         "Branch if Less or Equal (unsigned) — branch if $rs <= $rt unsigned.\n"
         "    Expands to `sltu $at, $rt, $rs` + `beq $at, $0, label`."},
    {.name = "blt",
     .what_it_means =
         "Branch if Less Than (signed) — branch if $rs < $rt. Expands to\n"
         "    `slt $at, $rs, $rt` + `bne $at, $0, label` (with an immediate,\n"
         "    `slti $at, $rs, imm` + `bne`)."},
    {.name = "bltu",
     .what_it_means =
         "Branch if Less Than (unsigned) — branch if $rs < $rt unsigned.\n"
         "    Expands to `sltu $at, $rs, $rt` + `bne $at, $0, label`."},
    {.name = "la",
     .what_it_means =
         "Load Address — set $rd to the 32-bit address of a label. Expands to\n"
         "    `lui $rd, hi(addr)` + `ori $rd, "
         "$rd, lo(addr)`, or just the `lui`\n"
         "    when the low 16 bits are 0; `la "
         "$rd, N($rs)` is `addi $rd, $rs, N`."},
    {.name = "ld",
     .what_it_means =
         "Load Doubleword — load 64 bits into a register pair. Expands to two\n"
         "    `lw` instructions covering offsets 0 and 4."},
    {.name = "li",
     .what_it_means =
         "Load Immediate — set $rd to a 32-bit constant. One that fits in 16\n"
         "    unsigned bits is one `ori $rd, "
         "$0, imm`; any other expands to `lui`\n"
         "    + `ori`, or just the `lui` when the low 16 bits are 0."},
    {.name = "move",
     .what_it_means = "Move — copy $rs into $rd. Encoded as "
                      "`addu $rd, $0, $rs` (adding $zero\n"
                      "    is the simplest way to copy a register)."},
    {.name = "mulo",
     .what_it_means =
         "Multiply with Overflow check — signed "
         "multiply that traps on overflow.\n"
         "    Expands to `mult` plus an overflow check plus `mflo`."},
    {.name = "mulou",
     .what_it_means =
         "Multiply Unsigned with Overflow check — unsigned variant of `mulo`."},
    {.name = "neg",
     .what_it_means =
         "Negate (signed; traps on overflow) — set $rd = -$rs. Encoded as\n"
         "    `sub $rd, $0, $rs`."},
    {.name = "negu",
     .what_it_means =
         "Negate Unsigned — set $rd = -$rs with no overflow trap. Encoded as\n"
         "    `subu $rd, $0, $rs`."},
    {.name = "nop",
     .what_it_means =
         "No Operation — do nothing for one cycle. Encoded as `sll $0, $0, 0`\n"
         "    (a shift writing to $zero, which is hardwired to 0 and discards\n"
         "    the result)."},
    {.name = "not",
     .what_it_means =
         "Bitwise NOT — set $rd to the one's complement of $rs. Encoded as\n"
         "    `nor $rd, $rs, $0` (NOR with $zero is logical NOT)."},
    {.name = "rem",
     .what_it_means =
         "Signed Remainder — set $rd = $rs % $rt. Expands to a zero-divisor\n"
         "    check (`bne` + `break`), then `div $rs, $rt` + `mfhi $rd`."},
    {.name = "remu",
     .what_it_means =
         "Unsigned Remainder — set $rd = $rs % $rt unsigned. Expands to a\n"
         "    zero-divisor check, then `divu $rs, $rt` + `mfhi $rd`."},
    {.name = "rol",
     .what_it_means =
         "Rotate Left — multi-instruction expansion using sll, srl, and or to\n"
         "    rotate the bits of $rs left."},
    {.name = "ror",
     .what_it_means =
         "Rotate Right — multi-instruction expansion using srl, sll, and or."},
    {.name = "sd",
     .what_it_means = "Store Doubleword — store 64 bits from "
                      "a register pair. Expands to two\n"
                      "    `sw` instructions at offsets 0 and 4."},
    {.name = "seq",
     .what_it_means =
         "Set if Equal — set $rd = ($rs == $rt) ? 1 : 0. Expands to `xor` +\n"
         "    `sltiu`."},
    {.name = "sne",
     .what_it_means = "Set if Not Equal — set $rd = ($rs != "
                      "$rt) ? 1 : 0. Expands to `xor` +\n"
                      "    `sltu` against $zero."},
    {.name = "sge",
     .what_it_means =
         "Set if Greater or Equal (signed) — $rd = ($rs >= $rt) ? 1 : 0."},
    {.name = "sgeu", .what_it_means = "Set if Greater or Equal (unsigned)."},
    {.name = "sgt",
     .what_it_means =
         "Set if Greater Than (signed) — $rd = ($rs > $rt) ? 1 : 0."},
    {.name = "sgtu", .what_it_means = "Set if Greater Than (unsigned)."},
    {.name = "sle",
     .what_it_means =
         "Set if Less or Equal (signed) — $rd = ($rs <= $rt) ? 1 : 0."},
    {.name = "sleu", .what_it_means = "Set if Less or Equal (unsigned)."},
    {.name = "ulh",
     .what_it_means = "Unaligned Load Halfword (signed) — "
                      "read 16 bits at a possibly-unaligned\n"
                      "    address. Expands to byte loads + shifts."},
    {.name = "ulhu",
     .what_it_means =
         "Unaligned Load Halfword Unsigned — zero-extending variant of `ulh`."},
    {.name = "ulw",
     .what_it_means =
         "Unaligned Load Word — read 32 bits at a possibly-unaligned address.\n"
         "    Expands to `lwl` + `lwr` (or a sequence of byte loads)."},
    {.name = "ush",
     .what_it_means =
         "Unaligned Store Halfword — write 16 bits at a possibly-unaligned "
         "address."},
    {.name = "usw",
     .what_it_means = "Unaligned Store Word — write 32 bits "
                      "at a possibly-unaligned address.\n"
                      "    Expands to `swl` + `swr`."},
    {.name = nullptr, .what_it_means = nullptr},
};

/* The table entry for the pseudo-op named NAME, or nullptr. */
static const struct pseudo_info* find_pseudo(const char* name) {
  for (int i = 0; pseudo_ops[i].name != nullptr; i++)
    if (strcmp(name, pseudo_ops[i].name) == 0) return &pseudo_ops[i];
  return nullptr;
}

/* The narrator only shows memory: memory_peek raises no exception and grows
   no segment, so it cannot change what the program does.  A location
   outside every segment reads as 0. */
static register_word peek_word(memory_address address) {
  register_word v;
  (void)memory_peek(address, 0x3, &v);
  return v;
}

static register_word peek_half(memory_address address) {
  register_word v;
  (void)memory_peek(address, 0x1, &v);
  return v;
}

static register_word peek_byte(memory_address address) {
  register_word v;
  (void)memory_peek(address, 0x0, &v);
  return v;
}

/* ------------ small printing helpers ------------ */

static void horizontal_rule(void) {
  write_output(message_out,
               "──────────────────────────────────────────────────────────\n");
}

/* Input-side reads: always from snapshot_registers, since templates run after the
   dispatch and gpr[] now holds post-execute values. */
static void say_input_register(int r) {
  if (r == 0) return; /* $zero is always 0; saying so is noise */
  write_output(message_out, "    $%s = 0x%08x  (decimal %d)\n",
               integer_register_names[r], snapshot_registers[r], snapshot_registers[r]);
}

static void say_input_immediate(int immediate_signed) {
  write_output(message_out, "    immediate = %d  (0x%04x)\n", immediate_signed,
               immediate_signed & 0xffff);
}

/* True for the I-type opcodes (primary opcode field PRIMARY) whose 16-bit
   immediate is zero-extended (andi, ori, xori), or, for lui, used as is
   in the upper half: none of them sign-extends it. */
static bool immediate_not_sign_extended(int primary) {
  return primary == 0x0c || primary == 0x0d || primary == 0x0e ||
         primary == 0x0f;
}

/* Result-side: print "$X: before -> after (decimal N)" using the
   snapshot for the before value and the live register for the after.
   Marks the register as touched so the side-effect fallback doesn't
   duplicate it. */
static void say_wrote_register(int r) {
  if (r == 0) return; /* writes to $zero are discarded; nothing to show */
  write_output(message_out, "    $%s:  0x%08x  →  0x%08x   (decimal %d)\n",
               integer_register_names[r], snapshot_registers[r], general_purpose_registers[r], general_purpose_registers[r]);
  touched_register[r] = true;
}

static void say_wrote_multiply_divide_high(void) {
  write_output(message_out, "    HI:   0x%08x  →  0x%08x\n", snapshot_multiply_divide_high, MULTIPLY_DIVIDE_HIGH);
  touched_multiply_divide_high = true;
}
static void say_wrote_multiply_divide_low(void) {
  write_output(message_out, "    LO:   0x%08x  →  0x%08x\n", snapshot_multiply_divide_low, MULTIPLY_DIVIDE_LOW);
  touched_multiply_divide_low = true;
}

/* For stores: print "0x<addr>: prior -> current". width selects the
   masking applied to both ends so byte/halfword stores display
   sensibly. Only valid when snapshot_has_memory is true. */
static void say_wrote_memory(int width) {
  if (!snapshot_has_memory) return;
  const uint32_t mask = (width == 1)   ? 0xff
                        : (width == 2) ? 0xffff
                                       : 0xffffffffu;
  register_word now;
  if (width == 1)
    now = peek_byte(snapshot_memory_address) & 0xff;
  else if (width == 2)
    now = peek_half(snapshot_memory_address) & 0xffff;
  else
    now = peek_word(snapshot_memory_address);
  write_output(message_out, "  Wrote to memory:\n");
  write_output(message_out, "    0x%08x:  0x%08x  →  0x%08x\n", snapshot_memory_address,
               ((uint32_t)snapshot_memory_value) & mask, ((uint32_t)now) & mask);
}

/* Per-block state for "Try it yourself" emission.  `try_block_active`
   is set when `say_try_registers` opens a block, cleared by
   `say_try_finish`; `try_lines_emitted` counts lines so the finish
   call knows how many generic-suggestion padding lines to add. */
static bool try_block_active;
static int try_lines_emitted;

/* Emit "Try it yourself" lines for up to four registers. Pass 0 (=$zero) to
   skip a slot; duplicates are skipped automatically. Each emitted line is
   also added to the tab-completion suggestions list.

   `say_try_finish` is called by the dispatch wrapper at the end of each
   instruction's render to pad the block to a 3-line minimum. */
static void say_try_registers(int a, int b, int c, int d) {
  int registers[4] = {a, b, c, d};
  write_output(message_out, "  Try it yourself:\n");
  try_block_active = true;
  try_lines_emitted = 0;
  char command[64];
  for (int i = 0; i < 4; i++) {
    const int r = registers[i];
    if (r == 0) continue;
    bool duplicate = false;
    for (int j = 0; j < i; j++)
      if (registers[j] == r) duplicate = true;
    if (duplicate) continue;
    snprintf(command, sizeof command, "print $%s", integer_register_names[r]);
    write_output(message_out, "    %s\n", command);
    add_suggestion(command);
    try_lines_emitted++;
  }
}

/* One-off "Try it yourself" line for hints that aren't a bare $reg —
   memory addresses, base+offset forms, etc. Writes the line aligned with
   a trailing # comment, and adds the bare cmd to suggestions. */
static void say_try(const char* command, const char* why) {
  write_output(message_out, "    %-22s # %s\n", command, why);
  add_suggestion(command);
  try_lines_emitted++;
}

/* Close out the "Try it yourself" block.  If fewer than 3 lines have
   been emitted (e.g. for `j` which has no register operands, or `mfhi`
   with one), pad with generic suggestions (`print_all_registers`, `step`)
   so every block is uniformly substantive.  Templates that emit more
   than 3 operand-specific lines (loads/stores) skip the padding.
   No-op if no try block was opened by this instruction. */
static void say_try_finish(void) {
  if (!try_block_active) return;
  if (try_lines_emitted < 3) {
    write_output(message_out,
                 "    print_all_regs         # show every register\n");
    add_suggestion("print_all_regs");
    try_lines_emitted++;
  }
  if (try_lines_emitted < 3) {
    write_output(
        message_out,
        "    step                   # advance to the next instruction\n");
    add_suggestion("step");
    try_lines_emitted++;
  }
  if (try_lines_emitted < 3) {
    write_output(
        message_out,
        "    continue               # run until the next breakpoint\n");
    add_suggestion("continue");
    try_lines_emitted++;
  }
  try_block_active = false;
}

/* ------------ per-opcode templates ------------

   Each template prints: "What it does", "Inputs read", "Will write",
   "Try it yourself". They share a uniform shape so a student sees the same
   layout across every instruction.

   Template operation_text strings use generic field placeholders `$rs`, `$rt`,
   `$rd` for readability at the call site (e.g. "compute $rs + $rt as a
   signed sum"). `substitute_field_registers()` rewrites those placeholders to the
   actual register names this particular instruction uses, so a student
   sees one coherent vocabulary ($a2, $v0, ...) instead of mixing generic
   $rs/$rt with concrete $a2 in the same sentence. Pass -1 for any field
   that doesn't apply to a given template. */

static void substitute_field_registers(char* destination, size_t cap, const char* source, int source_register,
                             int target_register, int destination_register) {
  size_t i = 0;
  while (*source && i + 1 < cap) {
    if (source[0] == '$' && source[1] == 'r' &&
        (source[2] == 's' || source[2] == 't' || source[2] == 'd') &&
        !isalnum((unsigned char)source[3]) && source[3] != '_') {
      int r = -1;
      switch (source[2]) {
        case 's':
          r = source_register;
          break;
        case 't':
          r = target_register;
          break;
        case 'd':
          r = destination_register;
          break;
        default:
          /* Never reached: the test above admits only $rs, $rt and $rd. */
          fatal_error(
              "internal error: subst_field_regs: unexpected register field "
              "'$r%c'\n",
              source[2]);
      }
      if (r >= 0 && r < 32) {
        const int n = snprintf(destination + i, cap - i, "$%s", integer_register_names[r]);
        if (n > 0) i += (size_t)n;
        if (i >= cap) i = cap - 1;
        source += 3;
        continue;
      }
    }
    destination[i++] = *source++;
  }
  destination[i] = '\0';
}

static void template_r3_arithmetic(int level, const mips_instruction* instruction,
                         const char* operation_label, const char* operation_text) {
  const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction), destination_register = DESTINATION_REGISTER(instruction);
  char buffer[512];
  substitute_field_registers(buffer, sizeof(buffer), operation_text, source_register, target_register, destination_register);
  write_output(message_out, "  What it did:\n");
  write_output(message_out, "    %s — %s; the result was placed in $%s.\n",
               operation_label, buffer, integer_register_names[destination_register]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    if (source_register == 0 && target_register == 0)
      write_output(message_out, "    (both source registers are $zero)\n");
    say_input_register(source_register);
    say_input_register(target_register);
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(destination_register);
    say_try_registers(source_register, target_register, destination_register, 0);
  }
}

static void template_i2_arithmetic(int level, const mips_instruction* instruction,
                         const char* operation_label, const char* operation_text) {
  const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
  const short immediate = (short)IMMEDIATE(instruction);
  const bool zero_extension = immediate_not_sign_extended(
      (int)(((uint32_t)instruction_encode(instruction) >> 26) & 0x3f));
  char buffer[512];
  /* For I-type, "$rt" in the operation_text refers to the source field; the
     destination is also rt architecturally but we always describe it
     by its concrete name in the surrounding "result was placed in" line.
     We map $rs and $rt to their concrete names; $rd is unused. */
  substitute_field_registers(buffer, sizeof(buffer), operation_text, source_register, target_register, -1);
  write_output(message_out, "  What it did:\n");
  write_output(message_out, "    %s — %s; the result was placed in $%s.\n",
               operation_label, buffer, integer_register_names[target_register]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(source_register);
    if (zero_extension)
      write_output(message_out, "    immediate = %u  (0x%04x, zero-extended)\n",
                   (unsigned)(uint16_t)immediate, (unsigned)(uint16_t)immediate);
    else
      say_input_immediate(immediate);
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(target_register);
    say_try_registers(source_register, target_register, 0, 0);
  }
}

static void template_shift(int level, const mips_instruction* instruction,
                      const char* operation_label, const char* operation_text) {
  const int target_register = TARGET_REGISTER(instruction), destination_register = DESTINATION_REGISTER(instruction), shift_amount = SHIFT_AMOUNT(instruction);
  char buffer[512];
  substitute_field_registers(buffer, sizeof(buffer), operation_text, -1, target_register, destination_register);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — %s by %d bit%s; the result was placed in $%s.\n",
               operation_label, buffer, shift_amount, shift_amount == 1 ? "" : "s", integer_register_names[destination_register]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(target_register);
    write_output(message_out, "    shift amount = %d\n", shift_amount);
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(destination_register);
    say_try_registers(target_register, destination_register, 0, 0);
  }
}

/* Effective-address arithmetic line shared by every base+offset
   load/store template.  Picks "+" or "-" based on the sign of off so
   "$fp - 4" reads naturally instead of "$fp + -4". */
static void say_effective_address(int base, short offset, memory_address effective_address) {
  const char* const sign = (offset < 0) ? "-" : "+";
  const int absolute_offset = (offset < 0) ? -offset : offset;
  write_output(
      message_out,
      "    Effective address = $%s %s %d  =  0x%08x %s %d  =  0x%08x.\n",
      integer_register_names[base], sign, absolute_offset, snapshot_registers[base], sign, absolute_offset, effective_address);
}

static void template_load(int level, const mips_instruction* instruction,
                     const char* operation_label, const char* operation_text, int width) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  /* Effective address uses snapshot base — the value the instruction
     actually used. If this load's rt happens to equal base (rare but
     legal), the post-execute gpr[base] is the loaded value, not the
     pre-execute base. */
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — read %s from memory at the effective address; "
               "placed the result in $%s.\n",
               operation_label, operation_text, integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    /* Loads don't modify memory, so peek_word post-execute returns the
       same value the instruction read. */
    register_word current = 0;
    if (width == 1)
      current = peek_byte(effective_address) & 0xff;
    else if (width == 2)
      current = peek_half(effective_address) & 0xffff;
    else
      current = peek_word(effective_address);
    write_output(message_out, "    memory at 0x%08x = 0x%08x\n", effective_address,
                 (uint32_t)current);
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(target_register);
    say_try_registers(base, target_register, 0, 0);
    char buffer2[64];
    snprintf(buffer2, sizeof buffer2, "print 0x%08x", effective_address);
    say_try(buffer2, "inspect that memory");
    snprintf(buffer2, sizeof buffer2, "print %d($%s)", offset, integer_register_names[base]);
    say_try(buffer2, "same memory, using the N($reg) form you wrote");
  }
}

static void template_store(int level, const mips_instruction* instruction,
                      const char* operation_label, const char* operation_text, int width) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — wrote %s from $%s into memory at the effective "
               "address.\n",
               operation_label, operation_text, integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    say_input_register(target_register);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    /* Mirror the load template's "memory at addr = X" line — shows
       what's being overwritten, masked to the store width so a byte
       store shows the byte that's being clobbered, not the full word. */
    if (snapshot_has_memory) {
      const uint32_t mask = (width == 1)   ? 0xff
                            : (width == 2) ? 0xffff
                                           : 0xffffffffu;
      write_output(message_out, "    memory at 0x%08x = 0x%08x\n", effective_address,
                   ((uint32_t)snapshot_memory_value) & mask);
    }
    say_wrote_memory(width);
    say_try_registers(base, target_register, 0, 0);
    char buffer2[64];
    snprintf(buffer2, sizeof buffer2, "print 0x%08x", effective_address);
    say_try(buffer2, "inspect that memory");
    snprintf(buffer2, sizeof buffer2, "print %d($%s)", offset, integer_register_names[base]);
    say_try(buffer2, "same memory, using the N($reg) form you wrote");
  }
}

/* lwl/lwr — load word left/right.  Each merges some bytes of the
   word containing the addressed byte into the destination register
   without disturbing the rest.  Pair an lwl with an lwr (or vice
   versa) to assemble a full 32-bit word from any byte address —
   the trick compilers use to load a misaligned word in two
   instructions instead of trapping. */
static void template_load_unaligned(int level, const mips_instruction* instruction,
                               const char* operation_label, const char* operation_text) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — merged %s into $%s; preserved the other bytes "
               "already there.\n",
               operation_label, operation_text, integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  write_output(message_out,
               "    Typically paired with the matching opcode to assemble a "
               "full word from a potentially-misaligned address.\n");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    say_input_register(target_register);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    const memory_address word_address = effective_address & ~3u;
    write_output(message_out,
                 "    memory at 0x%08x (containing word) = 0x%08x\n", word_address,
                 (uint32_t)peek_word(word_address));
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(target_register);
    say_try_registers(base, target_register, 0, 0);
  }
}

/* swl/swr — store word left/right.  Mirror of lwl/lwr on the
   store side: each writes some bytes of $rt into the word
   containing the addressed byte, leaving the other bytes
   intact.  Pair them to spill a 32-bit register to any byte
   address. */
static void template_store_unaligned(int level, const mips_instruction* instruction,
                                const char* operation_label, const char* operation_text) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — wrote %s from $%s into the word containing the "
               "effective address; preserved the other bytes of that word.\n",
               operation_label, operation_text, integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  write_output(message_out,
               "    Typically paired with the matching opcode to store a "
               "full word to a potentially-misaligned address.\n");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    say_input_register(target_register);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    if (snapshot_has_memory) {
      write_output(message_out, "    memory at 0x%08x = 0x%08x\n",
                   snapshot_memory_address, (uint32_t)snapshot_memory_value);
    }
    say_wrote_memory(4);
    say_try_registers(base, target_register, 0, 0);
  }
}

/* ll — load linked.  On real multiprocessor MIPS, ll also tags
   this CPU's link register with the address so a subsequent sc to
   the same address can detect intervening writes.  spim is
   single-threaded; ll behaves as a plain word load. */
static void template_load_linked(int level, const mips_instruction* instruction) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    Load Linked — read a 32-bit word from memory at the "
               "effective address; placed the result in $%s.\n",
               integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  write_output(message_out,
               "    On real multiprocessor MIPS, ll also tags a link register "
               "so a subsequent sc can detect intervening writes.  In spim's "
               "single-threaded model, ll is just a plain word load.\n");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    write_output(message_out, "    memory at 0x%08x = 0x%08x\n", effective_address,
                 (uint32_t)peek_word(effective_address));
    write_output(message_out, "  Wrote:\n");
    say_wrote_register(target_register);
    say_try_registers(base, target_register, 0, 0);
  }
}

/* sc — store conditional.  On real MIPS, sc only succeeds if no
   intervening write happened since the matching ll, and writes
   1 (success) / 0 (failure) to $rt.  spim's sc always stores
   unconditionally and does NOT update $rt — code that polls $rt
   after sc will see the value it had going in. */
static void template_store_conditional(int level,
                                  const mips_instruction* instruction) {
  const int target_register = TARGET_REGISTER(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    Store Conditional — wrote the 32-bit value from $%s into "
               "memory at the effective address.\n",
               integer_register_names[target_register]);
  say_effective_address(base, offset, effective_address);
  write_output(message_out,
               "    On real multiprocessor MIPS, sc only succeeds if no "
               "intervening write happened since the matching ll, and writes "
               "1 (success) or 0 (failure) to $%s.  In spim, sc always "
               "succeeds and does NOT update $%s.\n",
               integer_register_names[target_register], integer_register_names[target_register]);
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    say_input_register(target_register);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    if (snapshot_has_memory) {
      write_output(message_out, "    memory at 0x%08x = 0x%08x\n", effective_address,
                   (uint32_t)snapshot_memory_value);
    }
    say_wrote_memory(4);
    say_try_registers(base, target_register, 0, 0);
  }
}

/* lwc1, ldc1 — coprocessor-1 (FP) loads.  Transfer width bytes from
   memory to the FP register $fN.  ldc1 reads 8 bytes into the
   register pair $fN / $f(N+1) and (in spim) needs at least 4-byte
   alignment — real MIPS requires 8-byte alignment for ldc1. */
static void template_load_floating_point(int level, const mips_instruction* instruction,
                        const char* operation_label, int width) {
  const int floating_point_target = FLOATING_POINT_TARGET(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  if (width == 4) {
    write_output(message_out,
                 "    %s — read 4 bytes from memory at the effective "
                 "address; placed the result in FP register $f%d.\n",
                 operation_label, floating_point_target);
  } else {
    write_output(message_out,
                 "    %s — read 8 bytes from memory at the effective "
                 "address; placed them in the FP register pair $f%d / $f%d.\n",
                 operation_label, floating_point_target, floating_point_target + 1);
  }
  say_effective_address(base, offset, effective_address);
  if (width == 8) {
    write_output(message_out,
                 "    ldc1 in spim requires 4-byte alignment; real MIPS "
                 "requires 8-byte alignment.\n");
  }
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    if (width == 4) {
      write_output(message_out, "    memory at 0x%08x = 0x%08x\n", effective_address,
                   (uint32_t)peek_word(effective_address));
      write_output(message_out, "  Wrote:\n");
      write_output(message_out,
                   "    $f%d  ← loaded 4 bytes (typically interpreted as a "
                   "single-precision float, or as a raw 32-bit int).\n",
                   floating_point_target);
    } else {
      write_output(message_out,
                   "    memory at 0x%08x = 0x%08x 0x%08x  (low / high words)\n",
                   effective_address, (uint32_t)peek_word(effective_address), (uint32_t)peek_word(effective_address + 4));
      write_output(message_out, "  Wrote:\n");
      write_output(message_out,
                   "    $f%d / $f%d  ← loaded 8 bytes (typically interpreted "
                   "as a double-precision float).\n",
                   floating_point_target, floating_point_target + 1);
    }
    say_try_registers(base, 0, 0, 0);
  }
}

/* swc1, sdc1 — coprocessor-1 (FP) stores.  Transfer width bytes from
   FP register $fN to memory.  sdc1 writes 8 bytes (the pair $fN /
   $f(N+1)) and (in spim) needs at least 4-byte alignment. */
static void template_store_floating_point(int level, const mips_instruction* instruction,
                         const char* operation_label, int width) {
  const int floating_point_target = FLOATING_POINT_TARGET(instruction), base = BASE(instruction);
  const short offset = (short)IMMEDIATE_OFFSET(instruction);
  const memory_address effective_address = (memory_address)(snapshot_registers[base] + offset);
  write_output(message_out, "  What it did:\n");
  if (width == 4) {
    write_output(message_out,
                 "    %s — wrote 4 bytes from FP register $f%d into memory "
                 "at the effective address.\n",
                 operation_label, floating_point_target);
  } else {
    write_output(message_out,
                 "    %s — wrote 8 bytes from the FP register pair $f%d / "
                 "$f%d into memory at the effective address.\n",
                 operation_label, floating_point_target, floating_point_target + 1);
  }
  say_effective_address(base, offset, effective_address);
  if (width == 8) {
    write_output(message_out,
                 "    sdc1 in spim requires 4-byte alignment; real MIPS "
                 "requires 8-byte alignment.\n");
  }
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(base);
    write_output(message_out, "    offset = %d  (0x%04x)\n", offset, offset & 0xffff);
    write_output(message_out, "  Wrote to memory:\n");
    if (width == 4) {
      write_output(message_out, "    0x%08x  ← 4 bytes from $f%d\n", effective_address, floating_point_target);
    } else {
      write_output(
          message_out,
          "    0x%08x  ← 8 bytes from $f%d / $f%d (low / high words)\n", effective_address, floating_point_target,
          floating_point_target + 1);
    }
    say_try_registers(base, 0, 0, 0);
  }
}

/* The address a taken branch goes to, computed as run.c's BRANCH_INSTRUCTION
   does: the instruction's own PC (snapshot_program_counter; the live PC has moved on) plus
   the stored displacement, plus 4 more with delayed branches. */
static memory_address branch_target(const mips_instruction* instruction) {
  const memory_address target = snapshot_program_counter + (memory_address)BRANCH_OFFSET(instruction);
  return delayed_branches ? target + BYTES_PER_WORD : target;
}

/* The address a j/jal goes to, as run.c computes it: the top four bits
   of the PC above the 26-bit word index shifted left by 2. */
static memory_address jump_target(const mips_instruction* instruction) {
  return (snapshot_program_counter & 0xf0000000u) | ((memory_address)TARGET(instruction) << 2);
}

static void template_branch_2register(int level, const mips_instruction* instruction,
                            const char* operation_label, const char* operation_symbol,
                            bool taken) {
  const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
  const memory_address target = branch_target(instruction);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — would transfer to 0x%08x if ($%s %s $%s).\n", operation_label,
               target, integer_register_names[source_register], operation_symbol, integer_register_names[target_register]);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s %d  →  %s  →  branch was %s\n",
               snapshot_registers[source_register], operation_symbol, snapshot_registers[target_register], taken ? "true" : "false",
               taken ? "taken" : "NOT taken");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(source_register);
    say_input_register(target_register);
    write_output(message_out, "    branch target = 0x%08x\n", target);
    say_try_registers(source_register, target_register, 0, 0);
  }
}

static void template_branch_1register(int level, const mips_instruction* instruction,
                            const char* operation_label, const char* operation_symbol,
                            bool taken) {
  const int source_register = SOURCE_REGISTER(instruction);
  const memory_address target = branch_target(instruction);
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    %s — would transfer to 0x%08x if ($%s %s 0).\n", operation_label,
               target, integer_register_names[source_register], operation_symbol);
  write_output(message_out, "  Comparison:\n");
  write_output(message_out, "    %d %s 0  →  %s  →  branch was %s\n",
               snapshot_registers[source_register], operation_symbol, taken ? "true" : "false",
               taken ? "taken" : "NOT taken");
  if (level >= 2) {
    write_output(message_out, "  Inputs (before this step):\n");
    say_input_register(source_register);
    write_output(message_out, "    branch target = 0x%08x\n", target);
    say_try_registers(source_register, 0, 0, 0);
  }
}

/* ------------ syscall, the special case ------------ */

static void explain_syscall(int level) {
  /* Call number is the pre-execute $v0. syscall 5 (read_int) overwrites
     $v0 with the read integer, so post-execute gpr[VALUE_REGISTER_0] would be wrong. */
  const register_word v0 = snapshot_registers[VALUE_REGISTER_0];
  write_output(message_out, "  What it did:\n");
  write_output(message_out,
               "    System Call — invoked the kernel.  The call number was "
               "in $v0;\n    arguments were in $a0..$a3 (and $f12 for "
               "floats).\n");
  switch (v0) {
    case 1:
      write_output(message_out,
                   "    $v0 = 1  →  print_int: printed the integer in $a0.\n");
      write_output(message_out, "  Output:\n");
      write_output(message_out, "    Wrote the integer to the console.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 1\n");
        say_input_register(ARGUMENT_REGISTER_0);
        say_try_registers(VALUE_REGISTER_0, ARGUMENT_REGISTER_0, 0, 0);
      }
      break;
    case 4: {
      const memory_address a0 = (memory_address)snapshot_registers[ARGUMENT_REGISTER_0];
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
        say_try_registers(VALUE_REGISTER_0, ARGUMENT_REGISTER_0, 0, 0);
        char buffer2[64];
        snprintf(buffer2, sizeof buffer2, "print 0x%08x", a0);
        say_try(buffer2, "inspect string memory");
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
        say_wrote_register(VALUE_REGISTER_0);
        say_try_registers(VALUE_REGISTER_0, 0, 0, 0);
      }
      break;
    case 8: {
      const memory_address a0 = (memory_address)snapshot_registers[ARGUMENT_REGISTER_0];
      write_output(
          message_out,
          "    $v0 = 8  →  read_string: read a string into the buffer\n"
          "    at $a0, up to $a1 bytes.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 8\n");
        write_output(message_out, "    $a0 = 0x%08x  (buffer address)\n", a0);
        say_input_register(ARGUMENT_REGISTER_1);
        write_output(message_out, "  Wrote to memory:\n");
        write_output(message_out, "    0x%08x .. 0x%08x\n", a0,
                     a0 + (memory_address)snapshot_registers[ARGUMENT_REGISTER_1] - 1);
        say_try_registers(VALUE_REGISTER_0, ARGUMENT_REGISTER_0, ARGUMENT_REGISTER_1, 0);
      }
      break;
    }
    case 10:
      write_output(message_out, "    $v0 = 10 →  exit: halted the program.\n");
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    $v0 = 10\n");
        say_try_registers(VALUE_REGISTER_0, 0, 0, 0);
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
        say_input_register(ARGUMENT_REGISTER_0);
        say_try_registers(VALUE_REGISTER_0, ARGUMENT_REGISTER_0, 0, 0);
      }
      break;
    default:
      write_output(
          message_out,
          "    $v0 = %d (no detailed explanation for this syscall yet).\n", v0);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(VALUE_REGISTER_0);
        say_try_registers(VALUE_REGISTER_0, ARGUMENT_REGISTER_0, ARGUMENT_REGISTER_1, 0);
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

static void write_bits(char* out, uint32_t value, int high, int low) {
  const int width = high - low + 1;
  for (int i = 0; i < width; i++) {
    const int bit_position = high - i;
    out[i] = (value & (1u << bit_position)) ? '1' : '0';
  }
  out[width] = '\0';
}

/* Render an 8-column hex/binary table showing the 32-bit encoding
   broken into nibbles.  Row 1 is the hex digit, row 2 is its 4-bit
   binary equivalent.  Helps students trace `0x0c10000a` back to
   the bits the CPU's decoder actually sees.

   The nibble boundaries don't align with MIPS field boundaries
   (e.g. the 6-bit opcode crosses two nibbles); the field-layout box
   below this table shows the field-aligned grouping. */
static void render_hex_binary_table(uint32_t encoding) {
  char n[8][5];
  for (int i = 0; i < 8; i++) {
    const int high = 31 - 4 * i;
    write_bits(n[i], encoding, high, high - 3);
  }
  write_output(
      message_out,
      "    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐\n"
      "    │ hex │  %x  │  %x  │  %x  │  %x  │  %x  │  %x  │  %x  │  %x  │\n"
      "    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤\n"
      "    │ bin │%s │%s │%s │%s │%s │%s │%s │%s │\n"
      "    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘\n",
      (encoding >> 28) & 0xf, (encoding >> 24) & 0xf, (encoding >> 20) & 0xf,
      (encoding >> 16) & 0xf, (encoding >> 12) & 0xf, (encoding >> 8) & 0xf, (encoding >> 4) & 0xf,
      encoding & 0xf, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7]);
}

static void render_r_layout(uint32_t encoding, const char* mnemonic) {
  char bits_opcode[7], b_source_register[6], b_target_register[6], b_destination_register[6], bits_shift_amount[6], b_function_field[7];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_source_register, encoding, 25, 21);
  write_bits(b_target_register, encoding, 20, 16);
  write_bits(b_destination_register, encoding, 15, 11);
  write_bits(bits_shift_amount, encoding, 10, 6);
  write_bits(b_function_field, encoding, 5, 0);
  const int opcode = (int)((encoding >> 26) & 0x3f);
  const int source_register = (encoding >> 21) & 0x1f;
  const int target_register = (encoding >> 16) & 0x1f;
  const int destination_register = (encoding >> 11) & 0x1f;
  const int shift_amount = (encoding >> 6) & 0x1f;
  const int function_field = encoding & 0x3f;
  const char* const group = (opcode == 0x00)   ? "SPECIAL"
                            : (opcode == 0x1c) ? "SPECIAL2"
                            : (opcode == 0x1f) ? "SPECIAL3"
                                           : "?";

  write_output(message_out, "  Bit layout (R-type, encoding 0x%08x):\n", encoding);
  render_hex_binary_table(encoding);
  write_output(message_out,
               "     31    26 25  21 20  16 15  11 10   6 5     0\n"
               "    ┌────────┬───────┬───────┬───────┬───────┬────────┐\n"
               "    │ %s │ %s │ %s │ %s │ %s │ %s │\n"
               "    └────────┴───────┴───────┴───────┴───────┴────────┘\n"
               "      opcode    rs      rt      rd     shamt    funct\n"
               "       = %-2d   = $%-4s = $%-4s = $%-4s   = %-2d    = 0x%02x\n"
               "    -> opcode=0x%02x (%s) tells the CPU to look at funct;\n"
               "       funct=0x%02x selects the `%s` instruction.\n",
               bits_opcode, b_source_register, b_target_register, b_destination_register, bits_shift_amount, b_function_field, opcode, integer_register_names[source_register],
               integer_register_names[target_register], integer_register_names[destination_register], shift_amount, function_field, opcode, group,
               function_field, mnemonic);
}

static void render_i_layout(uint32_t encoding, const char* mnemonic) {
  char bits_opcode[7], b_source_register[6], b_target_register[6], bits_immediate[17];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_source_register, encoding, 25, 21);
  write_bits(b_target_register, encoding, 20, 16);
  write_bits(bits_immediate, encoding, 15, 0);
  const int opcode = (int)((encoding >> 26) & 0x3f);
  const int source_register = (encoding >> 21) & 0x1f;
  const int target_register = (encoding >> 16) & 0x1f;
  const int16_t immediate_signed = (int16_t)(encoding & 0xffff);
  const uint16_t immediate_unsigned = (uint16_t)(encoding & 0xffff);

  write_output(message_out, "  Bit layout (I-type, encoding 0x%08x):\n", encoding);
  render_hex_binary_table(encoding);
  write_output(message_out,
               "     31    26 25  21 20  16 15                 0\n"
               "    ┌────────┬───────┬───────┬──────────────────┐\n"
               "    │ %s │ %s │ %s │ %s │\n"
               "    └────────┴───────┴───────┴──────────────────┘\n"
               "      opcode    rs      rt        immediate (16-bit)\n"
               "      = 0x%02x  = $%-4s = $%-4s   = %d  (%s) / 0x%04x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               bits_opcode, b_source_register, b_target_register, bits_immediate, opcode, integer_register_names[source_register], integer_register_names[target_register],
               immediate_not_sign_extended(opcode) ? (int)immediate_unsigned : (int)immediate_signed,
               immediate_not_sign_extended(opcode) ? "unsigned" : "signed", immediate_unsigned, opcode,
               mnemonic);
}

static void render_j_layout(uint32_t encoding, const char* mnemonic) {
  char bits_opcode[7], b_target[27];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_target, encoding, 25, 0);
  const int opcode = (int)((encoding >> 26) & 0x3f);
  const uint32_t target = encoding & 0x03ffffff;

  write_output(message_out, "  Bit layout (J-type, encoding 0x%08x):\n", encoding);
  render_hex_binary_table(encoding);
  write_output(message_out,
               "     31    26 25                          0\n"
               "    ┌────────┬────────────────────────────┐\n"
               "    │ %s │ %s │\n"
               "    └────────┴────────────────────────────┘\n"
               "      opcode         target (26-bit word index)\n"
               "      = 0x%02x  = 0x%07x  ->  jump addr = (PC[31:28] | "
               "target<<2) = 0x%08x\n"
               "    -> opcode=0x%02x selects the `%s` instruction.\n",
               bits_opcode, b_target, opcode, target, (snapshot_program_counter & 0xf0000000u) | (target << 2),
               opcode, mnemonic);
}

static void explain_bit_layout(const mips_instruction* instruction) {
  if (instruction == nullptr) return;
  const uint32_t encoding = (uint32_t)instruction_encode(instruction);
  /* nop (sll $0,$0,0) really does encode to 0x00000000. Skip the diagram
     since "all zeros" carries no pedagogical signal of its own; the
     mnemonic-level explanation below covers it. */
  if (encoding == 0) return;

  const char* const mnemonic = instruction_opcode_name(instruction);
  const int opcode = (int)((encoding >> 26) & 0x3f);
  switch (opcode) {
    case 0x00: /* SPECIAL */
    case 0x1c: /* SPECIAL2 */
    case 0x1f: /* SPECIAL3 */
      render_r_layout(encoding, mnemonic);
      break;
    case 0x02: /* j */
    case 0x03: /* jal */
      render_j_layout(encoding, mnemonic);
      break;
    default:
      /* Every other primary opcode is I-type. */
      render_i_layout(encoding, mnemonic);
      break;
  }
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
static constexpr char UNKNOWN5[] = "  ?  ";
static constexpr char UNKNOWN6[] = "  ?   ";
static constexpr char UNKNOWN16[] = "       ?        ";
static constexpr char UNKNOWN26[] = "            ?             ";

/* SPECIAL-group: opcode == 0x00, funct selects the actual op. Table
   filled out for the integer ops spimulator's existing templates
   cover; entries not listed return nullptr (rendered as "?"). */
static const char* const special_function_field_to_name[64] = {
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
static const char* const register_immediate_target_register_to_name[32] = {
    [0x00] = "bltz",
    [0x01] = "bgez",
    [0x10] = "bltzal",
    [0x11] = "bgezal",
};

static void render_r_decode_step(uint32_t encoding, int step, const char* mnemonic,
                                 const char* group_name) {
  const uint32_t opcode = (encoding >> 26) & 0x3f;
  const uint32_t source_register = (encoding >> 21) & 0x1f;
  const uint32_t target_register = (encoding >> 16) & 0x1f;
  const uint32_t destination_register = (encoding >> 11) & 0x1f;
  const uint32_t shift_amount = (encoding >> 6) & 0x1f;
  const uint32_t function_field = encoding & 0x3f;

  char bits_opcode[7], b_source_register[6], b_target_register[6], b_destination_register[6], bits_shift_amount[6], b_function_field[7];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_source_register, encoding, 25, 21);
  write_bits(b_target_register, encoding, 20, 16);
  write_bits(b_destination_register, encoding, 15, 11);
  write_bits(bits_shift_amount, encoding, 10, 6);
  write_bits(b_function_field, encoding, 5, 0);

  const char* const field_source_register = (step >= 3) ? b_source_register : UNKNOWN5;
  const char* const field_target_register = (step >= 3) ? b_target_register : UNKNOWN5;
  const char* const field_destination_register = (step >= 3) ? b_destination_register : UNKNOWN5;
  const char* const field_shift_amount = (step >= 3) ? bits_shift_amount : UNKNOWN5;
  const char* const field_function_field = (step >= 2) ? b_function_field : UNKNOWN6;

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
    default:
      /* Never reached: explain_decoding_steps calls this with steps 1 to 3
       * only. */
      fatal_error("internal error: render_r_decode_step: no decode step %d\n",
                  step);
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬───────┬───────┬────────┐\n"
               "  │ %s │ %s │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴───────┴───────┴────────┘\n",
               bits_opcode, field_source_register, field_target_register, field_destination_register, field_shift_amount, field_function_field);
  write_output(message_out, "   opcode    %-7s %-7s %-7s %-7s %s\n",
               (step >= 3) ? "rs" : "?", (step >= 3) ? "rt" : "?",
               (step >= 3) ? "rd" : "?", (step >= 3) ? "shamt" : "?",
               (step >= 2) ? "funct" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", opcode);
  } else if (step == 2) {
    write_output(message_out,
                 "   = 0x%02x                                       "
                 "= 0x%02x\n",
                 opcode, function_field);
  } else {
    const bool shift_amount_used = mnemonic && (strcmp(mnemonic, "sll") == 0 ||
                                         strcmp(mnemonic, "srl") == 0 ||
                                         strcmp(mnemonic, "sra") == 0);
    write_output(message_out,
                 "   = 0x%02x    = $%-5s = $%-5s = $%-5s = %-5d = 0x%02x\n", opcode,
                 integer_register_names[source_register], integer_register_names[target_register], integer_register_names[destination_register], shift_amount,
                 function_field);
    if (!shift_amount_used) {
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
                 opcode, group_name);
  } else if (step == 2) {
    const char* const file_name =
        (opcode == 0x00 && function_field < 64 && special_function_field_to_name[function_field])
            ? special_function_field_to_name[function_field]
            : (mnemonic ? mnemonic : "?");
    write_output(message_out,
                 "  In the %s table, funct=0x%02x is `%s`. Now we know\n"
                 "  this is R-type %s, so the remaining fields are rs, rt, "
                 "rd,\n"
                 "  shamt — read them in turn.\n",
                 group_name, function_field, file_name, file_name);
  } else {
    /* Step 3 has no paragraph after its field table. */
  }
}

static void render_i_decode_step(uint32_t encoding, int step, const char* mnemonic) {
  const uint32_t opcode = (encoding >> 26) & 0x3f;
  const uint32_t source_register = (encoding >> 21) & 0x1f;
  const uint32_t target_register = (encoding >> 16) & 0x1f;
  const int16_t immediate_signed = (int16_t)(encoding & 0xffff);
  const uint16_t immediate_unsigned = (uint16_t)(encoding & 0xffff);
  const bool unsigned_immediate = immediate_not_sign_extended((int)opcode);

  char bits_opcode[7], b_source_register[6], b_target_register[6], bits_immediate[17];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_source_register, encoding, 25, 21);
  write_bits(b_target_register, encoding, 20, 16);
  write_bits(bits_immediate, encoding, 15, 0);

  const char* const field_source_register = (step >= 2) ? b_source_register : UNKNOWN5;
  const char* const field_target_register = (step >= 2) ? b_target_register : UNKNOWN5;
  const char* const field_immediate = (step >= 2) ? bits_immediate : UNKNOWN16;

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out,
                   "  Step 2 — read the remaining I-type fields.\n");
      break;
    default:
      /* Never reached: explain_decoding_steps calls this with steps 1 and 2
       * only. */
      fatal_error("internal error: render_i_decode_step: no decode step %d\n",
                  step);
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬──────────────────┐\n"
               "  │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴──────────────────┘\n",
               bits_opcode, field_source_register, field_target_register, field_immediate);
  write_output(message_out, "   opcode    %-7s %-7s %s\n",
               (step >= 2) ? "rs" : "?", (step >= 2) ? "rt" : "?",
               (step >= 2) ? (unsigned_immediate ? "imm (16-bit, unsigned)"
                                           : "imm (16-bit, signed)")
                           : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", opcode);
  } else {
    write_output(message_out, "   = 0x%02x    = $%-5s = $%-5s = %d  (0x%04x)\n",
                 opcode, integer_register_names[source_register], integer_register_names[target_register],
                 unsigned_immediate ? (int)immediate_unsigned : (int)immediate_signed, immediate_unsigned);
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x in the primary table is `%s`. This is an\n"
                 "  I-type instruction; the rest of the word is rs, rt, "
                 "and\n"
                 "  a 16-bit %s immediate.\n",
                 opcode, mnemonic ? mnemonic : "?",
                 unsigned_immediate ? "unsigned" : "signed");
  }
}

static void render_j_decode_step(uint32_t encoding, int step, const char* mnemonic) {
  const uint32_t opcode = (encoding >> 26) & 0x3f;
  const uint32_t target = encoding & 0x03ffffff;

  char bits_opcode[7], b_target[27];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_target, encoding, 25, 0);

  const char* const field_target = (step >= 2) ? b_target : UNKNOWN26;

  switch (step) {
    case 1:
      write_output(message_out, "  Step 1 — start with the opcode.\n");
      break;
    case 2:
      write_output(message_out, "  Step 2 — read the target field.\n");
      break;
    default:
      /* Never reached: explain_decoding_steps calls this with steps 1 and 2
       * only. */
      fatal_error("internal error: render_j_decode_step: no decode step %d\n",
                  step);
  }

  write_output(message_out,
               "  ┌────────┬────────────────────────────┐\n"
               "  │ %s │ %s │\n"
               "  └────────┴────────────────────────────┘\n",
               bits_opcode, field_target);
  write_output(message_out, "   opcode    %s\n",
               (step >= 2) ? "target (26-bit word index)" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", opcode);
  } else {
    write_output(message_out,
                 "   = 0x%02x    = 0x%07x\n"
                 "             -> actual address = (PC[31:28] | target<<2)\n"
                 "                                = 0x%08x\n",
                 opcode, target, (snapshot_program_counter & 0xf0000000u) | (target << 2));
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x in the primary table is `%s`. J-type —\n"
                 "  one big target field below.\n",
                 opcode, mnemonic ? mnemonic : "?");
  }
}

static void render_register_immediate_decode_step(uint32_t encoding, int step,
                                      const char* mnemonic) {
  const uint32_t opcode = (encoding >> 26) & 0x3f;
  const uint32_t source_register = (encoding >> 21) & 0x1f;
  const uint32_t target_register = (encoding >> 16) & 0x1f;
  const int16_t immediate_signed = (int16_t)(encoding & 0xffff);

  char bits_opcode[7], b_source_register[6], b_target_register[6], bits_immediate[17];
  write_bits(bits_opcode, encoding, 31, 26);
  write_bits(b_source_register, encoding, 25, 21);
  write_bits(b_target_register, encoding, 20, 16);
  write_bits(bits_immediate, encoding, 15, 0);

  const char* const field_source_register = (step >= 3) ? b_source_register : UNKNOWN5;
  const char* const field_target_register = (step >= 2) ? b_target_register : UNKNOWN5;
  const char* const field_immediate = (step >= 3) ? bits_immediate : UNKNOWN16;

  const char* const rt_instruction_name = (target_register < 32 && register_immediate_target_register_to_name[target_register])
                                 ? register_immediate_target_register_to_name[target_register]
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
    default:
      /* Never reached: explain_decoding_steps calls this with steps 1 to 3
       * only. */
      fatal_error(
          "internal error: render_regimm_decode_step: no decode step %d\n",
          step);
  }

  write_output(message_out,
               "  ┌────────┬───────┬───────┬──────────────────┐\n"
               "  │ %s │ %s │ %s │ %s │\n"
               "  └────────┴───────┴───────┴──────────────────┘\n",
               bits_opcode, field_source_register, field_target_register, field_immediate);
  write_output(message_out, "   opcode    %-7s %-7s %s\n",
               (step >= 3) ? "rs" : "?", (step >= 2) ? "rt-sel" : "?",
               (step >= 3) ? "offset (signed)" : "?");

  if (step == 1) {
    write_output(message_out, "   = 0x%02x\n", opcode);
  } else if (step == 2) {
    write_output(message_out, "   = 0x%02x            = %s\n", opcode, rt_instruction_name);
  } else {
    write_output(
        message_out,
        "   = 0x%02x    = $%-5s = %-6s = %d  (target = %s + (offset<<2))\n", opcode,
        integer_register_names[source_register], rt_instruction_name, (int)immediate_signed,
        delayed_branches ? "PC+4" : "PC");
  }

  if (step == 1) {
    write_output(message_out,
                 "  Opcode 0x%02x is the REGIMM group. Like SPECIAL, but the\n"
                 "  selector is in the rt field — not funct. Read rt next.\n",
                 opcode);
  } else if (step == 2) {
    write_output(message_out,
                 "  In the REGIMM table, rt=0x%02x is `%s`. This is I-type\n"
                 "  with a register tested against zero and a 16-bit branch\n"
                 "  displacement.\n",
                 target_register, rt_instruction_name);
  } else {
    /* Step 3 has no paragraph after its field table. */
  }
}

static void explain_decoding_steps(const mips_instruction* instruction) {
  if (instruction == nullptr) return;
  const uint32_t encoding = (uint32_t)instruction_encode(instruction);
  if (encoding == 0) return; /* true nop — skip */

  const char* const mnemonic = instruction_opcode_name(instruction);
  const int opcode = (int)((encoding >> 26) & 0x3f);

  write_output(message_out, "  Decoding 0x%08x step by step:\n", encoding);
  render_hex_binary_table(encoding);
  write_output(message_out, "\n");

  switch (opcode) {
    case 0x00: /* SPECIAL */
    case 0x1c: /* SPECIAL2 */
    case 0x1f: /* SPECIAL3 */ {
      const char* const group = (opcode == 0x00)   ? "SPECIAL"
                                : (opcode == 0x1c) ? "SPECIAL2"
                                               : "SPECIAL3";
      for (int s = 1; s <= 3; s++) {
        if (s > 1) write_output(message_out, "\n");
        render_r_decode_step(encoding, s, mnemonic, group);
      }
      break;
    }
    case 0x01: /* REGIMM */
      for (int s = 1; s <= 3; s++) {
        if (s > 1) write_output(message_out, "\n");
        render_register_immediate_decode_step(encoding, s, mnemonic);
      }
      break;
    case 0x02: /* j */
    case 0x03: /* jal */
      for (int s = 1; s <= 2; s++) {
        if (s > 1) write_output(message_out, "\n");
        render_j_decode_step(encoding, s, mnemonic);
      }
      break;
    default:
      /* Every other primary opcode is I-type. */
      for (int s = 1; s <= 2; s++) {
        if (s > 1) write_output(message_out, "\n");
        render_i_decode_step(encoding, s, mnemonic);
      }
      break;
  }
}

/* ------------ main entrypoints ------------ */

/* explain_before runs immediately before the dispatch switch in run_spim.
   It captures everything the post-execute narration will need to read —
   nothing is printed here, so by the time the (spim) prompt comes back,
   every visible line describes something that has already happened. */
void explain_before(const mips_instruction* instruction, memory_address address) {
  if (explain_level == 0) return;

  /* Once per session, before the first instruction executes: show the
     whole loaded program.  Gives the student a static view to match
     `run <ADDR>` arguments and branch targets against while stepping. */
  if (!listing_shown) {
    print_text_listing();
    listing_shown = true;
  }

  /* Tab-completion suggestions are per-instruction; clear before any
     template populates new ones. (At L1 nothing gets added, so the list
     remains empty — by design.) */
  explain_clear_suggestions();

  /* Snapshot of architectural state for the post-execute templates. */
  memcpy(snapshot_registers, general_purpose_registers, sizeof(snapshot_registers));
  snapshot_multiply_divide_high = MULTIPLY_DIVIDE_HIGH;
  snapshot_multiply_divide_low = MULTIPLY_DIVIDE_LOW;
  snapshot_program_counter = address;

  /* For stores, also snapshot the memory word that's about to be
     overwritten. By the time explain_after runs, the write has happened
     and peek_word would return the new value. */
  snapshot_has_memory = false;
  switch (OPCODE(instruction)) {
    case TOKEN_SW_OPCODE:
    case TOKEN_SH_OPCODE:
    case TOKEN_SB_OPCODE:
    case TOKEN_SC_OPCODE: {
      const int base = BASE(instruction);
      const short offset = (short)IMMEDIATE_OFFSET(instruction);
      snapshot_memory_address = (memory_address)(general_purpose_registers[base] + offset);
      snapshot_memory_value = peek_word(snapshot_memory_address);
      snapshot_has_memory = true;
      break;
    }
    case TOKEN_SWL_OPCODE:
    case TOKEN_SWR_OPCODE: {
      /* These merge bytes into the aligned word holding the address, so
         that word is the one to show before and after. */
      const int base = BASE(instruction);
      const short offset = (short)IMMEDIATE_OFFSET(instruction);
      snapshot_memory_address = (memory_address)(general_purpose_registers[base] + offset) & ~(memory_address)0x3;
      snapshot_memory_value = peek_word(snapshot_memory_address);
      snapshot_has_memory = true;
      break;
    }
    default:
      /* No other opcode's narration shows the memory it overwrote (swc1
       * and sdc1 store too, but their template names only the address),
       * so there is nothing to snapshot. */
      break;
  }
}

void explain_program_exit(const mips_instruction* instruction) {
  if (explain_level == 0) return;
  explain_print_step_header(snapshot_program_counter, instruction);
  const int status = program_exit_status();
  write_output(message_out,
               "  What it did:\n"
               "    System Call %d (%s) — the program ended.\n",
               snapshot_registers[VALUE_REGISTER_0], snapshot_registers[VALUE_REGISTER_0] == 10 ? "exit" : "exit2");
  if (status != spim_return_value) {
    write_output(message_out,
                 "  The program exited with status %d (128 + exception code "
                 "%d:\n  an exception occurred earlier in the run).\n\n",
                 status, first_bad_exception);
  } else {
    write_output(message_out, "  The program exited with status %d.\n\n",
                 status);
  }
}

explain_snapshot explain_save_snapshot(void) {
  explain_snapshot s = {.multiply_divide_high = snapshot_multiply_divide_high,
                        .multiply_divide_low = snapshot_multiply_divide_low,
                        .program_counter = snapshot_program_counter,
                        .has_memory = snapshot_has_memory,
                        .memory_address = snapshot_memory_address,
                        .memory_value = snapshot_memory_value};
  memcpy(s.registers, snapshot_registers, sizeof(s.registers));
  return s;
}

void explain_restore_snapshot(const explain_snapshot* s) {
  memcpy(snapshot_registers, s->registers, sizeof(snapshot_registers));
  snapshot_multiply_divide_high = s->multiply_divide_high;
  snapshot_multiply_divide_low = s->multiply_divide_low;
  snapshot_program_counter = s->program_counter;
  snapshot_has_memory = s->has_memory;
  snapshot_memory_address = s->memory_address;
  snapshot_memory_value = s->memory_value;
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
  CATEGORY_ARITHMETIC,
  CATEGORY_LOGICAL,
  CATEGORY_DATA_TRANSFER,
  CATEGORY_CONDITION_BRANCH,
  CATEGORY_UNCONDITIONAL_JUMP,
  CATEGORY_COMPARISON,
  CATEGORY_CONSTANT,
  CATEGORY_SYSTEM,
  CATEGORY_COUNT
} instruction_category;

typedef enum {
  MODIFIER_U_UNSIGNED_ARITHMETIC,   /* `u` on arith: no overflow trap */
  MODIFIER_U_ZERO_EXTEND,      /* `u` on narrow load: zero-extend not sign */
  MODIFIER_U_UNSIGNED_COMPARE, /* `u` on slt: compare as unsigned numbers */
  MODIFIER_I_IMMEDIATE,        /* `i` suffix: 16-bit constant in instruction */
  MODIFIER_V_VARIABLE_SHIFT,   /* `v` suffix: shift count from a register */
  MODIFIER_AL_AND_LINK,        /* `al` suffix: write $ra (subroutine call) */
  MODIFIER_L_LIKELY,           /* `l` suffix: branch-likely (delay-slot nullify) */
  MODIFIER_W_WORD,             /* word-width memory access (32 bits) */
  MODIFIER_H_HALFWORD,         /* halfword-width memory access (16 bits) */
  MODIFIER_B_BYTE,             /* byte-width memory access (8 bits) */
  MODIFIER_COUNT
} instruction_modifier;

static const char* category_names[CATEGORY_COUNT] = {
    [CATEGORY_ARITHMETIC] = "Arithmetic",
    [CATEGORY_LOGICAL] = "Logical",
    [CATEGORY_DATA_TRANSFER] = "Data transfer",
    [CATEGORY_CONDITION_BRANCH] = "Conditional branch",
    [CATEGORY_UNCONDITIONAL_JUMP] = "Unconditional jump",
    [CATEGORY_COMPARISON] = "Comparison",
    [CATEGORY_CONSTANT] = "Constant",
    [CATEGORY_SYSTEM] = "System / coprocessor",
};

/* Category descriptions are intentionally short — they say what
   defines the family, not which instructions are in it.  Listing
   members (e.g. "Includes direct jumps, register-indirect jumps...")
   was useful when these were shown once per session, but Phase C
   made the category preamble always-on, and the member enumeration
   becomes noise on every specific instruction the student looks at.
   Keep each description one conceptual sentence. */
static const char* category_descriptions[CATEGORY_COUNT] = {
    [CATEGORY_ARITHMETIC] = "Integer math on registers. (COD §2.2.)",
    [CATEGORY_LOGICAL] = "Bitwise operations and shifts. (COD §2.6.)",
    [CATEGORY_DATA_TRANSFER] =
        "Move data between registers and memory.  MIPS is\n"
        "    load/store — math never operates on memory directly.\n"
        "    (COD §2.3, §2.9.)",
    [CATEGORY_CONDITION_BRANCH] =
        "Test a condition; if true, transfer PC to a labeled target.\n"
        "    (COD §2.7.)",
    [CATEGORY_UNCONDITIONAL_JUMP] =
        "Transfer PC unconditionally — no condition test. (COD §2.8.)",
    [CATEGORY_COMPARISON] =
        "Compare two values and set a register to 1 or 0; a following\n"
        "    beq/bne branches on the result. (COD §2.7.)",
    [CATEGORY_CONSTANT] =
        "Build a constant in a register, with no memory access: `lui`\n"
        "    sets the upper 16 bits, `ori` the lower 16. (COD §2.10.)",
    [CATEGORY_SYSTEM] = "Operating-system services and miscellaneous control.",
};

static const char* modifier_letters[MODIFIER_COUNT] = {
    [MODIFIER_U_UNSIGNED_ARITHMETIC] = "`u`",   [MODIFIER_U_ZERO_EXTEND] = "`u`",
    [MODIFIER_U_UNSIGNED_COMPARE] = "`u`", [MODIFIER_I_IMMEDIATE] = "`i`",
    [MODIFIER_V_VARIABLE_SHIFT] = "`v`",   [MODIFIER_AL_AND_LINK] = "`al`",
    [MODIFIER_L_LIKELY] = "`l`",           [MODIFIER_W_WORD] = "`w`",
    [MODIFIER_H_HALFWORD] = "`h`",         [MODIFIER_B_BYTE] = "`b`",
};

static const char* modifier_descriptions[MODIFIER_COUNT] = {
    [MODIFIER_U_UNSIGNED_ARITHMETIC] =
        "no overflow trap: the `u` form wraps around on overflow,\n"
        "          where the plain (no-`u`) form traps.  Despite the\n"
        "          name the operands are not treated as unsigned.",
    [MODIFIER_U_UNSIGNED_COMPARE] =
        "unsigned: compare the operands as unsigned numbers.  The\n"
        "          plain (no-`u`) form compares them as signed.",
    [MODIFIER_U_ZERO_EXTEND] =
        "unsigned: zero-extend the loaded value to 32 bits. The\n"
        "          plain (no-`u`) load sign-extends instead.",
    [MODIFIER_I_IMMEDIATE] =
        "immediate form: one operand is a 16-bit constant baked\n"
        "          into the instruction word, not a register. Lets\n"
        "          small constants be supplied without a separate\n"
        "          load.",
    [MODIFIER_V_VARIABLE_SHIFT] =
        "variable shift: the shift count comes from a register\n"
        "          (rs), not the 5-bit `shamt` field. Allows the\n"
        "          count to be computed at runtime.",
    [MODIFIER_AL_AND_LINK] =
        "and link: save the return address (PC+4, or PC+8 with\n"
        "          delayed branches) into $ra. Turns the\n"
        "          branch/jump into a subroutine call.",
    [MODIFIER_W_WORD] =
        "word width: the load/store transfers 32 bits — one full\n"
        "          register's worth.  MIPS is word-aligned, so the\n"
        "          effective address must be a multiple of 4.",
    [MODIFIER_H_HALFWORD] =
        "halfword width: the load/store transfers 16 bits.  The\n"
        "          effective address must be a multiple of 2.",
    [MODIFIER_B_BYTE] =
        "byte width: the load/store transfers 8 bits — the smallest\n"
        "          addressable unit on MIPS.  No alignment\n"
        "          requirement.",
    [MODIFIER_L_LIKELY] =
        "likely: if the branch is not taken, the instruction in the\n"
        "          delay slot is NOT executed (\"nullified\").  The\n"
        "          plain (no-`l`) branch always executes the delay\n"
        "          slot regardless of the branch outcome.  Used by\n"
        "          compilers to fill the delay slot more aggressively.",
};

static void lookup_classification(const mips_instruction* instruction,
                                  instruction_category* category, instruction_modifier modifiers[3],
                                  int* modifier_count) {
  *modifier_count = 0;
  *category = CATEGORY_COUNT; /* default = unknown */
  const char* const from = instruction->expansion_of;
  if (from != nullptr && strcmp(from, "nop") == 0) {
    /* A nop does nothing; the shift it is encoded as is not the point. */
    return;
  } else if (from != nullptr &&
             (strcmp(from, "li") == 0 || strcmp(from, "la") == 0) &&
             (OPCODE(instruction) == TOKEN_LUI_OPCODE ||
              OPCODE(instruction) == TOKEN_ORI_OPCODE)) {
    /* The lui/ori of an li or la builds a constant (an address is one). */
    *category = CATEGORY_CONSTANT;
    return;
  } else {
    /* Classified by its own opcode below. */
  }
  switch (OPCODE(instruction)) {
    /* Arithmetic */
    case TOKEN_ADD_OPCODE:
    case TOKEN_SUB_OPCODE:
    case TOKEN_MUL_OPCODE:
    case TOKEN_MULT_OPCODE:
    case TOKEN_DIV_OPCODE:
    case TOKEN_MFHI_OPCODE:
    case TOKEN_MFLO_OPCODE:
      *category = CATEGORY_ARITHMETIC;
      break;
    case TOKEN_ADDU_OPCODE:
    case TOKEN_SUBU_OPCODE:
    case TOKEN_MULTU_OPCODE:
    case TOKEN_DIVU_OPCODE:
      *category = CATEGORY_ARITHMETIC;
      modifiers[(*modifier_count)++] = MODIFIER_U_UNSIGNED_ARITHMETIC;
      break;
    case TOKEN_ADDI_OPCODE:
      *category = CATEGORY_ARITHMETIC;
      modifiers[(*modifier_count)++] = MODIFIER_I_IMMEDIATE;
      break;
    case TOKEN_ADDIU_OPCODE:
      *category = CATEGORY_ARITHMETIC;
      modifiers[(*modifier_count)++] = MODIFIER_I_IMMEDIATE;
      modifiers[(*modifier_count)++] = MODIFIER_U_UNSIGNED_ARITHMETIC;
      break;

    /* Logical */
    case TOKEN_AND_OPCODE:
    case TOKEN_OR_OPCODE:
    case TOKEN_XOR_OPCODE:
    case TOKEN_NOR_OPCODE:
    case TOKEN_SLL_OPCODE:
    case TOKEN_SRL_OPCODE:
    case TOKEN_SRA_OPCODE:
      *category = CATEGORY_LOGICAL;
      break;
    case TOKEN_ANDI_OPCODE:
    case TOKEN_ORI_OPCODE:
    case TOKEN_XORI_OPCODE:
      *category = CATEGORY_LOGICAL;
      modifiers[(*modifier_count)++] = MODIFIER_I_IMMEDIATE;
      break;
    case TOKEN_SLLV_OPCODE:
    case TOKEN_SRLV_OPCODE:
    case TOKEN_SRAV_OPCODE:
      *category = CATEGORY_LOGICAL;
      modifiers[(*modifier_count)++] = MODIFIER_V_VARIABLE_SHIFT;
      break;

    /* Data transfer */
    case TOKEN_LW_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_W_WORD;
      break;
    case TOKEN_LH_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_H_HALFWORD;
      break;
    case TOKEN_LHU_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_H_HALFWORD;
      modifiers[(*modifier_count)++] = MODIFIER_U_ZERO_EXTEND;
      break;
    case TOKEN_LB_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_B_BYTE;
      break;
    case TOKEN_LBU_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_B_BYTE;
      modifiers[(*modifier_count)++] = MODIFIER_U_ZERO_EXTEND;
      break;
    case TOKEN_SW_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_W_WORD;
      break;
    case TOKEN_SH_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_H_HALFWORD;
      break;
    case TOKEN_SB_OPCODE:
      *category = CATEGORY_DATA_TRANSFER;
      modifiers[(*modifier_count)++] = MODIFIER_B_BYTE;
      break;
    case TOKEN_LUI_OPCODE:
      *category = CATEGORY_CONSTANT;
      break;

    /* Conditional branch */
    case TOKEN_BEQ_OPCODE:
    case TOKEN_BNE_OPCODE:
    case TOKEN_BGEZ_OPCODE:
    case TOKEN_BGTZ_OPCODE:
    case TOKEN_BLEZ_OPCODE:
    case TOKEN_BLTZ_OPCODE:
      *category = CATEGORY_CONDITION_BRANCH;
      break;

    /* Comparison */
    case TOKEN_SLT_OPCODE:
      *category = CATEGORY_COMPARISON;
      break;
    case TOKEN_SLTU_OPCODE:
      *category = CATEGORY_COMPARISON;
      modifiers[(*modifier_count)++] = MODIFIER_U_UNSIGNED_COMPARE;
      break;
    case TOKEN_SLTI_OPCODE:
      *category = CATEGORY_COMPARISON;
      modifiers[(*modifier_count)++] = MODIFIER_I_IMMEDIATE;
      break;
    case TOKEN_SLTIU_OPCODE:
      *category = CATEGORY_COMPARISON;
      modifiers[(*modifier_count)++] = MODIFIER_I_IMMEDIATE;
      modifiers[(*modifier_count)++] = MODIFIER_U_UNSIGNED_COMPARE;
      break;
    case TOKEN_BGEZAL_OPCODE:
    case TOKEN_BLTZAL_OPCODE:
      *category = CATEGORY_CONDITION_BRANCH;
      modifiers[(*modifier_count)++] = MODIFIER_AL_AND_LINK;
      break;
    case TOKEN_BEQL_OPCODE:
    case TOKEN_BNEL_OPCODE:
    case TOKEN_BGEZL_OPCODE:
    case TOKEN_BGTZL_OPCODE:
    case TOKEN_BLEZL_OPCODE:
    case TOKEN_BLTZL_OPCODE:
      *category = CATEGORY_CONDITION_BRANCH;
      modifiers[(*modifier_count)++] = MODIFIER_L_LIKELY;
      break;
    case TOKEN_BGEZALL_OPCODE:
    case TOKEN_BLTZALL_OPCODE:
      *category = CATEGORY_CONDITION_BRANCH;
      modifiers[(*modifier_count)++] = MODIFIER_AL_AND_LINK;
      modifiers[(*modifier_count)++] = MODIFIER_L_LIKELY;
      break;

    /* Unconditional jump */
    case TOKEN_J_OPCODE:
    case TOKEN_JR_OPCODE:
    case TOKEN_JR_HB_OPCODE:
      *category = CATEGORY_UNCONDITIONAL_JUMP;
      break;
    case TOKEN_JAL_OPCODE:
    case TOKEN_JALR_OPCODE:
    case TOKEN_JALR_HB_OPCODE:
      *category = CATEGORY_UNCONDITIONAL_JUMP;
      modifiers[(*modifier_count)++] = MODIFIER_AL_AND_LINK;
      break;

    /* System */
    case TOKEN_SYSCALL_OPCODE:
      *category = CATEGORY_SYSTEM;
      break;
    default:
      /* Any other opcode has no classification: *cat stays CATEGORY_COUNT (unknown).
       */
      break;
  }
}

static void emit_category_preamble(const mips_instruction* instruction) {
  instruction_category category;
  instruction_modifier modifiers[3];
  int modifier_count;
  lookup_classification(instruction, &category, modifiers, &modifier_count);

  if (category == CATEGORY_COUNT) return; /* unknown opcode; skip preamble */

  write_output(message_out, "  Category: %s\n", category_names[category]);
  write_output(message_out, "    %s\n", category_descriptions[category]);

  if (modifier_count > 0) {
    const char* const mnemonic = instruction_opcode_name(instruction);
    write_output(message_out, "  Modifiers in `%s`:\n", mnemonic);
    for (int i = 0; i < modifier_count; i++) {
      const instruction_modifier m = modifiers[i];
      write_output(message_out, "    %s — %s\n", modifier_letters[m],
                   modifier_descriptions[m]);
    }
  }
}

/* The `.hb` forms of jr and jalr: what the hazard barrier is for, and that
   spim, which has no pipeline hazards to clear, runs them as the plain
   jump. */
static void say_hazard_barrier(void) {
  write_output(message_out,
               "    The `.hb` (hazard barrier) form also waits until earlier\n"
               "    changes to processor state have taken effect; spim has no\n"
               "    such hazards, so it runs this as the plain jump.\n");
}

/* render_dispatch: walks the opcode switch and emits each template's
   per-opcode "What it did / Inputs / Wrote / Try it yourself" content.
   Called from explain_after only. Reads inputs from snapshot_registers[], outputs
   from live gpr[]. */
static void render_dispatch(int level, const mips_instruction* instruction) {
  switch (OPCODE(instruction)) {
    /* Arithmetic, R-type */
    case TOKEN_ADD_OPCODE:
      template_r3_arithmetic(level, instruction, "Add",
                   "computed $rs + $rt as a signed sum; trapped on overflow");
      break;
    case TOKEN_ADDU_OPCODE:
      template_r3_arithmetic(level, instruction, "Add Unsigned",
                   "computed $rs + $rt with no overflow trap");
      break;
    case TOKEN_SUB_OPCODE:
      template_r3_arithmetic(
          level, instruction, "Subtract",
          "computed $rs - $rt as a signed difference; trapped on overflow");
      break;
    case TOKEN_SUBU_OPCODE:
      template_r3_arithmetic(level, instruction, "Subtract Unsigned",
                   "computed $rs - $rt with no overflow trap");
      break;
    case TOKEN_AND_OPCODE:
      template_r3_arithmetic(level, instruction, "Bitwise AND",
                   "computed the bitwise AND of $rs and $rt");
      break;
    case TOKEN_OR_OPCODE:
      template_r3_arithmetic(level, instruction, "Bitwise OR",
                   "computed the bitwise OR of $rs and $rt");
      break;
    case TOKEN_XOR_OPCODE:
      template_r3_arithmetic(level, instruction, "Bitwise XOR",
                   "computed the bitwise XOR of $rs and $rt");
      break;
    case TOKEN_NOR_OPCODE:
      template_r3_arithmetic(level, instruction, "Bitwise NOR",
                   "computed NOT($rs OR $rt)");
      break;
    case TOKEN_SLT_OPCODE:
      template_r3_arithmetic(level, instruction, "Set on Less Than (signed)",
                   "set the destination to 1 if $rs < $rt (signed), else 0");
      break;
    case TOKEN_SLTU_OPCODE:
      template_r3_arithmetic(level, instruction, "Set on Less Than (unsigned)",
                   "set the destination to 1 if $rs < $rt (unsigned), else 0");
      break;
    case TOKEN_MUL_OPCODE:
      template_r3_arithmetic(level, instruction, "Multiply (MIPS32)",
                   "computed the low 32 bits of $rs * $rt");
      break;

    /* Arithmetic, I-type */
    case TOKEN_ADDI_OPCODE:
      template_i2_arithmetic(level, instruction, "Add Immediate",
                   "computed $rs + immediate; trapped on overflow");
      break;
    case TOKEN_ADDIU_OPCODE:
      template_i2_arithmetic(level, instruction, "Add Immediate Unsigned",
                   "computed $rs + sign-extended immediate, no overflow trap");
      break;
    case TOKEN_ANDI_OPCODE:
      template_i2_arithmetic(level, instruction, "AND Immediate",
                   "computed $rs AND zero-extended immediate");
      break;
    case TOKEN_ORI_OPCODE:
      template_i2_arithmetic(level, instruction, "OR Immediate",
                   "computed $rs OR zero-extended immediate");
      break;
    case TOKEN_XORI_OPCODE:
      template_i2_arithmetic(level, instruction, "XOR Immediate",
                   "computed $rs XOR zero-extended immediate");
      break;
    case TOKEN_SLTI_OPCODE:
      template_i2_arithmetic(level, instruction, "Set Less Than Immediate (signed)",
                   "set destination to 1 if $rs < imm (signed)");
      break;
    case TOKEN_SLTIU_OPCODE:
      template_i2_arithmetic(level, instruction, "Set Less Than Immediate (unsigned)",
                   "set destination to 1 if $rs < imm (unsigned)");
      break;

    /* LUI is special: I1t-type, only writes high bits. */
    case TOKEN_LUI_OPCODE: {
      const int target_register = TARGET_REGISTER(instruction);
      const short immediate = (short)IMMEDIATE(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Load Upper Immediate — placed the 16-bit immediate "
                   "into the\n"
                   "    upper 16 bits of $%s, zeroing the lower 16. Often "
                   "paired with\n"
                   "    `ori` to construct a full 32-bit constant.\n",
                   integer_register_names[target_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_immediate(immediate);
        write_output(message_out, "  Wrote:\n");
        say_wrote_register(target_register);
        say_try_registers(target_register, 0, 0, 0);
      }
      break;
    }

    /* Shifts */
    case TOKEN_SLL_OPCODE:
      if (DESTINATION_REGISTER(instruction) == 0 && TARGET_REGISTER(instruction) == 0 &&
          SHIFT_AMOUNT(instruction) == 0) {
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
          say_try_registers(0, 0, 0, 0);
        }
      } else {
        template_shift(level, instruction, "Shift Left Logical",
                  "shifted $rt left, filling with 0");
      }
      break;
    case TOKEN_SRL_OPCODE:
      template_shift(level, instruction, "Shift Right Logical",
                "shifted $rt right, filling with 0");
      break;
    case TOKEN_SRA_OPCODE:
      template_shift(level, instruction, "Shift Right Arithmetic",
                "shifted $rt right, filling with the sign bit");
      break;

    /* Loads */
    case TOKEN_LW_OPCODE:
      template_load(level, instruction, "Load Word", "a 32-bit word", 4);
      break;
    case TOKEN_LB_OPCODE:
      template_load(level, instruction, "Load Byte (signed)",
               "one byte, sign-extended", 1);
      break;
    case TOKEN_LBU_OPCODE:
      template_load(level, instruction, "Load Byte Unsigned",
               "one byte, zero-extended", 1);
      break;
    case TOKEN_LH_OPCODE:
      template_load(level, instruction, "Load Halfword (signed)",
               "16 bits, sign-extended", 2);
      break;
    case TOKEN_LHU_OPCODE:
      template_load(level, instruction, "Load Halfword Unsigned",
               "16 bits, zero-extended", 2);
      break;

    /* Stores */
    case TOKEN_SW_OPCODE:
      template_store(level, instruction, "Store Word", "the 32-bit value", 4);
      break;
    case TOKEN_SB_OPCODE:
      template_store(level, instruction, "Store Byte", "the low 8 bits", 1);
      break;
    case TOKEN_SH_OPCODE:
      template_store(level, instruction, "Store Halfword", "the low 16 bits", 2);
      break;

    /* Unaligned word loads/stores */
    case TOKEN_LWL_OPCODE:
      template_load_unaligned(level, instruction, "Load Word Left",
                         "the high bytes of the addressed word");
      break;
    case TOKEN_LWR_OPCODE:
      template_load_unaligned(level, instruction, "Load Word Right",
                         "the low bytes of the addressed word");
      break;
    case TOKEN_SWL_OPCODE:
      template_store_unaligned(level, instruction, "Store Word Left",
                          "the high bytes of $rt");
      break;
    case TOKEN_SWR_OPCODE:
      template_store_unaligned(level, instruction, "Store Word Right",
                          "the low bytes of $rt");
      break;

    /* Atomic primitives — degraded to plain load/store in single-
       threaded spim, but the narration explains the real semantics. */
    case TOKEN_LL_OPCODE:
      template_load_linked(level, instruction);
      break;
    case TOKEN_SC_OPCODE:
      template_store_conditional(level, instruction);
      break;

    /* Coprocessor-1 (floating-point) loads/stores */
    case TOKEN_LWC1_OPCODE:
      template_load_floating_point(level, instruction, "Load Word Coprocessor 1", 4);
      break;
    case TOKEN_LDC1_OPCODE:
      template_load_floating_point(level, instruction, "Load Double Coprocessor 1", 8);
      break;
    case TOKEN_SWC1_OPCODE:
      template_store_floating_point(level, instruction, "Store Word Coprocessor 1", 4);
      break;
    case TOKEN_SDC1_OPCODE:
      template_store_floating_point(level, instruction, "Store Double Coprocessor 1", 8);
      break;

    /* Branches. The comparison value is read from snapshot_registers[] since by the
       time the template runs, an instruction like `beq $t0, $t0, ...`
       has already executed (though it would not have modified rs/rt
       anyway). */
    case TOKEN_BEQ_OPCODE:
      template_branch_2register(level, instruction, "Branch if Equal",
                      "==", snapshot_registers[SOURCE_REGISTER(instruction)] == snapshot_registers[TARGET_REGISTER(instruction)]);
      break;
    case TOKEN_BNE_OPCODE:
      template_branch_2register(level, instruction, "Branch if Not Equal",
                      "!=", snapshot_registers[SOURCE_REGISTER(instruction)] != snapshot_registers[TARGET_REGISTER(instruction)]);
      break;
    case TOKEN_BGEZ_OPCODE:
      template_branch_1register(level, instruction, "Branch if Greater or Equal Zero",
                      ">=", (register_word)snapshot_registers[SOURCE_REGISTER(instruction)] >= 0);
      break;
    case TOKEN_BGTZ_OPCODE:
      template_branch_1register(level, instruction, "Branch if Greater Than Zero", ">",
                      (register_word)snapshot_registers[SOURCE_REGISTER(instruction)] > 0);
      break;
    case TOKEN_BLEZ_OPCODE:
      template_branch_1register(level, instruction, "Branch if Less or Equal Zero",
                      "<=", (register_word)snapshot_registers[SOURCE_REGISTER(instruction)] <= 0);
      break;
    case TOKEN_BLTZ_OPCODE:
      template_branch_1register(level, instruction, "Branch if Less Than Zero", "<",
                      (register_word)snapshot_registers[SOURCE_REGISTER(instruction)] < 0);
      break;

    /* Jumps. PC change is captured by the side-effect fallback at the
       end of explain_after, so per-jump templates don't repeat it. */
    case TOKEN_J_OPCODE: {
      const memory_address target = jump_target(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump — jumped unconditionally to 0x%08x.\n", target);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n    (none)\n");
        say_try_registers(0, 0, 0, 0);
      }
      break;
    }
    case TOKEN_JAL_OPCODE: {
      const memory_address target = jump_target(instruction);
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
        say_wrote_register(31);
        say_try_registers(31, 0, 0, 0);
      }
      break;
    }
    case TOKEN_JR_OPCODE:
    case TOKEN_JR_HB_OPCODE: {
      const int source_register = SOURCE_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump Register — jumped to the address held in $%s.\n"
                   "    (Used to return from a subroutine when $rs == $ra.)\n"
                   "    Destination = 0x%08x.\n",
                   integer_register_names[source_register], (memory_address)snapshot_registers[source_register]);
      if (OPCODE(instruction) == TOKEN_JR_HB_OPCODE) say_hazard_barrier();
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        say_try_registers(source_register, 0, 0, 0);
      }
      break;
    }
    case TOKEN_JALR_OPCODE:
    case TOKEN_JALR_HB_OPCODE: {
      const int destination_register = DESTINATION_REGISTER(instruction), source_register = SOURCE_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Jump and Link Register — saved the return address "
                   "into $%s,\n    then jumped to the address in $%s "
                   "(0x%08x).\n",
                   integer_register_names[destination_register], integer_register_names[source_register], (memory_address)snapshot_registers[source_register]);
      if (OPCODE(instruction) == TOKEN_JALR_HB_OPCODE) say_hazard_barrier();
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        write_output(message_out, "  Wrote:\n");
        say_wrote_register(destination_register);
        say_try_registers(source_register, destination_register, 0, 0);
      }
      break;
    }

    /* HI / LO */
    case TOKEN_MFHI_OPCODE: {
      const int destination_register = DESTINATION_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Move From HI — copied the special HI register into "
                   "$%s.\n"
                   "    HI holds the high half of a multiplication, or the "
                   "remainder of a division.\n",
                   integer_register_names[destination_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    HI = 0x%08x\n", snapshot_multiply_divide_high);
        write_output(message_out, "  Wrote:\n");
        say_wrote_register(destination_register);
        say_try_registers(destination_register, 0, 0, 0);
      }
      break;
    }
    case TOKEN_MFLO_OPCODE: {
      const int destination_register = DESTINATION_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Move From LO — copied the special LO register into "
                   "$%s.\n"
                   "    LO holds the low half of a multiplication, or the "
                   "quotient of a division.\n",
                   integer_register_names[destination_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        write_output(message_out, "    LO = 0x%08x\n", snapshot_multiply_divide_low);
        write_output(message_out, "  Wrote:\n");
        say_wrote_register(destination_register);
        say_try_registers(destination_register, 0, 0, 0);
      }
      break;
    }
    case TOKEN_MULT_OPCODE: {
      const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Multiply (signed) — computed $%s * $%s as a 64-bit "
                   "result.\n"
                   "    The high 32 bits went to HI; the low 32 bits went to "
                   "LO.\n"
                   "    Use mfhi / mflo to retrieve them.\n",
                   integer_register_names[source_register], integer_register_names[target_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        say_input_register(target_register);
        write_output(message_out, "  Wrote:\n");
        say_wrote_multiply_divide_high();
        say_wrote_multiply_divide_low();
        say_try_registers(source_register, target_register, 0, 0);
      }
      break;
    }
    case TOKEN_DIV_OPCODE: {
      const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Divide (signed) — computed $%s / $%s.\n"
                   "    Quotient went to LO; remainder went to HI.\n",
                   integer_register_names[source_register], integer_register_names[target_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        say_input_register(target_register);
        write_output(message_out, "  Wrote:\n");
        say_wrote_multiply_divide_high();
        say_wrote_multiply_divide_low();
        say_try_registers(source_register, target_register, 0, 0);
      }
      break;
    }
    case TOKEN_MULTU_OPCODE: {
      const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Multiply Unsigned — computed $%s * $%s as a 64-bit "
                   "unsigned result.\n"
                   "    The high 32 bits went to HI; the low 32 bits went to "
                   "LO.\n"
                   "    Use mfhi / mflo to retrieve them. No overflow trap.\n",
                   integer_register_names[source_register], integer_register_names[target_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        say_input_register(target_register);
        write_output(message_out, "  Wrote:\n");
        say_wrote_multiply_divide_high();
        say_wrote_multiply_divide_low();
        say_try_registers(source_register, target_register, 0, 0);
      }
      break;
    }
    case TOKEN_DIVU_OPCODE: {
      const int source_register = SOURCE_REGISTER(instruction), target_register = TARGET_REGISTER(instruction);
      write_output(message_out, "  What it did:\n");
      write_output(message_out,
                   "    Divide Unsigned — computed $%s / $%s, treating both "
                   "operands as unsigned.\n"
                   "    Quotient went to LO; remainder went to HI.\n",
                   integer_register_names[source_register], integer_register_names[target_register]);
      if (level >= 2) {
        write_output(message_out, "  Inputs (before this step):\n");
        say_input_register(source_register);
        say_input_register(target_register);
        write_output(message_out, "  Wrote:\n");
        say_wrote_multiply_divide_high();
        say_wrote_multiply_divide_low();
        say_try_registers(source_register, target_register, 0, 0);
      }
      break;
    }

    /* Syscall */
    case TOKEN_SYSCALL_OPCODE:
      explain_syscall(level);
      break;

    default:
      write_output(message_out,
                   "  (no detailed explanation for this opcode yet — "
                   "see the disassembly above)\n");
      break;
  }
}

/* explain_after runs immediately after the dispatch switch (and PC
   advance) in run_spim. This is where the narration actually emits —
   by the time the user sees it, everything described has already
   happened, so we can use past tense throughout. */
void explain_after(const mips_instruction* instruction) {
  if (explain_level == 0) return;
  const int level = explain_level;

  /* Reset the "touched by template" tracking so the side-effect
     fallback below knows what's already been named. */
  memset(touched_register, 0, sizeof touched_register);
  touched_multiply_divide_high = false;
  touched_multiply_divide_low = false;

  explain_print_step_header(snapshot_program_counter, instruction);

  /* L3 shows a single bit-field diagram; L4 expands that into the
     CPU's hierarchical decoding sequence (multiple frames, same shape,
     fields filling in progressively). The L4 final frame equals the
     L3 box, so we don't show both. */
  if (level == 3) {
    explain_bit_layout(instruction);
  } else if (level >= 4) {
    explain_decoding_steps(instruction);
  } else {
    /* Below level 3 there is no bit layout. */
  }

  /* Pseudo-op header / continuation hint, from the assembler's record of
   * what this instruction was emitted for: a header on the first
   * instruction of an expansion, a continuation hint on the others. */
  if (instruction->expansion_of != nullptr) {
    const char* const name = instruction->expansion_of;
    const int index = instruction->expansion_index;
    const int count = instruction->expansion_count;
    const bool pseudo = instruction->expansion_of_pseudo;
    const char* const l1_label = pseudo ? "Pseudo-op" : "Expanded";
    const struct pseudo_info* const p = pseudo ? find_pseudo(name) : nullptr;
    if (index > 1) {
      if (level >= 2) {
        write_output(message_out,
                     "  (continuation of the `%s` expansion above — same "
                     "source line,\n   this was real instruction %d of %d)\n",
                     name, index, count);
      } else {
        /* L1 compact form of the continuation hint. */
        write_output(message_out, "  %s: `%s` (continuation, %d of %d)\n",
                     l1_label, name, index, count);
      }
    } else if (level >= 2) {
      /* Full block: header + description + footer. */
      if (p != nullptr) {
        write_output(message_out,
                     "  Pseudo-instruction `%s` (as written in source):\n"
                     "    %s\n",
                     name, p->what_it_means);
      } else if (pseudo) {
        write_output(message_out,
                     "  Pseudo-instruction `%s` (as written in source).\n",
                     name);
      } else {
        write_output(message_out,
                     "  `%s` (as written in source) needs more than one real "
                     "instruction:\n"
                     "    its operand does not fit in one, so the assembler "
                     "expanded the line.\n",
                     name);
      }
      if (count == 1) {
        write_output(message_out,
                     "  This was the single real instruction the assembler "
                     "emitted for it.\n");
      } else {
        write_output(message_out,
                     "  This was the first of the %d real instructions the "
                     "assembler emitted for it.\n",
                     count);
      }
    } else if (count == 1) {
      /* L1 compact form: single line, no verbose description.  Keeps
         every L1 instruction block roughly uniform in height while
         still signaling "you wrote a pseudo-op here." */
      write_output(message_out, "  %s: `%s` (single real instruction)\n",
                   l1_label, name);
    } else {
      write_output(message_out, "  %s: `%s` (first of %d real instructions)\n",
                   l1_label, name, count);
    }
  } else {
    /* Written as itself: this instruction gets no pseudo-op header. */
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

    /* Pad the "Try it yourself" block to a 3-line minimum if the
       template opened one.  No-op for L1 (no try block at all) and
       for templates whose specific suggestions already reach the
       minimum. */
    say_try_finish();

    /* Load destination annotation: if a load completed but its
       destination register value didn't change (loaded the same value
       already there), the say_wrote_register line shows "0xV → 0xV" which
       is technically true but easy to misread as "nothing happened."
       Annotate explicitly. */
    switch (OPCODE(instruction)) {
      case TOKEN_LW_OPCODE:
      case TOKEN_LB_OPCODE:
      case TOKEN_LBU_OPCODE:
      case TOKEN_LH_OPCODE:
      case TOKEN_LHU_OPCODE: {
        const int target_register = TARGET_REGISTER(instruction);
        if (target_register != 0 && snapshot_registers[target_register] == general_purpose_registers[target_register]) {
          write_output(message_out,
                       "    (the load did happen — the memory value matched "
                       "the prior register value)\n");
        }
        break;
      }
      default:
        /* Only loads get the unchanged-destination note. */
        break;
    }

    /* Side-effect fallback: anything that changed but wasn't named by
       the template. Captures unexpected mutations (e.g. exception
       paths, templates that don't yet cover all of an op's writes). */
    bool header_done = false;
    for (int i = 1; i < GENERAL_PURPOSE_REGISTER_COUNT; i++) {
      if (general_purpose_registers[i] != snapshot_registers[i] && !touched_register[i]) {
        if (!header_done) {
          write_output(message_out,
                       "  Side effects (changed but not named above):\n");
          header_done = true;
        }
        write_output(message_out,
                     "    $%s:  0x%08x  →  0x%08x   (decimal %d)\n",
                     integer_register_names[i], snapshot_registers[i], general_purpose_registers[i], general_purpose_registers[i]);
      }
    }
    if (MULTIPLY_DIVIDE_HIGH != snapshot_multiply_divide_high && !touched_multiply_divide_high) {
      if (!header_done) {
        write_output(message_out,
                     "  Side effects (changed but not named above):\n");
        header_done = true;
      }
      write_output(message_out, "    HI:  0x%08x  →  0x%08x\n", snapshot_multiply_divide_high, MULTIPLY_DIVIDE_HIGH);
    }
    if (MULTIPLY_DIVIDE_LOW != snapshot_multiply_divide_low && !touched_multiply_divide_low) {
      if (!header_done) {
        write_output(message_out,
                     "  Side effects (changed but not named above):\n");
        header_done = true;
      }
      write_output(message_out, "    LO:  0x%08x  →  0x%08x\n", snapshot_multiply_divide_low, MULTIPLY_DIVIDE_LOW);
    }

    /* PC always advances; print it last. */
    write_output(message_out, "  PC:  0x%08x  →  0x%08x\n", snapshot_program_counter, PROGRAM_COUNTER);
  }

  write_output(message_out, "\n");
}
