# Plan: per-demo symbol tables (C variable → MIPS location)

## Goal

For each in-scope `.asm` demo, replace the existing terse
`#VARIABLES` header block with a **symbol-table** block that
explicitly maps every C variable in the paired `-1.c` source to
its MIPS location — either a register name (`$s1`, `$t9`) or a
stack offset (`-4($fp)`).  Where a stack-resident value is
temporarily hoisted into a register for a sequence of ops (the
common case around `jal` because `$t`-regs are caller-save),
call that out too.

Where a demo carries a real stack frame, prepend an ASCII-art
diagram showing the frame layout before the symbol table.

## Why this helps a novice

The existing `#VARIABLES` block answers "which registers does
this file use?"  A novice working through the demo needs to
answer a different question: "where does the C variable I'm
following live right now, and where does it live at the next
line?"  Today they have to reconstruct that mapping by reading
every `lw`/`sw`/`move` in the body.

Three concrete wins:

1. **Cross-call discipline becomes visible.**  The hardest-won
   convention in MIPS (callee-save `$s`-regs survive `jal`,
   caller-save `$t`-regs do not) is currently inferred from
   the existence of `move $s1, $v0` lines.  A "Cross-call
   saves" subsection names the convention directly.
2. **Stack-frame demos become legible.**  04, 07, 08 lay out
   frames explicitly — the lesson is "data lives at numerical
   offsets from $fp."  An ASCII frame diagram + slot ↔ name
   table makes the offsets self-documenting instead of forcing
   the reader to compute them.
3. **Spill/reload patterns become teachable.**  When a stack
   slot is loaded into a register, used for three lines, and
   then dropped, that pattern is the *point* of demos like 07
   and 08.  Saying "m: 0($fp), temporarily in $t0 around line
   90" puts the lifetime on the page.

## Format

### A.  Symbol table (every in-scope demo)

```
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In <function-name>:
#     <C name>      <location>            <free-form notes if needed>
#     ...
#
#   In <next-function>:
#     ...
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $sN   <- <what it holds>     <when it's live>
#     ...
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg / function arg
#     $v0   syscall selector / return value
#     $tN..$tM  scratch (and what they're scratch FOR)
```

Rules:

- One section per C function present in the asm (`main`, plus any
  per-demo subroutines like `atoi`, `str_eq`, `print_uint`).
- A location is exactly one of:  `$reg`,  `<offset>($fp)`,
  `<offset>($sp)`, or `<.data label>` (for buffers / constants
  that have a symbolic address rather than a runtime location).
- The "notes" column is free-form prose for anything non-obvious
  — typically a brief "temporarily in $tN around line NN"
  pointer when a stack slot is hoisted into a register for a
  short window.
- The **Cross-call saves** subsection is mandatory whenever the
  demo issues `jal` (even to its own subroutines).  It is the
  load-bearing reason the table exists.
- Skip the **Volatile** subsection if everything in it is
  already obvious from the other sections (e.g. one-syscall
  demos).

### B.  Stack frame diagram (frame-carrying demos only — 04, 07, 08)

Prepended to the symbol table, NOT a replacement for it:

```
#STORAGE LAYOUT
#
#   Stack at peak (...annotate when the diagram applies):
#
#         higher addresses
#           +-------------+
#    $fp -> | <slot 0>    |   0($fp)    \
#           | <slot 1>    |   4($fp)     |  <what this group is>
#           | ...         |              /
#           +-------------+
#           : (gap, if any) :
#           +-------------+
#    $sp -> | ...         |
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#   ...
```

Rules:

