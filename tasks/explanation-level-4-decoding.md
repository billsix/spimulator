# Plan: L4 — progressive bit-field decoding

A fourth `-explain` level that adds a step-by-step decoder walkthrough.
Each step is a redrawing of the bit-field diagram, identical in shape
to the L3 box, with one or two more fields filled in than the previous
step. The progression mirrors what the CPU's hierarchical decoder
does: read the opcode, decide which sub-table to consult, read the
selector, then read the remaining fields once the format is known.

This is L3's bit-layout block expanded into an animation of the
decoding logic. At L4, the single L3 box is replaced by the
progressive sequence — the final frame in the sequence is identical
to what L3 would have shown, so no information is lost; the student
just gets the steps to derive it as well.

## What each frame looks like

Fields that haven't been decoded yet show their raw bits inside the
box, but their *label* below the box is `?` to signal "the decoder
hasn't reached this field yet." When a field becomes known, its label
fills in. One blank line separates frames.

### R-type via SPECIAL — `addu $t0, $a0, $a1`, encoding `0x00853021`

```
Decoding 0x00853021 step by step:

  Step 1 — start with the opcode.
  +--------+-------+-------+-------+-------+--------+
  | 000000 |   ?   |   ?   |   ?   |   ?   |   ?    |
  +--------+-------+-------+-------+-------+--------+
   opcode    ?       ?       ?       ?        ?
   = 0x00
  Opcode 0x00 is the SPECIAL group. The CPU can't decide the
  operation from the opcode alone — read the funct field next.

  Step 2 — read funct (bits [5:0]).
  +--------+-------+-------+-------+-------+--------+
  | 000000 |   ?   |   ?   |   ?   |   ?   | 100001 |
  +--------+-------+-------+-------+-------+--------+
   opcode    ?       ?       ?       ?     funct
   = 0x00                                    = 0x21
  In the SPECIAL table, funct=0x21 is `addu`. Now we know
  this is R-type addu, so the remaining fields are rs, rt,
  rd, shamt — read them in turn.

  Step 3 — full layout.
  +--------+-------+-------+-------+-------+--------+
  | 000000 | 00100 | 00101 | 01000 | 00000 | 100001 |
  +--------+-------+-------+-------+-------+--------+
   opcode    rs      rt      rd     shamt   funct
   = 0x00    = $a0   = $a1   = $t0  = 0     = 0x21
                                    (unused
                                    for addu)
```

### I-type, opcode alone identifies — `addiu $a1, $sp, 4`, encoding `0x27a50004`

```
Decoding 0x27a50004 step by step:

  Step 1 — start with the opcode.
  +--------+-------+-------+------------------+
  | 001001 |   ?   |   ?   |        ?         |
  +--------+-------+-------+------------------+
   opcode    ?       ?            ?
   = 0x09
  Opcode 0x09 in the primary table is `addiu`. This is an
  I-type instruction; the rest of the word is rs, rt, and a
  16-bit signed immediate.

  Step 2 — full layout.
  +--------+-------+-------+------------------+
  | 001001 | 11101 | 00101 | 0000000000000100 |
  +--------+-------+-------+------------------+
   opcode    rs      rt           imm (16-bit, signed)
   = 0x09    = $sp   = $a1        = 4
                                  (sign-extends to 0x00000004
                                   before the addition)
```

### REGIMM — `bgez $t0, target`, encoding `0x05010003`

```
Decoding 0x05010003 step by step:

  Step 1 — start with the opcode.
  +--------+-------+-------+------------------+
  | 000001 |   ?   |   ?   |        ?         |
  +--------+-------+-------+------------------+
   opcode    ?       ?            ?
   = 0x01
  Opcode 0x01 is the REGIMM group. Like SPECIAL, but the
  selector is in the rt field — not funct. Read rt next.

  Step 2 — read rt-as-selector (bits [20:16]).
  +--------+-------+-------+------------------+
  | 000001 |   ?   | 00001 |        ?         |
  +--------+-------+-------+------------------+
   opcode    ?     rt-sel         ?
   = 0x01          = 0x01
  In the REGIMM table, rt=0x01 is `bgez`. This is I-type
  with a register tested against zero and a 16-bit branch
  displacement.

  Step 3 — full layout.
  +--------+-------+-------+------------------+
  | 000001 | 01000 | 00001 | 0000000000000011 |
  +--------+-------+-------+------------------+
   opcode    rs    rt-sel   offset (16-bit, signed)
   = 0x01    = $t0  = bgez   = 3  (branch target =
                                   PC+4 + (3<<2) = 0x0040004c
                                   if $t0 ≥ 0)
```

### J-type — `jal main`, encoding `0x0c100009`

