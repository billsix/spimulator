# Step 1 — pilot the asm simplification style + write the reference doc

**Part of:** `asm-simplification-sweep.md`
**Depends on:** nothing (first step)
**Next:** the per-topic rollout steps (filed once this pilot fixes the style)

**Status:** proposed — needs go-ahead to implement
**Priority:** 5
**Difficulty:** 5

## BLUF

Prove the Shape-B target style on ~4 representative demos, author
`tasks/reference/mips-asm-teaching-style.md` as the durable style guide, and build the
`tools/` checker that re-derives traced snapshot values from a real spim run. The maintainer
(William Emerison Six <billsix@gmail.com>) signs off on the pilot before any rollout starts.
"Done" = the pilot demos converted and golden-green, the reference doc written, the
value-checker working and run on the pilot demos, and maintainer sign-off recorded here.

## Context

See the umbrella `asm-simplification-sweep.md` for the full design and the settled
decisions (Shape B, columnar per-routine SYMBOL TABLE with a `bytes` column, traced values,
exempt deliberate-lesson files). This step turns those decisions into a concrete, approved
style on a small set before committing ~80 files to it.

## Pilot demo set (one of each shape)

Pick the smallest clear example of each shape so the reference doc can show all cases:

- `intro/increment-ints/increment-ints-1.asm` or `intro/print1through10/print1through10.asm`
  — a **register-only-today, no-frame** simple demo. This is the sharpest test of the
  2026-05-17 reversal: it gains a stack frame it did not have.
- `arguments/factorial/factorial.asm` — a **loop with a counter + accumulator**. Already
  carries a prose SYMBOL TABLE and is register-only today; the worked example in the design
  discussion was built from it (loads `n`/`result` from `-4`/`-8($fp)` into scratch regs,
  stores back; snapshot at `factorial 5`, pass 4: slot `-8($fp)=60` shadowed in `$t0`).
- `recursion/fibonacci/fibonacci.asm` or `arguments/gcd/gcd.asm` — a routine that **must
  save `$ra`/locals across a `jal`**, so the frame earns its keep and the SYMBOL TABLE's
  dual register+memory columns are fully exercised.
- one `transforms/` filter (e.g. `transforms/head/head.asm`) — a **byte-loop over I/O**, to
  confirm the style reads well when the "value" is a running count rather than arithmetic.

(Confirm/adjust during the pilot; the goal is to cover: no-frame→frame, loop, cross-`jal`
save, I/O loop.)

## What to produce

1. **Converted pilot `.asm` files**, each in the full target style:
   - every C local given a `-N($fp)` home; scratch registers are working copies only;
   - a **per-routine columnar SYMBOL TABLE**
     (`C variable | C type | bytes | register | memory addr | role / notes`, ~76 cols);
   - an **ascii-art frame snapshot with traced values** at a few key points, for one
     canonical input stated in the file;
   - the existing 4-layer comments (L1 C block, L3 block comments, L4 inline) preserved;
     ABI register names; no behavior change.
2. **`tasks/reference/mips-asm-teaching-style.md`** — the durable guide: the Shape-B
   default and WHY it reverses the 2026-05-17 register-only decision; the SYMBOL TABLE
   column spec with a rendered example; the snapshot/traced-value convention and where
   snapshots go; the **exempt deliberate-lesson files** named as intentional
   counter-examples; the canonical-input convention; a pointer to the value-checker.
3. **`tools/check_asm_traced_values.*`** (name/impl TBD) — parses each demo's snapshot
   annotations, runs the demo under spim with its canonical input, and fails if a traced
   value disagrees with the run. Wire it into the examples gate (propose, don't auto-add to
   CI without the maintainer). This is what keeps traced values from rotting silently.

## Verification

- `run-demo.sh` PASS (C side AND asm-under-spim) for each pilot demo — stdout goldens
  unchanged, proving the register→stack rewrite is behavior-preserving.
- The value-checker passes on the pilot demos, and is shown to FAIL when a traced value is
  deliberately corrupted (prove the gate bites).
- Containerized `make image` gate green.
- **Maintainer sign-off on the pilot style recorded here** before the rollout steps are
  filed.

## Open questions

None blocking — design is settled in the umbrella. Record here, during the pilot: the final
pilot demo set, the exact snapshot-placement rule, and the checker's annotation format (how
a snapshot marks which values are machine-checkable).
