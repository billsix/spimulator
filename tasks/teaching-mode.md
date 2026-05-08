# Teaching Mode (`-explain`)

Status: **MVP implemented (untested)**. Build with the project's normal
meson flow; the user is expected to run the actual build out-of-container.

## Implementation summary (what shipped)

- New CLI flags `-explain` / `-x` (and the negated `-noexplain` / `-nx`)
  parsed in `src/spim.c`, declared globally in `src/explain.c` as
  `bool explain_mode`.
- Usage string in `src/spim.c` updated.
- New files: `include/explain.h`, `src/explain.c`. `meson.build` updated.
- `src/run.c` calls `explain_before(inst, PC)` immediately after
  `DO_DELAYED_UPDATE()` and `explain_after(inst)` immediately after
  `PC += BYTES_PER_WORD`. Both no-op when `explain_mode` is false, so
  default behavior is unchanged.
- Snapshot covers all GPRs, HI, LO, and PC; the "After execution" diff
  prints any GPR/HI/LO that changed, plus PC always.
- Memory peeks for load/store narration go through `peek_word` /
  `peek_half` / `peek_byte` which save/restore `exception_occurred` and
  `CP0_BadVAddr` so a bad-address peek can't taint the real dispatch.
- Per-opcode templates implemented for all MVP opcodes from the list below.
- Pseudo-op source line (`SOURCE(inst)`) is shown via "As written in source"
  when `accept_pseudo_insts` is on and the source line is present.
- `syscall` has `$v0`-aware sub-explanations for syscalls 1, 4, 5, 8, 10, 11.
  Other syscall numbers fall back to a generic message.
- Smoke test: `tests/tt.explain.s` (no golden file yet — see "deferred").

### Open issues / follow-ups (found during first end-to-end test, 2026-05-08)

These are confirmed by running `spimulator -explain -f tests/tt.explain.s`
and reviewing the captured output. None block the MVP; pick up next pass.

