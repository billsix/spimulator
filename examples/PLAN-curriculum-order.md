# Plan: reading order for the demos (engagement-first)

## Goal

Produce a recommended **reading order** for the demos under
`src/`, optimised for a specific student profile (described
below).  Per the existing project decision, this is a reading
order — not a filesystem rename.  Filesystem numbering stays
where it is; this plan produces a doc / book chapter ordering
based on the audience model.

## Audience model

The student we're optimising for:

- **Knows the algorithms.** They've written bubble sort,
  computed Fibonacci, and seen Towers of Hanoi in Java (or
  Python, or Kotlin — some high-level language with garbage
  collection and a class library).  Algorithmic content is
  comfortable territory.
- **Barely knows C.**  They can read it, but it's not their
  daily language.  The C version of each demo serves as a
  one-step-closer-to-the-metal version of the algorithm —
  "the same code I'd write in Java, but with pointers and
  manual memory."
- **Doesn't know assembly.**  That's why they're here.
- **Engagement comes from familiarity.**  They want to see
  how their existing knowledge translates to a lower level —
  *"I know bubble sort.  How does it actually run on a CPU?"*
- **NOT from abstract pedagogy.**  A demo like
  `mxPlusB(int m, int x, int b)` — present only to
  demonstrate stack-based subroutine linkage — has no hook
  for this audience.  They didn't take linear algebra to come
  hear about `m*x+b`.  They came to learn assembly.

The corollary: every demo in the main reading order should
either be a **recognisable algorithm** (fizzbuzz, gcd, bubble
sort, fibonacci, hanoi, queens, sieve, …) or a **recognisable
Unix tool** (cat, wc, head, tee, …).  Demos with no
recognisable hook get demoted to an appendix.

## What this supersedes

This plan replaces an earlier version of this file that was
framed around "programmer new to assembly, needs concepts
topologically ordered."  That framing produced a defensible
order but treated the demos as instruments for delivering
concepts.  The new framing treats them as **recognisable
content** the student walks in already caring about.

The two framings disagree most sharply on demos like 07
(mxPlusB) and 08 (testStringsForEquality with str1/str2/str3),
where the algorithm-as-vehicle has been replaced by
mechanism-as-vehicle.  The new framing drops those.

## What gets dropped from the main path

These exist on disk and stay there (the existing project
decision: directory order ≠ pedagogical order), but they
don't appear in the reading order:

### `07-subrountines-1.asm` and `07-subrountines-2.asm`

`int mxPlusB(int m, int x, int b)` — the longhand and the
idiomatic versions of the stack-based subroutine calling
convention, written around an abstract algebraic function.

**Why dropped:** the student didn't come here to learn
about `m*x+b`.  The subroutine calling convention is taught
instead via demos whose subroutines do something the student
already cares about:

- `atoi` (used in 20-factorial, 22-gcd, 23-head-file,
  26-fibonacci, 30-hanoi) — parses an integer from a string.
- `str_eq` (used in 23-head-file) — compares two strings.
- `print_uint` (used in 18-cksum, 20-factorial) — prints an
  unsigned 32-bit integer.

Each of those is a subroutine **that does something the
student would recognise as useful**.  mxPlusB is not.

The 07 demos are preserved as `/examples/src/07-subrountines/`
for the curious reader; an appendix page could explain "this
is how subroutine linkage used to be spelled out before the
jal/jr $ra convention," and a student who wants the deep dive
can find it there.

### `08-testStringsForEquality-1.asm`

`int str_eq(const char *s1, const char *s2)` with `str1`,
`str2`, `str3` hardcoded.  Same audience problem: the
algorithm is non-trivial but the *content* is "compare these
three meaningless strings I made up."

**Why dropped:** string comparison is taught in 23-head-file's
`str_eq` subroutine, where the comparison serves a recognisable
purpose (parsing the `-n` flag of a head-like utility).

### `06-commaAndPeriodCounter.asm`

Counts commas and periods on stdin.  Functional task but
"counting commas" isn't recognisable as anything.

**Why dropped:** the same shape (multi-counter byte loop) is
covered by 12-wc, which counts bytes and lines — the real Unix
utility every student has seen.

## What gets demoted to sidebar

These stay in the main path but as short asides rather than
chapter centerpieces:

- **05-print-out-ascii.asm** — walks -128 to 127 printing each.
  Useful for sign-extension intuition (`lb` and signed
  comparison) but the demo itself isn't compelling.  Best as
  a one-page sidebar within Part 2 or Part 3.
