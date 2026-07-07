# Make each demo's embedded C (in the .asm) match the real .c

**Status:** proposed — not started
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

Now that the book (pgu port) exists, the examples don't all need to be
teaching-oriented in the same way — but they do need to be *consistent*. Each
`.asm` demo embeds its paired C source as a header comment block, and for some
demos (bubble sort was the observed case) the embedded C does **not** match
the actual `.c` file next to it. Audit every pair, find the mismatches, and
make the embedded C and the real C agree.

Known example: `examples/src/algorithms/bubble-sort/bubble-sort.asm`'s
embedded C block vs `bubble-sort.c` — not the same.

(`testStringsForEquality-1.asm` even embeds an abridged C with a comment
admitting it — "same pattern for str1/str3 and str2/str3" — so some drift is
deliberate abridgment, some is real divergence. The audit should distinguish
the two and eliminate both: the embedded block should be the real C, verbatim.)

## Plan

1. **Inventory.** For every `examples/src/**/<demo>.asm` with a paired
   `<demo>.c` (and the pgu `src/` pairs if the same convention holds), extract
   the embedded C comment block and diff it against the actual `.c`
   (normalize: strip the `#`/comment prefixes, whitespace). A small script in
   the repo (`scripts/check-c-asm-parity.sh` or python) beats doing it by eye
   and can then be kept as a checker.
2. **Classify each mismatch:**
   - *Comment is stale* — the `.c` evolved; update the embedded block to the
     current `.c`, verbatim.
   - *Asm diverged from the C* — the asm implements something different from
     the `.c` (different algorithm shape, different output). Decide per demo
     which side is right, then fix the other. The paired-demo contract
     (`examples/tests/run-demo.sh` diffs C output against asm output against
     one golden) means observable behavior already matches — mismatches will
     mostly be in structure/abridgment, not behavior.
3. **Fix, demo by demo,** starting with bubble-sort (the reported case).
4. **Keep it fixed.** Wire the parity checker from step 1 into the meson test
   suite (or the Dockerfile test block) so embedded-C drift fails the build,
   the same way the goldens do.

## Decision needed before step 4

Verbatim embedding makes the checker trivial but means every `.c` edit touches
two files. Alternative: drop the embedded C entirely and point at the `.c`
file by name (the book now carries the pedagogy). That's Bill's call —
"examples don't need to be teaching oriented" suggests trimming may be
acceptable; the checker makes either policy enforceable.

## Verification

- Parity script reports zero mismatches across all pairs.
- `meson test` both suites green (behavior unchanged unless a demo's asm was
  found genuinely divergent and corrected — call those out individually).