1. **Disassembly column shows `nop` for every shift.** ✓ Fixed 2026-05-11
   (see follow-up #16 in `teaching-mode-coverage.md`).
   Pre-existing spimulator bug in `format_an_inst` (`src/inst.c:649-654`):
   the `R2sh_TYPE_INST` case prints "nop" when `ENCODING(inst) == 0`, but
   `ENCODING` is `0x00000000` for *every* instruction assembled from source
   (the assembler never populates it — visible as the all-zero hex column
   on every disassembly line). Result: `sll $t4, $t2, 2` shows up as
   "nop" in the disassembly even though it really executes as a shift.

   Correct check: `RD(inst) == 0 && RT(inst) == 0 && SHAMT(inst) == 0`
   (matches what the teaching-mode `Y_SLL_OP` case now uses, after the
   first round of fixes). Same fix would also explain why the encoding
   column is uniformly `0x00000000` and might motivate populating it for
   real, but that's out of scope for this issue.

   Scope: `src/inst.c` only, ~3 lines. Independent of teaching mode but
   makes the `-explain` output less confusing because the disassembly
   line will then agree with the explanation below it.

2. **Multi-instruction pseudo-op expansions lose source attribution on
   the second half.** ✓ Fixed 2026-05-11 (see Gap 2 status update in
   `teaching-mode-coverage.md`). When the assembler expands `la $a0, hello` into
   `lui $at, 0x1001` + `ori $a0, $at, 8`, only the first instruction
   carries `source_line`. The second appears with no `; <src>` comment
   and no pseudo-op hint, so the student sees a mysterious `ori` follow
   a `lui`. Same thing happens for `lw $t5, src` (→ `lui` + `lw`) and
   `sw $t2, dst` (→ `lui` + `sw`).

   Possible fix: in `explain_before`, when an instruction has no
   `source_line` AND the previous instruction was a `lui $at, X` whose
   source was a pseudo-op or a symbol-relative load/store, emit a
   "(continuation of the `<la|lw|sw>` expansion above)" hint. Needs to
   track the previous instruction's metadata across calls.

   Lower priority — a teacher in the loop can explain it. But it'd be
   nicer for self-study.

3. **First `lw` reports "(no register changes)" when the loaded value
   equals the previous register value.** ✓ Fixed 2026-05-11
   (see follow-up #15 in `teaching-mode-coverage.md`). In the test run, `lw $a0, 0($sp)`
   loads argc (0x00000001) into `$a0`, but `$a0` already held 0x00000001
   from spim's startup, so the diff sees no change and prints
   "(no register changes)". Technically correct, pedagogically wrong —
   the load *did* happen.

   Fix: in `explain_after`, if the just-executed instruction is a load
   (use `opcode_is_load_store(OPCODE(inst))` plus a check that it's a
   load not a store), always print the destination register and its
   value with a "(loaded from memory)" suffix even if the value is
   unchanged. Probably easiest to do this in the per-opcode template
   (`tpl_load`) by stashing the destination, or add a small "post-hint"
   API parallel to the snapshot.

   Scope: `src/explain.c` only.

### Deferred from the original spec

- **Runtime REPL toggle (`explain` / `noexplain` commands).** Skipped for
  now because it requires touching `src/scanner.l` and `src/parser.y`,
  which regenerates flex/bison output. The `-noexplain` CLI flag is
  there for invocation-time control. Add later if needed.
- **Golden expected-output file (`tests/tt.explain.expected`).**
  Output formatting may still change; pin it once the format settles.
- **Stretch items unchanged** (FP / CP0 instruction explanations, color
  output, `why` REPL command).

### Files actually touched

- `src/spim.c` — flag parsing (around the `-quiet` block) and usage string.
- `src/run.c` — added the two hook calls, plus `#include "explain.h"`.
- `src/explain.c` — new (~570 lines).
- `include/explain.h` — new.
- `meson.build` — added `src/explain.c` to `source_files`.
- `tests/tt.explain.s` — new smoke test.

---

## Original design (kept for reference)

## Goal

spimulator is used as a teaching tool for people learning MIPS assembly for
the first time. Today, when a student types `step`, the simulator silently
executes one instruction and prints `(spim) ` again. The student has to
already know:

- what the instruction does,
- which registers/memory addresses it reads and writes,
- which spim REPL commands will let them inspect those values.

That is precisely what they're trying to learn. We want a CLI flag that turns
the simulator into an explainer: before each step it narrates the upcoming
instruction in plain English, names the relevant inputs and shows their
current values, and tells the student exactly what to type to inspect those
values themselves. After the step, it shows what changed.

## User experience

Invoke with a new flag:

```
spimulator -explain -f helloworld.s
```

(Short alias: `-x`. Also accept `-teach` if we want to be explicit; pick one
and document it.)

When `-explain` is on, `step` (and `run`, when it stops on a breakpoint)
produces output like this. Example using `helloworld.s`:

```
(spim) step

About to execute at 0x00400000:
    li $v0, 4              # syscall 4 (print_str)

  What it does:
    Load Immediate — set register $v0 to the constant 4.
    (Pseudo-instruction; the assembler expands this to:
        ori $v0, $zero, 4)

  Inputs read:
    (none — this is a constant load)

  Will write:
    $v0  (currently 0x00000000)

  Try it yourself:
    print $v0           # see $v0 before/after

[step executes]

After execution:
    $v0:  0x00000000  ->  0x00000004   (decimal 4)
    PC:   0x00400000  ->  0x00400004
```

For an instruction that touches memory:

```
About to execute at 0x00400010:
    lw $t1, foobar

  What it does:
    Load Word — read a 32-bit word from memory at the address of label
    `foobar` and place it in register $t1.

  Inputs read:
    address: foobar  =  0x10010010
    memory at 0x10010010 currently:  0x00000000

  Will write:
    $t1  (currently 0xdeadbeef)

  Try it yourself:
    print foobar        # show the memory at the symbol
    print $t1           # show the destination register
```

For `syscall` we should special-case the explanation based on `$v0`:

```
About to execute at 0x00400008:
    syscall

  What it does:
    System call. The call number lives in $v0; arguments live in $a0..$a3.
    $v0 = 4, which is print_string: print the null-terminated string whose
    address is in $a0.

  Inputs read:
    $v0 = 4           (print_string)
    $a0 = 0x10010000  (address of "Hello World")
    memory at 0x10010000:  "Hello World"

  Output:
    Writes the string to the simulator console.

  Try it yourself:
    print $v0
    print $a0
    print 0x10010000
```

### Output rules

- Use the existing `write_output(message_out, ...)` plumbing — do NOT use
  `printf` directly; it has to honor the simulator's console redirection.
- Indent the explanation block by two spaces so a student can visually
  separate it from program output.
- Show register values in both hex and decimal when they fit; show addresses
  as hex only.
- When an input register is `$zero`, omit it (it's always 0; saying so is
  noise).
- For "Try it yourself" lines, only emit commands that are actually valid in
  the existing REPL. The supported forms are `print <reg>`, `print <symbol>`,
  `print <hex-or-dec-address>` — see `PRINT_CMD` in `src/spim.c:455`.

## CLI flag

- Long form: `-explain`
- Short form: `-x`
- Default: off (so existing behavior is unchanged).
- Adds one new global `bool explain_mode` declared next to `quiet` etc.
  in `src/spim.c:117-126`. Parse it in the arg loop in `src/spim.c:171-258`,
  alongside `-quiet` / `-noquiet`.
- Update the usage string at `src/spim.c:262-278`.

There should also be a runtime toggle, since teachers may want to flip it
on/off mid-session: extend `parse_spim_command` (`src/spim.c:373`) with an
`EXPLAIN_CMD` and `NOEXPLAIN_CMD`, plus matching tokens in the scanner
(`src/scanner.l`) and the keyword table.

## Where the explanation hooks in

The execution loop is in `run_spim` at `src/run.c:168`. Each iteration
fetches an instruction with `read_mem_inst(PC)` (line 230) and dispatches on
`OPCODE(inst)` via a giant `switch` starting at line 252.

Two integration points:

1. **Pre-execute narration.** Right after `inst = read_mem_inst(PC)` and
   the error checks (after line 244, before `DO_DELAYED_UPDATE()`), if
   `explain_mode` is set, call a new `explain_before(inst, PC)`.
2. **Post-execute diff.** After the `switch` body completes for one step,
   before the loop iterates, call `explain_after(inst, prev_state)` where
   `prev_state` is a snapshot we take pre-execute. The snapshot only needs
   to cover the regs/memory the instruction will touch — but for a first
   pass it's simpler to snapshot all of `R[]`, `HI`, `LO`, `PC`, the FP
   registers, and the specific memory word for loads/stores.

Put the new functions in a new file `src/explain.c` with header
`include/explain.h`, and add to `meson.build`'s `source_files` list
(`meson.build:47-57`).

## The explanation table

The dispatch in `run.c` already has one case per opcode. The explanation
needs the same shape: one entry per opcode that knows

- a short human description ("Add", "Branch on Equal", "Load Word", ...),
- which fields of the `instruction` struct identify the inputs (rs/rt/imm/
  target/memory),
- which field is the destination (rd/rt/PC/memory),
- the suggested `print` commands.

Two reasonable encodings:

**Option A — parallel switch.** Mirror `run.c`'s switch in
`explain.c` with a `case` per `Y_*_OP`. Verbose but the cases stay close to
the semantics they're describing, which matters for keeping them in sync.

**Option B — table-driven.** Add fields to the X-macro in `op.h` (the file
is already an X-macro: `OP(name, token, type, ...)`) so each opcode gets a
human description and an "input/output kind" tag. Then a small driver in
`explain.c` formats based on the type plus the inst fields.

Option B is cleaner long-term. Option A is faster to ship and harder to get
out of sync. **Recommend Option A for the MVP**, with a note to migrate to B
once we know which fields the explanation actually needs.

`format_an_inst` (`src/inst.c:574`) already turns an `instruction` into a
disassembled line — reuse it for the "About to execute" line. Don't
re-implement disassembly.

## Phasing

### MVP — ship this first

Cover the opcodes that show up in any "first week of MIPS" course:

- arithmetic: `add`, `addi`, `addiu`, `addu`, `sub`, `subu`, `mul`,
  `mult`, `div`
- logical: `and`, `andi`, `or`, `ori`, `xor`, `xori`, `nor`, `sll`, `srl`,
  `sra`
- compare-set: `slt`, `slti`, `sltu`, `sltiu`
- memory: `lw`, `sw`, `lb`, `lbu`, `sb`, `lh`, `lhu`, `sh`, `la`
- branches/jumps: `beq`, `bne`, `bgez`, `bltz`, `bgtz`, `blez`, `j`, `jal`,
  `jr`, `jalr`
- moves: `move`, `mfhi`, `mflo`, `li`, `lui`
- system: `syscall` (with a `$v0`-aware sub-explanation for the common
  syscalls 1, 4, 5, 8, 10, 11)
- `nop`

Anything not covered should fall back to the current behavior plus a single
line: `  (no detailed explanation for this opcode yet)`.

### Stretch

- FP instruction explanations (the `Y_*_S_OP` / `Y_*_D_OP` family).
- Coprocessor / CP0 instructions.
- A `-explain-color` mode that uses ANSI escapes to highlight changed
  registers in the post-step diff. Gated behind isatty(stdout) so dumps
  stay clean.
- A `why` REPL command that prints the explanation for the *next*
  instruction without executing it.

## Decisions

Settled with the user (2026-05-08):

1. **Flag name:** `-explain`, short form `-x`.
2. **Narration scope:** narrate **every executed instruction**, including
   during `run` — not just `step` and breakpoint hits. Implementation
   consequence: gate the new hooks on `explain_mode` directly, not on
   `run_spim`'s existing `display` parameter (which is what currently
   distinguishes step from run).
3. **Pseudo-instruction expansion:** show the underlying real instruction(s)
   for pseudo-ops like `li` and `la` when `accept_pseudo_insts` is on.
   Skip the "expands to" line when it's off.
4. **Memory dump format:** hex by default. String interpretation only inside
   the `syscall` case for syscalls that take a string-pointer argument
   (e.g. print_string).

Note on (2): a `run` over a long program will produce a *lot* of output. We
should still ship it that way per the user's call, but if it turns out to be
unusable in practice we may want to revisit and add a `-explain-step-only`
variant later.

## Testing

- Add `tests/tt.explain.s` — a small program that hits one of each
  MVP opcode category. Run it with `-explain` and capture stdout; compare
  against a golden file `tests/tt.explain.expected`.
- Manual: run `helloworld.s` end-to-end with `-explain` and confirm a
  student could follow it without prior MIPS knowledge.
- Confirm `-explain` does not change behavior of any existing test in
  `tests/`.

## Files this will touch

- `src/spim.c` — flag parsing, usage string, REPL toggle commands
- `src/run.c` — call sites for `explain_before` / `explain_after` in
  `run_spim`
- `src/explain.c` (new) — the explanation logic
- `include/explain.h` (new)
- `src/scanner.l`, `src/parser.y` — new REPL keyword tokens for
  `explain`/`noexplain` (only if we ship the runtime toggle)
- `meson.build` — add `src/explain.c`
- `tests/tt.explain.s` (new), `tests/tt.explain.expected` (new)
- `README.md` / `Documentation/spim.man` — document the flag
