# Plan: layered comments in the .asm demos

A working note for how the per-demo MIPS files under `src/` are
annotated.  The repository is teaching material, so the comments
are the product — readability and pedagogical clarity matter more
than terseness.

> **Status (end of 2026-05-17):** Layer 5 (the PC + encoding
> prefix) was **removed** in a late-evening simplification.  Bill
> decided that folding the `-explain=4` trace into the .asm files
> created two sources of truth and added clutter; students who
> want encoding detail run `spimulator -explain 4` themselves.
> All 17 demos (01–17) now use only L1–L4.

## Pedagogical principle

> "Only introduce something if it teaches them something familiar
> already, but in a new light, given more complicated programs."
> — Bill, 2026-05-17

Every new concept lands at the demo where the previous demos have
built the simpler picture and a new wrinkle now needs it.  Not
before.

## Stack-frame pacing — decision record

(Formerly the open `FIXES-pending.md` §1, kept here so the
reasoning is captured in one place.)

Demos 01–06 originally all carried a stack frame.  Most of those
frames were ceremony — locals fit in temporary registers and the
prologue/teardown did no work.  On 2026-05-17 Bill picked the pgu
pacing: **01–06 are register-only; the stack frame first appears
where it has a teaching purpose.**

| Demo | Stack | Reason |
|------|-------|--------|
| 01–03, 05, 06 | none | locals fit in $t0..$t2 |
| 04-1, 04-2    | yes  | introduces the stack; **alignment lesson** is the point — 04-1 deliberately misaligns the int32_t at 1($fp) so SPIM raises an alignment fault, 04-2 fixes it |
| 07-1, 07-2, 08-1 | yes | first demos with a `jal`/subroutine call — must save `$ra` somewhere |

04 is the only "early" demo whose stack frame survives the strip,
and it's there because the alignment fault is *the* lesson.

## The layers, with where each one is introduced

### Layer 1 — C source block (top of file)

Always.  Quoted verbatim from the matching `*-1.c`.  Bridges the
student from C, which they already know.

### Layer 2 — routine contract (`#PURPOSE` / `#VARIABLES`)

Always.  Borrowed from `/pgu/src/*.s`.  Describes what each
register is for, and (when the demo uses one) the stack frame
layout.

### Layer 3 — block-level C-statement comments

Always.  Above each chunk of assembly, the C statement the chunk
implements.

### Layer 4 — per-instruction inline mechanic comments

Always.  To the right of every instruction, in plain English,
focused on what *that* instruction does mechanically — which
register, which syscall number, which branch target.  Stays
complementary with L3 (intent vs mechanic), never duplicates it.

### Layer 5 — REMOVED 2026-05-17 evening

Originally a `# [PC]  word  →  real` prefix above every
instruction at 05 and later.  We rolled it across 05, 06, 07-1,
07-2, 08-1; Bill then called it back out as making the files
worse (two sources of truth, crowded page).  **Decision: layer 5
stays out.**  Students who want PC, encoding, or progressive
bit-layout decoding run `spimulator -explain 4 -file demo.asm`
themselves.  The trace output is the authoritative source.

The per-file "Pseudo-instructions in this file" tables and the
"Absolute `$sp`/`$fp` values are arbitrary" sidebar (07-1) went
with the strip.  07-1's `#NOTES` block kept a 3-line summary of
the absolute-vs-relative point.

`src/REFERENCE-encodings.md` stays as the `-explain` companion
doc — the R/I/J format boxes, the register-number ↔ ABI-name
table, and the "Two spim gotchas" section at its bottom are still
useful when reading trace output.

### "Absolute $sp/$fp values are arbitrary" — survives as a 3-line note

The original full-paragraph sidebar in 07-1 went with the L5
strip.  A condensed 3-line version sits in 07-1's `#NOTES` block
saying the hex values shown in `spimulator -explain` traces are
arbitrary and only the offsets matter.  Worth knowing because
the trace makes those absolute numbers concretely visible.

### Register-name discipline

All `.asm` files use the ABI name the source line uses.  The
disassembler now emits `$fp` for register 30 (it used to emit
`$s8`, the older callee-save name — see `REFERENCE-encodings.md`).
The one remaining inconsistency: register 0 prints as `$r0` in
disassembly even though sources usually write `$0` or `$zero`.

### The "set `$v0` before `jr $ra`" lesson lives in exit

