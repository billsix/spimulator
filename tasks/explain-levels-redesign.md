# Redesign the teaching-mode levels around what the learner knows

**Status:** proposed — needs go-ahead (open questions below)
**Priority:** 4
**Difficulty:** 8
**Created:** 2026-11-05 (William Emerison Six <billsix@gmail.com>)
**Depends on:** `tasks/archive/2026/11/11/explain-mode-correctness-bugs.md` (fix the bugs first)

## BLUF

The maintainer felt the four `-explain` levels "weren't done all that well". An audit (2026-11-05) agreed and
found why: the levels grow by *amount of output*, not by *what the learner knows*. L1 is barely more than
plain stepping; L2 assumes terms it never teaches; L3/L4 switch subject to bit encoding. This task rebuilds
the levels around the learner model in `tasks/reference/teaching-mode-learner-model.md`, the same approach
hanoi used. Done means:

- narration levels 1–3 match that doc's table;
- encoding is its own opt-in view, plus a REPL `explain [ADDR]` command;
- every message passes the "no term above its level" check;
- per-level, per-concept goldens and the cross-level property checks pass;
- the man page, usage text, `CLAUDE.md` and the pgu chapter describe it identically;
- that reference doc drops its "draft" status.

## Context (read first)

1. `tasks/reference/teaching-mode-learner-model.md`: knowledge states K0–K5, the two-axis level design, the
   wording principles, the testing contract. This task implements that doc.
2. hanoi's model docs, which this copies in approach: github.com/billsix/towersofhanoi
   `tasks/reference/learner-knowledge-states.md` and `ui-design-principles.md`.
3. The code:
   - `src/explain.c`: `explain_before`, which snapshots registers/HI/LO/PC and prints the full listing once;
     and `explain_after`, which prints the step header, the bit layout (L3) or decode steps (L4), the
     pseudo-op header, the category preamble, `render_dispatch` → `tpl_*`/`explain_syscall`,
     `say_try_finish`, and the PC line.
   - Flag parsing in `src/spim.c`: `-explain=N` / `-x=N` with N in 0..4; bare `-explain` means 3;
     `-noexplain`.
   - Hooks in `src/run.c` `run_spim`.
   - Tab completion reads the Try-it suggestions through `explain_suggestion*()`.
4. Design history: `tasks/archive/2026/05/23/teaching-mode.md` (original);
   `tasks/archive/2026/07/07/explanation-levels-and-completion.md` (L1 was meant to say what the instruction
   did and what changed, but the code drifted to header-only); `.../explanation-level-4-decoding.md`;
   `tasks/archive/2026/05/23/explain-mode-consistency-audit.md` (measured line counts only).

## What the audit found about the levels (as of 2026-11-05, after the bug list)

**What each level printed** (`print1through10`, 165 steps; L1/L2/L3/L4 printed 1,316 / 4,731 / 6,876 /
8,839 lines, so L2 is about 29 lines per instruction):

- **L1:** only `Stepped at PC`, `memory[pc] = 0xENC → disasm`, `source line N`, and a pseudo-op line. It
  never says what the instruction did or what changed, which is the main thing a beginner needs.
- **L2:** adds category + COD citation, modifier glossary, "What it did", inputs, "Wrote:", a Try-it block,
  and the PC line.
- **L3:** L2 plus a hex/binary field box, placed *before* the meaning.
- **L4:** replaces the L3 box with 2–3 decode frames.

**Problems with the progression:**

- Bare `-explain` drops a beginner at L3 bit boxes.
- Categories and modifier glossaries print in full on every step (142 COD citations and 105 modifier
  blocks in one run). The code comment says "first-time full, then short", which is stale.
- The Try-it block is padded to 3 lines with generic `print_all_regs`/`step`/`continue`.
- Unchanged writes (`$v0: 0x4 → 0x4`) are flagged only for loads.
- Stack addresses also get a decimal form (2147483596).
- Wrap width varies between about 70 and over 100 columns.

**Knowledge used before it is taught:**

- **L1:** hex, PC, machine code, `$r0` (the legend promises ABI names; `$r0`, `$zero` and `$0` all appear),
  `lui 4097` in decimal where the address is 0x1001…, and `$at`. The 6 startup instructions before `main` and
  4 after it (`__start` in `exceptions.s`) are never identified as startup code.
