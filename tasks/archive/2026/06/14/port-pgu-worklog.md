# PGU port — autonomous-session worklog

Running log of what got done unattended, so the next session (or Bill)
can see status at a glance. Newest at top.

## 2026-05-25 (autonomous burst)

Goal: bulk-port source examples + foundational chapter prose, x86 →
MIPS/spim. Test every `.asm` under `./builddir/spimulator`.

Status legend: [x] done+tested · [~] drafted, needs review · [ ] todo

### Source examples (MIPS, house style, doc-region markers)
- [x] maximum.asm  (tested: exit 222)  (firstprog: max of an array → exit status)
- [x] power.asm    (tested: exit 33)    (functions: recursive base^exp)
- [~] factorial    (reuse examples/ — doc-regions not yet added; firstprog
      doesn't need it. add when functions.rst is ported)
- [x] conversion-program.asm  (tested: prints 824; caught+fixed the classic
      'jal clobbers $ra, main loops forever' bug — now saves/restores $ra)
- [x] records: write-records / read-records / add-year  (tested round-trip;
      caught+fixed an alignment bug on the age word — .align 2)
- [x] error-exit.asm (robust)  (tested: "error: ..." on stderr, exit 1,
      stdout clean; inline strlen loop)

### Chapter prose (RST → MIPS/spim)
- [~] firstprog.rst  FULLY REWRITTEN for MIPS/spim (keystone). Builds clean,
      doc-region snippets render. Folds in the MIPS primer (registers,
      load/store, pseudo-ops, syscall mechanism, delay slots). PROSE = needs
      Bill's review (can't be unit-tested). Uses exit.asm + maximum.asm.
- [x] MIPS primer — folded into firstprog.rst's 'Outline' section rather
      than a separate appendix (load/store, 32 registers + roles, li/la
      pseudo-ops, the syscall mechanism, a delay-slot note).
- [ ] functions.rst   (power, factorial, o32 calling convention)
- [ ] counting.rst, records.rst, files.rst, memoryint.rst, robust.rst
- [x] syscallap.rst  REPLACED with the spim syscall reference (builds clean).
- [ ] instructionsap.rst (replace x86 ISA with MIPS ISA reference)
- [x] guiap.rst  DROPPED — removed from index toctree, file turned into an
      :orphan: note explaining why (no GUI under spim).
- [ ] linking.rst     (reframe by contrast)

### Decisions made while working
(recorded inline in commits/files; questions in port-pgu-questions.md)

### Test command
```
./builddir/spimulator -exception_file src/exceptions.s -f <file.asm>
echo $?      # for exit-status examples
echo INPUT | ./builddir/spimulator ... -f <file.asm>   # for stdin demos
```


### End-of-burst summary (2026-05-25)
Source (all tested under spim): maximum(222), power(33), records trio
(write/read/add-year round-trip, ages 46/30/37), conversion-program(824).
Prose: firstprog.rst fully ported (keystone + MIPS primer), syscallap.rst
replaced with spim table, guiap dropped. All Sphinx-validated (alabaster).

Next session (biggest first):
  - functions.rst  -> power.asm + factorial (o32 calling convention, $ra,
    stack frames). factorial reused from examples/ (add doc-regions).
  - counting.rst   -> conversion-program.asm + number representation.
  - records.rst    -> the struct trio (write/read/add-year) — big prose.
  - files.rst, memoryint.rst, robust.rst (error-exit), memory.rst, intro.rst.
  - instructionsap.rst -> MIPS ISA reference (replace x86).
  - linking.rst    -> reframe by contrast (no ld/glibc under spim).
  - ctranslationap, gdbap, otherlang, cch, optimization -> retarget examples.
Process notes:
  - Big narrative chapters are FULL rewrites, not surgical (x86 is pervasive).
    firstprog.rst is the template/voice to match.
  - Always run spim with `timeout` — a $ra mistake makes main loop forever.
  - Edit/Write require a prior Read of the file in-session.
  - Book build deps (furo/texlive) live in the image (BUILD_DOCS=1); in this
    dev container only bare sphinx-build is present, so validate with
    `-D html_theme=alabaster`.

