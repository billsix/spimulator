# Assembly simplification sweep for novices (umbrella)

**Status:** proposed — needs go-ahead to start the pilot (design settled with the
maintainer, William Emerison Six <billsix@gmail.com>, 2026-10-31)
**Priority:** 6
**Difficulty:** 7

## BLUF

Rewrite the hand-written MIPS `.asm` teaching demos so a novice sees **C values living on
the stack**, not scattered across registers. Two coupled changes to every (non-exempt)
demo: (1) give each C local a **canonical home on the stack** and use scratch registers
only as working copies — "as few registers as possible" — and (2) carry a **columnar
SYMBOL TABLE per routine** plus an **ascii-art stack-frame snapshot with traced values**,
so a student can read the C-to-asm relationship and "forget as much as possible about what
C values are stored in which registers." This is a step-task initiative: a pilot defines
the exact style + a reference doc for the maintainer's sign-off, then a per-topic rollout
across ~80 files. "Done" (umbrella) = every non-exempt demo converted, each still passing
its stdout golden, plus a gate that catches stale traced values.

## Context — what the maintainer asked for

Verbatim intent (William Emerison Six <billsix@gmail.com>, 2026-10-31): sweep all the
assembly files to make them simpler for a novice — "as few registers used as possible, I
know it is less performant," because that supports the second part: the assembly doesn't
show the stack frames nearly as much as wanted. The maintainer wants that "all over the
place, so that the students can see the relationship of the c to the assembly, and can
forget as much as possible about what C values are stored in which registers, because
they'll mainly see the values on the stack."

### Design decisions reached in discussion (2026-10-31)

- **Shape B (chosen over Shape A).** Every C local gets a stack home; scratch registers
  hold only working copies loaded in to operate and stored back. (Shape A — keep
  register-only demos as-is and show stack art only where a frame already exists — was
  rejected: the maintainer wants the stack-value view everywhere.)
- **This deliberately REVERSES the 2026-05-17 "register-only where locals fit in
  temporaries" decision**, corpus-wide. That decision (recorded in
  `tasks/archive/2026/05/23/PLAN-asm-comments.md`: "Most of those frames were ceremony")
  made simple demos register-only on purpose. We are overturning it knowingly; the pilot's
  reference doc must state the new default and why it changed.
- **Columnar SYMBOL TABLE, one table per routine**, six columns in this order (location
  grouped right after the name, per the maintainer 2026-10-31):
  `C variable | memory addr | register | C type | bytes | role / notes`.
  A stack-homed value fills BOTH `memory addr` (its `-N($fp)` home) and `register` (its
  current scratch copy) — that is how "a stack position that is also in a register" reads in
  the table. A register-only value (`argc` at entry) has `—` in `memory addr`; a `.data`
  value has `—` in `register`. These table/diagram comment blocks are exempt from the
  80-column rule (they are tables, like `-fverbose-asm` output); instruction lines stay ≤80.
- **Traced values.** The ascii-art frame snapshot shows concrete runtime values from ONE
  canonical input per demo (e.g. `factorial 5` → `result = 60` at loop pass 4), with the
  slot value and its register shadow both shown. Static layout alone was rejected — the
  maintainer wants the values.
  - **Consequence (the maintainability catch):** a hand-traced value in a comment can go
    stale, and the stdout goldens CANNOT catch it (they diff stdout, not comments). So the
    rollout MUST include a `tools/` checker that re-derives each snapshot's values from a
    real spim run and fails the gate on a mismatch. Non-optional.
  - Keep snapshots to a FEW key points per demo (e.g. loop top, after a `jal`), not every
    instruction, to limit the rot surface.
- **Exempt the deliberate-lesson files** (the maintainer's call) — these teach the
  register / idiomatic / bug lesson on purpose and must NOT be converted to Shape B. The
  pilot's reference doc names them as intentional counter-examples:
  - `extras/subrountines/subrountines-1.asm` (longhand stack) vs `-2.asm` (idiomatic
    register calling convention) — the register-vs-stack contrast IS the demo.
  - `recursion/get-char-from-user/get-char-from-user-1.asm` vs `-2.asm` — deliberate
    bug/fix pair.
  - `algorithms/rpn/rpn.asm` + `algorithms/rpn/rpn-1-stack.asm` — operand stack IS `$sp`
    on purpose.
  - `intro/exit/exit.asm` — the `set $v0 before jr $ra` exit lesson.
  - (Confirm the full exempt set during the pilot; these are the known ones.)

### Prior art to read before touching any file

- `tasks/archive/2026/05/23/PLAN-asm-comments.md` — the 4-layer comment system (L1 C-source
  block, L2 routine contract + SYMBOL TABLE, L3 block C-statement comments, L4
  per-instruction mechanics; L5 was tried and removed) and the register-only decision this
  sweep reverses. The new columnar SYMBOL TABLE is an extension of L2, not a reinvention.
  Preserve its disciplines: ABI register names (`$fp` not `$s8`, `$zero`/`$0` conventions),
  preserve intentional bugs.
- `tasks/reference/example-c-teaching-style.md` — the C-side style (declare locals at top,
  flat control flow) the demos are written in; it already notes locals "line up with the
  frame slot," consistent with Shape B.
- `examples/TEACHING-ASSEMBLER-INTERNALS.md` (student-facing) and
  `examples/READING-ORDER.md` — update if the sweep changes what they describe.

### Safety net

The sweep CHANGES INSTRUCTIONS (unlike the 2026-05 comment-only pass), so behavior must be
preserved. `examples/tests/run-demo.sh` runs BOTH the C binary and the asm-under-spim and
diffs stdout against pinned goldens — that is the per-file behavior gate. A register→stack
move does not change stdout, so a green golden after conversion proves the rewrite is
faithful. The traced-value checker (above) is the SECOND gate the goldens don't provide.

## Steps (the index — each its own step-task; file rollout steps after the pilot)

- [x] **Step 1 — pilot + style reference doc**
  (archived: `tasks/archive/2026/10/31/asm-simplification-sweep-step-1-pilot-and-style-doc.md`).
  DONE 2026-10-31: converted `increment-ints-1`, `factorial`, `gcd`, `wc`; authored
  `tasks/reference/mips-asm-teaching-style.md`; built `tools/check_asm_canonical.py`.
  Maintainer signed off on the style and the transient-temporary boundary.
- [ ] **Step 2+ — per-topic rollout** (filed after Step 1 fixes the style): `intro/`,
  `arguments/`, `transforms/`, `fileio/`, `algorithms/`, `recursion/`, `lang/calc/`,
  `extras/`, `lib/`. One step-task per topic folder (roughly), each converting its
  non-exempt demos, gated green per batch. Ordering by Priority + "Depends on: Step 1";
  not `blocked` (within our control).

Don't over-scaffold: the rollout step docs are written once the pilot pins the exact style,
so they can cite it instead of guessing.

## Open questions

None blocking the pilot — all design decisions above are settled. The pilot itself will
surface the precise exempt set and the snapshot-placement rule, which it records in
`mips-asm-teaching-style.md`.