- **The legend** teaches `$rs/$rt/$rd/shamt` at L1, where they are never used.
- **L2** assumes sign/zero extension, overflow traps, effective address, alignment, HI/LO, "kernel", the
  COD textbook, and delay slots (`jal` is glossed "PC+8 with delayed branches").

## Plan

1. **Glossary first.** Add a term → introducing-level table (in the reference doc and as data the tests
   read). Inventory every term the templates use, assign each to a K-state, and decide each term's
   one-sentence first-use explanation.
2. **Split the axes.**
   - `-explain=1..3` is narration depth; bare `-explain` means 1.
   - Add `-encoding` / `-encoding=decode`, carrying today's L3 box and L4 frames unchanged.
   - Add a REPL `explain [ADDR]` command, the deferred `why`: it decodes one instruction without running it.
   - Map the old flags as in open question 2.
3. **Rewrite L1 ("Trace")**, the biggest change:
   - one sentence of what happened, then the changed registers and memory;
   - the pseudo-op's real expansion, read from the assembler's AST (the data behind `-show-expansion`) rather
     than the fixed table;
   - startup and exit code marked;
   - exceptions and the exit status narrated in plain words.
4. **Rework L2 ("Explain")**:
   - inputs and effective-address arithmetic;
   - the syscall table entry;
   - register roles on first use;
   - category and modifiers in full on first use only, then a short form;
   - Try-it only when relevant.
5. **Move the K3–K5 material to L3 ("Machine")**: extension, traps, alignment, delay slots, HI/LO, frame
   notes, COD citations.
6. **Apply the wording principles throughout:**
   - one register spelling;
   - hex addresses, decimal + hex values;
   - meaning before mechanism;
   - one wrap width.
7. **Tests**, per the reference doc's testing contract:
   - per-level, per-concept goldens under `env -i`;
   - properties: identical program output and exit status across levels, branch target equals the next PC,
     narrated expansion equals the emitted instructions, monotonic sections, and the template-vs-glossary
     lint;
   - replace the single 4,716-line L2 golden with the small ones.
8. **Docs:**
   - `Documentation/spimulator.1`, the usage text, `CLAUDE.md` (its teaching-mode bullet);
   - `pgu/docs/source/examplesap.rst` ("Reading -explain output"), `examples/TEACHING-ASSEMBLER-INTERNALS.md`,
     `examples/READING-ORDER.md`;
   - a `ChangeLog` entry, since the flags change.

   Then drop the reference doc's "draft" status.
9. **Ports** (added 2026-11-11): the new `-encoding` flag and the REPL `explain` command are part of
   the command-line and REPL surface every port mirrors; add them to each port, or make each port
   reject them the way C rejects an unknown flag, until `tasks/engine-ports-teaching-mode.md` ports the
   narration (`CLAUDE.md`: "A C behavior change must be followed in every port"; the port suites (`port-python`, `port-haskell`, `port-rust`) fail until each port matches).
10. **Gates:** `meson test` (line editing enabled and disabled), a nested `make image` (including the
   sanitizer gate), and `BUILD_TREE_SITTER=1` if any `opcodes.tsv` column is touched.

## Open questions

1. Adopt the learner model and the two-axis design in `tasks/reference/teaching-mode-learner-model.md`
   (narration L1–3 plus a separate encoding view)? Recommendation: yes. The audit found the
   meaning-vs-encoding mix is the root cause.
2. How should the old `-explain=3`/`-explain=4` behave once encoding is separate: (a) keep them as aliases
   for `-explain=3 -encoding` and `-explain=3 -encoding=decode`, or (b) make them an error that names the new
   flags? Recommendation: (a), so existing pgu chapters and scripts keep working.
3. Should the default level for bare `-explain` change from 3 to 1? This changes what existing users see.
   Recommendation: yes, because a beginner is the target.
4. Build the REPL `explain [ADDR]` command in this task, or split it into its own task? Recommendation: in
   this task. It is where the encoding view naturally lives, and it lets L1–3 drop encoding without losing it.
