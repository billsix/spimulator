# PGU port — questions for Bill (logged during autonomous session)

Questions raised while working unattended on the x86→MIPS/spim port.
None block progress; I made a judgment call (recorded) and kept going.
Review at leisure; flip any call you disagree with.

## Open questions

1. **Where do the freshly-authored gap examples live?** The plan says
   `pgu/src/` for gaps (maximum, power, conversion-program, records).
   I'm writing them as `pgu/src/<name>.asm` in the spim examples house
   style. Alternative: add them to `examples/src/...` too so the
   curriculum gains them. For now they live only in `pgu/src/`.
   Decision: pgu/src/ only. OK?

2. **Reused examples: literalinclude path.** For examples that already
   exist in `examples/`, the book chapter literalincludes across the
   repo, e.g. from `pgu/docs/source/firstprog.rst`:
   `../../../examples/src/intro/exit/exit.asm`. That triple-`..`
   reach-across works but couples book↔curriculum. Confirm you're OK
   with the cross-tree include vs. wanting copies in `pgu/src/`.

3. **Dead x86 `.s` files in `pgu/src/`.** As I port each chapter I'm
   removing the now-unreferenced x86 `.s` (e.g. `exit.s`, `maximum.s`).
   If you'd rather keep them around for side-by-side x86-vs-MIPS
   contrast, say so and I'll stop deleting.

4. **records struct layout.** PGU's record is firstname(64) /
   lastname(64) / address(128) / age(4) = 260 bytes (x86, byte-packed).
   For MIPS I'm keeping the same field sizes but the record is
   word-aligned (260 is already a multiple of 4). Good?

(Will append more as they arise.)

## Resolutions (answered by Bill, 2026-05-25)

1. Gap examples live in `pgu/src/` — confirmed ("wherever you suggest").
2. **Reuse = COPY into a consistent spot, do NOT cross-reference across
   the repo.** Applied: reused examples are copied into `pgu/src/`
   (same home as the gap examples); the book literalincludes from
   `../../src/<name>.asm` only. Reverted the doc-region edit I had made
   to `examples/src/intro/exit/exit.asm`; `exit.asm` now lives copied
   at `pgu/src/exit.asm`.
3. Dead x86 `.s` files: keep during porting, **delete by the end**.
4. Word alignment: good (kept `.align 2` on the record buffer).

Terminology: the simulator is **spimulator** (a fork of James Larus's
spim). Swept bare "spim" -> "spimulator" in the prose written so far;
kept one lineage mention in firstprog ("a fork of James Larus's spim").

NOTE for a later cleanup pass: PGU's factorial is *recursive* (the
recursion lesson), but examples/arguments/factorial is *iterative* — so
it was NOT an exact reuse. I wrote a fresh recursive `pgu/src/factorial.asm`
(tested, factorial(5)=120). The iterative examples/ one is untouched.

## Q5: memoryint.rst — the virtual-memory section (needs your call)

memoryint.rst's middle section, "Every Memory Address is a Lie," teaches
real-OS **virtual memory**: page tables, swap, physical-vs-virtual
addresses, the kernel's page-fault handling. spimulator does NOT model
any of this — it presents a flat, simple address space, and `sbrk` just
hands back more of it. Options for the port:

  (a) KEEP it as a "how real systems work" contrast section, with a note
      that spimulator simplifies all this away (educational, honest).
  (b) TRIM it to a short "real machines add a virtual-memory layer;
      spimulator doesn't, so addresses here are real offsets into one
      flat space" and move on.
  (c) DROP it.

Lean: (b) — a short contrast keeps the concept without implying spim
does paging. Tell me which you want; it's the one nontrivial decision in
this chapter. The rest is mechanical (brk -> sbrk, the allocator
walkthrough -> alloc.asm doc-regions, register retargeting).

Also: alloc.asm is written + TESTED (allocate_init/allocate/deallocate;
self-test confirms a freed block is reused). The chapter prose is the
remaining work and is the single largest rewrite left.
