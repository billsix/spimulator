# Explain-mode per-instruction consistency audit

## Status — not started

Audit + plan filed after running spimulator on `tests/tt.explain.s`
at `-explain=1`, `2`, `3`, and `4`, capturing all 130 executed
instructions per level (520 instruction blocks total), and
comparing narration shape across mnemonics within each level.

The narration system delivers different per-instruction line counts
across mnemonics — sometimes by design (first-encounter long-form
descriptions), sometimes by accident (template asymmetries).  This
audit catalogues the variance and proposes targeted fixes.

## How the data was collected

```sh
ninja -C builddir
for level in 1 2 3 4; do
  ./builddir/spimulator -explain=$level -f tests/tt.explain.s \
      > /tmp/explain_L$level.txt 2>&1
done
```

Per-mnemonic average line counts measured by extracting blocks
between consecutive `Stepped at PC = ...` headers.

## Distribution summary

| Level | Total lines | Avg per inst | Range (min-max) | Spread |
|---|---|---|---|---|
| L1 | 1104 | 8.5 | 6 - 12 | ~2× |
| L2 | 2961 | 22.8 | 16 - 31 | ~2× |
| L3 | 4039 | 31.1 | 18 - 39 | ~2× |
| L4 | 5769 | 44.4 | 18 - 53 | ~3× |

L1 is mostly uniform (90 of 130 instructions take exactly 7 lines;
outliers are pseudo-op-expansion continuations of `la`/`li` →
`lui+ori`).

L2-L4 have much wider per-mnemonic spread.  Highlights below.

## Per-mnemonic line counts at L2 (sorted)

```
16.0  j             # outlier low end
18.0  jr, nop
18.5  bgez
19.0  bgtz, blez, bltz, mfhi, mflo
19.3  bne
20.0  sra, srl
21.0  add, and, nor, or, sll, slt, xor
21.2  addi
21.7  beq
22.0  andi, div, jalr, slti, sub, xori
22.3  mult
22.4  addu
22.6  lui
23.0  sltiu, sltu, subu
24.0  divu
24.2  ori
24.3  syscall
24.5  addiu, jal
25.0  lb, multu, sb
26.0  lh, sh, sw
26.5  lw
27.0  lhu
29.0  lbu             # outlier high end
```

`lbu` runs almost 2× the length of `j`.  Many of those differences
have specific root causes — listed below.

## Concrete inconsistencies found

### 1. "What it did" prefix shape varies between instructions

Most instructions render as `Mnemonic Name — verb-phrase`:

| Mnemonic | "What it did" line |
|---|---|
| `add` | `Add — computed $t0 + $t1 as a signed sum; ...` |
| `lw` | `Load Word — read a 32-bit word from memory ...` |
| `sw` | `Store Word — wrote the 32-bit value from $t2 ...` |
| `jal` | `Jump and Link — saved the return address ...` |
| `jr` | `Jump Register — jumped to the address held in $ra.` |

But `j` lacks the preamble:

| `j` | `Jumped unconditionally to 0x00400188.` |

No "Jump —" prefix.  This makes `j` visually different from every
other branch/jump.

**Fix:** add the `Jump — ` prefix (or whatever the canonical
naming is) to `j`'s template in `src/explain.c`.  Audit the rest
of the templates for similar gaps.

### 2. Zero-offset omission in Inputs section

For load/store with offset:

```
sw with offset 4:
  Inputs (before this step):
    $at = 0x10010000  (decimal 268500992)
    $t2 = 0x0000000c  (decimal 12)
    offset = 4  (0x0004)             ← shown

sb with offset 0:
  Inputs (before this step):
    $t7 = 0x10010004  (decimal 268500996)
    $t8 = 0x000000ab  (decimal 171)
                                     ← offset = 0 NOT shown
```

The offset line disappears when offset is zero.  A student reading
the suite of load/store examples sees an inconsistent number of
input lines for what is the same instruction shape.

