# Integer-conversion hardening for the big-endian build paths

**Status:** blocked
**Blocked on:** a big-endian build/host to compile and run the `#ifdef SPIM_BIGENDIAN`
paths under the conversion gate (the maintainer has no big-endian hardware; considered unlikely).
**Recheck:** build the core on (or cross-compiled for) a big-endian target with
`-Wconversion -Wsign-conversion`; cleared when those paths compile warning-clean and the suite passes.
**Priority:** 9 · **Difficulty:** 4
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)

## BLUF

The integer-conversion gate (`tasks/archive/2026/10/16/integer-conversion-hardening.md`) made the
simulator core clean under `-Wconversion -Wsign-conversion` for the **little-endian** build — the only
one the host and CI compile. The `#ifdef SPIM_BIGENDIAN` branches were **not** compiled, so their
latent sign-conversions were left untouched. If spim is ever built big-endian, those branches must get
the same treatment (and the gate, now on under both gcc and clang, would flag them). Mechanical and
low-risk, but unverifiable without a big-endian build — hence blocked.

## Known sites (little-endian twins already fixed)

In `src/memory.c`, the byte/half load-shift helpers have paired `#ifdef SPIM_BIGENDIAN` / `#else`
branches. The little-endian (`#else`) side was cast during the conversion pass, e.g.
`tmp = (mem_word)((unsigned)tmp >> (8 * (addr & 0x3)));`; the big-endian side
(`tmp = (unsigned)tmp >> (8 * (3 - (addr & 0x3)));`) was not. The `SPIM_BIGENDIAN` blocks to revisit
are near the four `#ifdef SPIM_BIGENDIAN` markers in `read_memory`/`read_memory_mapped` byte-extract
code (two load twins plus their kernel-segment equivalents). Any other `SPIM_BIGENDIAN` block that does
signed/unsigned word shuffling is in scope too.

## Plan (when unblocked)

1. Build big-endian (real or cross) with `-Wconversion -Wsign-conversion`; collect the warning list
   (the gate will surface them).
2. Mirror the cast each little-endian twin already uses (same policy as the main task: explicit cast
   to the type already implicitly converted to; value-identical).
3. Verify: the big-endian build is warning-clean under the gate and the regression suite passes.

## Cross-links

- `tasks/archive/2026/10/16/integer-conversion-hardening.md` — the little-endian pass + the cast policy.
- `github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md` — integer-conversion rule.
