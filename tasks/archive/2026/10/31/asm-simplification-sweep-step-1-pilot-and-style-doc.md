# Step 1: Piloted the asm simplification style and wrote the reference doc

**Part of:** `tasks/archive/2026/10/31/asm-simplification-sweep.md`
**Next:** the per-topic rollout, steps 2–10 (listed in the umbrella)
**Completed:** 2026-10-31

The Shape-B style decided in the umbrella (every C local stack-homed, a per-routine columnar SYMBOL
TABLE, traced-value frame snapshots, deliberate-lesson files exempt) was proved on four demos before
the ~80-file rollout. The step produced the converted demos, the durable style guide
`tasks/reference/mips-asm-teaching-style.md`, and the value checker. The plan named the checker
`tools/check_asm_traced_values.*` (name and implementation to be decided); it was built as
`tools/check_asm_canonical.py`. The maintainer, William Emerison Six <billsix@gmail.com>, signed off
on the style ("1) yes") and on the transient-temporary boundary below ("2) that's fine").

## Pilot set (one of each shape)

- `examples/src/intro/increment-ints/increment-ints-1.asm`: register-only with no frame, the
  sharpest test of the reversed 2026-05-17 register-only decision. It gained a frame; with no `jal`,
  no `$ra` is saved.
- `examples/src/arguments/factorial/factorial.asm`: a loop with counter and accumulator, the worked
  example from the design discussion (`n`/`result` at `-4`/`-8($fp)`; snapshot at `factorial 5`,
  pass 4: `-8($fp)=60` shadowed in `$t0`).
- `examples/src/arguments/gcd/gcd.asm`: two `jal atoi` calls, so `$ra` and argv are saved on
  the stack and the table's register and memory columns are both exercised. It was chosen
  over `examples/src/recursion/fibonacci/fibonacci.asm`.
- `examples/src/transforms/wc/wc.asm`: an I/O byte loop with `&c` on the stack. It was chosen over
  `head`, whose argv parsing and `os_open` error paths made it a poor first example, and because it
  has a golden.

`print1through10` was the alternative for the first slot; it was converted later in step 2.

## Decisions (recorded in the style doc)

- Column order: `C variable | memory addr | register | C type | bytes | role / notes`.
- Frame convention: `$fp` is a fixed base, with saved `$ra` at `-4`, saved `$fp` at `-8`, locals
  below. `$ra` is saved only when the routine makes a `jal`, since syscalls preserve it.
- "How few registers": persistent C locals get stack homes; a transient sub-expression temporary
  consumed immediately (gcd's `t = a % b`) may stay in a scratch register, rather than spilling it
  as a full memory machine would. The maintainer confirmed this boundary.
- Shared leaf helpers (`atoi`, `print_uint`) stay register-based and are labelled as such.
- Checker scope: `tools/check_asm_canonical.py` reads a `#CANONICAL` block in each converted file
  and guarantees the canonical run's final output; intermediate snapshot values are human-traced
  from the same run. It was not wired into the gate automatically.
- Width: SYMBOL TABLE and SNAPSHOT blocks are exempt from 80 columns; instruction lines stay
  within 80.
- Verification reality: only about a third of the demos have a golden, so the rollout's primary gate
  became before/after output equivalence, plus goldens where present, plus the checker.

The existing four-layer comments (L1 C block, L3 block comments, L4 inline) and ABI register names
were kept.

## Results

All four demos were byte-identical to their pre-change output over an input range including edge and
error cases. `meson test --suite examples` passed 34/34, including wc's golden. The checker passed
on all four and failed on a deliberately corrupted value, showing the gate bites. This record does
not state that the containerized `make image` gate was run for this step.
