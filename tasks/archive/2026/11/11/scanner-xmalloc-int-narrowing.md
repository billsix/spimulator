# Review the `(int)(len + 16)` narrowing in scanner.c's line-buffer xmalloc

**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`

## Outcome

`erroneous_line` and `source_line` (`src/scanner.c`) allocated `(size_t)(int)(len + 16)` with `len`
a `size_t` from `strlen`. The inner `(int)` narrowed the size to a signed int before it was widened
again. That predated the integer-conversion pass, which added only the outer cast. Both now
allocate `len + 16`, as the `snprintf` beside each already did. Nothing changes for any real source
line.

## Verification

gcc and clang build warning-clean under the core's `-Wconversion -Wsign-conversion` gate, and the
regression suite passes.

Flagged and deferred in `tasks/archive/2026/10/16/integer-conversion-hardening.md`.
