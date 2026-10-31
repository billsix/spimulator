# Step 1 — pilot the asm simplification style + write the reference doc

**Part of:** `asm-simplification-sweep.md`
**Depends on:** nothing (first step)
**Next:** the per-topic rollout steps (filed once this pilot fixes the style)

**Status:** done + archived 2026-10-31 — four pilot demos converted + verified, reference
doc + value-checker written, and the maintainer (William Emerison Six <billsix@gmail.com>)
signed off on the style ("1) yes"). The style is durable in
`tasks/reference/mips-asm-teaching-style.md`; the per-topic rollout is tracked by the
umbrella `asm-simplification-sweep.md`. The transient-temporary boundary (gcd's `t = a % b`
stays in a register) was confirmed ("2) that's fine").
**Priority:** 5
**Difficulty:** 5

## Done so far (2026-10-31)

- Converted + verified byte-identical to pre-change output (before/after diff over an input
  range, incl. edge/error cases): `intro/increment-ints/increment-ints-1.asm` (no-frame →
  frame; no `jal` so no saved `$ra`), `arguments/factorial/factorial.asm` (loop; the worked
  example from the design discussion), `arguments/gcd/gcd.asm` (two `jal atoi`, `$ra` + argv
  on the stack), `transforms/wc/wc.asm` (I/O byte loop; `&c` on the stack). `meson test
  --suite examples` 34/34 (wc's golden passes).
- Wrote `tasks/reference/mips-asm-teaching-style.md` (the durable style guide).
- Wrote `tools/check_asm_canonical.py` + a `#CANONICAL` block in each converted file;
  verified it passes on all four and FAILS on a deliberately corrupted value.

### Decisions settled during the pilot (recorded in the reference doc)

- **Column order:** `C variable | memory addr | register | C type | bytes | role / notes`.
- **Frame convention:** `$fp` is a fixed base; layout `-4` saved `$ra` / `-8` saved `$fp` /
  locals below; **save `$ra` only when the routine makes a `jal`** (syscalls preserve it).
- **"How few registers":** persistent C locals get stack homes; a transient sub-expression
  temporary consumed immediately (e.g. gcd's `t = a % b`) may stay in a scratch register.
  **Flag for sign-off:** confirm this boundary, vs full memory-machine spilling of `t` too.
- **Shared leaf helpers** (`atoi`/`print_uint`) stay register-based (labelled as such).
- **Checker scope:** guarantees the canonical run's final output; intermediate snapshot
  values are human-traced from that same run. Not auto-wired into the gate.
- **Width:** SYMBOL TABLE / SNAPSHOT blocks are exempt from 80 cols; instruction lines ≤80.
- **Verification reality:** most demos have NO golden (~⅓ do), so the rollout's primary gate
  is before/after output equivalence, plus goldens where they exist, plus the checker.

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
- one `transforms/` filter — a **byte-loop over I/O**. Used `transforms/wc/wc.asm` (simple
  two-counter loop, and it has a golden) rather than `head` (whose multi-shape argv parsing
  and `os_open` error paths make it a poor *first* example).

(Final pilot set: increment-ints-1, factorial, gcd, wc — covers no-frame→frame, loop,
cross-`jal` save, I/O loop.)

## What to produce

1. **Converted pilot `.asm` files**, each in the full target style:
   - every C local given a `-N($fp)` home; scratch registers are working copies only;
   - a **per-routine columnar SYMBOL TABLE**
     (`C variable | memory addr | register | C type | bytes | role / notes`);
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
