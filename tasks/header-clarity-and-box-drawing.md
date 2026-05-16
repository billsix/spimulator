# Plan: clarify the Stepped header + Unicode box-drawing

Two related polish passes for the L1–L4 output:

1. **Replace ASCII `+-|`** in the bit-field boxes and section
   dividers with Unicode box-drawing characters
   (`┌─┬─┐ │ └─┴─┘ ─`). Same shape and width, cleaner visual.
2. **Make the per-step header self-explanatory** to a student who
   doesn't already know how to read a disassembler — by emitting a
   one-time legend on the first instruction of each session and by
   relabeling the per-step header so each piece of information is
   labeled.

Neither change affects level gating, decoder logic, or any of the
existing template content; it's a presentation-only pass.

## Part 1 — Unicode box-drawing

Replace these ASCII glyphs:

| Position             | Current | New |
|----------------------|---------|-----|
| Top-left corner      | `+`     | `┌` |
| Top-right corner     | `+`     | `┐` |
| Top T-joint          | `+`     | `┬` |
| Bottom-left corner   | `+`     | `└` |
| Bottom-right corner  | `+`     | `┘` |
| Bottom T-joint       | `+`     | `┴` |
| Horizontal segment   | `-`     | `─` |
| Vertical segment     | `\|`    | `│` |
| Section divider (hr) | `-` × N | `─` × N |

The current renderers all use a *symmetric* top/bottom (because `+`
serves as both upper and lower corner/joint in ASCII). Unicode
distinguishes corners and T-joints by position, so the top and bottom
rows of each box need to differ. Mechanical change in each of the
seven renderers (the three L3 ones plus the four L4 ones).

UTF-8 is already established in the explain output (we print `→` in
every "Wrote:" line, `\(em` em-dashes in the prose, etc.), so no
encoding plumbing is needed. The character widths in monospace fonts
are all single-width — the existing column math doesn't change.

### Sample after the change

Current L3:

```
  +--------+-------+-------+-------+-------+--------+
  | 000000 | 00100 | 00101 | 01000 | 00000 | 100001 |
  +--------+-------+-------+-------+-------+--------+
```

After:

```
  ┌────────┬───────┬───────┬───────┬───────┬────────┐
  │ 000000 │ 00100 │ 00101 │ 01000 │ 00000 │ 100001 │
  └────────┴───────┴───────┴───────┴───────┴────────┘
```

Section divider currently:

```
----------------------------------------------------------
```

After:

```
──────────────────────────────────────────────────────────
```

### Implementation

`hr()` in `explain.c` switches to a `─`-filled string. Each of the
seven box renderers gets its three-line literal replaced — top, bits,
bottom rows now use distinct corner glyphs. The `%s` slots and width
math are unchanged.

## Part 2 — Header clarity

### Current

```
----------------------------------------------------------
Stepped 0x00400128:
    [0x00400128]    0x214affff  addi $t2, $t2, -1               ; 118: addi    $t2, $t2, -1
```

Five distinct pieces of information, none labeled. For a student who
hasn't read a disassembler before, the redundant address column and
the source-line comment both look suspicious.

### Proposed

Two changes, working together:

**(a) One-time legend.** The first call to `explain_after` in a
session emits a short labeled walkthrough explaining what each piece
of the upcoming "Stepped" header means. Subsequent steps skip the
legend.

**(b) Relabel and split the per-step header** so the machine view
(address, encoding, real instruction) and source view (line + source)
are on distinct lines with explicit labels — every step, not just
the first.

#### Legend (emitted once per session)

```
══════════════════════════════════════════════════════════
About the output
══════════════════════════════════════════════════════════

Every step begins with a header like:

    Stepped at PC = 0x00400128:
        memory[0x00400128] = 0x214affff   →   addi $t2, $t2, -1
        source.s line 118:                     addi $t2, $t2, -1

What each part means:

  PC = 0xADDR
        The program counter — the address in memory of the
        instruction that just executed. (Shown again on the
        next line as the address of the encoded word.)

  memory[0xADDR] = 0xWORD
        The 32-bit machine code stored at that address. This
        is what the CPU's decoder reads.

  → real-instruction
        The disassembled form — what the CPU actually ran,
        with registers shown in ABI names ($t2, $sp, $a0)
        rather than register numbers.

  source.s line N: ...source line as you wrote it...
        The line from your assembly file the assembler read
        for this instruction. If it doesn't match the real
        instruction on the line above, you wrote a pseudo-
        instruction (e.g. `la $t7, dst` expands to `lui` +
        `ori` — the source line appears once but two real
        instructions get narrated).

(This header is shown once per session. Use `-explain=0` to
disable narration.)
```