- Only for demos with a real stack frame.  Register-only demos
  (everything from 09 onward except cksum's print_uint scratch)
  do not get the diagram.
- The diagram shows the frame at its peak occupancy.  If $fp and
  $sp move during the demo, annotate which moment the diagram
  describes.
- Lower addresses below, higher addresses above — MIPS convention.

### C.  Inline annotations in the body

Already-existing `# $t0 <- m` style inline comments on `lw`/`sw`
lines carry the spill/reload information at the use site.  Do
**not** add a second `(m: 0($fp) -> $t0)` trailer to those lines
— it duplicates and creates drift.

The one place to add an explicit cross-reference is at a `jal`
call site where the symbol-table's "Cross-call saves" entry would
be most relevant to a reader following the body:

```
        # print_uint(result)
        move $a0, $s2                # arg <- result   ($s0/$s1/$s2 all
                                     #                  survive `jal` by
                                     #                  convention; see
                                     #                  symbol table)
        jal print_uint
```

One such marker per cross-call boundary is enough; resist
peppering them.

### D.  Replacing #VARIABLES

The new `#SYMBOL TABLE` block replaces the existing `#VARIABLES`
block entirely.  Anything in `#VARIABLES` that named a register
either becomes a row in the new table or is dropped if redundant
with the new "Volatile" subsection.

## Per-demo applicability

The cutline between "apply" and "skip" is a judgment call;
proposed below for review.

### Tier 1 — strong value (apply, with full table and frame diagram)

- **get-char-from-user-1.asm**, **-2.asm** — frame + the
  intentional alignment bug.  The bug becomes visible the moment
  you see "user_input: 1($fp)" in the table.
- **subrountines-1.asm**, **-2.asm** — the canonical
  calling-convention demo.  -1 is the longhand version (every
  call-block slot named); -2 is the register-passing variant.
- **testStringsForEquality-1.asm** — frame + str_eq +
  intentional read-past-frame bug.

### Tier 2 — high value (apply, full table, no frame diagram)

- **cksum.asm** — `$s` saves + private `print_uint`
  subroutine + 256-entry lookup table.  Best demo for the
  Cross-call saves story.
- **factorial.asm** — atoi + cross-call saves for `n` and
  `result`.  First post-argv demo.
- **gcd.asm** — two atois back-to-back; the "park argv in
  $s2 while calling atoi" dance is subtle and benefits from
  being named.
- **head.asm** — three function-call flows (str_eq,
  atoi, open) and the most argv state of any demo.
- **tee.asm** — fd array in `.data` + variable argc +
  per-block fan-out.  The fd-array entry is the first "named
  composite" in the table.

### Tier 3 — moderate value (apply, smaller table)

- **print-out-ascii.asm** — single-counter loop in $s.
- **commaAndPeriodCounter.asm** — two $s counters + byte
  loop.
- **wc.asm** — same shape as 06 with one more counter.
- **head.asm** — counter + early termination.
- **rev.asm** — line buffer with pointer arithmetic.
- **nologin.asm** — open/close, introduces the "fd in $s0"
  pattern.
- **expand.asm** — column-tracking state machine.
- **echo.asm** — argv walk preview (no atoi yet).
- **cat.asm** — small extension of 15 with argv.

### Tier 4 — skip (trivial / one variable / no payoff)

- **helloworld.asm** — one string constant.
- **print1through10.asm** — one counter.
- **increment-ints.asm** — two ints, trivial naming.
- **clear.asm** — one string constant.
- **yes.asm** — one string constant in an infinite loop.
- **cat.asm** — $t0 + $t1 scratch only; existing comments
  suffice.
- **tr.asm** — byte-conditional with a sentinel; the lesson
  is the conditional, not the storage layout.

(11 and 16 are the closest calls.  Demote either to Tier 3 if
the table format ends up working well on the similar-shape
wc / expand demos.)

## Worked examples

### factorial.asm (Tier 2)

```
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only      (clobbered after first syscall)
#     argv          $a1 at entry           (walked once to fetch argv[1])
#     n             $s1                    (parsed from argv[1] via atoi)
#     result        $s2                    (running factorial product)
#
#   In atoi subroutine:
#     value         $v0                    (accumulator -- becomes the return)
#     sign          $t1                    (+1 or -1)
#     digit         $t0                    (one byte, decoded to 0..9)
#     ten           $t2                    (constant multiplier)
#
#   In print_uint subroutine:
#     n             $t9                    (remaining int, divided down to 0)
#     base          $t3                    (constant 10)
#     ptr           $t2                    (write cursor into digitsBuf)
#     digit         $t0                    (one remainder, 0..9)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra                (so we can `jr $ra` to exit cleanly)
#     $s1   <- n      held across `jal print_uint`
#     $s2   <- result held across `jal print_uint`
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg / function arg
#     $v0   syscall selector / return value
```

### subrountines-1.asm (Tier 1)

```
#STORAGE LAYOUT
#
#   No callee-save registers in this demo — every named C value
#   lives on the stack.  Working registers ($t0..$t4, $a0, $v0)
#   are volatile.
#
#   Stack at peak (during a mxPlusB call, before mxPlusB adopts $fp):
#
#         higher addresses
#           +-------------+
#    $fp -> | result1     |   0($fp)    \
#           | result2     |   4($fp)     |  main's locals (3 cells)
#           | return_val  |   8($fp)    /
#           +-------------+
#    $sp -> | m           |   0($sp)    \
#           | x           |   4($sp)     |
#           | b           |   8($sp)     |  call block laid out by
#           | &result_cell| 12($sp)      |  main; mxPlusB will
#           | continuation| 16($sp)      |  do `move $fp, $sp` and
#           | saved $fp   | 20($sp)     /   read these as 0..20($fp)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     result1        0($fp)                temporarily in $a0 at line 157
#                                          (loaded just before print_int)
#     result2        4($fp)                temporarily in $a0 at line 197
#     return_value   8($fp)                temporarily in $v0 at line 213
#                                          (see the existing intentional-bug
#                                           NOTE in the body)
#
#   In mxPlusB (callee, after `move $fp, $sp`):
#     m              0($fp)                temporarily in $t0 (line 90)
#     x              4($fp)                temporarily in $t1 (line 91)
#     b              8($fp)                temporarily in $t2 (line 92)
#     result         (none — computed in $t4 and stored directly via
#                     &result_cell; never named in memory inside mxPlusB)
#     &result_cell   12($fp)               temporarily in $t0 (line 98)
#     continuation   16($fp)               temporarily in $t0 (line 102)
#     saved $fp      20($fp)               restored TO $fp at line 103
#
#   Volatile working registers:
#     $a0   syscall arg
#     $v0   syscall selector / return value
#     $t0..$t4  scratch (the m*x+b product; also the load-then-use
#                        pattern for every stack slot above)
```

## Drift mitigation

Same risk that made Layer 5 a net negative: two sources of truth
disagree as the demo evolves.  Mitigations:

1. **Bound the scope.**  Tier 4 demos do not get a table.  This
   keeps the rollout to ~17 files, not 30+.
2. **Line numbers are approximate.**  Phrase line references as
   "around line NN" or "in the read loop" rather than "line 87"
   to make the table tolerant of small reflows.
3. **Body comments stay authoritative.**  The existing inline
   `$t0 <- m` style on `lw`/`sw` lines is the source of truth
   for register usage.  The symbol table is a *summary* of those
   facts at the top of the file; if the body and table
   disagree, the body wins (and the table needs an update).
4. **Skip the inline cross-reference at every spill site.**
   Only mark the `jal` call sites where Cross-call saves are the
   point.

## Order of work

The roll-out is per-file and trivially parallelisable.  Suggested
order (highest pedagogical lift first, so format issues surface
early):

1. **factorial.asm** — full Tier-2 table; sets the template.
2. **subrountines-1.asm**, **-2.asm** — full Tier-1 with
   frame diagram; stresses the format on the canonical demo.
3. **testStringsForEquality-1.asm**, **get-char-from-user-1.asm**,
   **-2.asm** — remaining Tier 1.
4. **cksum.asm**, **gcd.asm**, **head.asm**,
   **tee.asm** — remaining Tier 2.
5. **Tier 3 batch** — 05, 06, 12, 13, 14, 15, 17, 19, 21.

After step 1, pause for format review — it's much cheaper to
fix the template once than after twelve more files use it.
After step 2, pause again for the frame-diagram style.

## Out of scope

- Tier 4 demos.  Existing comments are enough.
- The C side (`*-1.c`).  C source carries variable names
  natively in source; a duplicate symbol table there would just
  restate the function signatures.
- A canonical "calling convention" reference document.  The
  symbol tables are per-demo; a global one might emerge later
  but isn't needed to start.
- Automated consistency checks.  Could be a future linter
  (parse the table, grep the body), but the rollout doesn't
  block on it.

## Open questions

- **Cutline 11/16 vs Tier 3.**  Decide after the first three
  Tier-3 demos land.  If the smaller-table form reads cleanly
  there, promoting 11 and 16 is cheap.
- **Frame diagram for cksum's `digitsBuf`?**  `digitsBuf`
  lives in `.data`, not on the stack — currently I'd list it
  in the symbol table with location `digitsBuf` rather than
  drawing a `.data` diagram.  Revisit if the `.data` layout
  feels under-represented after 18 lands.
