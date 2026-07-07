# Remove the embedded C blocks from the example .asm files

**Status:** proposed — not started; decision made 2026-07-07
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
