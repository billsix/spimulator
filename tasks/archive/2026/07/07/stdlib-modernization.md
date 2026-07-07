# Replace hand-rolled utilities with their standard-library equivalents

**Status:** DONE — archived 2026-07-07.  All four "do" rows executed:
both `map_*_to_name_val_val` searches are now `bsearch(3)` with matching
comparators, `zmalloc` is `calloc(3)`-backed (fatal wrapper kept), and
`str_copy` is deleted with all ~27 call sites on `strdup(3)` (Tier B2 of
codebase-cleanup-plan resolved per its recommendation).  Verified: full
meson suite green and the 25-test regression suite green under the ASan
build of spim.
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

After the `str_stream` → `open_memstream` replacement: "anything more you
see like that" — custom code predating (or duplicating) a standard
facility — write it up, do it, archive it.

## Survey

Swept `src/` + `include/` for hand-rolled equivalents of libc/POSIX:

| Site | Custom thing | Standard replacement | Verdict |
|---|---|---|---|
| `spim-utils.c` `map_string_to_name_val_val` | hand-rolled binary search (char-walk compare) | `bsearch(3)` + `strcmp` comparator | **do** |
| `spim-utils.c` `map_int_to_name_val_val` | hand-rolled binary search on `value1` | `bsearch(3)` + int comparator | **do** |
| `spim-utils.c` `zmalloc` | `malloc` + `memset` | `calloc(3)` (keep the fatal-on-NULL wrapper — that's policy, not reimplementation) | **do** |
| `spim-utils.c` `str_copy` | `xmalloc` + `strlcpy` = strdup by hand | `strdup(3)` (this is codebase-cleanup Tier B2, resolved per its own recommendation) | **do** |
| `instruction.c` sort_*_table | — | already `qsort(3)` | nothing to do |
| `spim.c` `read_input` | byte-wise fd reads | deliberate (leaves stdin buffered for the next syscall; console handling) | keep |
| `spim.c` `str_prefix` | prefix match with min-length | no standard equivalent (command abbreviation) | keep |
| `xmalloc` | fatal-on-NULL malloc wrapper | policy wrapper, not a reimplementation | keep |

## Notes / tradeoffs

- **bsearch comparators must match the tables' sort order.** The string
  tables (scanner keyword table from opcodes.h, register table) are
  lexicographically sorted; the old char-walk compare and `strcmp` agree
  on ordering for these all-ASCII keys (the old walk compared plain
  `char`, strcmp compares `unsigned char` — differs only for high-bit
  bytes, which no mnemonic/register name contains). The int tables are
  qsorted at init by the same `value1` the comparator reads.
- **`str_copy` → `strdup` changes the OOM story** from fatal_error to a
  NULL return that ~25 unchecked call sites would dereference. Accepted
  per the Tier B2 recommendation ("strdup is standard and shorter"): spim
  is a short-lived teaching process, and an OOM segfault vs. an OOM
  fatal_error is a distinction without a practical difference here.
- `strlcpy` usage drops from 2 sites to 1 (`spim.c` keeps its bounded
  copy, which is a genuine strlcpy use).

## Verification

Full meson suite + the regression suite under an ASan build of the
`spimulator` target (same playbook as the memstream change).
