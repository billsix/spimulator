# Separated I/O from the algorithm (+ unit tests) for cksum and bubble-sort

**Completed:** 2026-10-30 (away batch, branch `separate-io-remaining-demos`; gpg signing was turned
off for the batch and restored after)

This carried the I/O-separation + unit-test pattern from the two little-language evaluators (`calc`:
`tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md`; `rpn`:
`tasks/archive/2026/10/30/rpn-separate-io-and-unit-tests.md`) to the other I/O-coupled demos, at the
request of William Emerison Six <billsix@gmail.com> (2026-10-30). The method, including the lessons
learned here, is in `tasks/reference/incremental-example-decomposition.md`.

The discriminator was that separation and unit tests pay off when the algorithm is a nontrivial pure
function hiding inside an I/O loop. Where the algorithm is the I/O loop (a per-byte map such as
`tr`, `rot13`, `yes`, `clear`, `touch`, or `head`'s "stop after N lines"), the split only renames
the loop, so those demos were left whole.

## Decisions (2026-10-30, agent discretion; the maintainer authorized "cksum and bubble-sort, and
anything else" while away)

1. Scope was `cksum` and `bubble-sort`, both with a clean pure seam and existing goldens to reuse.
   `wc` was left whole: its algorithm is a two-counter byte loop with no nontrivial pure core, and
   it had no golden baseline, so decomposing it first needs its own golden and run-demo wiring, a
   coverage-gap task rather than I/O separation.
2. `strings` was left whole. Its printable-run state machine is testable but closer to "the loop is
   the lesson".
3. Naming used the uniform `-core` / `-core-test` suffix; the evaluators keep their verb-named
   `-eval` / `-eval-test`.

## What was done

- **cksum** (`examples/src/fileio/cksum/`) got both variants. `cksum-core.{c,asm}` factors the POSIX
  CRC32 into a pure `unsigned int cksum_of(const unsigned char*, unsigned int)`, with `my_main`
  reading stdin and calling it; it shares `cksum.expected`. This factoring was new, since
  `cksum-1-stdin` computes inline. `cksum-core-test.{c,asm,expected}` drives `cksum_of` over a
  hardcoded battery (empty, `"a"`, `"abc"`, `"hello"+LF`, `"123456789"`) printing
  `<label> => <crc> <count>`, with the golden taken from the system `cksum` as oracle. Gotcha:
  spim's assembler renders `\n` inside an `.asciiz` label as a real newline, so that case's label is
  written `"hello"+LF`; the data buffer still holds a real newline.
- **bubble-sort** (`examples/src/algorithms/bubble-sort/`) got only
  `bubble-sort-core-test.{c,asm,expected}`. `bubble_sort(int*, int)` was already a pure function
  called from `my_main`, and `bubble-sort-1-fixed` already runs it on a fixed array, so a `-core`
  program would have duplicated the finished demo. The battery covers unsorted, sorted, reversed,
  duplicates, single, empty and negatives, printing `<input> => <sorted>`.
- Each demo was wired into `examples/src/meson.build` (build list and `lib_demo_tests` golden list),
  `examples/tests/run-demo.sh` and `examples/READING-ORDER.md`. `examples/src/Makefile` needed no
  edit: it globs `*.c` and no new lib was added.

## Results

The examples suite passed 33/33, with both the C and the asm byte-diffed against goldens, and the
containerized `make image` gate passed.