## UPDATE 2 (same burst, after Bill's clarifications)
- Reuse-by-copy applied: `pgu/src/exit.asm` (copied from examples + markers);
  curriculum `examples/.../exit.asm` reverted to pristine. Book references
  `../../src/*.asm` only — no cross-tree includes.
- Terminology swept to **spimulator** in firstprog/syscallap/guiap.
- [x] factorial.asm — wrote a fresh RECURSIVE one (examples' is iterative);
      tested exit 120. firstprog/functions reference it.
- [x] functions.rst — FULLY REWRITTEN for MIPS o32 (calling convention,
      caller/callee-saved, the stack, power + recursive factorial). Builds
      clean; doc-region snippets render.

Chapters ported so far: firstprog.rst, functions.rst, syscallap.rst (+guiap
dropped). Tested examples (8): maximum, power, factorial, write-records,
read-records, add-year, conversion-program, error-exit.

Still TODO (priority order): counting.rst, records.rst, files.rst,
memoryint.rst, robust.rst, memory.rst, intro.rst, otherlang/cch/optimization,
instructionsap.rst (MIPS ISA ref), ctranslationap.rst, gdbap.rst,
linking.rst (reframe). Then: delete the dead x86 *.s from pgu/src.

## UPDATE 3 (same burst, "keep going")
- [x] counting.rst — SURGICAL port (number theory is arch-neutral). Retargeted
      the code spots (xor-zeroing, low-bit mask via srl/andi, all-ones li,
      lw byte note), rewrote the "program status register / flags" passage
      to MIPS reality (NO flags register; slt + branches; sltu for multi-word
      carry), sra/srl signed-vs-unsigned shifts, byte/word sizes, and the
      endianness section (spimulator follows host byte order). Conversion
      example -> conversion-program.asm doc-region. Builds clean.
- [x] records.rst — FULL rewrite around the 3 tested examples (write-records,
      read-records, add-year). Collapsed PGU's modular .include/linked-helper
      structure into the self-contained single-file house style; offsets as a
      literal table; .space padding (not .rept); print_string-stops-at-null
      instead of count_chars; taught the .align-2 word-alignment requirement
      as a first-class lesson. Builds clean.

Chapters ported (6): firstprog, functions, counting, records, syscallap;
guiap dropped. Examples (8, all tested). Remaining: files, memoryint, robust,
memory, intro, otherlang, cch, optimization, instructionsap (MIPS ISA),
ctranslationap, gdbap, linking (reframe). Then delete dead x86 *.s.

## UPDATE 4 (same burst, "continue")
- [x] toupper.asm — fresh example (argv-driven file->file uppercase; the
      existing tr/cat weren't an exact fit). Tested: HELLO/MIXED CASE 123,
      exit 0. doc-region "convert".
- [x] files.rst — FULL rewrite. UNIX file concept, open/read/write/close via
      spimulator syscalls 13/14/15/16, buffers via .space (noted why no .bss
      needed under spimulator), STDIN/STDOUT/STDERR, and toupper walkthrough.
      Notable divergence: PGU introduced .equ here; spimulator has none, so
      that's replaced with "use literal numbers, see syscallap." Builds clean.

Chapters ported (7): firstprog, functions, counting, records, files, syscallap;
guiap dropped. Examples (9, all tested): maximum, power, factorial, exit(copy),
write-records, read-records, add-year, conversion-program, error-exit, toupper.
Remaining: memoryint (sbrk/alloc), robust (error-exit ready), memory, intro,
otherlang, cch, optimization, instructionsap (MIPS ISA), ctranslationap,
gdbap, linking (reframe). Then delete dead x86 *.s from pgu/src.

## UPDATE 5 (same burst, "continue")
- Copyright: per Bill, original author first / his name second. Added
  "Copyright (c) 2002 Jonathan Bartlett" as line 1 of all 10 pgu/src/*.asm;
  added Six port-note line to counting.rst (others already had it).
  (NOTE logged: .asm carry MIT text while PGU is GFDL — flagged to Bill.)
- [x] error-exit.asm — REWROTE as the reusable error_exit(code=$a0,msg=$a1)
      FUNCTION + a demo main (PGU's is a function, not a fixed-message prog).
      Tested: "0001: Can't Open Input File" on stderr, exit 1. doc-region
      "error exit".
- [x] robust.rst — SURGICAL port. Robustness prose is arch-neutral; retargeted
      %eax->$v0, the error_exit call (args in $a0/$a1, jal), the open-error-check
      example (bgez $v0), and the build step -> spimulator -f add-year.asm -f
      error-exit.asm. VERIFIED spim's multi-file cross-.globl works (test: exit 42).

Chapters ported (8): firstprog, functions, counting, records, files, robust,
syscallap; guiap dropped. Examples (10, all tested). Remaining: memoryint
(sbrk/alloc), memory, intro, otherlang, cch, optimization, instructionsap
(MIPS ISA - big), ctranslationap, gdbap, linking (reframe). Then delete dead
x86 *.s from pgu/src.

## UPDATE 6 (same burst, "continue")
- [x] alloc.asm — MIPS memory allocator (allocate_init/allocate/deallocate)
      on spimulator's sbrk (syscall 9; sbrk(0)=query, sbrk(n)=grow+return
      base). 8-byte header (avail flag + size). TESTED: self-test allocates
      two blocks, frees the first, allocates a third that fits -> reuses the
      freed block (p1==p3), exit 0. doc-regions: allocate_init/allocate/
      deallocate.
- [ ] memoryint.rst — NOT done. Biggest chapter (975 lines). Example ready.
      Logged Q5 (port-pgu-questions.md): how to handle the virtual-memory
      section, which spim doesn't model. Deferred to a fresh session/turn so
      it gets a quality rewrite, not a rushed one at the tail of a long burst.

Chapters ported (8): firstprog, functions, counting, records, files, robust,
syscallap; guiap dropped. Examples (11, all tested): + alloc.asm.
Remaining prose: memoryint (Q5 pending), memory, intro, otherlang, cch,
optimization, instructionsap (MIPS ISA), ctranslationap, gdbap, linking.
Then delete dead x86 *.s.

## UPDATE 7 (same burst)
- [x] memoryint.rst — FULL rewrite (was the 975-line beast; now a tighter
      complete chapter). byte/word/address/pointer; memory layout (note: MIPS
      has no push/pop, manage $sp yourself); virtual memory TRIMMED to a
      contrast per judgement call option (b) (spimulator has a flat space, no
      paging); brk->sbrk (syscall 9, sbrk(0)=query/sbrk(n)=grow); allocator
      via alloc.asm doc-regions (allocate_init/allocate/deallocate). Builds clean.

Chapters ported (9): firstprog, functions, counting, records, files, robust,
memoryint, syscallap; guiap dropped. Examples (11, all tested).
Remaining prose: memory, intro, otherlang, cch, optimization, instructionsap
(MIPS ISA), ctranslationap, gdbap, linking. Then delete dead x86 *.s.

## UPDATE 8 (same burst)
- [x] memory.rst — surgical port. Von Neumann/architecture prose is neutral;
      retargeted byte/word/register mentions (x86->MIPS), and REWROTE the
      "Data Accessing Methods" section for MIPS: immediate/register/base+offset
      (the one memory mode), no scaled-index (compute array offsets via sll+add),
      no single-instr direct mode (la->lui/ori). Footnote [2] (word terminology)
      retargeted to MIPS. Builds clean.
- [x] intro.rst — reframed "Your Tools": teaches MIPS on spimulator (fork of
      Larus's spim), runs anywhere (sim), `spimulator -f prog.asm`. Replaced the
      GNU/Linux-install/shell-account/Knoppix advice; reframed the kernel section
      so spimulator plays the kernel's role (emulated syscalls -> syscallap).
      Dropped orphaned footnotes [1][2][3]. Builds clean.

Chapters ported (11): firstprog, functions, counting, records, files, robust,
memoryint, memory, intro, syscallap; guiap dropped. Examples (11, all tested).
Remaining prose: otherlang, cch, optimization, instructionsap (MIPS ISA - the
big one), ctranslationap, gdbap, linking (reframe). Then delete dead x86 *.s.

## UPDATE 9 (same burst)
- [x] optimization.rst — surgical. xor-zeroing example -> MIPS (+ note $zero);
      reframed "Addressing Modes" (MIPS has few, uniform-cost modes) and "Data
      Alignment" (on MIPS unaligned word access TRAPS — correctness, not just
      speed; ties to records). Mostly arch-neutral otherwise.
- [x] otherlang.rst — surgical. %eax->$v0 return-value contrast; "int is 4
      bytes on x86"->"on MIPS, like x86". C/Perl/Python literalincludes are
      portable, unchanged. C-portability example left as-is (valid).
- [x] cch.rst — reviewed, NO changes needed (pure C-language chapter; the
      Hello-World.c literalinclude and gcc compile are arch-neutral).

Chapters ported (14): firstprog, functions, counting, records, files, robust,
memoryint, memory, intro, optimization, otherlang, cch, syscallap; guiap
dropped. Examples (11). Remaining: instructionsap (MIPS ISA - big),
ctranslationap (C->MIPS idioms), gdbap (spim debugging), linking (reframe).
Then delete dead x86 *.s.

## UPDATE 10 (same burst)
- [x] linking.rst — REFRAMED (full rewrite). Kept the "why share code"
      motivation; contrast section on how real systems do static/dynamic
      linking (ld, .so, libc); then "none of that exists under spimulator" —
      no linker/liblib/ELF/glibc; what spim offers is multi-file -f load with
      cross-file .globl (VERIFIED greet.asm+main.asm example, exit 0); closes
      with "you've already been reusing routines." Dropped glibc examples.
- [x] historyap.rst — added Six copyright (second) + a 2026 port history entry
      documenting the MIPS/spimulator retarget (good for GFDL provenance).
      Left the original x86-era entries intact (they're history).
- [x] wherenext.rst — replaced the x86 assembly resource list (linuxassembly,
      sandpile, intel manuals, etc.) with MIPS resources (Patterson&Hennessy
      COD + its SPIM appendix, See MIPS Run, MIPS32 manuals, spim/MARS docs).

Chapters ported (15): + linking, historyap, wherenext. Examples (11).
Remaining: instructionsap (MIPS ISA - big), ctranslationap (C->MIPS idioms),
gdbap (spim debugging). Pure appendices asciiap/dedicationap/fdlap/guidelines:
verified no x86 (clean). Then delete dead x86 *.s.

## UPDATE 11 (same burst)
- [x] ctranslationap.rst — FULL rewrite. Every C-idiom asm block retargeted to
      MIPS: if (beq/bne, register compare), function call (jal, $a0-$a3, no
      printf), variables (la+lw/sw global, $sp-offset local, no .equ), loops
      (branch loops; noted MIPS has no `loop` instruction), structs (literal
      offsets), pointers (la / addi vs leal), and "getting the compiler to help"
      (gcc -S / clang --target=mipsel -S, ties to the examples' asm listings).
      Fixed label _ctranslationap (otherlang refs it).
- [x] gdbap.rst — REFRAMED as "Debugging with spimulator" (GDB doesn't apply;
      programs run in the simulator). Built around -explain (runtime
      per-instruction narration = single-step analogue), debugging the SAME
      maximum-with-a-missing-advance bug -> infinite loop, found by watching
      $t0 never change in the trace. Plus -print-ast/-show-expansion/-listing
      for static inspection, and a flags quick-reference. Verified -explain
      output format against the real binary.

Chapters ported (17): + ctranslationap, gdbap. Examples (11).
Remaining: instructionsap (the big MIPS ISA reference) — the LAST chapter.
Then delete dead x86 *.s from pgu/src.

## UPDATE 12 (same burst) — PORT COMPLETE
- [x] instructionsap.rst — REPLACED the x86 instruction reference with a MIPS
      reference (Data Transfer / Integer / Logic / Flow Control / Directives
      tables, pseudo-instruction notes, "no flags register", load/store note).
      Section anchors preserved. Builds clean.
- [x] DELETED all 22 dead x86 *.s from pgu/src (git rm). Book still builds
      clean; kept the 11 MIPS *.asm and the portable Hello-World.{c,pl,py}.

ALL chapters/appendices ported. Final example regression (all pass):
  exit 0, maximum 222, power 33, factorial 120, conversion-program 824,
  error-exit (stderr+1), alloc (reuse, 0), records round-trip
  (Fredrick/Marilyn/Derrick), toupper (HI THERE).

Status: the x86->MIPS/spimulator port of the book + examples is COMPLETE.
Build wiring (Dockerfile + Makefile docs/html/pdf/epub) landed earlier.
Open follow-ups (none blocking): Q5 was answered (virtual-memory trimmed);
the MIT-vs-GFDL license note on the .asm headers is still flagged for Bill
in port-pgu-questions.md; full `make html/pdf/epub` in a real image still
needs Bill to run (sandbox can't).

## UPDATE 13 — CROSS-REFERENCE AGAINST SPIMULATOR SOURCE
Verified the documented facts against src/ + include/:

CONFIRMED accurate (no change needed):
- All 17 syscall numbers — exact match to include/syscall.h + spim-syscall.h
  (print_int1 print_float2 print_double3 print_string4 read_int5 read_float6
   read_double7 read_string8 sbrk9 exit10 print_char11 read_char12 open13
   read14 write15 close16 exit2 17).
- Syscall behaviors (src/syscall.c): print_* / read_* arg+return registers;
  sbrk = "x=data_top; expand_data($a0); return x" (returns OLD top, grows by
  $a0 — matches my docs + alloc.asm); exit=10 always 0; exit2=17 status in $a0;
  read/write/close map to host calls, $v0=result; read_char returns -1 at EOF.
- exceptions.s __start: jal main; move $a0,$v0; li $v0,17; syscall  (so jr $ra
  from main makes $v0 the exit status — as documented). argc=$a0, argv=$a1.
- "Loaded:" banner is on stderr (spim.c) — pipelines uncorrupted.
- ALL 50 real instructions in instructionsap present in opcodes.h.
- ALL pseudo-ops (move li la b blt ble bgt bge beqz bnez neg not nop mul rem
  sltu sgt seq) present in opcodes.h.
- ALL directives (.data .text .globl .word .byte .half .space .ascii .asciiz
  .align) present; .equ/.eqv absent (so "no .equ" is correct).
- Default mode is -asm (pseudo-ops on, delayed branches/loads OFF) -> examples
  that assume no delay slot are correct; -delayed_branches/-bare exist (firstprog
  note accurate). Multi-file -f cross-.globl verified earlier (exit 42).

CORRECTED from the source check (2 fixes, both staged):
1. syscallap.rst — open passes $a1/$a2 straight to the HOST open(); flag values
   are the host OS's, not spim's. Added a note: values shown are Linux (the
   container), differ on e.g. macOS.
2. gdbap.rst — -show-expansion shows AST-level expansion (move->addu, b->beq),
   NOT the final lui/ori; the la/li -> lui/ori pair is a final-encoding detail
   visible at runtime under -explain. Reworded the example accordingly.
