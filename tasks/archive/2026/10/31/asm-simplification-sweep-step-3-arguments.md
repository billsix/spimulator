# Step 3 — asm simplification rollout: arguments/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 4 — `recursion/`

**Status:** done + archived 2026-10-31 — 4 demos converted, all byte-identical to
pre-change output, all 4 checker-anchored (3 by `#CANONICAL`, tee via a stdout-only anchor).
**Priority:** 5
**Difficulty:** 4

## BLUF

Convert the 4 non-exempt `arguments/` demos to the stack-visible style pinned in
`tasks/reference/mips-asm-teaching-style.md`. `arguments/` has 6 `.asm`; the pilot already
did `factorial` and `gcd`; none are exempt. "Done" = each demo byte-identical to its
pre-change output and checker-green; the step archives.

## Context

Cite `tasks/reference/mips-asm-teaching-style.md` for the style. These 4 used the old
"$s-parking" idiom (argc→$s1, argv→$s2, `$ra`→$s0, every C local in a callee-save register) —
exactly the convention Shape B replaces with stack homes.

## The 4 files and what each needed

- **`echo/echo.asm`** — no `jal` (only syscalls); `argc`, `argv`, and loop index `i` all live
  across the print syscalls, so all three get stack homes (16-byte frame, saved `$fp` only).
- **`factor/factor.asm`** — `jal atoi`, so saves `$ra`. Locals `n`, `d` on the stack; argc/argv
  used once at entry so they stay in `$a0`/`$a1`. The inner `n /= d` re-divides from the slots
  rather than holding a quotient across the print syscalls (clean load→operate→store).
- **`seq/seq.asm`** — `jal atoi`; locals `m`, `n`, `i` on the stack, plus `argv` (it must
  survive the first `atoi` to reach `argv[2]` in the 2-arg form); argc stays in `$a0`.
- **`tee/tee.asm`** (the Phase-C capstone) — the C array `int fds[8]` moves onto the stack as a
  contiguous 32-byte region (`fds[0]` at `-60($fp)`, `&fds[k] = ($fp-60)+4k`); `nfds`, the two
  loop indices, `n`, and argc/argv all get slots. `static char buf[4096]` correctly stays in
  `.data` (C `static` = program-lifetime storage, not a frame slot). No `jal` → no saved `$ra`.

### `#CANONICAL` coverage

- `echo`, `factor`, `seq` carry `#CANONICAL` (args-driven); all green.
- `tee` carries a `#CANONICAL` that runs in **stdout-only mode** from a new input file
  `arguments/tee/tee.input` (so the checker creates no files); its SNAPSHOT is hand-traced
  from the file-writing headline invocation `echo hello | tee a.txt b.txt`, noted in-file.

## Verification (done)

- Before/after output equivalence across a range incl. edge/error cases: all 4 byte-identical
  (echo: 0/1/many args; factor: 1, 7, 12, 13, 0, 100, 360, 65537, no-arg; seq: 1-arg, 2-arg,
  descending, no-arg; tee: 2 files, stdout-only, >8 files, cannot-open). tee's written files
  are md5-identical to the pre-change output.
- `tools/check_asm_canonical.py` on all 4: PASS.
- No golden tests exist for any of these 4 (confirmed). `make image` is the batch "done" bar.

## Exempt

None in `arguments/`.
