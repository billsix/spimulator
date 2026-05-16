# Plan: move the narration to after execution

## Why

Today `explain_before` prints the bulk of the per-instruction
explanation (header, bit-layout, pseudo-op header, "What it does",
Inputs, Will-write, Try-it-yourself) *before* the dispatch switch
actually runs the instruction; `explain_after` adds a small
register-diff block *after*. The wording reflects that:
"About to execute", "Will write", "currently 0x…".

But the user only ever sees the screen *after* the step has completed
— both blocks have already been emitted and the `(spim)` prompt is
back. So future-tense narration like "Will write: $t0 (currently
0x00000000)" describes the state at a moment that's already past by
the time it's read. That's confusing for a student trying to reason
about what just happened.

This plan moves the narration to fire from `explain_after`, in past
tense, with both before-state (from the existing snapshot) and
after-state (live registers) available in one block. `explain_before`
shrinks to "snapshot + one-line header."

## Goals

- Output reads in time order: every line describes something that
  has already happened by the time the student sees it.
- No information loss: every datum currently shown (effective
  address, comparison values, syscall details, bit layout) still
  appears, just relabeled and reordered.
- No level-table changes: L1/L2/L3 still gate the same sections per
  `tasks/explanation-levels-and-completion.md`.
- Tab-completion suggestions still populate at L2+ (unchanged).
- GUI compatibility note from the previous plan still holds: the
  `int level` parameter is threaded through; sections remain
  explicitly delineated for the eventual struct-and-render refactor.

## Sample output before/after

### Before (current L2, `add $t0, $a0, $a1`, $a0=5, $a1=3, $t0=0 → 8)

```
About to execute at 0x00400010:
    add $t0, $a0, $a1
  What it does:
    Add — compute $a0 + $a1. Result is placed in $t0.
  Inputs read:
    $a0 = 0x00000005  (decimal 5)
    $a1 = 0x00000003  (decimal 3)
  Will write:
    $t0  (currently 0x00000000)
  Try it yourself:
    print $a0
    print $a1
    print $t0
After execution:
    $t0:  0x00000000  ->  0x00000008   (decimal 8)
    PC:   0x00400010  ->  0x00400014
```

### After (draft 2 L2)

```
Stepped 0x00400010:  add $t0, $a0, $a1
  What it did:
    Add — computed $a0 + $a1; the result was placed in $t0.
  Inputs (before this step):
    $a0 = 0x00000005  (decimal 5)
    $a1 = 0x00000003  (decimal 3)
  Wrote:
    $t0:  0x00000000  →  0x00000008   (decimal 8)
    PC:   0x00400010  →  0x00400014
  Try it yourself:
    print $a0
    print $a1
    print $t0
```

One block, past tense, before→after deltas inline. Saves a separator
and reads in chronological order.

### Branch (taken, beq $t0, $t0, target)

```
Stepped 0x00400010:  beq $t0, $t0, label
  What it did:
    Branch if Equal — would have transferred to 0x00400020
    if ($t0 == $t0).
  Inputs (before this step):
    $t0 = 5
    branch target = 0x00400020
  Comparison:
    5 == 5  →  true  →  branch was taken
  Wrote:
    PC:  0x00400010  →  0x00400020
  Try it yourself:
    print $t0
```

### Load (lw $t0, 4($sp), $sp=0x7ffff000, mem at 0x7ffff004 = 0x42)

```
Stepped 0x00400010:  lw $t0, 4($sp)
  What it did:
    Load Word — read a 32-bit word from memory.
    Effective address = $sp + 4 = 0x7ffff000 + 4 = 0x7ffff004.
  Inputs (before this step):
    $sp = 0x7ffff000  (decimal 2147479552)
    offset = 4
    memory at 0x7ffff004 = 0x00000042
  Wrote:
    $t0:  0x00000000  →  0x00000042   (loaded from memory)
    PC:   0x00400010  →  0x00400014
  Try it yourself:
    print $sp
    print $t0
    print 0x7ffff004
    print 4($sp)
```

### Store (sw $t0, 4($sp), $t0=0x99, prior mem = 0x42)

This is the case that needs a small extension to the snapshot — we
have to remember the pre-store memory contents because by the time
`explain_after` runs, memory holds the new value:

