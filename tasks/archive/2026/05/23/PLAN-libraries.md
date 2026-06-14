# Plan: introduce students to the idea of a library

## Goal

Teach what a library *is* — a bundle of pre-written subroutines
with a documented calling-convention contract that other programs
can call into — using spim's existing multi-file `load` support.
No new spim feature required; the curriculum just leans into a
capability that's already there.

## Status note

Written 2026-05-23 as a sketch.  Bill flagged this is likely to
take another direction after some thinking, so this doc captures
the gist rather than a full spec.

## What spim already supports

- **Command line** loads exactly one file (`spimulator -f
  foo.asm`).  `src/spim.c:496` guards against a second `-f`.
- **REPL** `load` command can be called repeatedly and
  **accumulates** — each new file's text/data/symbols are
  appended to the simulator's existing state without resetting.
  No "linker" step exists; the parser writes directly into the
  growing text segment and symbol table.  Forward references
  resolve across loads.

So this works today, with zero spim changes:

```
spimulator
(spim) load "libio.asm"     # defines print_int:, print_string:, …
(spim) load "myprog.asm"    # main: jal print_int
(spim) run
```

## The pedagogical hook

A library IS just a bundle of pre-written subroutines you can
call.  spim's no-separate-linker model makes that lesson cleaner
than a real toolchain would — no "what's an object file", no
"what does ld do" detour.  What's left to teach is the part that
actually matters:

- **The calling-convention contract** between caller and library
  — `$v0` for return, `$a0..$a3` for args, `$s*` callee-save,
  `$t*` caller-save, `$ra` discipline around `jal`.
- **Why the contract has to be a contract** — if `libio` clobbers
  `$s0`, the program calling into it silently breaks.  The
  discipline is the load-bearing idea.
- **Documented vs assumed interfaces.**  A library header
  comment block enumerating each entry point's expected inputs
  and outputs is the asm equivalent of a `.h` file.

## Concrete shape (rough)

A `lib/` (or similarly named) topic folder under `src/` with:

- **`libio.asm`** — print_int, print_string, print_char,
  print_uint, read_int, read_string, read_char.  Most of these
  already live duplicated across the curriculum's existing demos;
  consolidating into one library file makes the "we keep
  rewriting this" pain visible *and* fixes it.
- **`libstr.asm`** — strlen, strcmp, str_eq, strcpy.  Similarly
  scattered across existing demos.
- **`libmath.asm`** — atoi, itoa, gcd, factorial-iter,
  pow.  Building blocks the algorithms demos already need.
- **`hello-via-lib.asm`** — minimal program calling
  `libio:print_string`.  The "wire-up" demo.
- **`echo-via-lib.asm`** — argv walk + `libio:print_string`.
- A new READING-ORDER part: "Part 8 — using a library."

Each library file's header comment block is the contract: every
entry point, its inputs ($aN registers), its outputs ($vN),
which registers it preserves vs clobbers, any preconditions.

## Three UX options for invoking

(Decision point — pick when curriculum direction firms up.)

1. **REPL-only.**  Document `load "libio.asm"; load "prog.asm";
   run` as the canonical workflow.  Zero spim changes.  Slightly
   awkward — students have to invoke interactively, can't pipe
   `cat input | spimulator -f`.
2. **Concatenation wrapper.**  A small shell script
   `spim-with-lib LIB PROG ...` that does
   `cat LIB PROG > /tmp/combined.asm; spimulator -f
   /tmp/combined.asm ...`.  Works with stdin/argv normally.
   Loses the file boundary in `-listing` output and stack-trace
   line numbers.
3. **Multi-`-f` in spim.**  ~10-line patch to `src/spim.c:486`:
   make `-f` append-able so `spimulator -f libio.asm -f prog.asm
   args...` parses each in order, accumulating into one symbol
   table the same way the REPL does.  Cleanest UX; the only
   real "spim change" of the three.

Recommendation if this lands as-sketched: option 3.  It's small,
matches real linker UX (`gcc a.o b.o c.o`), keeps source-file
boundaries intact for tooling, and the curriculum can use
spim-via-CLI uniformly with or without libraries.

## What's NOT in scope

- Real ELF object files, real linking, real `ld` — spim doesn't
  have any of this and adding it would defeat the simplicity
  that makes spim useful pedagogically.
- Dynamic loading / shared libraries.  Same reasoning.
- Symbol versioning, weak symbols, library search paths.
- musl or any other actual libc port.  (Earlier session
  considered vendoring musl as a host-libc for the spim binary
  itself; orthogonal to this and dropped.)

## Open question — does this even survive contact with the curriculum?

Bill flagged a likely pivot.  Reasons this sketch might not be
the right answer:

- The existing curriculum already TEACHES the calling
  convention through demos like `cksum` (private `print_uint`
  subroutine) and `factorial` (private `atoi`).  A formal library
  layer might be more notation than insight.
- Multi-file `load` is a spim convenience, not a real-world
  abstraction.  If the lesson is "this is how libraries work in
  practice," we'd want to talk about object files / linking —
  which spim deliberately doesn't model.
- An IO library used by every demo creates a curriculum-wide
  dependency that complicates the "each demo is self-contained
  and readable in isolation" promise the existing READING-ORDER
  trades on.

So: capture the idea, file it, revisit after Bill decides
direction.