**Fix:** show `offset = N (0xN...)` unconditionally for every
load/store with a base+offset addressing mode.  Even `offset = 0`
is pedagogically meaningful — it tells the student "no displacement,
read directly from the base register's address."

### 3. Modifier description verbosity is heterogeneous

The first-encounter-long-form table in
`src/explain.c:modifier_descriptions[]` is inconsistent in how
much detail each modifier gets:

| Modifier | First-encounter description (long form) |
|---|---|
| `b` | `byte width: 8 bits.` (1 line) |
| `h` | `halfword width: 16 bits.` (1 line) |
| `w` | `word width: 32 bits (full register width).` (1 line) |
| `i` | 4-line multi-paragraph indented |
| `v` | 3-line multi-paragraph indented |
| `al` | 3-line multi-paragraph indented |
| `u` (arithmetic) | 4-line + cross-reference note |
| `u` (zero-extend) | 4-line + cross-reference note |

The width modifiers get one-line descriptions; everyone else gets
multi-paragraph treatment.  This is uneven — a student seeing the
width modifier first concludes "the explanation is terse," then
hits the immediate or unsigned modifier and gets a paragraph.

**Fix:** Pick one consistent depth.  Either expand the width
modifiers to match the multi-paragraph style, or compress the
verbose modifiers.  My recommendation: **expand the widths**, since
a student first seeing `b — byte width: 8 bits.` doesn't learn
*why* MIPS has byte/halfword/word loads at all.  A 2-3 line
paragraph explaining the load-store byte/halfword/word width
distinction would match the depth of the `u` modifier
descriptions.

### 4. First-encounter logic creates reading-order dependency

By design (per `category_seen[]` / `modifier_seen[]` flags in
`explain.c`), each category and each modifier gets the full
description the *first* time it appears in a session.  Subsequent
instances of the same category/modifier get bare labels.

This is **fine for top-down sequential reading** but creates a
trap for students who skim to a specific instruction:

- `lw` (first load) shows: `Category: Data transfer\n    Move data
  between registers and memory. MIPS is a load/store ...` (4-line
  description).
- `lb` (later load) shows: `Category: Data transfer` (bare label
  only).

If the student looks up `lb` in isolation, the category description
isn't there.

**Two possible fixes:**

A. **Always show category and modifier descriptions** at every
   occurrence.  Cost: longer total output (~30-50% more lines at
   L2).  Benefit: predictable per-instruction shape.

B. **Show description on first AND last occurrence**, OR every
   Nth occurrence.  Cost: minimal extra lines.  Benefit: skimmers
   are more likely to land on a described block.

C. **Add an option flag** (e.g. `-explain=2-verbose` or
   `-explain=2-once`) to let the user pick.

   My recommendation: **A**, plus accept the line-count growth.
   The current saving (descriptions appearing only once) is a
   compactness optimization; the consistency win is bigger.

### 5. Try-it-yourself suggestions vary in count and content

| Mnemonic | "Try it yourself:" line count |
|---|---|
| `add` (R-type) | 3 (`print $t0`, `print $t1`, `print $t2`) |
| `lw` (load) | 4 (adds 2 memory-inspection forms) |
| `sb` (store) | 4 (adds 2 memory-inspection forms) |
| `j` (jump) | 1 (`print_all_regs`) |
| `jr` (jump-register) | 1 (`print $ra`) |
| `jal` (jump-and-link) | (varies) |

