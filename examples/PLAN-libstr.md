# Plan: libstr — string and memory primitives

## Goal

Port a focused set of `<string.h>` functions from musl as a
teaching library at `/examples/src/lib/libstr/`.  The MIPS
asm side trades musl's optimization tricks (alignment-aware
word-at-a-time loops, SIMD, Two-Way string matching) for naive
implementations that read on one screen and illustrate the
underlying idea.

## Functions

Picked for "useful + teaches a distinct idea + survives
spim's syscall surface."  No malloc-needing functions
(per the no-malloc/free constraint).

| Function | C signature | Pedagogical hook |
|---|---|---|
| `strlen`   | `size_t strlen(const char *s)`                | byte loop with NUL sentinel (consolidates existing `count-chars.c`) |
| `strcmp`   | `int strcmp(const char *a, const char *b)`    | parallel byte loop, three-way exit |
| `strncmp`  | `int strncmp(const char *a, const char *b, size_t n)` | as strcmp + bounded counter |
| `strcpy`   | `char *strcpy(char *dst, const char *src)`    | byte copy until NUL |
| `strncpy`  | `char *strncpy(char *dst, const char *src, size_t n)` | bounded copy + NUL-fill |
| `strchr`   | `char *strchr(const char *s, int c)`          | byte search, returns ptr |
| `memchr`   | `void *memchr(const void *s, int c, size_t n)` | bounded byte search |
| `memcpy`   | `void *memcpy(void *dst, const void *src, size_t n)` | byte copy by count |
| `memset`   | `void *memset(void *s, int c, size_t n)`      | byte fill by count |
| `memmove`  | `void *memmove(void *dst, const void *src, size_t n)` | overlap-aware copy direction |

10 functions.  Deliberately **not** included:
- `strdup` — needs malloc.
- `strstr` — musl uses Two-Way (~150 lines); naive doable but
  bigger than the rest.  Defer to a follow-up if wanted.
- `strspn`, `strcspn`, `strpbrk` — less common in the
  curriculum so far.  Defer.
- `strtok_r` — stateful API; pedagogically interesting but
  separate concern.  Defer.

## Calling-convention contract

| Slot | Use |
|---|---|
| `$a0..$a3` | inputs, by C-call order |
| `$v0` | return value (pointer or int) |
| `$s*` | preserved (callee-save) |
| `$t*` | clobbered freely |
| `$ra` | preserved by the library; leaf in every function |

All functions are leaves — no `jal` inside.  No `$ra` save
needed.  No `.data`.  Some are tail-callable but spim doesn't
care about tail-call optimization, so just `jr $ra` at the end.

## Consolidation with existing helpers

`/examples/src/count-chars.c` already implements `strlen`
(under the name `count_chars`).  The libstr port should
**absorb** it:

- Rename `count_chars` → `strlen` in the new library.
- Delete `/examples/src/count-chars.c` and update its 1-2 demo
  consumers to call `strlen` from libstr instead.
- The MIPS asm side of strlen is already familiar pattern
  from existing demos (cksum, wc, etc.) — codify it.

No other existing helpers overlap with this libstr scope.

## Why this helps a novice

- **The byte loop with sentinel is THE foundational MIPS
  pattern** — strlen, strcmp, strcpy, strchr all use it.
  Seeing them side-by-side as variations on one shape
  reinforces the pattern.
- **`memcpy` vs `memmove` is a real bug source.**  The
  overlap-direction check teaches signed comparison via `slt`
  in a meaningful context: get it wrong and the overlapping
  case scrambles bytes.
- **`memset` chains naturally to bump-allocator zero-fill** —
  even though we're skipping malloc, demos that grab a
  buffer via `sbrk` and zero it via `memset` reinforce the
  "library composes" idea.
- **Three-way exit in strcmp.**  Most students reach for
  `if (a < b) return -1; else if (a > b) return 1; else
  return 0;` — musl's `return *a - *b;` (after unsigned cast)
  is shorter and worth contrasting in the C source comment.

## Structure

```
/examples/src/lib/libstr/
    libstr.h            # C declarations
    libstr.c            # C implementations (naive, mirroring asm)
    libstr.asm          # MIPS implementations
    libstr.md           # contract, function table, idiom catalogue
```

Single `.c` and single `.asm` file per library.  10 functions
× ~10-25 asm lines each ≈ 150-200 lines per file.  Larger
than libctype; still readable in one sitting.

## Worked example — `strcmp`

C side (musl-style, ASCII-only):

```c
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
```

MIPS side (naive, no alignment tricks):

```asm
        .text
        .globl  strcmp
strcmp:
        lbu     $t0, 0($a0)         # *a
        lbu     $t1, 0($b)          # *b
        bne     $t0, $t1, .L_done   # differ — exit with the byte diff
        beq     $t0, $zero, .L_done # both zero — strings equal
        addiu   $a0, $a0, 1
        addiu   $a1, $a1, 1
        j       strcmp              # tail call (loop)
.L_done:
        subu    $v0, $t0, $t1
        jr      $ra
```

Roughly 8 instructions.  The `j strcmp` tail-call shows that
without function-call mechanics, looping and recursing are the
same operation.

## Test/demo

`str-demo.{c,asm}` at `/examples/src/lib/libstr-demo/`:

- Hardcoded inputs covering: equal/unequal/prefix string
  pairs, single-byte chars to search for, memcpy with various
  sizes, memmove forward and backward overlaps.
- Prints `name=PASS` or `name=FAIL expected=X got=Y` for each
  subcase.  Total ~20 subcases.
- Output diffs deterministically against a golden file.

Invocation (once multi-`-f` lands):

```sh
spimulator -f src/lib/libstr/libstr.asm \
           -f src/lib/libstr-demo/str-demo.asm
```

## Build integration

Add to `src/meson.build`:

- `libstr_lib_obj` — compile `libstr.c` once.
- `str_demo_c` — links against `libstr_lib_obj`.
- `str_demo_asm` — meson `test()` running spim on the .asm,
  diffing against `str-demo.expected`.

## What's NOT in scope

- `strdup`, `strndup` — malloc.
- `strstr`, `strspn`, `strcspn`, `strpbrk`, `strtok` /
  `strtok_r` — defer to follow-up.
- Wide-char `wcs*` and `wmem*` — locale.
- musl's word-at-a-time / alignment-aware optimizations.
  The pedagogical version is the naive byte loop.
- A `strerror` that maps errno — spim has no errno surface.

## Ordering

Library #2 of three (libctype → **libstr** → libstdlib).

Depends on: libctype not strictly required, but `strcmp` /
`strncmp` demos that touch alphabetic data feel more
complete alongside libctype.  Order them together.

Required by: libstdlib's `atoi` chains into `strlen` for
some implementations (not musl's, but a teaching version
might).

## Status

Not started.  Estimated effort: ~one day for the 10 functions
+ demo + golden + meson wiring + the count-chars.c
consolidation sweep.
