# Port "Programming from the Ground Up" to MIPS / spim

**Status:** planned — all scope decisions made; ready to start Phase 0
**Started:** 2026-05-25

## Goal

Bring the book *Programming from the Ground Up* (PGU) — a beloved
intro-to-assembly book that teaches x86-32 Linux assembly to
newcomers — into `/spimulator/pgu`, rewriting its assembly examples
(and the prose that explains them) to **MIPS running on spim**. The
pedagogical spine is unchanged and is the whole point: *assembly is
taught through the Unix process model.* spim is "just another Unix
process in the host environment — it just happens to use a different
assembly language." A program sets a status code the shell sees as
`$?`, reads stdin, writes stdout, opens files, takes argv. PGU
teaches x86 through that lens; this port teaches MIPS through the
same lens. A Makefile target builds the book (Sphinx → HTML/PDF/EPUB)
the way `/pgu` already does.

This is the realization of the "PGU rewrite targeting MIPS-on-spim"
direction recorded in `/pgu/SESSION_NOTES.md` (2026-05-19). Its
simulator-side prerequisite — spim behaving as a well-behaved Unix
process (exit status, pipeline-clean stdout, failure exit codes) —
**already landed** (see `tasks/archive/unix-process-conformance.md`),
so the port is unblocked.

---

## Analysis

### What PGU is, physically

- **The book**: 26 Sphinx RST files in `/pgu/docs/source/` — 13 main
  chapters + ~11 appendices + index. Ported from upstream DocBook
  (`/pgu/upstreamSource/`); that porting QA is largely done.
- **The examples**: hand-written x86-32 `.s` files in `/pgu/src/`
  (plus C/Perl/Python comparison snippets and the multi-arch C ports
  in `/pgu/src/c/`).
- **The build**: a podman-based `Makefile` whose `docs`/`html`/`pdf`/
  `epub` targets run Sphinx inside the container via
  `entrypoint/{html,pdf,epub}.sh`, writing to `output/pgu/`.

### How the book embeds assembly (this drives the effort model)

**11 chapters** use Sphinx `.. literalinclude:: ../../src/<file>.s` to
pull example source directly into the page; the rest use inline `::`
literal blocks typed into the RST. Chapters and their included
sources:

| Chapter | literalincludes |
|---|---|
| firstprog | exit.s, maximum.s |
| counting | integer-to-string.s, conversion-program.s |
| functions | power.s, factorial.s |
| records | record-def.s, linux.s, read-record.s, write-record.s, write-records.s, read-records.s, count-chars.s, write-newline.s, add-year.s |
| files | toupper-nomm-simplified.s |
| memoryint | alloc.s |
| robust | error-exit.s |
| linking | helloworld-nolib.s, helloworld-lib.s, printf-example.s |
| otherlang | Hello-World.{c,pl,py} |
| cch | Hello-World.c |
| guiap | gnome-example.{s,c,py} |

**Consequence:** porting a `literalinclude` example = (a) provide the
MIPS `.asm` and (b) rewrite the surrounding prose, which today
explains the x86 code line-by-line (registers, `int $0x80`). The
prose rewrite is the larger half. Inline-`::` chapters
(ctranslationap, gdbap, syscallap, instructionsap, much of counting)
need every code block retyped in MIPS.

### What ALREADY exists in spim's world (the central finding)

`/spimulator/examples/` is a **mature, comprehensive 60-example MIPS
curriculum** — paired hand-written `.asm` + freestanding C, its own
`READING-ORDER.md`, `TEACHING-ASSEMBLER-INTERNALS.md`, build
(meson), golden tests (`tests/run-demo.sh`), and shared conventions
(`os.h`/`io.h`/`crt0.h`). It already embodies the exact philosophy
this port is about. The signature `intro/exit/exit.asm` *explicitly
references PGU* and teaches `$?` via spim syscall 17.

**Overlap of PGU's signature examples with what exists:**

| PGU example | In spim examples? | Where |
|---|---|---|
| exit (status → `$?`) | ✅ yes | `intro/exit/exit.asm` |
| helloworld (no libc) | ✅ yes | `intro/helloworld/helloworld.asm` |
| factorial (recursion) | ✅ yes | `arguments/factorial/factorial.asm` |
| file read/transform/write (toupper) | ✅ equivalent | `transforms/tr/tr.asm`, `fileio/cat/cat.asm` |
| recursion + stack frames | ✅ rich | `recursion/{fibonacci,hanoi,queens,get-char}` |
| dynamic memory (brk/sbrk) | ✅ yes | `algorithms/sieve`, `transforms/tac` |
| **maximum** (max of an array) | ❌ missing | — |
| **power** (exponent loop) | ❌ missing | — |
| **conversion-program** (standalone) | ❌ missing | atoi exists only inside factorial |
| **records** (firstname/lastname/address/age struct: read-records, write-records, add-year) | ❌ missing | no struct example anywhere |

