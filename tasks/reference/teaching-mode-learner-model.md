# Teaching mode (`-explain`) — the learner model and design principles

**Status:** draft design, written 2026-11-05 for the maintainer's review. It describes the *target* teaching
mode; the code does not follow it yet. `tasks/explain-levels-redesign.md` brings the code into line, and
`tasks/explain-mode-correctness-bugs.md` fixes the outright bugs first. Once the redesign is done, this doc
describes the shipped behaviour and the "Status" line goes.

Modelled on hanoi's two UX reference docs (github.com/billsix/towersofhanoi
`tasks/reference/learner-knowledge-states.md` and `ui-design-principles.md`): describe what the learner
**knows**, not what screens exist; teach one thing at a time; never use a word before it is taught.

## Why model the learner, not the output

The teaching mode is a first-class feature (`CLAUDE.md`, Conventions). The audit on 2026-11-05 found that
the levels had been designed as *amounts of output* (L1 a header, L2 more sections, L3 a bit box, L4 decode
frames). Each level therefore used terms it never taught, and the jump between levels mixed two unrelated
subjects: what an instruction *means*, and how it is *encoded*. Starting from what the learner knows gives a
test that each message must pass: **no message at level N uses a term first taught above level N.**

## Who the learner is

Someone learning MIPS assembly from this project's examples (`examples/READING-ORDER.md`) or the MIPS port
of *Programming from the Ground Up* (`pgu/`). They may know a high-level language (Java, Python, maybe C).
They don't know how a machine runs a program. They run `spimulator -explain FILE.s` and read the narration
alongside the `.asm` source.

## Knowledge states

| State | The learner knows | Introduced by |
|---|---|---|
| **K0** | Maybe a high-level language. Nothing about the machine. | — |
| **K1** | A program is a list of instructions in memory. The PC says which runs next. Registers are named boxes holding numbers. Addresses are written in hex. | L1 |
| **K2** | Register roles: `$zero` is always 0; `$v0`/`$a0` carry syscall codes and arguments; `$at` belongs to the assembler; `$ra` holds the return address. Pseudo-instructions are shorthand the assembler expands into real ones. Labels are addresses. Memory has `.text`, `.data` and a stack. A startup stub runs before `main` and after it returns. | L1 (pseudo-op expansion, startup marking) and L2 (register roles on first use) |
| **K3** | Base + offset addressing; alignment; word/half/byte sizes; sign vs zero extension; stack frames; HI/LO; overflow traps; branch-and-link. | L2 (effective address) and L3 |
| **K4** | Instruction formats R/I/J, their fields, and the opcode/funct tables. | the separate encoding view |
| **K5** | Branch delay slots; exceptions and the kernel; coprocessor 0 (`-bare`). | L3 + the encoding view |

## The levels: two separate axes

**Narration depth** (`-explain=1..3`; bare `-explain` means 1) answers "what happened and why". **The
encoding view** (`-encoding`, `-encoding=decode`, and the REPL command `explain [ADDR]`, which decodes one
instruction without running it) answers "how are these bits laid out". Encoding is opt-in because a field box
on every one of a loop's 165 steps is noise; a learner asks for it when curious.

| Level | Moves the learner | Shows |
|---|---|---|
| **L1 "Trace"** | K0 → K1, and the pseudo-op part of K2 | The source line. One plain sentence of what the instruction did. The registers and memory that changed (decimal first, hex beside). A pseudo-instruction shown as its real expansion (`li $v0, 1` became `ori $v0, $zero, 1`). Startup and exit code labelled as such. An exception or the program's exit, with its status, in plain words. |
| **L2 "Explain"** | K1 → K2/K3 | Everything in L1, plus: the inputs read; the effective-address arithmetic for loads and stores; the syscall table entry; `$at`/`$zero`/`$ra` explained the first time they appear; the instruction's category and the meaning of its letters (`u`, `i`, …) in full on first use, then by short name; a "try it yourself" hint only when one fits. |
| **L3 "Machine"** | K3 → K5 | Everything in L2, plus: extension, trap, alignment and delay-slot details; HI/LO; stack-frame notes; textbook (COD) section citations. |

Each level is a strict superset of the one below. Something taught in full once is shortened afterwards.

## Wording principles

1. **One spelling per thing.** Registers use ABI names (`$zero`, never `$r0`/`$0` in narration). One word
   per concept across all levels and docs.
2. **Hex for addresses, decimal + hex for values.**
3. **Meaning before mechanism.** What the instruction did comes first. Inputs come next. Encoding comes last,
   and only when asked for.
4. **Say what really happened.** A pseudo-instruction's description comes from the assembler's actual
   expansion (the AST behind `-show-expansion`), not a fixed table that can disagree with it. A branch's
   narrated target equals the PC that actually comes next.
5. **Never go quiet on failure.** An exception and the program's exit status are always narrated, at every
   level. A narration level must never change the program's output or exit status.
6. **A glossary of term → introducing level**, checked automatically against the message templates (see
   Testing).
7. **One wrap width** for every message.

## Testing contract

- **Short golden files per level and per concept:** pseudo-op expansion, load/store, branch taken and not
  taken, syscall, exception, exit. Run under `env -i` so the environment can't leak in.
- **Property checks across all levels:**
  - stdout (minus narration) and exit status are identical at L0–L3;
  - a taken branch's narrated target equals the next PC;
  - the narrated expansion equals the emitted instructions;
  - each level's section set contains the level below's;
  - no template uses a glossary term above its level.

## Related

- `src/explain.c` (`explain_before`, `explain_after`, `lookup_classification`, `render_dispatch`, the
  `tpl_*` templates, `say_try*`); hooks in `src/run.c` `run_spim`; flag parsing in `src/spim.c`.
- History: `tasks/archive/2026/05/23/teaching-mode.md`,
  `tasks/archive/2026/07/07/explanation-levels-and-completion.md`,
  `tasks/archive/2026/07/07/explanation-level-4-decoding.md`,
  `tasks/archive/2026/05/23/explain-mode-consistency-audit.md`.
- The other teaching surfaces (`-print-ast`, `-show-expansion`, `-listing`): `examples/TEACHING-ASSEMBLER-INTERNALS.md`.
