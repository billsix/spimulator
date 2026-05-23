# Explain coverage for the rest of MIPS's base+offset families

## Goal

Add per-instruction narration in `src/explain.c` (plus matching
test coverage in `tests/tt.explain.s`) for the load/store
families that today fall through to the default "no detailed
explanation for this opcode yet" branch:

- **Unaligned word access** — `lwl`, `lwr`, `swl`, `swr`
- **Atomic primitives** — `ll`, `sc`
- **Floating-point loads/stores** — `lwc1`, `swc1`, `ldc1`, `sdc1`

Each uses base+offset addressing (`mnemonic $rt, N($base)`), so
the existing "Effective address = $base + N" idiom carries over,
but each family has additional semantics worth narrating.

## Why

- The default-branch fallthrough is a teaching dead-end: students
  see the disassembly line but get no help understanding what
  the instruction does.
- Companion task
  [`explain-stack-frame-offsets.md`](explain-stack-frame-offsets.md)
  fixes the *offset-diversity* gap for the integer family; this
  task closes the *instruction-family* gap.
- `lwl`/`lwr` in particular are easy to mistake for "two
  separate loads" — narration is the right place to explain that
  they're a coordinated pair that assembles a word from a
  potentially misaligned address.
- `ll`/`sc` are pedagogically central to any discussion of
  lock-free synchronization (even though spim's `sc` always
  succeeds in single-threaded mode, the pattern matters).
- FP loads/stores are the gateway to the broader FP-family work
  already filed under
  [`NEXT-SESSION.md`](NEXT-SESSION.md) — covering the four base+offset
  ops here is a natural foothold.

## State of the world

`src/explain.c`:

- No case statements exist for any of these opcodes.
- They fall through to the default at line ~2118:
  ```c
  "  (no detailed explanation for this opcode yet — see the disassembly above)\n"
  ```
- The only existing mention of `lwl`/`lwr`/`swl`/`swr` in
  `explain.c` is a comment around line 1426 describing
  pseudo-op expansion.

`tests/tt.explain.s`:

- None of the ten instructions appear anywhere.  The smoke test
  has zero coverage for them.

The integer templates (`tpl_load` at line ~698, `tpl_store` at
line ~737) are good models — both render the effective-address
arithmetic and a Reads/Writes summary.  The new templates can
mirror their shape.

## The change

### 1. New templates in src/explain.c

Add three small templates, modeled on `tpl_load` / `tpl_store`:

- **`tpl_load_unaligned` / `tpl_store_unaligned`**
  - Prints the same "Effective address" line.
  - Adds: "merges the bytes at the high (low) end of the
    addressed word into the destination, leaving the other
    bytes unchanged — paired with the matching `lwr` (`lwl`)
    to assemble a misaligned word."
- **`tpl_load_linked` / `tpl_store_conditional`**
  - Prints the effective-address line.
  - `ll`: "loads the word and tags this CPU's link register
    with the address; a subsequent `sc` to the same address
    succeeds only if no intervening write happened."
  - `sc`: "stores the word conditionally; in spim's
    single-threaded model this always succeeds and writes 1
    to `$rt` to signal success."  Make the "always succeeds in
    spim" note explicit so the student doesn't think they're
    seeing a hardware-faithful simulation.
- **`tpl_load_fp` / `tpl_store_fp`**
  - Prints the effective-address line.
  - Width-aware: `lwc1`/`swc1` move 4 bytes to/from `$fN`;
    `ldc1`/`sdc1` move 8 bytes to/from the `$fN`/`$f(N+1)`
    register pair and require 8-byte alignment.
  - Add a Reads/Writes summary keyed on `$fN` (or the pair).

Wire them into the opcode switch alongside the existing load /
store cases, using whichever token spellings spim's `tokens.h`
defines (likely `TOK_LWL_OPCODE` / `TOK_LWR_OPCODE` /
`TOK_SWL_OPCODE` / `TOK_SWR_OPCODE` / `TOK_LL_OPCODE` /
`TOK_SC_OPCODE` / `TOK_LWC1_OPCODE` / `TOK_SWC1_OPCODE` /
`TOK_LDC1_OPCODE` / `TOK_SDC1_OPCODE` — verify before wiring).

### 2. Test additions in tests/tt.explain.s

Append four focused blocks:

```asm
        # ── Block C: unaligned word access ──
        .data
ualign: .byte 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
        .text
        la      $t0, ualign
        lwl     $t1, 3($t0)        # high bytes of misaligned word
        lwr     $t1, 0($t0)        # low bytes
        swl     $t2, 3($t0)
        swr     $t2, 0($t0)

        # ── Block D: atomic primitives ──
        la      $t0, ualign
        ll      $t1, 0($t0)
        addi    $t1, $t1, 1
        sc      $t1, 0($t0)        # in spim, always sets $t1=1

        # ── Block E: FP word loads/stores ──
        la      $t0, ualign        # 8-byte aligned
        lwc1    $f0, 0($t0)
        swc1    $f0, 4($t0)

        # ── Block F: FP double loads/stores ──
        ldc1    $f2, 0($t0)        # needs 8-byte alignment
        sdc1    $f2, 0($t0)
```

(The exact data symbol can share `ualign` since each block resets
`$t0`; double-check 8-byte alignment for Blocks E/F.)

### 3. Golden regen

Regenerate `tests/tt.explain.expected` after the new narration
and test additions land together.

## Verification

1. `ninja -C builddir` clean.
2. Run by hand:
   `./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s | less`
   — scroll to the four new blocks; confirm:
   - No `(no detailed explanation for this opcode yet)` lines
     appear anywhere in the output (count goes from N to 0; was
     already 0 before this task started).
   - Each new instruction prints the effective-address line
     with sensible values.
   - The semantics blurb reads right for each family.
3. Regenerate the golden:
   `./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s > tests/tt.explain.expected`
4. `meson test -C builddir` → 22/22 green.
5. Re-skim the golden diff to confirm only the new blocks
   appear in the change.

## Out of scope

- The wider FP-family narration (add.s / sub.s / c.lt.s /
  cvt.* — ~70 opcodes) already filed under
  [`NEXT-SESSION.md`](NEXT-SESSION.md) as the "Gap 1 P8" stretch.
  This task only covers the four FP **memory** ops.
- True multi-threaded `ll`/`sc` semantics in spim — out of scope
  for the simulator, and the narration just notes that `sc`
  always succeeds in this model.
- Offset-diversity tweaks for the integer family —
  [`explain-stack-frame-offsets.md`](explain-stack-frame-offsets.md)
  owns that.

## Open questions

- For `ldc1`/`sdc1`, does spim already validate 8-byte alignment
  and raise an exception otherwise?  If yes, mention that in the
  narration; if no, file a separate alignment-validation task.
  (Check `src/run.c` for the LDC1 / SDC1 execution path.)
- `lwl`/`lwr` are endianness-sensitive.  spim defaults to little
  endian on x86 hosts; the narration should either say "(little-
  endian; the high/low end depends on the host)" or be precise
  about which bytes land where, to avoid teaching wrong intuition
  on a big-endian build.

## Status

Not started.
