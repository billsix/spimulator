# Fix the verified idiosyncrasies (HIGH fatal_error bug first)

**Status:** proposed — needs go-ahead. Created 2026-08-27 (William Emerison Six <billsix@gmail.com>).
**Priority:** 4
**Difficulty:** 3

## Goal

Land the mechanical correctness fixes from the completed audit, harvested into
`tasks/reference/inherited-idiosyncrasies.md`. Scope: **the self-justifying correctness fixes only** —
cosmetic renames/demo-name fixes are excluded (they need maintainer direction).

## Plan

- [ ] **FIRST, alone: the HIGH bug** — `src/spim.c:1502-1508` `fatal_error`: delete the
      `fmt = va_arg(args, char*);` line (it overwrites the format string), add `va_end(args)`. This bug
      breaks all 14 error paths (UB/segfault/wrong output) — fix and verify it independently of the rest.
- [ ] `src/dump-opcodes.c:61` — `main(int argc, char** argv)` → `int main(void)` (implicit-int; args
      unused).
- [ ] `src/spim-utils.c:442,451` — `xmalloc(int size)`/`zmalloc(int size)` → `size_t size`.
- [ ] `src/parser.c:328-332` — drop the dead `kernel` parameter.
- [ ] Verify each in a small mechanical commit: **`meson test`** suite + the **`make image`** ASan gate
      (the HIGH `fatal_error` fix should visibly un-break error-path tests).

## Notes

Comment/code drift and copy-paste asm residue are catalogued in the reference doc but are lower value and
partly need direction — not in this pass. No open questions for the correctness fixes above.