- **16-tr.asm** — uppercases stdin.  The pattern (byte-
  conditional transform) is the same as 29-rot13 but simpler.
  Best as a one-page warmup right before 29-rot13.

## The reading order — six parts

### Part 1: First contact (5 demos)

Minimum syntax to read asm at all.  Quick, mostly self-evident.

1. `01-helloworld` — the simplest possible program; one syscall.
2. `02-print1through10` — `li`, `addi`, a branch loop.
3. `03-increment-ints` — multiple `$t-regs`, integer ops.
4. `09-clear` — ANSI escape bytes; immediate visual reward
   ("oh, I made the terminal do something").
5. `10-yes` — the tightest possible loop; you've used `yes`
   in a shell, here's what it looks like.

### Part 2: Algorithms you already know — no argv yet (4 demos)

Familiar algorithms in asm.  Each one introduces a new MIPS
concept on territory the student walks in confident about.

6. `25-fizzbuzz` — modulo, multi-way branching.  The student
   wrote this in their first Java week; here it is in asm.
7. `28-bubble-sort` — nested loops, `.word` array, in-place
   mutation, the swap-via-temp pattern.
8. `31-pascals-triangle` — in-place right-to-left array update.
9. `32-sieve` — `.space` for working memory, byte-granularity
   `lb`/`sb`, the "240 BC algorithm" framing.

### Part 3: Unix filters — stdin byte loops (6 demos)

Recognisable tools, simple stdin → stdout transforms.

10. `12-wc` — byte and line counters.
11. `13-head` — early termination of the input loop.
12. `14-rev` — line buffering + walk-the-buffer-backwards.
13. `16-tr` (sidebar) — uppercase byte transform.
14. `29-rot13` — byte transform with wraparound; the modulo
    arithmetic is the new asm idea.  Self-inverse property
    gives an immediate sanity check.
15. `17-expand` — stream-state counter for tab expansion.

### Part 4: Files (3 demos)

File descriptors and block I/O — still no argv.

16. `11-cat` — block I/O via syscall 14/15.  Same shape as
    `12-wc` but moving 4 KiB at a time.
17. `15-nologin` — first `open`/`close`.  Reads
    `/etc/nologin.txt` and falls back to a default message.
18. `18-cksum` — the heaviest demo before argv.  Combines a
    256-entry `.data` lookup table, bitwise ops, a private
    `print_uint` subroutine with the `$s*` save discipline,
    AND the `$t1`-must-not-be-clobbered caveat.  Worth a
    chapter to itself.

### Part 5: argv — taking inputs from the command line (7 demos)

The point at which programs start taking student-supplied
inputs.  Introduces the crt0.h shim, `parse_int`, and the
"$s* held across jal" cross-call discipline.

19. `19-echo` — argv walk; no atoi, no algorithm.  Just
    establishes that `$a0=argc`, `$a1=argv`.
20. `20-factorial` — argv + `atoi` as a private subroutine.
    First demo with a meaningful "save $ra in $s0 across jal"
    pattern.
21. `22-gcd` — two atois back-to-back; the "park argv in $s2"
    trick.  Familiar algorithm, makes the $s* discipline feel
    earned rather than ceremonial.
22. `27-binary-search` — `.word` array, two search variants
    (linear + binary), `print_idx_or_nf` helper.  Target from
    argv.
23. `21-cat-file` — argv + open.  Replaces 15-nologin's
    hardcoded path with `argv[1]`.
24. `23-head-file` — three flows in one demo: `str_eq` for the
    `-n` flag, `atoi` for `N`, `open` for the filename.
    Richest argv demo.
25. `24-tee` — variable argc, fd array in `.data`, fan-out
    write.  No `jal` at all but the most state-heavy demo
    (every local lives in `$s*` because every syscall is a
    clobber boundary).

### Part 6: Stack frames and recursion (5 demos)

The deepest material.  By now the student has seen `$s*`
saves; this part introduces the per-call stack frame for
state that can't live in `$s*` (because each recursive
invocation would overwrite the same register).

26. `04-get-char-from-user-1` — intentional misaligned frame.
    The bug.
27. `04-get-char-from-user-2` — the fix.  Demonstrates word-
    aligned frame layout.
28. `26-fibonacci` — first per-call stack frame.  Iter vs rec
    in one demo, so the contrast is one diff away.
29. `30-hanoi` — per-call frame with FOUR saved args.
    Recursion with non-trivial output as a side effect.
