# Remove the embedded C blocks from the example .asm files

**Status:** DONE — executed and archived 2026-07-07. Removed 466 lines of
transcribed-C header blocks across 38 `.asm` demos (script-driven: maximal
column-0 comment runs in the file header whose lines are blank-`#` or
3+-space-indented and that contain a line ending in `{` or a lone `}` —
verified against a marker-anchored pass to catch the two files whose
`# C source — see` line runs into prose). The `# C source — see <demo>.c`
pointer lines were kept; the 8 demos whose block was a prose summary (comm,
base64, cut, nl, od, tac, tail, uniq) were left intact, as were the lib
files' small per-function `# C:` contract snippets (different convention —
flag to Bill separately if those should go too). Verified: all 55 demos load
clean under spimulator, helloworld + bubble-sort produce correct output,
regression suite 29/29. The reintroduction-checker (step 3) was skipped as
not-yet-earning-its-keep; trivial to add later.
**Created:** 2026-07-07

## Request (Bill, 2026-07-07, two rounds)

Originally: the C source embedded as a comment block at the top of each demo
`.asm` has drifted from the real `.c` next to it (bubble-sort was the
observed case), and with the book now carrying the pedagogy, the examples
don't all need to be teaching-oriented — so bring them back into agreement.

**Decision (Bill, same day): don't sync them — delete them.** For all the
examples, the commented-out C at the top of the `.asm` should simply be
removed. The paired `.c` file sitting in the same directory *is* the C
reference; a transcription of it in comments is a second source of truth that
only drifts.

## Scope

- Every `examples/src/**/<demo>.asm` (and `-1.asm`/`-2.asm` variants) that
  carries a transcribed-C comment block: **remove that block**.
- **Keep** everything else in the headers: `#PURPOSE`, `#SYMBOL TABLE`,
  `#STORAGE LAYOUT`, `#VARIABLES`, intentional-bug NOTEs, and all inline
  body comments. Only the "here is the C source, commented out" block goes.
- Where useful, leave a one-line pointer in its place
  (`# C version: ./bubble-sort.c`) — cheap, can't drift.
- `pgu/` is out of scope — that's the book tree, different conventions.

## Plan

1. Inventory which `.asm` files carry an embedded-C block (grep for the
   telltale transcribed-C markers — `# int `, `# while (`, `#   return`,
   `# __attribute__((noreturn))` etc. — then eyeball the hit list; the block
   shape varies a little per file).
2. Remove the blocks file by file, most-recently-touched demos first
   (bubble-sort as the template case). Add the one-line `# C version:`
   pointer where the header doesn't already name the pairing.
3. Guard against reintroduction: a small checker (grep-based is enough) that
   fails if a `.asm` re-grows a transcribed-C block; wire into the test
   suite or lint.sh if it earns its keep — decide once the sweep is done.

## Verification

Comment-only change: `meson test` both suites must stay green with **zero
golden diffs** (the demos' output can't change from deleting comments). Spot
re-read a couple of the edited files to confirm the surviving header blocks
(#PURPOSE / #SYMBOL TABLE / #STORAGE LAYOUT) still read coherently without
the C block they sometimes referenced.

## Coordination

- [`string-equality-audit.md`](string-equality-audit.md) is auditing demo
  comparison routines in the same files — if a fix lands there, it edits the
  real `.c`/`.asm` only; nothing to keep in sync once the embedded blocks are
  gone (which is the point).
- [`code-idiosyncrasies-audit.md`](code-idiosyncrasies-audit.md) reads all
  the demo files anyway; doing this removal first shrinks what that audit has
  to read past.