There's a per-category rationale for the count (loads/stores get
memory inspection; jumps usually only need register inspection).
But the lack of any baseline ("always offer print_all_regs as the
last suggestion") means the block is sometimes 1 line and
sometimes 4.

**Fix:** Settle on a target block size (say, 3-4 lines minimum)
per instruction.  Pad with `print_all_regs` or `step` suggestions
for instructions with no obvious operand-specific hints.

### 6. Asymmetry between load/store narration

For loads (`lw`, `lb`, `lh`, etc.):
- Inputs: register + memory value at effective address (the value
  being read).
- Wrote: register before → after.

For stores (`sw`, `sb`, `sh`):
- Inputs: registers (base + value).  No "memory at addr (before)" line.
- Wrote to memory: address before → after.

The store's Inputs section doesn't show what was previously at
the destination address.  For loads, the input memory value is
shown because it's the input.  But for stores, the *prior* memory
value would be useful pedagogically (so the student sees what
they're overwriting).

**Fix:** add a "memory at addr (before) = X" line to the Inputs
section of store templates.

### 7. Modifier-coverage gaps

The modifier system handles `u`, `i`, `v`, `al`, `w`, `h`, `b`.

- Branch-likely instructions (`beql`, `bnel`, `bgezl`, etc.) carry
  the `l` suffix meaning "with delay-slot nullification on
  fall-through" — **no modifier tag**.  These render with the
  same "Category: Conditional branch" but the `l` aspect is
  invisible.

- `addiu` carries both `i` and `u` modifiers — works.

- `mfhi`/`mflo` have no modifier.  Their `hi`/`lo` register access
  is part of the mnemonic itself; arguably this is correct.

- `lui` (load upper immediate) is a separate template entirely
  — does it use the modifier framework?  Check.

**Fix:** add `l` modifier for the branch-likely variants.  Audit
remaining instructions for missing modifiers.

## Proposed work, in phases

### Phase A — Fix the obvious bugs (1-2 hours)

- Add the missing "Jump —" prefix to `j`'s "What it did" template.
- Audit every template for missing mnemonic-name prefix.
- Always show `offset = N` in load/store Inputs, even when N=0.

### Phase B — Modifier description rebalance (2-3 hours)

- Expand `b`/`h`/`w` descriptions to 2-3 lines, matching the
  depth of `u`/`i`/`v` descriptions.
- Add `l` modifier (branch-likely) tag and description.
- Audit for any other instruction-family modifier gaps.

### Phase C — Pick a category/modifier first-encounter policy (1-2 hours)

- Decide between "always show" (option A above) and the current
  "show once" behavior.
- Implement the choice, accept the line-count change.
- Update the explain golden output in
  `tests/explain.expected.txt` if applicable.

### Phase D — Try-it-yourself baseline (1 hour)

- Pick a minimum line count per instruction (e.g. 3).
- For instructions whose specific suggestions don't reach the
  baseline, pad with generic suggestions (`print_all_regs`, `step`).

### Phase E — Store memory snapshot (1 hour)

- Add "memory at addr (before) = X" to store templates' Inputs
  section.  Uses the existing `snap_mem_addr` / `snap_mem_val`
  machinery that's already populated for the after-step diff.

## Verification

After each phase:

1. `ninja -C builddir`
2. `meson test -C builddir` — 22/22 green
3. `./builddir/spimulator -explain=2 -f tests/tt.explain.s | wc -l`
   — re-measure the new baseline line count (will grow).
4. Per-mnemonic-spread check: rerun the distribution measurement
   to confirm range tightens.  Target: max/min ratio under 1.5×
   at L2.

## What's intentionally out of scope

- The bit-layout rendering at L3 (it's already uniform across all
  instructions — that's a wins-side data point, not a gap).
- The progressive decoder at L4 (same; it's consistently 2-3 frames
  per instruction).
- The post-step diff (Wrote: / PC: / side-effects) — those are
  already uniform.
- The category names themselves (Arithmetic, Logical, Data
  transfer, Conditional branch, Unconditional jump, System) —
  consistent with COD 4e Green Sheet, no change needed.

## Estimated total effort

~6-8 hours including verification.  Output line counts will grow
modestly (maybe 10-20%) because of the consistency fixes; gain
in per-instruction predictability outweighs the bulk cost.
