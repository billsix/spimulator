# Audit the code for inherited idiosyncrasies

**Status:** DONE — audit complete 2026-08-25; findings harvested to a reference doc 2026-08-27
(William Emerison Six <billsix@gmail.com>).

## Outcome

The audit is complete and its verified findings are harvested into
**`tasks/reference/inherited-idiosyncrasies.md`** (so they survive this archival). Headline: **one HIGH
latent bug** — `fatal_error` (`src/spim.c:1502-1508`) overwrites its own format string via a stray
`va_arg`, breaking all 14 error paths (independently re-confirmed 2026-08-27) — plus the "random void
argc" case (`dump-opcodes.c:61` implicit-int `main`), `xmalloc`/`zmalloc` taking `int`, a dead `kernel`
param, and catalogued comment/asm drift. Demo/book layers are clean.

**Follow-on task created:** `tasks/fix-inherited-idiosyncrasies.md` (HIGH `fatal_error` fix first, then
the mechanical fixes; cosmetic renames excluded pending direction).

The full original itemized findings table is preserved in this doc's git history (and the reference doc
points here). Lean archived record per convention.