```
Stepped 0x00400010:  sw $t0, 4($sp)
  What it did:
    Store Word — wrote $t0 to memory.
    Effective address = $sp + 4 = 0x7ffff000 + 4 = 0x7ffff004.
  Inputs (before this step):
    $sp = 0x7ffff000
    $t0 = 0x00000099  (decimal 153)
    offset = 4
  Wrote to memory:
    0x7ffff004:  0x00000042  →  0x00000099
  Wrote:
    PC:  0x00400010  →  0x00400014
  Try it yourself:
    print $sp
    print $t0
    print 0x7ffff004
    print 4($sp)
```

## Implementation

### `explain_before` becomes minimal

```c
void explain_before(instruction* inst, mem_addr addr) {
  if (explain_level == 0) return;
  explain_clear_suggestions();

  /* Snapshot architectural state for explain_after to diff against. */
  memcpy(snap_R, R, sizeof snap_R);
  snap_HI = HI;
  snap_LO = LO;
  snap_PC = addr;

  /* For stores, also snapshot the memory word that's about to be
     overwritten — by the time we narrate it in explain_after the
     write has happened and peek_word would return the new value. */
  snap_has_mem = false;
  switch (OPCODE(inst)) {
    case Y_SW_OP: case Y_SH_OP: case Y_SB_OP: {
      int base = BASE(inst);
      short off = (short)IOFFSET(inst);
      snap_mem_addr = (mem_addr)(R[base] + off);
      snap_mem_val  = peek_word(snap_mem_addr);
      snap_has_mem  = true;
      break;
    }
  }
}
```

That's all `explain_before` does. No printing, no template dispatch.

### `explain_after` does everything else

Same structure the current `explain_before` has, but reading inputs
from `snap_R[]` instead of `R[]`, with verbs in past tense, and
emitting the diff inline at the end of each template instead of as a
separate "After execution" block.

The top of `explain_after` prints the one-line header so it appears
*before* the per-template body, even though it's emitted from
`explain_after`:

```c
void explain_after(instruction* inst) {
  if (explain_level == 0) return;
  int level = explain_level;

  hr();
  write_output(message_out, "Stepped 0x%08x:  ", snap_PC);
  char* dis = inst_to_string(snap_PC);
  write_output(message_out, "%s", dis);
  if (dis[strlen(dis) - 1] != '\n') write_output(message_out, "\n");
  free(dis);

  if (level >= 3) explain_bit_layout(inst);
  emit_pseudo_op_header(inst);   /* same logic as today, rephrased */

  switch (OPCODE(inst)) { /* same big switch, past-tense templates */ }

  write_output(message_out, "\n");
}
```