So the genuinely new example work is small and well-defined:
**maximum, power, a standalone conversion-program, and the records
trio.** Everything else has a strong MIPS equivalent already.

### spim's syscall/Unix model (what the prose retargets onto)

spim emulates a tiny OS: syscall number in `$v0`, args in `$a0–$a3`,
`syscall` traps. Relevant services: print_int(1), print_string(4),
read_int(5), sbrk(9), exit(10), print_char(11), read_char(12),
open(13), read(14), write(15), close(16), **exit2(17 — status in
`$a0`, the `$?` mechanism)**. This is a clean, honest analogue to
PGU's `int $0x80` table — the syscall *appendix* becomes a spim
syscall table, and the mechanism prose maps almost 1:1.

### Per-chapter porting difficulty (condensed)

- **Trivial (prose ~arch-neutral, copy with light edits):** intro,
  asciiap, dedicationap, historyap, fdlap, guidelines, wherenext,
  memory (32-bit either way), optimization.
- **Moderate (rewrite asm + retarget prose, concept maps cleanly):**
  firstprog, counting, functions, robust, otherlang, cch, gdbap.
- **Hard (asm rewrite + substantial prose rethink):** records, files,
  memoryint (sbrk maps, but struct/IO prose is x86-shaped),
  ctranslationap (every block is x86).
- **Replace wholesale:** instructionsap (x86 ISA ref → MIPS ISA ref),
  syscallap (Linux int-0x80 table → spim syscall table).
