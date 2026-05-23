# Archived task / plan documents

Plan docs whose work has landed (or that have been superseded by
a different direction).  Kept for historical context — the
decisions, rationale, alternatives considered, and "what's NOT
in scope" sections often outlive the work itself.

## libctype (May 2026)

First teaching libc — eight ASCII character classification and
case-conversion functions adapted from musl's src/ctype/.  Pure
leaf functions, no `.data`, no `$ra` discipline — the simplest
of the three planned libraries (libctype → libstdlib → libstr).

- **[`PLAN-libctype.md`](PLAN-libctype.md)** — final-state writeup
  covering the 8 functions, the calling-convention contract,
  the worked `isdigit` example, the demo's printable-ASCII
  table, the consolidation from per-function .c files to one
  bundled .c (mirroring the .asm side), and open follow-ups
  (meson test wiring; READING-ORDER pointer once libstr/libstdlib
  also land).

Files added: `src/lib/libctype/` (library) and
`src/lib/libctype-demo/` (paired C + asm demo + golden).

## Unix toolchest tier 1+2 (May 2026)

12 new Unix-tool demos at slots 28-39: `seq`, `touch`, `factor`,
`cp`, `uniq`, `nl`, `cut`, `od`, `tac`, `tail`, `comm`, `base64`.
Each paired C + asm, smoke-tested against the real system tool
where applicable.

- **[`PLAN-tier1-tier2-tools.md`](PLAN-tier1-tier2-tools.md)** —
  final-state writeup of all 12 demos with their per-demo
  teaching ideas, and notes on the two non-obvious defects fixed
  during smoke-testing (`.asciiz` octal-escape decoder in spim;
  `$at`-reservation papercut).

## Remove hardcoded inputs (May 2026)

Six earlier demos had hardcoded inputs replaced with argv or
stdin: fizzbuzz (`N` from argv), bubble-sort (ints from stdin +
`$a3` EOF), pascals-triangle (`rows` from argv), sieve (`LIMIT`
from argv + sbrk-allocated bit array), binary-search (target
from argv + sorted ints from stdin), queens (generalized to
N-queens from argv).

- **[`PLAN-remove-hardcodes.md`](PLAN-remove-hardcodes.md)** —
  per-demo rewrite plan, what changed for each, the new asm
  patterns each demo lands.

## Stdin-or-file argv convention (May 2026)

Standardized the "this demo reads stdin OR a file" argv shape
across the filter demos (`wc`, `head`, `rev`, `expand`, `cat`,
`cksum`).  Convention: `demo` reads stdin, `demo -` reads stdin
(explicit), `demo FILE` opens FILE.  Pure-filter demos (`tr`,
`rot13`) take no file arg to match real Unix behavior.

- **[`PLAN-stdin-or-file.md`](PLAN-stdin-or-file.md)** —
  convention writeup, per-demo rewrite notes.

## Library curriculum sketch (superseded May 2026)

Initial sketch for introducing students to the concept of a
library by leaning into spim's multi-file `load` accumulation.
Three UX options proposed (REPL-only, concatenation wrapper,
multi-`-f` in spim).  Sketch was written 2026-05-23 and pivoted
the same day — Bill chose to go in a different direction with
a more focused musl-derived libc port (libctype, libstdlib,
libstr).

- **[`PLAN-libraries.md`](PLAN-libraries.md)** — the original
  sketch.  Useful as historical record for "why we landed on a
  per-namespace .c+.asm library structure rather than a single
  libio.asm with everything."  The multi-`-f` recommendation
  from this doc did land — see
  [`/spimulator/tasks/cli-multi-file-load.md`](../../../tasks/cli-multi-file-load.md).