`inst_to_string(snap_PC)` works because `snap_PC` points at the
just-executed instruction (the dispatch switch ran it, then
`PC += 4`, then we're here). Memory hasn't moved.

### Template changes (mechanical)

Each `tpl_*` takes the same `(int level, instruction* inst, ...)` it
takes today. Inside, change:

1. **Reads that describe inputs** — `R[rs]`, `R[rt]`, `R[base]` —
   become `snap_R[rs]`, `snap_R[rt]`, `snap_R[base]`. (For `tpl_load`
   the effective address uses the snapshot base, which is the value
   the instruction actually used.)
2. **Reads that describe results** — `R[rd]`, `R[rt]` after a load —
   stay as `R[i]` to get the post-execute value.
3. **Wording**: "compute" → "computed", "will write" → "wrote",
   "will become" → "became", "About to execute" → "Stepped",
   "currently" → describe before/after deltas inline.
4. **Replace `say_will_write_reg`** (which used to print
   "$X (currently …)") with `say_wrote_reg(rd)` that prints
   "$X: snap_R[rd] → R[rd]" with the decimal annotation.
5. **The generic "After execution" diff at the bottom of
   `explain_after`** goes away. The per-template `Wrote:` blocks
   now own that responsibility. Add a fallback: any register or
   HI/LO/PC that changed but wasn't named by the template gets a
   "Side effect:" line — protects against template/op mismatch and
   gives explicit signal when something unexpected fires (e.g., an
   exception path).

### Stores get one extra helper

```c
static void say_wrote_mem(int width) {
  if (!snap_has_mem) return;
  reg_word now;
  if (width == 1) now = peek_byte(snap_mem_addr) & 0xff;
  else if (width == 2) now = peek_half(snap_mem_addr) & 0xffff;
  else now = peek_word(snap_mem_addr);
  write_output(message_out, "  Wrote to memory:\n");
  write_output(message_out, "    0x%08x:  0x%08x  →  0x%08x\n",
               snap_mem_addr, (uint32)snap_mem_val, (uint32)now);
}
```

Called from `tpl_store` after the input block.

### Pseudo-op header rephrase

Today:

```
  Pseudo-instruction `la` (as written in source):
    Load Address — ...
  The line below is the first real instruction the assembler emitted.
```

After:

```
  Pseudo-instruction `la` (as written in source):
    Load Address — ...
  This was the first real instruction the assembler emitted for it.
```

Continuation hint:

```
  (continuation of the `la` pseudo-op expansion above —
   same source line, this was the next real instruction emitted)
```

Same logic in `find_pseudo_in_source` + `pending_pseudo_*`; only the
trailing verb tense changes.

## Snapshot additions

New file-static state in `src/explain.c`:

```c
static bool      snap_has_mem;
static mem_addr  snap_mem_addr;
static reg_word  snap_mem_val;
```

Set in `explain_before` only for `Y_SW_OP` / `Y_SH_OP` / `Y_SB_OP`;
read in `explain_after`'s `tpl_store` path. No other instructions
need a memory snapshot (loads don't modify memory; the rest don't
touch it).

## Files touched

- `src/explain.c` — the bulk of the change. The big switch moves
  bodily from `explain_before` to `explain_after`. Each template's
  input-reading code switches from `R[]` to `snap_R[]`. Wording goes
  past-tense. Generic post-step diff at the bottom of `explain_after`
  is replaced with per-template "Wrote" blocks plus a "Side effect:"
  fallback for anything the template didn't cover.
- `include/explain.h` — no signature changes; existing
  `explain_before(inst, addr)` / `explain_after(inst)` keep working.
- `src/run.c` — no change needed. The two hook call sites
  (`run.c:253` for `explain_before` and `run.c:1439` for
  `explain_after`) are already positioned correctly: before and after
  the dispatch switch.
- `tests/tt.explain.s` — unchanged. The smoke test exercises the
  same opcodes; only the output text differs. Line counts will
  drop slightly (one fewer separator block per instruction; "About to
  execute" + "After execution" merge into one block).

## Verification

After build, the regression tests still pass (they don't read
explain output). The explain smoke test changes line count modestly:

```sh
./builddir/spimulator -ef src/exceptions.s -explain=N \
  -f tests/tt.explain.s | wc -l
```

Expect: L1 / L2 / L3 each drop by roughly the count of instructions
narrated (one fewer block boundary per instruction). Spot-check by
eye that every "currently" / "will" / "about to" turned into past
tense and that the diff numbers line up between the "Inputs" block
and the "Wrote" block.

## Open questions

- **"Stepped" vs "Step:" header.** "Stepped" is a single verb, parses
  fast. Alternatives: "Step:", "Just ran:", "After:". Pick one and
  use it consistently. (Recommend "Stepped".)
- **Side-effect fallback granularity.** When a register changes that
  no template named, do we list it as "Side effect: $X = …" or fold
  it into the same "Wrote:" block? Folding is cleaner; listing
  separately is louder when something unexpected fires. (Recommend
  folding for the common case, with a small `(unexpected)` annotation
  if the template advertised no writes but a register changed.)
- **The level-3 bit-layout block.** Today it appears between the
  disassembly and "What it does". In draft 2 it'd appear between the
  one-line "Stepped" header and "What it did:", which preserves the
  flow. No special change needed.
- **Pseudo-op header placement** is between bit-layout and the
  template. Same place, same logic; only the verb tense at the end
  of the header changes. No restructuring.

## Order of work

One commit, the change is fundamentally one motion (move-and-relabel).
Steps:

1. Add `snap_has_mem` / `snap_mem_addr` / `snap_mem_val` statics +
   the `Y_SW_OP/Y_SH_OP/Y_SB_OP` snapshot in `explain_before`.
2. Strip `explain_before` down to "snapshot + clear_suggestions".
   Move the disassembly + bit-layout + pseudo-op header + opcode
   switch to the top of `explain_after`.
3. In each template, swap `R[]` → `snap_R[]` for input-side reads;
   leave result-side reads as `R[]`. Update wording to past tense.
4. Replace `say_will_write_reg` with `say_wrote_reg` (before → after).
5. Replace `say_try` / the load/store memory hints — wording only,
   suggestion strings unchanged.
6. Drop the current generic "After execution" block; add the
   "Side effect:" fallback walk at the end of each template's
   write-out (or once at the bottom of `explain_after`).
7. Add `say_wrote_mem` for the store templates.
8. Rebuild, eyeball the smoke output, run the regression tests.

Probably a half-day's work with the code loaded.