```
Decoding 0x0c100009 step by step:

  Step 1 — start with the opcode.
  +--------+----------------------------+
  | 000011 |             ?              |
  +--------+----------------------------+
   opcode               ?
   = 0x03
  Opcode 0x03 in the primary table is `jal`. J-type — one
  big target field below.

  Step 2 — full layout.
  +--------+----------------------------+
  | 000011 |  00010000000000000000001001 |
  +--------+----------------------------+
   opcode    target (26-bit word index)
   = 0x03    = 0x0100009
             -> actual address = (PC[31:28] | target<<2)
                                = 0x00400024
             -> $ra ← PC+4 (or PC+8 if delayed branches)
```

## Frame count by format

| Format                          | Frames | Why                                         |
|---------------------------------|--------|---------------------------------------------|
| R-type via SPECIAL (op=0x00)    | 3      | opcode → funct → rs/rt/rd/shamt             |
| R-type via SPECIAL2 (op=0x1c)   | 3      | opcode → funct → rest                       |
| R-type via SPECIAL3 (op=0x1f)   | 3      | opcode → funct → rest                       |
| REGIMM (op=0x01)                | 3      | opcode → rt-as-selector → rs/offset         |
| I-type, opcode-only             | 2      | opcode → rs/rt/imm                          |
| J-type (op=0x02/0x03)           | 2      | opcode → target                             |
| COP1 (op=0x11, FP)              | 3      | opcode → fmt → funct → rest                 |

SPECIAL2 / SPECIAL3 / COP1 are listed for completeness; their L3
support today is the generic R-type/I-type renderer, and L4 inherits
the same coverage. Adding full SPECIAL2/3 and FP support is a
follow-up that pairs with the FP family work in `tasks/teaching-mode-coverage.md`.

## Replacement, not addition

At L4, the single L3 bit-layout block is **replaced** by the
progressive sequence — not shown in addition. Rationale:

- The final frame of the sequence is byte-identical to what the L3
  box would have shown.
- Showing both would duplicate ~6 lines per instruction with no new
  information.
- The progressive sequence subsumes the L3 box: a student who only
  cares about the final layout can read the last frame.

Level relationships become:

- L1: semantic only.
- L2: + interactive (Inputs / Wrote / Try it yourself).
- L3: + single bit-layout box.
- L4: + progressive decoding sequence (final frame == L3 box).

Existing level-table in `tasks/explanation-levels-and-completion.md`
gets one new row for "Progressive bit-field decoding" with the L4 ✓.

## Implementation outline

### New top-level dispatcher

```c
/* Walk the CPU's hierarchical decoding of `inst`, drawing the
   bit-field box at each step with progressively more fields filled
   in. Called at L4 only; replaces explain_bit_layout for that level. */
static void explain_decoding_steps(instruction* inst);
```

Called from `explain_after` at the same point `explain_bit_layout` is
called today, but inside an `if (level >= 4)` branch. The existing
`if (level >= 3)` becomes `if (level == 3) explain_bit_layout(inst);
else if (level >= 4) explain_decoding_steps(inst);`.

### Parametric box renderer

Replace the three hand-rolled `render_r_layout` / `render_i_layout` /
`render_j_layout` calls with one renderer that takes a "what's known"
descriptor:

```c
enum field_state { FIELD_UNKNOWN, FIELD_KNOWN };

struct frame_field {
  const char* bits;      /* "000000" — always shown */
  const char* label;     /* "opcode" or "?" */
  const char* value_line;/* "= 0x00" or NULL */
};

static void draw_frame(const struct frame_field* fields, int n_fields,
                       const char* prose_after);
```

The progressive renderer fills in the `fields` array step by step and
calls `draw_frame` with the prose explaining what was just decoded
and what to read next. Same width math as the current
`render_r_layout`/etc., just driven by data.

Existing `render_r_layout`/`render_i_layout`/`render_j_layout` keep
their job at L3 unchanged — `explain_decoding_steps` builds its own
frames with `draw_frame` directly. Code duplication is fine; the
shapes are similar enough that a future cleanup can DRY them, but
forcing a unified renderer up front is premature.

### Lookup tables (new)

```c
static const char* special_funct_to_mnemonic[64] = {
  [0x00] = "sll",  [0x02] = "srl",   [0x03] = "sra",
  [0x04] = "sllv", [0x06] = "srlv",  [0x07] = "srav",
  [0x08] = "jr",   [0x09] = "jalr",
  [0x10] = "mfhi", [0x11] = "mthi",  [0x12] = "mflo", [0x13] = "mtlo",
  [0x18] = "mult", [0x19] = "multu", [0x1a] = "div",  [0x1b] = "divu",
  [0x20] = "add",  [0x21] = "addu",  [0x22] = "sub",  [0x23] = "subu",
  [0x24] = "and",  [0x25] = "or",    [0x26] = "xor",  [0x27] = "nor",
  [0x2a] = "slt",  [0x2b] = "sltu",
  /* … */
};

static const char* regimm_rt_to_mnemonic[32] = {
  [0x00] = "bltz",   [0x01] = "bgez",
  [0x10] = "bltzal", [0x11] = "bgezal",
  /* trap-on-condition immediates fill the rest at MIPS32+ */
};
```