After the spim Unix-process-conformance fixes (2026-05-19),
the value in `$v0` at every `jr $ra` from main becomes the
shell's `$?`.  Every syscall along the way clobbers `$v0`
with its syscall number, so the discipline for a main that
returns via `jr $ra` is to insert `li $v0, 0` (or whatever
status) on the line above.

The canonical explanation of this — including the syscall
mechanism, the syscall-17 form, and the equivalence with
`jr $ra`+`li $v0, N` — lives in **the header comment block of
`exit/exit.asm`**.  All other demos can assume the
reader has seen it.  When introducing a new demo, do NOT
repeat the lesson; just write the `li $v0, 0` line before the
final `jr $ra` (or use `syscall 17` directly with a status in
`$a0`).

## Per-file state

All 17 .asm files carry the same four layers (L1+L2+L3+L4).
Layer 5 was rolled out and then rolled back — see above.

| File range | Stack frame | Notes |
|---|---|---|
| 01–03                    | none — register-only (pgu pacing) | |
| 04-1                     | yes — the alignment-bug lesson | intentional bugs documented in #NOTES |
| 04-2                     | yes — alignment fix | swapped-syscall oddity documented |
| 05, 06                   | none — register-only | |
| 07-1                     | yes — longhand `j`/stored-continuation call | "$sp/$fp arbitrary" point lives in #NOTES |
| 07-2                     | yes — idiomatic `jal`/`jr $ra` call | |
| 08-1                     | yes — caller saves $ra | |
| 09–17  (unix-tool ports) | varies | see PLAN-unix-tools.md |

(increment-ints-2.asm was deleted on 2026-05-17 — it had become
byte-identical to -1 after the pgu strip.)

## Discipline — keep the layers from echoing each other

* **L3 (block) = intent.** What we're computing, named in C terms.
  E.g. `# i++;`, `# print_string("\n");`.
* **L4 (inline) = mechanic.** Which register, which syscall number,
  what the address actually is.  E.g. `# load i into $t0`,
  `# syscall 4 = print_string`.
* **L5 (encoding prefix) = wire format.** What the CPU actually
  sees.  Helpful exactly when a source line and the wire form
  diverge — pseudos that expand, immediates that get sign-extended,
  `la` symbols whose addresses split across `lui`+`ori`.

If two layers say the same thing, one of them comes out.

## Remaining work

Nothing required on the existing 01–08 set.  The pending tracks
are:

* `PLAN-unix-tools.md` — add sbase/ubase ports (clear, yes,
  cat-stdin, wc-stdin, etc.) and renumber-vs-append decision.
* Sphinx book — only `ch01.rst` exists; chapters for 02–08 are
  unwritten.
* The `.txt` traces under `src/*/NN.txt` were captured against the
  ORIGINAL framed source (before the pgu strip).  They're stale
  for 01–06 — re-run `spimulator -explain 4 -file ...` if Bill
  wants fresh ones for the documentation.  03.txt in particular
  was generated against the now-deleted 03-2, so it's stale by
  two changes.

## Open question

* **Inline-comment column drift.** No longer an active issue; L5
  prefix sits *above* the source line, not inline, so the inline
  column is unchanged.  Revisit if Bill ever wants L5 on the same
  line as the source.

## Notes for future Claude sessions

* `/pgu/src/*.s` is the inspiration for L2's `#PURPOSE` / `#INPUT`
  / `#OUTPUT` / `#VARIABLES` headers and for L4's right-side inline
  style.  Start with `factorial.s`, `power.s`, `count-chars.s`,
  `integer-to-string.s`.
* `/pgu/src/c/` is the inspiration for the freestanding C style
  and `os.h`'s inline-asm syscall wrappers.
* The MIPS instructions in each .asm file should NOT be modified
  when adding comments.  The intentional-teaching bugs in `04-1`
  (alignment + la-instead-of-byte) and the swapped-syscall-numbers
  oddity in `04-2` are pedagogical artifacts — comment them, don't
  silently fix them.
* Register names follow the source's ABI alias.  Never `$s8` /
  `$r0` outside `REFERENCE-encodings.md`.
* `spimulator` lives at `/spimulator/builddir/spimulator`.  For
  the smoke-test wrapper see `/tmp/run-spim.sh` (created earlier;
  may not survive a fresh container — recreate as needed).