#### Per-step header (every step)

Restructured from one packed line to two labeled lines:

```
──────────────────────────────────────────────────────────
Stepped at PC = 0x00400128:
    memory[0x00400128] = 0x214affff   →   addi $t2, $t2, -1
    source.s line 118:                     addi $t2, $t2, -1
```

For a pseudo-op case, the source/real divergence becomes visually
obvious:

```
──────────────────────────────────────────────────────────
Stepped at PC = 0x004000d4:
    memory[0x004000d4] = 0x3c011001   →   lui $at, 4097 [dst]
    source.s line 89:                      la $t7, dst
```

The student reads down the column and sees "I wrote `la`, the CPU
actually ran `lui`" without needing to know the disassembler's `;`
comment convention.

### Why two lines, not one

Compressing onto one line keeps the existing screen real estate but
loses the ability to label each piece. Two lines:

- explicitly separate the "what the CPU sees" view from the "what
  you typed" view;
- give each side enough room for its label to fit comfortably;
- make the pseudo-op divergence (when source ≠ real) jump out as
  vertically misaligned text instead of a `;`-buried comment.

The cost is one extra line per instruction. Across a 200-instruction
smoke test at L1 that's +200 lines (~+10%); at L4 it's ~+4% relative
to current.

### Implementation notes

- A file-static `static bool legend_emitted` in `explain.c`, set true
  the first time `explain_after` runs, reset by the existing
  `explain_clear_suggestions()` hook on `reinitialize` (so a student
  who reinits sees the legend again — arguably useful, definitely
  consistent).
- The legend is gated on `explain_level > 0` and emitted only once.
  At level 0 (off), no legend.
- The per-step header restructure needs to split out the source line
  from the disassembly. Today `inst_to_string` returns a single
  string with `; LINE: source` appended when a source line is
  attached. The simplest implementation: parse off the `;` portion at
  the explain_after call site, print the machine view, then print
  the source view as its own line. Slightly hacky but localized;
  cleaner alternative is to add a new accessor to `inst.c` returning
  the two halves separately.

### Files touched

- `src/explain.c` — most of the change. The seven box renderers, the
  `hr()` divider, the legend, and the `explain_after` header
  restructure.
- Possibly `src/inst.c` / `include/inst.h` if we add a clean accessor
  to get the source-line half separately from the disassembly half.
  Optional; can start with inline parsing in `explain.c`.

## Out of scope (noted for later)

These came up while reviewing the output but aren't covered here:

- **`[dst]` / `[label-0xADDR]` annotations** in the disassembly
  output (relocation hints and branch displacement labels). Worth
  explaining inline or in the legend; deferred because they touch
  `inst.c`'s disassembler formatting.
- **`$at`, `$r0` register-role hints.** The L2 "Inputs read" /
  "Wrote" lines could annotate `$at` with "(assembler-temporary —
  reserved for pseudo-op expansion)" the first time a pseudo-op
  uses it. This is the same hook already noted as a follow-up in
  `tasks/teaching-mode-coverage.md` ("ABI register-role
  annotations").
- **Register-name tooltips in general** — explaining `$t` vs `$s`
  vs `$a` vs `$v` vs `$ra` / `$sp` / `$fp` / `$gp`. Larger pedagogy
  project; out of scope for this pass.

## Order of work

1. **Part 1 (box-drawing)** first. Mechanical, isolated, low-risk —
   touch the seven renderers and `hr()`, rebuild, eyeball.
2. **Part 2 (header + legend)** second. Two sub-steps:
   - Restructure the per-step header in `explain_after` (split machine
     view and source view onto two labeled lines).
   - Add the legend with the once-per-session flag.

Both together: probably half a day.

## Verification

After build:

```sh
ninja -C builddir
cd tests
../builddir/spimulator -delayed_branches -delayed_loads -noexception -file tt.bare.s | tail -1
../builddir/spimulator -ef ../src/exceptions.s -file tt.core.s < tt.in | tail -1
../builddir/spimulator -ef ../src/exceptions.s -file tt.le.s | tail -1
cd ..
for lvl in 0 1 2 3 4; do
  echo "L$lvl: $(./builddir/spimulator -ef src/exceptions.s -explain=$lvl \
                    -f tests/tt.explain.s | wc -l) lines"
done
```

Expected: regression tests still pass. Line counts at L1–L4 each
bump by ~one line per narrated instruction (the source-line split),
plus ~30 lines of legend at the top of each level's output.

Spot-check: render a pseudo-op step (e.g. a `la` expansion) and a
plain real-instruction step at L2, confirm the pseudo-op case's
source line is visibly different from the machine view.
