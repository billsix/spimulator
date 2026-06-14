# Plan: libctype — ASCII character classification

## Goal

Port 8 `<ctype.h>` functions from musl as a teaching library at
`examples/src/lib/libctype/`.  Each function pairs a tiny C
source (mirroring musl's ASCII-only version) with a hand-written
MIPS asm implementation, documented with a calling-convention
header block.  This is the simplest library — all functions are
range checks, no `.data`, no `jal` inside, pure leaf functions.

## Functions

| Function | C signature | Behavior |
|---|---|---|
| `isdigit`  | `int isdigit(int c)`  | 1 if '0'..'9' else 0 |
| `isalpha`  | `int isalpha(int c)`  | 1 if 'A'..'Z' or 'a'..'z' else 0 |
| `isalnum`  | `int isalnum(int c)`  | `isalpha(c) || isdigit(c)` |
| `islower`  | `int islower(int c)`  | 1 if 'a'..'z' else 0 |
| `isupper`  | `int isupper(int c)`  | 1 if 'A'..'Z' else 0 |
| `isspace`  | `int isspace(int c)`  | 1 if `\t\n\v\f\r` or `' '` else 0 |
| `toupper`  | `int toupper(int c)`  | uppercase if lowercase else c unchanged |
| `tolower`  | `int tolower(int c)`  | lowercase if uppercase else c unchanged |

ASCII-only.  No locale, no wide-char variants (`iswalpha`,
etc.).  musl's source under `examples/musl/src/ctype/` for
each is 2-6 lines; the porting is mechanical.

## Calling-convention contract

Every libctype function uses one shape, documented in the
library header comment and re-stated at each function:

| Slot | Use |
|---|---|
| `$a0`  | input byte (in the low 8 bits; high bits ignored) |
| `$v0`  | output (0/1 for classifiers; converted byte for to-case) |
| `$s*`  | preserved (callee-save discipline) |
| `$t*`  | clobbered freely (caller-save) |
| `$ra`  | preserved (leaf — never `jal`s) |

Pedagogically the **leaf-function discipline** is the lesson:
no `$ra` save needed, no frame, no `$s` register touched, no
inter-function dependency.  Establishes the baseline before
libstr starts chaining calls.

## Why this helps a novice

- **First "library" they touch.**  Eight tiny, obviously
  correct functions exported via `.globl`.  Establishes the
  pattern that other libraries (libstr, libstdlib) will
  follow.
- **Range checks teach `slt`/`sltu`.**  Every classifier is
  some variant of "is `$a0` in `[lo, hi]`?" — the canonical
  signed/unsigned comparison + branch pattern.
- **The leaf case for calling convention.**  No `$ra`
  pressure, no frame.  Useful baseline before students see
  multi-call examples.
- **A clean fold into existing demos.**  `wc`, `tr`, `rot13`,
  `expand`, `cksum` all do per-byte classification today; a
  follow-up sweep can replace inline tests with `jal isalpha`
  etc., making the pedagogical link visible.

## Structure

```
examples/src/lib/libctype/
    libctype.h          # C-side public declarations
    libctype.c          # C-side implementations (musl-style)
    libctype.asm        # MIPS-side implementations
    libctype.md         # library docs: contract, function table
```

Single `.c` and single `.asm` per library (not file-per-
function) — 8 functions × ~10 asm lines each ≈ 80 lines per
file.  Reads on one screen.

## Worked example — `isdigit`

C side:

```c
int isdigit(int c) { return (unsigned)c - '0' < 10; }
```

MIPS side:

```asm
        .text
        .globl  isdigit
isdigit:
        addiu   $v0, $a0, -48       # $v0 = c - '0'
        sltiu   $v0, $v0, 10        # $v0 = ($v0 < 10) ? 1 : 0
        jr      $ra
```

Three instructions.  No frame, no save.  `sltiu` does both the
bound check and the boolean-fy in one shot — students may
discover the trick by reading musl's source.

## Test/demo

One demo program `ctype-demo.{c,asm}` at
`examples/src/lib/libctype-demo/`:

- Loops `c = 0..127`, calls all 8 classifiers + 2 converters,
  prints a row per byte like:
  ```
   65 'A'  isalpha=1 isalnum=1 isupper=1 islower=0 isspace=0 isdigit=0  to_upper='A' to_lower='a'
  ```
- Exercises the whole library; the output diffs deterministically
  against a golden file.

Invocation, once the multi-`-f` task lands:

```sh
spimulator -f src/lib/libctype/libctype.asm \
           -f src/lib/libctype-demo/ctype-demo.asm
```

Until then, REPL: `load "libctype.asm"; load "ctype-demo.asm";
run`.

## Build integration

Add to `src/meson.build`:

- `libctype_lib_obj` — compile `libctype.c` once, link into any
  C demo that uses it.
- `ctype_demo_c` — the C demo binary, links libctype.
- `ctype_demo_asm` — meson `test()` entry that runs the .asm
  version under spim, diffs against `ctype-demo.expected`.

Parallel ports across architectures (the existing `os.h`
multi-arch shim) come for free since libctype is pure C with
no syscalls.

## What's NOT in scope

- Wide-char variants (`iswalpha`, etc.) — needs locale infra.
- Locale-aware classifiers (`isalpha_l` etc.) — same.
- The `_ctype_b_loc` table-driven implementation glibc uses —
  pedagogically opaque vs. the range-check version.

## Ordering

This is library #1 in the proposed sequence
(libctype → libstr → libstdlib) because:
- Smallest function bodies (1-3 asm instructions each).
- No inter-library deps.
- Leaf functions only — no need to teach `$ra` save/restore
  before this lands.
- libstdlib's `atoi` will `jal isdigit` / `jal isspace`, so
  libctype is a hard prerequisite.

## Status

Landed 2026-05-23.  C side and asm side produce byte-identical
output (95 rows covering printable-ASCII 32..126); diffed via
`diff -q` with no output.

### What landed

`examples/src/lib/libctype/`:
- `libctype.h` — public declarations + calling-convention contract
- `libctype.c` — all 8 functions in one consolidated file
  (initially per-function .c files; consolidated to one file to
  mirror the .asm side and reduce clutter)
- `libctype.asm` — all 8 MIPS implementations in one file,
  leaf-function-only discipline; isalnum/toupper/tolower inline
  the helper checks rather than `jal` to keep no-$ra-save
  semantics
- `LICENSE-musl` — full MIT text + per-file derivation notes

`examples/src/lib/libctype-demo/`:
- `ctype-demo.c` — loops 32..126, prints one row per byte
- `ctype-demo.asm` — same loop in MIPS; private `_ps`/`_pi`/`_pc`
  print helpers at the bottom (factor into libio.asm later)
- `ctype-demo.expected` — pinned 95-line golden output

`examples/src/meson.build`:
- `libctype_lib` static lib
- `lib_demos` foreach pattern wiring the demo to link against
  libctype + io_lib

### Attribution

Every C and asm file carries a 4-line block citing musl, URL,
license, and the LICENSE-musl pointer.

### Open follow-ups

- Wire the demo into a real meson test that runs both sides and
  diffs against the golden (today, verification is manual via
  the `diff -q` shown above).  Needs an /examples test
  infrastructure that doesn't exist yet.
- READING-ORDER.md gets a "Part 8 — libraries" pointer once
  libstr + libstdlib also land.