A primary-opcode → mnemonic table for the I-type and J-type branches
would mirror what's already implicit in `inst_op_name()`. Probably
simplest: extend `inst.c`'s existing decoding helpers rather than
duplicate them here. The L4 narration calls `inst_op_name()` for the
"this is `addu`" / "this is `addiu`" lines and uses the local funct /
rt tables only for the "in the SPECIAL table, funct=0x21 is …"
narrative line.

### CLI / docs

- `src/spim.c`: argument parser accepts level 4. Current range is
  0–3; broaden to 0–4. Three exact strings get bumped:
  - the digit-range check in `-explain=N` (now `'0'..'4'`).
  - the digit-range check in the bare `-explain N` form.
  - the error string `"requires a level in 0..3"` becomes `"0..4"`.
- Usage string in `src/spim.c`: one more line in the `-explain [N]`
  description (level 4 = + progressive decoding).
- `Documentation/spim.1`: add a `Level 4 — decoder walkthrough` bullet
  inside the `.RS` block alongside the existing 1/2/3 entries.

## Files touched

- `include/explain.h` — no signature changes; possibly a
  level-4 constant if we want a name instead of a magic number.
- `src/explain.c` — new `explain_decoding_steps()` plus helpers
  (`draw_frame`, SPECIAL funct table, REGIMM rt table). Existing
  `explain_bit_layout` call site in `explain_after` becomes a small
  switch on level.
- `src/spim.c` — argument parser range bump and usage string.
- `Documentation/spim.1` — level 4 bullet.
- `tasks/explanation-levels-and-completion.md` — append a row to
  the level-table for "Progressive bit-field decoding".

## Verification

Standard playbook:

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

Expected: L0=3, L1/L2/L3 unchanged from current counts (1789 / 2515 /
3593), L4 ≈ L3 plus the per-instruction expansion. Rough estimate:
each instruction's L3 box is ~6 lines; the progressive sequence is
~3 frames × ~7 lines plus prose, ~25 lines. Net add ≈ ~19 lines per
narrated instruction. For ~200 narrated instructions in the smoke
test, L4 lands somewhere around 7,500–8,000 lines.

Spot-check by eye: pick one instruction of each format from the
smoke output, confirm each step shows the right "?" placeholders and
the right narrative prose ("opcode 0x00 is SPECIAL", etc.).

## Open questions / risks

- **Width drift.** The existing L3 box is ~60 columns. The
  progressive sequence repeats it 2–3 times. Wrapped terminals at
  80 columns will be fine; narrower terminals might look ugly.
  Acceptable.
- **`shamt` annotation.** For non-shift R-type ops like `addu`,
  shamt is "unused for addu". Worth saying explicitly in step 3 so
  the student doesn't think the CPU is reading garbage. The example
  above does this; the implementation should match.
- **SPECIAL2/SPECIAL3/COP1 coverage.** Today these fall into the
  generic R-type / I-type renderer at L3. At L4 the progressive
  decoder would say something like "opcode 0x1c is SPECIAL2; read
  funct" but the SPECIAL2 funct table is incomplete in the L3 code
  too. Acceptable to leave the SPECIAL2/SPECIAL3 narration generic
  ("funct=0xNN selects this op — see the MIPS reference") until
  SPECIAL2/SPECIAL3 instructions get their own templates.
- **Number of "?" placeholders.** For a wide I-type immediate field
  (16 bits), one big "?" in the middle of the field box reads fine;
  for the 26-bit J-type target, a single "?" centered is also fine.
  Watch for alignment if any field's bit-width changes display
  between frames.

## Order of work

One commit. Steps:

1. Write `draw_frame` and the SPECIAL-funct / REGIMM-rt lookup
   tables.
2. Write `explain_decoding_steps`. Start with the I-type case (2
   frames, simplest), then J-type, then SPECIAL-R, then REGIMM.
3. Wire into `explain_after`: replace the single
   `if (level >= 3) explain_bit_layout(inst);` with the L3/L4 switch.
4. Bump CLI range in `src/spim.c` (0..3 → 0..4) and update usage.
5. Update man page.
6. Update `tasks/explanation-levels-and-completion.md` level table.
7. Build, regression-test, eyeball one of each format in the smoke
   output.

Estimated effort: half-day with the code loaded.
