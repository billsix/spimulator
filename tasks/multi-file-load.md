# Multi-file assembly ("linking"): load two .asm files into one program

**Status:** proposed — not started
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

Add something like linking: load two assembly files into one program. Could be
a preprocessor over the ASM files, or something at runtime.

## Why

The library curriculum already wants this. `tasks/libstr.md` writes its
planned invocation as:

```sh
spimulator -f src/lib/libstr/libstr.asm -f src/lib/libstr-demo/str-demo.asm
```

("once multi-`-f` lands"). Today each demo that uses a library routine has to
carry a private copy (`atoi:`, `str_eq:`, `print_uint:` are each duplicated
per demo). One shared `libstr.asm` / `libctype.asm` loaded alongside the demo
is the natural next step, and mirrors how real toolchains separate translation
units from programs.

## Design options

### Option A — runtime: accept multiple `-f` / sequential `load` (recommended)

spim already assembles into a shared text/data segment with one global symbol
table. Investigate first (this may be *nearly* free):

- Does the REPL's `load` command already merge a second file into the current
  program, or does it reinitialize? If it merges, the CLI side is just
  "allow `-f` to repeat and parse each file in order."
- Cross-file references: file 1 defines `strlen:`, file 2 does `jal strlen`.
  The assembler's forward-reference/backpatch machinery already handles
  undefined-at-first-use labels within a file; check whether unresolved
  symbols survive to the *end of all files* before erroring (they must, for
  file 2 → file 1 references to work in either load order).
- Symbol visibility: upstream SPIM historically treats labels as global across
  files (`.globl` matters for the linker it never had). Decide: all labels
  shared (simplest, matches SPIM tradition) vs honoring `.globl` for export
  and keeping non-global labels file-local (more realistic linking semantics,
  more work, better lesson). Could do the first now, the second as a follow-up
  teaching feature.

### Option 0 — first, check whether it already works

Before building anything: try it. At the REPL, `load "lib.asm"` then
`load "main.asm"` then `run` — spim's `load` may already be additive into the
shared segments/symbol table. Also try `-f a.asm -f b.asm` on the CLI to see
what the parser currently does with a repeated flag. Record the result here;
everything below is contingent on it.

### Option B — preprocessor: concatenate .asm files before parsing

A trivial `cat`-style pre-pass (or an `.include "file.asm"` directive). Pros:
zero simulator-core changes if done as a directive in the scanner. Cons: label
collisions between files surface as confusing duplicate-definition errors at
one giant file's line numbers unless line/file tracking is added — and spim's
diagnostics are per-file/line, so this muddies error messages the teaching
tool cares about.

### Option C — a fake "linker" executable

A small separate tool that takes two (or N) text `.asm` files and combines
them into one new `.asm` — resolving nothing, just concatenating with
collision checks (duplicate labels, duplicate `main:`) and perhaps a
provenance comment per section. Pedagogically nice: it makes "the linker" a
visible, inspectable step, and spim itself stays untouched. Could live as
`scripts/spim-ld` (python) or a tiny C program. Downside is the same as
Option B: diagnostics point at the combined file unless it emits file/line
markers spim understands.

Recommendation: **Option A** (after Option 0's check). Per-file line numbers
stay intact for diagnostics, and it unblocks `libstr.md` exactly as written.
An `.include` directive (B) or the visible fake-linker (C) can still be added
later as a teaching artifact.

## Follow-on once loading works: a real libc library file

The payoff (Bill, 2026-07-07): with library loading in place, port the
standard musl libc functions **not already implemented** as one large library
`.asm` (+ paired C), and then **extract the patterns already duplicated in
existing demos** (each demo currently carries private copies of `atoi:`,
`str_eq:`, `print_uint:`, …) so demos `jal` into the shared library instead.

- Already done: `examples/lib/libctype`, `libstdlib` (musl-adapted).
- Already planned: `tasks/libstr.md` (10 string/memory functions) — that
  becomes the first tranche of this port and its demo shows the two-file
  invocation.
- Then: survey what the demos reinvent (`grep -rn '^atoi:\|^str_eq:\|^print_uint:'
  examples/src`), promote those into the library, and de-duplicate the demos.
- Constraint as ever: spim's 17-syscall surface (no malloc-dependent
  functions unless built on sbrk) and naive readable implementations over
  musl's optimized ones.

## Sketch (Option A)

- `src/spim.c` argument parser: `-f`/`-file` currently records one file;
  collect a list instead, `read_assembly_file()` each in order.
- REPL `load`: confirm/keep additive behavior; `reinitialize` remains the way
  to start over.
- Error path: after all files parse, report any still-unresolved labels with
  the file that referenced them.

## Tests

- `tests/tt.link-a.s` (defines a routine) + `tests/tt.link-b.s` (calls it):
  run with both `-f` orders; both must produce the same output.
- A duplicate-label case: two files both defining `main:` should produce a
  clear error naming both files.
- Regression: single-file invocations unchanged (whole existing suite).

## Out of scope

- Real object files / relocation / separate assembly. This is
  multi-source-file assembly into one address space, not an ELF linker.
- `.globl`-scoped visibility (possible follow-up, noted above).