30. `33-queens` — backtracking.  Per-call frame holds the
    `col` loop counter, which has to be stack-resident
    because each recursive call has its own.

## Why this order works for the audience

1. **Hook within 30 minutes.**  Part 1 takes maybe an hour;
   Part 2 starts immediately with fizzbuzz, which the
   student wrote in their first programming course.  They're
   doing recognisable work in asm within the first sitting.

2. **No abstract demos.**  Every chapter centerpiece is
   either an algorithm they know or a Unix tool they've used.
   Subroutines, arrays, stack frames are all introduced via
   recognisable hooks.

3. **Concepts arrive when motivated.**  The crt0.h argv shim
   doesn't show up until Part 5 — by which time the student
   has wanted to give the program input for many demos.  The
   stack frame doesn't show up until Part 6 — by which time
   they've seen $s* saves get inadequate (the implicit "what
   if the function calls itself?" question is ready).

4. **C as bridge, asm as goal.**  Every demo's paired `.c`
   file is the student's familiar territory ("looks like the
   Java I'd write, but with pointers"); the `.asm` is the
   destination.  The pair makes the gap navigable.

5. **No demo numbered higher than 33** is currently in the
   curriculum.  All 30 main-path demos exist.

## Concept buildup by part

| Concept | Introduced in |
|---|---|
| `.asciiz`, syscall 4 | Part 1 (01) |
| `li`, `addi`, branches, loops | Part 1 (02, 03) |
| ANSI escape bytes | Part 1 (09) |
| Infinite loops | Part 1 (10) |
| modulo via `div`/`mfhi`, multi-way branching | Part 2 (25) |
| nested loops, in-place array mutation, swap | Part 2 (28) |
| right-to-left in-place update | Part 2 (31) |
| `.space` for working memory, byte-granular `lb`/`sb` | Part 2 (32) |
| stdin byte loop, multi-counter | Part 3 (12) |
| early termination | Part 3 (13) |
| line buffer + reversal | Part 3 (14) |
| byte-conditional transform | Part 3 (16, 29) |
| stream-state counter | Part 3 (17) |
| block I/O via syscall 14/15 | Part 4 (11) |
| open/close (syscall 13/16) | Part 4 (15) |
| `$s*` callee-save save discipline, lookup tables, bitwise ops | Part 4 (18) |
| argv via `crt0.h` shim | Part 5 (19) |
| `atoi`, private subroutines | Part 5 (20) |
| cross-call `$s*` saves becoming load-bearing | Part 5 (22) |
| `.word` arrays, strided indexed access | Part 5 (27) |
| argv + file open | Part 5 (21) |
| string compare + flag parsing + multiple subroutines | Part 5 (23) |
| variable argc, fd array, fan-out writes | Part 5 (24) |
| stack frame layout, word alignment | Part 6 (04-1, 04-2) |
| per-call stack frame for recursion | Part 6 (26) |
| per-call frame with multiple saved args | Part 6 (30) |
| backtracking via per-call frame | Part 6 (33) |

## Open questions

- **Where exactly do 05 and 16 land as sidebars?**  Best
  candidates: 05 just before or after 03 (it's a signed-walk
  variant of 03's integer ops); 16 right before 29 (same
  byte-conditional shape).
- **Should 18-cksum be its own Part rather than tail of
  Part 4?**  It packs in more concepts than any other single
  demo.  Could argue Part-4-is-just-cat-and-nologin, and
  18-cksum gets a dedicated "Part 4.5" or absorbs into
  Part 5 as "the heavyweight before argv arrives."
- **Where do the dropped demos go?**  An appendix at the end
  of the book ("Other demos in the tree") seems right.  The
  text could explicitly say "these exist as historical /
  alternative implementations; here's why they aren't in the
  main reading order."
- **Should each part have a "you've seen the algorithm in
  Java — here's the asm" preamble?**  Probably yes,
  especially for Part 2.  A two-sentence reminder of the
  algorithm's Java form would make the bridge explicit.

## Order of work

1. Validate this order on real students (Bill knows the
   class).
2. Produce `READING-ORDER.md` at `/examples/` root, just the
   ordered list of demos with one-line rationale per entry.
3. (Eventually) write the Sphinx book chapters from this
   skeleton.

## Out of scope

- Renaming files on disk.
- Writing the actual book chapters.
- New demos.  Any gaps the order surfaces ("Part 2 needs an
  argv-free recursive demo") go on a separate to-do.
- Reassessing the CS demos in detail — PLAN-cs-demos.md is
  the authority for what each algorithmic demo should teach.
