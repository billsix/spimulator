# Step 2 — asm simplification rollout: intro/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 3 — `arguments/`

**Status:** in progress — converting the 4 non-exempt `intro/` demos.
**Priority:** 5
**Difficulty:** 3

## BLUF

Convert the non-exempt `intro/` demos to the stack-visible style pinned in
`tasks/reference/mips-asm-teaching-style.md`. "Done" = each demo produces byte-identical
output to its pre-change version, every annotated demo passes
`tools/check_asm_canonical.py`, and the step archives. `intro/` has 6 `.asm`; `exit/exit.asm`
is an exempt deliberate-lesson file; the pilot already did `increment-ints/increment-ints-1.asm`.

## Context

The style, frame convention, column spec, and verification method are in
`tasks/reference/mips-asm-teaching-style.md` (cite it, don't re-derive). This step cites it.

## The 4 files and what each needed

- **`print1through10/print1through10.asm`** — the only substantive Shape-B conversion here:
  the loop counter `int i` was register-only (`$t0` for the whole program); it now has a
  stack home at `-8($fp)` with `$t0` as a working copy (load → test/print → increment →
  store back). No `jal` (only syscalls), so the 8-byte frame saves `$fp` but not `$ra`.
  Also fixed the stale `# C source — see print1through10-1.c` comment to `print1through10.c`.
- **`clear/clear.asm`**, **`helloworld/helloworld.asm`** — NO C locals (just a fixed string
  in `.data`) and NO `jal`, so no stack frame (one with nothing in it would be the "ceremony"
  the style doc forbids). Converted the old `#VARIABLES` block to a per-routine SYMBOL TABLE
  that shows the string living in `.data` with its address travelling in `$a0`, plus a
  SNAPSHOT. Instructions unchanged.
- **`yes/yes.asm`** — same no-locals/no-frame shape; converted the header to the SYMBOL TABLE
  form. Instructions unchanged.

### `#CANONICAL` coverage (not every demo can carry one)

- `print1through10` and `helloworld` carry `#CANONICAL` blocks (checked green).
- `clear` has none: its output is terminal control bytes (`ESC[2J…`), not text the checker's
  line diff can express. Noted in-file.
- `yes` has none: the loop never terminates, so the checker (which runs to completion) cannot
  anchor it. Noted in-file.

## Verification (done)

- Before/after output equivalence: all 4 byte-identical (`yes` compared on a bounded prefix,
  since it is infinite; its instructions were unchanged anyway).
- `tools/check_asm_canonical.py` on the two annotated demos: both PASS.
- No golden tests exist for any of these 4 (confirmed), so before/after equivalence + the
  checker are the gates; the containerized `make image` gate is the "done" bar for the batch.

## Exempt (unchanged, per the style doc)

- `intro/exit/exit.asm` — the `set $v0 before jr $ra` exit lesson.