- **Doesn't exist under spim — drop or reframe:** guiap (no GUI in
  spim — drop), the **glibc-linking** material in `linking` plus
  `helloworld-lib.s` / `printf-example.s` (spim has no dynamic
  linker/glibc; reframe `linking` to spim's multi-file `-f a.asm -f
  b.asm` shared-memory model + the `la`/`lui`/`ori` symbol story, or
  drop the libc half).

### MIPS-specific material PGU never had to teach (new sections)

Load/store architecture (no memory operands in ALU ops; every access
is `lw`/`sw`), 32 registers + the o32 register-role discipline
($a0–$a3, $s0–$s7 callee-saved, $ra, $zero), branch **delay slots**,
`la`/`li` pseudo-op expansion (`lui`+`ori` hi/lo split), and "there is
no ELF/linker here." spim's own teaching flags (`-print-ast`,
`-show-expansion`, `-listing`, `-explain`) are a gift here — the book
can show source → pseudo-op → real instruction → bytes, which PGU
could never do.

---

## Source-embedding convention: named doc-regions (DECIDED)

The book does **not** `literalinclude` whole files. Source files are
annotated with named region markers, and the RST pulls focused
snippets between them. In MIPS asm (comment char `#`):

```asm
# doc-region-begin exit syscall
        li $a0, 0           # the status code the shell sees as $?
        li $v0, 17          # syscall 17 = exit2 (status in $a0)
        syscall
# doc-region-end exit syscall
```

and in the chapter RST:

```rst
.. literalinclude:: ../../src/intro/exit/exit.asm
   :language: gas
   :start-after: doc-region-begin exit syscall
   :end-before: doc-region-end exit syscall
   :lineno-match:
   :caption: intro/exit/exit.asm
```

This is the same pattern Bill uses elsewhere (e.g. modelviewprojection
`demo05.py` doc-regions). Benefits: one runnable, tested file feeds
many targeted excerpts; prose discusses a small region at a time;
`:lineno-match:` keeps line numbers honest against the real file.
Implication: any reused `examples/` file gets doc-region comment
markers added (benign comments; see Decision 1 for in-place vs. copy).

## Proposed approach

1. **Scaffold `/spimulator/pgu` by copying `/pgu` as-is** (book RST +
   Sphinx `conf.py` + build scripts), per "take all the source from
   /pgu as is." Honest starting point; preserves PGU's structure,
   narrative arc, and attribution.
2. **Examples: reuse what fits, fresh for the gaps (DECIDED).** Where
   an existing `/spimulator/examples` MIPS file already does exactly
   what a chapter needs, **use it** (add doc-region markers; do not
   rewrite). Author only the genuine gaps fresh in MIPS — **maximum,
   power, conversion-program, the records trio** — in the existing
   house style (license header, PURPOSE, symbol table, PGU
   cross-reference like `exit.asm` has). Don't re-port what already
   works.
3. **Rewrite prose chapter-by-chapter**, retargeting registers/
   syscalls/mechanism onto MIPS+spim and inserting the new
   MIPS-only sections where they belong (load/store early; delay
   slots and pseudo-op expansion around firstprog/counting).
4. **Replace** instructionsap → MIPS ISA reference, syscallap → spim
   syscall table. **Drop** guiap. **Reframe** `linking` to spim's
   reality.
5. **Build:** a `Makefile` in `/spimulator/pgu` with a book target
   (Sphinx → HTML/PDF/EPUB) mirroring `/pgu`'s, adapted to spim's
   container story (Dockerfile + meson world rather than the podman
   Makefile). Optionally add a Dockerfile layer so the book builds in
   the image.

### Phasing (each phase is a reviewable chunk; none auto-starts)

- **Phase 0 — scaffold + build skeleton.** Copy `/pgu` → `/spimulator/pgu`;
  get an *unmodified* Sphinx build working under spim's toolchain via
  the new Makefile target (prove the pipeline before touching
  content). Decide the examples-housing question (fork 1).
- **Phase 1 — foundation.** intro, memory, firstprog (exit + maximum),
  the syscall-mechanism prose, plus the new MIPS-primer material
  (load/store, registers, delay slots, pseudo-ops). Establishes the
  voice for the rest.
- **Phase 2 — core.** counting (integer-to-string, conversion-program),
  functions (power, factorial), robust (error-exit).
- **Phase 3 — data & files.** records (the struct trio — the biggest
  new authoring), files (toupper → reuse tr/cat patterns), memoryint
  (alloc via sbrk).
- **Phase 4 — appendices & reframes.** syscallap (spim table),
  instructionsap (MIPS ISA), ctranslationap (MIPS blocks), gdbap
  (spim/`-explain` debugging), linking reframe, drop guiap.
- **Phase 5 — comparison/closing + polish.** otherlang, cch,
  optimization, wherenext; full book build clean in all three
  formats; cross-link to the `examples/` curriculum.

---

## Decisions (all made)

1. **Examples: reuse in place, fresh for gaps.** Where an existing
   `/spimulator/examples` MIPS file already does exactly what a
   chapter needs, the book `literalinclude`s it **in place** from
   `../../examples/src/...` and the doc-region markers are added to
   that file (benign comments; book and curriculum share one tested
   source). Author fresh, in `pgu/src/`, only the genuine gaps —
   **maximum, power, conversion-program, the records trio.** Don't
   re-port what already works.

2. **Un-portable chapters.** **Drop** guiap (no GUI in spim).
   **Reframe `linking` by contrast** — real Linux links glibc with
   `ld`/ELF; spim has no linker, so the chapter teaches spim's
   multi-file `-f a.asm -f b.asm` shared-memory model and the
   `la`→`lui`/`ori` symbol-resolution story instead (the contrast is
   itself a Unix lesson, fits the "spim is just another Unix process"
   framing); drop `helloworld-lib.s` / `printf-example.s`. **Replace**
   the x86 instruction + syscall appendices with MIPS/spim tables.

3. **Build: standalone Makefile, no image wiring (yet).** A
   `pgu/Makefile` with a Sphinx book target (HTML/PDF/EPUB) like
   `/pgu`, run on demand. Do **not** wire it into the spimulator
   Dockerfile for now (avoids adding the Sphinx/LaTeX toolchain to the
   image); revisit if we later want the book to ship in the container.

4. **Scope: core arc first.** Land Phases 0–3 (scaffold + build →
   intro/memory/firstprog → counting/functions/robust →
   records/files/memoryint) — the spine that teaches "assembly via the
   Unix process" — then review before doing the appendices and
   comparison chapters (Phases 4–5).

---

## Open questions / notes

- **Licensing/attribution.** PGU's prose is under the GNU FDL
  (`fdlap.rst`); the spim examples are MIT (Bill's). A derived/rewritten
  book must keep the FDL lineage + document the modifications
  (historyap is the place). Worth getting right early.
- **`la`/large-address reality.** Confirm spim's `la` expansion and
  whether the book should show `lui`/`ori` explicitly (the
  `-show-expansion` flag makes this teachable).
- **Records struct layout under spim.** firstname/lastname/address/age
  — pick offsets/alignment that read cleanly in MIPS (word-aligned
  fields); this is the one genuinely new data-structure lesson.
- **Relationship to `examples/tasks/PLAN-build-matrix.md`** (multi-arch
  C→asm listings) and the just-landed native asm-listings Makefile —
  the book can point at those as "see the compiler's translation,"
  reinforcing the C-vs-asm thread PGU already has.

---

## Sources read for this analysis

- `/pgu`: docs/source/*.rst (chapter + literalinclude map), src/*.s
  inventory, top-level Makefile + entrypoint build, SESSION_NOTES.md.
- `/spimulator/examples`: full `.asm` inventory by topic, os.h/io.h/
  crt0.h conventions, READING-ORDER.md, TEACHING-ASSEMBLER-INTERNALS.md,
  tests/run-demo.sh, intro/exit/exit.asm (the signature `$?` example).
