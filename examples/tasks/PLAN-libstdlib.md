# Plan: libstdlib — number conversion and program-control primitives

## Goal

Port a focused set of `<stdlib.h>` functions from musl as a
teaching library at `/examples/src/lib/libstdlib/`.  The library
is small (5 functions) but pedagogically important because it's
the first one that **composes** other libraries — `atoi` calls
into libctype's `isspace` and `isdigit`, demonstrating the
inter-library calling convention up close.

## Functions

| Function | C signature | Pedagogical hook |
|---|---|---|
| `atoi`   | `int atoi(const char *s)`                       | first inter-library `jal`; chains into libctype |
| `abs`    | `int abs(int x)`                                | branchless via `sra` + `xor` trick (or naive `bgez` branch) |
| `labs`   | `long labs(long x)`                             | same shape as abs (note: MIPS32 `long` == `int`) |
| `bsearch` | `void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))` | function pointer via `jalr` — first indirect-call demo |
| `_Exit`  | `void _Exit(int status)`                        | thinnest wrapper over spim syscall 17 (exit2) |

5 functions.  Deliberately **not** included:
- `exit` (the full version) — musl's pulls in atexit, thread
  locks, and weak aliases; reduce to `_Exit` for this library
  and document the difference.
- `qsort` — musl uses smoothsort + atomic ops (~220 lines); a
  naive quicksort is doable but defer to a follow-up.
- `malloc`, `free`, `calloc`, `realloc` — per the explicit
  no-malloc/free constraint.
- `strtol`, `strtoul`, `atol`, `atoll` — atoi covers the
  pedagogical territory; the others are size variations.
- `div`, `ldiv`, `rand`, `srand`, `getenv`, `setenv`,
  `system`, `mblen`, … — out of scope for this curriculum.

## Calling-convention contract

| Slot | Use |
|---|---|
| `$a0..$a3` | inputs by C-call order |
| `$v0`      | return value |
| `$s*`      | preserved (callee-save) |
| `$t*`      | clobbered freely |
| `$ra`      | preserved by the library |

**Key escalation:** `atoi` and `bsearch` are NOT leaf
functions.  `atoi` calls libctype's `isspace`/`isdigit`;
`bsearch` calls the user's comparator via `jalr`.  Both need
a real stack frame to save `$ra` and any `$s*` they want to
preserve across the call.  This is the first library that
forces students to write frame-allocation prologues/epilogues
in a "library" context (rather than a one-off demo).

## Consolidation with existing helpers

`/examples/src/string-to-int.c` already implements `atoi`
(under the name `parse_int`).  The libstdlib port should
**absorb** it:

- Rename `parse_int` → `atoi` in the new library.
- Update existing demo consumers (factorial, gcd, others
  per SESSION_NOTES) to call `atoi` from libstdlib.
- Delete `string-to-int.c` once consumers migrate.

## Why this helps a novice

- **First inter-library call.**  `atoi` `jal`s into
  `isdigit`/`isspace`.  Students see what it means for libstdlib
  to depend on libctype: compile times, load order, the contract
  that `isdigit` preserves `$s*` so atoi can keep its loop
  state there.
- **`bsearch` is the indirect-call lesson.**  Every other
  `jal` in the curriculum so far has been to a labeled function.
  bsearch takes a function pointer in `$a3`, then `jalr $a3`
  to invoke it.  This is the gateway to function pointers,
  vtables, callbacks, the whole indirect-dispatch family.
- **`abs` is the branchless-trick poster child.**  Naive:
  `bgez x, done; subu x, $0, x; done:`.  Branchless: `srai
  mask, x, 31; xor t, x, mask; subu r, t, mask`.  Worth showing
  both in the asm and discussing.
- **`_Exit` is the right answer to "how does main return a
  status."**  spim's existing curriculum already documents
  `li $v0, 17; syscall` (the "exit lesson" lives in
  `src/intro/exit/exit.asm` per SESSION_NOTES).  Codifying it
  as a library function lets demos write `jal _Exit` instead
  of inlining the syscall.

## Structure

