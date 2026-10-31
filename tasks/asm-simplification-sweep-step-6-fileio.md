# Step 6 — asm simplification rollout: fileio/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** Step 7 — `lib/`

**Status:** done + archived 2026-10-31 — all 9 demos converted, byte-identical to pre-change
output (cp/tee files md5-identical), checker green on all 7 annotated ones; the 3 cksum goldens
unaffected (outputs byte-identical to the pinned goldens).
**Priority:** 5
**Difficulty:** 6

## BLUF

Convert all 9 `fileio/` demos to the stack-visible style. None are exempt. Three cksum demos
are golden-tested (`cksum-1-stdin`, `cksum`, `cksum-core-test`) — an extra safety net. "Done" =
each demo byte-identical to its pre-change output; the step archives.

## Context

Cite `tasks/reference/mips-asm-teaching-style.md`. Key fact that shaped this topic: **every
large buffer in these demos is `static` in C** (`static char buf[BUFSIZE]`, `static const
crctab[256]`, comm's `static char line_a/line_b`), so it correctly stays in `.data`, not on a
frame. The conversions move only the scalar locals (fds, counts, pointers, ck/len) onto the
stack. All 9 make no `jal` except the cksum demos (which `jal print_uint`/`jal cksum_of`) and
comm (`jal read_line`/`jal line_cmp`); the no-jal ones save `$fp` only.

## The 9 files

- **touch** — path + fd on the stack; no jal.
- **nologin** — fd + n on the stack; always exits via syscall 17 (never returns); no `#CANONICAL`
  (output depends on whether `/etc/nologin.txt` exists on the host).
- **cat** — fd + argv1 + n on the stack; STDIN/"-"/FILE dispatch preserved.
- **cp** — argv + src/dst fds + n on the stack.
- **comm** — fa/fb/la/lb + argv on the stack; `read_line`/`line_cmp` kept as register-based leaf
  helpers; no `#CANONICAL` (tab-prefixed output columns — significant leading whitespace a
  comment anchor can't carry).
- **cksum-1-stdin, cksum, cksum-core, cksum-core-test** — ck/len (and fd/filename for the full
  cksum) homed on the stack; `ck` is reloaded/stored around each byte's CRC update so the
  checksum is visibly on the stack (slower, as the maintainer accepts). A nice side effect: with
  `len` on the stack, the old "print_uint must avoid $t1 so len survives" hack is gone.
  `cksum_of` is the pure-algorithm teaching focus, so it too gets a frame (ck, i); `print_uint`
  stays a shared leaf helper. The CRC table (`static const`) stays in `.data`.

### `#CANONICAL` coverage

- touch (error path, creates no file), cat (reuses `cat/cat.input`, new), cp (error path),
  cksum-1-stdin / cksum / cksum-core (reuse the existing `cksum*.input`), cksum-core-test
  (the clean 5-line battery) — all green.
- nologin (env-dependent) and comm (tab-prefixed columns) carry none; noted in-file.

## Verification (done)

- Full before/after output equivalence across modes/edge/error cases, all 9 byte-identical
  (cp's written file md5-identical). `tools/check_asm_canonical.py` green on all 7 annotated.
- The 3 cksum demos' outputs are byte-identical to their pinned `run-demo.sh` goldens, so the
  examples suite stays green; `make image` is the batch "done" bar.

## Exempt

None in `fileio/`.
