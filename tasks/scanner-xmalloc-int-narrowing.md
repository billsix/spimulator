# Review the `(int)(len + 16)` narrowing in scanner.c's line-buffer xmalloc

**Status:** proposed — needs go-ahead
**Priority:** 7 · **Difficulty:** 2
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)

## BLUF

Two sibling helpers in `src/scanner.c` allocate a line buffer with
`(char*)xmalloc((size_t)(int)(len + 16))`, where `len` is a `size_t`. The inner `(int)` narrows the
size back to a signed `int` before it is widened again to `size_t` for `xmalloc` — a pre-existing,
latent narrowing (harmless in practice: `len` is a single source line's length). It surfaced during the
integer-conversion pass but was left as-is because removing it is a *logic* change, not a
conversion-warning fix. Decide whether to drop the inner `(int)` so the size stays `size_t` end to end.

## Context

The two sites (identify by content — line numbers drift):
`char* r = (char*)xmalloc((size_t)(int)(len + 16));` followed by
`snprintf(r, len + 16, ...)`, in the two line-annotation helpers near the end of `src/scanner.c`.

The conversion pass added the outer `(size_t)` to silence the int->size_t warning; the inner `(int)`
was already there (it predates this work). With the inner `(int)` gone, the expression is
`xmalloc(len + 16)` with `len` a `size_t` — no narrowing, no cast needed. The `snprintf` length arg on
the next line already uses `len + 16` directly, so the two should match.

## Plan

1. Confirm `len`'s type and provenance at both sites (it is a `size_t` line length; never near INT_MAX).
2. Replace `(size_t)(int)(len + 16)` with `(len + 16)` at both sites (drop both casts — the arg is
   already `size_t`); keep the `snprintf` arg consistent.
3. Verify the core still builds warning-clean under the conversion gate (gcc + clang) and the suite
   passes. Trivial; behavior unchanged for any realistic source line.

## Cross-links

- `tasks/archive/2026/10/16/integer-conversion-hardening.md` — where this was flagged and deferred.