```
/examples/src/lib/libstdlib/
    libstdlib.h         # C declarations
    libstdlib.c         # C implementations
    libstdlib.asm       # MIPS implementations
    libstdlib.md        # contract, function table, dependency notes
```

`libstdlib.asm` depends on `libctype.asm`'s exported labels
(`isdigit`, `isspace`).  Document this prominently — at load
time, libctype must be loaded first (or alongside) libstdlib.

## Worked example — `abs`

C side:

```c
int abs(int x) { return x < 0 ? -x : x; }
```

MIPS side (naive branching version):

```asm
        .text
        .globl  abs
abs:
        bgez    $a0, .L_pos         # $a0 >= 0 → already positive
        subu    $v0, $zero, $a0     # $v0 = -$a0
        jr      $ra
.L_pos:
        move    $v0, $a0
        jr      $ra
```

The library doc shows the branchless variant as an
alternative implementation in a sidebar:

```asm
        sra     $t0, $a0, 31        # $t0 = sign-extended mask (0 or -1)
        xor     $t1, $a0, $t0       # one's-complement if negative
        subu    $v0, $t1, $t0       # add 1 (via subtracting -1) if neg
        jr      $ra
```

Same result, no branch.  Worth the conversation about why
the branchless version exists (pipeline stalls in real MIPS,
though spim doesn't model them).

## Worked example — `_Exit`

C side:

```c
void _Exit(int status) {
    /* spim syscall 17: exit2 — terminates with $a0 as exit code */
    register int v0 asm("$v0") = 17;
    asm volatile("syscall" : : "r"(v0), "r"(status));
    __builtin_unreachable();
}
```

MIPS side:

```asm
        .text
        .globl  _Exit
_Exit:
        li      $v0, 17             # syscall: exit2
        syscall                     # never returns
```

Two instructions.  The simplest possible library function.
Worth contrasting with the broken syscall-10 path the
Unix-process-conformance work fixed (per SESSION_NOTES):
syscall 10 ignores `$a0`, syscall 17 honors it.

## Test/demo

`stdlib-demo.{c,asm}` at `/examples/src/lib/libstdlib-demo/`:

- atoi: feed various strings ("42", "  -17  ", "0", "abc",
  empty), print expected and actual side by side.
- abs/labs: trivial set of inputs (positive, negative,
  INT_MIN, zero).  Note INT_MIN as a UB edge case worth
  discussing.
- bsearch: sorted array of ints, search for present and
  absent keys, comparator written in MIPS.
- _Exit: not testable as PASS/FAIL inside the demo (it ends
  the program); instead, the demo's last line is `jal _Exit
  $v0=42` and the test driver verifies exit status 42.

Invocation (once multi-`-f` lands):

```sh
spimulator -f src/lib/libctype/libctype.asm \
           -f src/lib/libstdlib/libstdlib.asm \
           -f src/lib/libstdlib-demo/stdlib-demo.asm \
           42
```

The `42` ends up as `argv[1]`, atoi'd inside the demo, used
as the exit status.

## Build integration

- `libstdlib_lib_obj` — compile `libstdlib.c` once.  Links
  against `libctype_lib_obj`.
- `stdlib_demo_c` — links both library objects.
- `stdlib_demo_asm` — meson `test()` running spim on the
  three .asm files in the right order, diffing against
  `stdlib-demo.expected` and checking the exit status
  separately.

## What's NOT in scope

- `malloc`, `free`, `calloc`, `realloc` — per explicit
  exclusion.
- Full `exit` (atexit callbacks, stdio flush) — needs
  threading + stdio abstractions spim doesn't have.
- `strtol` / `strtoul` / `atol` — atoi covers the
  pedagogical ground.
- `qsort` — separate task if wanted.
- `rand` / `srand` — no need yet; demos use hardcoded data.
- Environment variables (`getenv`, `setenv`) — spim has no
  envp.
- `system` — spim has no fork/exec.

## Ordering

Library #3 of three (libctype → libstr → **libstdlib**).

Depends on: libctype is a hard prerequisite for `atoi`'s
character classification.

## Status

Partial — atoi landed 2026-05-23.  Remaining functions (abs,
labs, bsearch, _Exit) and the `parse_int` → `atoi` consolidation
sweep are open.

### atoi (landed 2026-05-23)

`/examples/src/lib/libstdlib/`:
- `libstdlib.h` — public declarations (just `atoi` for now;
  others appended as they land)
- `libstdlib.c` — atoi mirroring musl's algorithm (skip
  whitespace, optional sign, accumulate as a NEGATIVE int to
  avoid INT_MIN overflow)
- `libstdlib.asm` — atoi as the curriculum's first non-leaf
  library function.  20-byte frame saves $ra + $s0..$s2; uses
  `jal isspace` and `jal isdigit` (cross-file calls into
  libctype, resolved via spim's multi-`-f` symbol-table
  accumulation).  Uses the `10n = (n<<3) + (n<<1)` shift+add
  trick for constant multiplication.
- `LICENSE-musl` — full MIT text + per-file derivation notes

`/examples/src/lib/libstdlib-demo/`:
- `atoi-demo.c` — exercises 12 representative cases (positive,
  negative, leading whitespace, leading '+', stops at first
  non-digit, empty input, INT_MIN, INT_MAX, multi-whitespace
  with tab + newline)
- `atoi-demo.asm` — same cases as a `.data` table of input
  pointers, looped through; private `_ps`/`_pi` print helpers
- `atoi-demo.expected` — pinned 12-line golden

`/examples/src/meson.build`:
- `libstdlib_lib` static lib (links libctype headers for the C
  side's `isspace`/`isdigit` includes)
- `atoi-demo` executable wired to link both libs

**Verified**: C version and asm version under spim produce
byte-identical 12-line output (`diff -q` says nothing).

**Gotcha discovered**: spim's parser does NOT accept the unary
minus on character literals (`-'0'` fails with "Expected
integer").  Wrote `-48` directly in the asm.  Worth a small
spim follow-up if `-'X'` should be supported.

### abs / labs (landed 2026-05-23)

`/examples/src/lib/libstdlib/`:
- `libstdlib.{h,c}` — `abs` and `labs` added.  C-side implementations
  mirror musl directly (`return x > 0 ? x : -x;`).  On MIPS32
  `long == int` so labs is structurally identical to abs.
- `libstdlib.asm` — exports `labs` only (NOT `abs`).  Branching
  version (`bgez` + `subu`) as the primary; branchless
  `sra`+`xor`+`subu` shown in the comment block as a sidebar.

`/examples/src/lib/libstdlib-demo/`:
- `abs-demo.{c,asm}` — 8 cases covering 0, ±1, ±100, ±INT_MAX,
  and the INT_MIN edge case where -INT_MIN overflows to itself
- `abs-demo.expected` — pinned 8-line golden

**Gotcha discovered**: spim reserves `abs` as a built-in MIPS
pseudoinstruction (`abs $rd, $rs`).  Cannot use `abs` as a
label in spim asm at all.  Workaround: asm side exports only
`labs`; C side still has both names; document the conflict in
both files' header blocks.  Asm callers wanting "abs" call
`labs` (identical semantics on MIPS32).

**Verified**: C and asm produce byte-identical 8-line output.

### Open follow-ups (in plan but not in this turn)
- **bsearch**: function pointer via `jalr` — first indirect-call
  demo in the curriculum.
- **_Exit**: thin wrapper over syscall 17.
- **parse_int kept; atoi available alongside**: 27 demo files in
  `/examples/src/` call `parse_int` (defined in
  `string-to-int.c`).  After discussion 2026-05-23: Bill keeps
  `parse_int` as the simple teaching helper for clean argv
  strings (no whitespace skip, no `+`, no INT_MIN safety
  required for typical demo inputs).  `atoi` from libstdlib is
  the strict-libc version available for demos that want INT_MIN
  safety, whitespace skipping, or `+` acceptance.  No
  curriculum-wide migration sweep planned.  (Also unrelated:
  `read_int_from_stdin` in `read-int.c` is the stdin-reading
  helper, NOT a string parser — it stays put regardless.)
