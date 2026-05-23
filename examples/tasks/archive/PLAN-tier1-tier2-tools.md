# Plan: add Tier 1 + Tier 2 Unix-tool demos

## Status — landed (2026-05-19)

All 12 demos shipped, paired C + asm, slot 28-39.  Each
demo's spim output was smoke-tested against the C build and,
where applicable, against the system Unix tool (`factor`,
`od -c`, `base64`, `comm`).

Notable defects found and resolved during smoke-testing:

- **od** depended on `\134` octal escape in `.asciiz`,
  which surfaced a real spim bug (scanner.l's `copy_str`
  shifted the first octal digit by 3 instead of 6 bits, so
  `\134` decoded to `$` instead of `\`).  See
  [`/spimulator/tasks/octal-escape-fix.md`](../../../spimulator/tasks/octal-escape-fix.md);
  fixed in scanner.l line 493 and guarded by
  `tests/tt.octal_escape.s`.
- **base64** had an emit_char subroutine that clobbered
  `$t0`, while the caller held `b0` in `$t0` across the
  four emit_char calls per triple.  Moved scratch to `$t8`,
  added a header comment naming the constraint.

What landed:

- Slot 28-31 freed for Part 7 (previous extras renumbered
  to 40-43).  Cross-refs swept across .c/.asm/.md.
- All 12 demos at `src/<slot>-<name>/<slot>-<name>.{c,asm}`
  plus meson.build entries.
- `READING-ORDER.md` has a new Part 7 section and concept
  table additions.

| Slot | Demo | Status |
|---|---|---|
| 28 | `seq M N` | landed |
| 29 | `touch FILE` | landed |
| 30 | `factor N` | landed |
| 31 | `cp SRC DST` | landed |
| 32 | `uniq` | landed |
| 33 | `nl` | landed |
| 34 | `cut -c N-M` | landed |
| 35 | `od -c` | landed (after spim octal fix) |
| 36 | `tac` (sbrk) | landed |
| 37 | `tail -n N` | landed |
| 38 | `comm A B` | landed |
| 39 | `base64` (encode) | landed (after `$t0` clobber fix) |

---

## Goal

Add 12 new Unix-tool demos to the curriculum, each pairing a
freestanding C version with a MIPS asm port.  Inspired by
`/examples/sbase/` (which has reference implementations for
most) and authored from scratch where sbase doesn't cover.

The 12 demos cover the asm patterns the current curriculum is
missing:

- **ring buffer** (`tail`)
- **pure argv→print loop with no I/O** (`seq`, `factor`)
- **output to a created file** (`cp`, `touch`)
- **multi-base / formatted output** (`od`, `nl`)
- **previous-line cache** (`uniq`)
- **buffer-all-then-emit** (`tac`)
- **range parsing in argv** (`cut`)
- **bit-twiddling across byte triples** (`base64`)
- **two-file simultaneous read** (`comm`)

## Reference implementations

| Demo | sbase ref | Lines | Strategy |
|---|---|---|---|
| `tail` | `sbase/tail.c` | 229 | Port the `-n N` path only; skip `-f`, `-c`, `-r`. |
| `seq` | `sbase/seq.c` | 147 | Integer M..N only; skip float, format strings. |
| `od` | `sbase/od.c` | 332 | Port `-c` (printable + escapes) only; skip `-x`/`-o`/`-d` for v1 — or pick a single base. |
| `cp` | `sbase/cp.c` | 63 | Strip the `-aipR` flag machinery, just `cp SRC DST`. |
| `uniq` | `sbase/uniq.c` | 144 | Default (collapse adjacent) only; skip `-c`/`-d`/`-u`. |
| `nl` | `sbase/nl.c` | 212 | Default body numbering only; skip header/footer modes. |
| `cut` | `sbase/cut.c` | 215 | `-c LIST` only (no `-f`/`-b`/delimiter). Single range `N-M`. |
| `tac` | — | — | From scratch.  Read all lines into sbrk'd memory or a static cap; emit in reverse. |
| `factor` | — | — | From scratch.  argv → trial-divide → print_int. |
| `base64` | — | — | From scratch.  3 bytes in → 4 chars out.  Encode only for v1. |
| `comm` | `sbase/comm.c` | 97 | Default 3-column output (lines only in A / only in B / in both). |
| `touch` | `sbase/touch.c` | 159 | Plain `touch FILE` — `open(O_WRONLY \| O_CREAT, 0644); close;`.  Skip `-a`/`-m`/`-r`/`-t`. |

All ports follow the same shape as the current curriculum's
demos — freestanding C with `crt0.h`, hand-written MIPS asm.

## Per-demo asm-pattern brief

### Tier 1

#### `tail -n N [FILE|-]`  *(new asm pattern: ring buffer)*

Allocate a fixed array of `N` line-buffer slots (each ~256
bytes).  Read lines into slot `(i % N)`; on EOF, walk
slots `(i+1)..N+i` printing each.  The mod-N indexing makes
the "fixed window of latest items" pattern visible.

Argv shape: `tail [-n N] [FILE|-]`.  Default N=10.

#### `seq M N`  *(new asm pattern: pure argv → print loop)*

Two atois, then `for (i = M; i <= N; i++) print_int(i)`.
First demo with **no input I/O at all** — the program is just
argv → algorithm → stdout.  ~30 instructions of asm.

#### `od -c [FILE|-]`  *(new asm pattern: row-oriented formatted output)*

Read bytes; every 16 bytes print:

```
0000000   h   e   l   l   o   ,       w   o   r   l   d   \n
0000016
```

The leading offset is right-aligned in a fixed-width field.
Printable bytes show as chars; non-printable as 3-char escape
(`\n`, `\t`, …) or a 3-digit octal.  Introduces width-padded
integer print + a per-byte mini-formatter.

For v1, do `-c` (character + escape).  Hex (`-x`) would be a
~20-line variant — could add later.

#### `cp SRC DST`  *(new asm pattern: open with O_CREAT for output)*

```c
src = open(argv[1], O_RDONLY, 0);
dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
while ((n = read(src, buf, 4096)) > 0) write(dst, buf, n);
close(src); close(dst);
```

First demo to call `open` with `O_CREAT | O_WRONLY | O_TRUNC`
to produce a NEW file.  We saw `O_RDONLY` in cat/nologin;
the file-creation flags are the missing half.  Flag values for
spim's host-passthrough open: `O_WRONLY = 1`, `O_CREAT = 0100
= 64`, `O_TRUNC = 01000 = 512` → combined `577`.  Mode `0644 =
420`.

#### `uniq [FILE|-]`  *(new asm pattern: previous-line cache)*

Read line by line.  Compare current line to a saved "previous
line" buffer.  If equal, drop; if different, print and copy
the current line to the prev buffer.  Demonstrates "you have
to keep prior input around to compare against the next."

#### `nl [FILE|-]`  *(new asm pattern: right-padded integer output)*

For each line, emit `"     N\t<line>"` with the integer right-
aligned in a 6-char field.  Requires a `print_int_padded(int
n, int width)` helper — first demo with formatted-number
output.

### Tier 2

#### `cut -c N-M [FILE|-]`  *(new asm pattern: range parsing in argv)*

Parse `N-M` from argv via two atois separated by '-'.  Then
for each line, print bytes at offsets N-1..M-1 (1-indexed in
real `cut`).  Adds a "parse two ints out of one argv token"
mini-pattern.

#### `tac [FILE|-]`  *(new asm pattern: buffer-all-then-emit)*

Read all input into a growing buffer (sbrk-allocated, or a
static cap with the excess truncated).  On EOF, walk lines in
reverse — find the last `\n` before EOF, print line, walk back
to the previous `\n`, etc.

The first demo to use `sbrk` (syscall 9) for dynamic memory.
Or alternatively, a 64 KiB static `.space` cap — simpler
asm but loses the "dynamic memory" lesson.  Both options
covered below in "open questions."

#### `factor N`  *(new asm pattern: trial-divide loop, pure argv)*

Print the prime factors of N, space-separated.  `for (d = 2;
d * d <= n; d++) while (n % d == 0) { print_int(d); n /= d; }`,
then print the remaining n if > 1.

Pure argv + integer math + print_int.  No input I/O.  Pairs
naturally with `sieve`.

#### `base64 [FILE|-]`  *(new asm pattern: 3-byte → 4-char bit packing)*

Read input 3 bytes at a time.  Each triple of 8-bit bytes
becomes four 6-bit indices into a 64-character alphabet:

```
[8 8 8] -> [6 6 6 6]
```

Each 6-bit index maps to one of `A-Za-z0-9+/`.  Pad with `=`
at the end if input length isn't a multiple of 3.  Output
should also wrap at 76 columns (real-Unix convention).

Introduces explicit bit-shift composition across bytes — a
pattern the curriculum hasn't shown.

Encode-only for v1.  Decode would be the inverse and is a
follow-up.

#### `comm A B`  *(new asm pattern: two simultaneously-open fds)*

Open two files.  Read one line from each.  Compare; print
in the appropriate column (3-column output: `only-in-A
only-in-B both`).  Advance whichever file's line was lower
(or both if equal).  Repeat until both EOF.

First demo to have **two read fds open at the same time**.
Both must be tracked, both must be closed.

Input files must be sorted (real `comm` requires this); we
don't need to sort, just consume in order.

#### `touch FILE`  *(new asm pattern: file creation with no read/write)*

```c
int fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
if (fd < 0) error;
close(fd);
```

Tiny demo — maybe 20 lines on each side.  Isolates the
"create an empty file" operation from any read/write loop.
If real `touch` ever finds the file already exists, it
updates the mtime — we don't have utimes, so we just exit
cleanly (the open with O_CREAT is a no-op if the file
already exists and we don't write to it).

## Slotting in the reading order

Two options for how to integrate the 12 demos into the
existing 27-demo curriculum.

### Option A — append as Part 7 (low churn)

Add a new "Part 7: deeper Unix tools" containing all 12,
numbered 28-39.  Existing demos unchanged.

Pros: zero renumbering churn, no cross-reference sweep.
Cons: the engagement arc currently peaks at queens; new
demos after that feel like an extension rather than part of
the main reading order.

### Option B — interleave at natural slots (high churn)

Insert each demo at the pedagogically-right point.  Suggested
slotting:

| Demo | Natural slot | Renumbers |
|---|---|---|
| `nl` | after wc | 1 demo back-bumped |
| `tail` | after head | several |
| `uniq` | after rev | several |
| `tac` | after rev (or tr) | several |
| `od` | after rot13 | several |
| `base64` | after rot13 | several |
| `cp` | after cat | several |
| `touch` | after nologin | several |
| `seq` | after echo | several |
| `cut` | after gcd (or binary-search) | several |
| `factor` | after binary-search | several |
| `comm` | after tee (multi-file) | several |

Every demo numbered above the insertion point gets renumbered.
About 30+ cross-reference updates in `.c` / `.asm` / `.md`
files.

Pros: pedagogically clean.  Cons: large mechanical sweep,
similar in scope to the engagement-first rename we already
did.

**My lean: Option A (append).**  The engagement-first
principle is preserved, the new demos serve as a "deeper Unix
tools" extension after the recursion climax, and there's zero
cross-reference cleanup.  Future readers can take or leave
Part 7 demos individually without disturbing Parts 1-6.

If you'd rather Option B, that's the cleaner long-term shape
but ~150 file-touches; explicitly worth confirming before
starting.

## Implementation order (Option A — append as 28-39)

Sorted by difficulty, easy first:

| New # | Demo | Difficulty | Asm size estimate |
|---|---|---|---|
| 28 | `seq` | tiny | ~30 instr |
| 29 | `touch` | tiny | ~25 instr |
| 30 | `factor` | small | ~50 instr |
| 31 | `cp` | small | ~70 instr |
| 32 | `uniq` | small | ~80 instr |
| 33 | `nl` | small-medium | ~90 instr |
| 34 | `cut` | medium | ~100 instr |
| 35 | `od` | medium | ~120 instr |
| 36 | `tac` | medium | ~110 instr (sbrk variant) / ~80 (static cap) |
| 37 | `tail` | medium | ~120 instr (ring buffer) |
| 38 | `comm` | medium | ~140 instr (two fds) |
| 39 | `base64` | medium | ~100 instr (bit-twiddling) |

(Existing extras 28-31 would shift up to 40-43; or — alternative — leave the extras where they are and number these 32-43, putting the new Part 7 right after the extras.  The latter is fine since extras aren't in the main reading order anyway.)

**My recommendation: place Part 7 BEFORE the extras**, so the
filesystem ordering remains "main path 01-27 → tier-2 tools
28-39 → extras 40-43."  Reading-order numbering matches
filesystem; the extras stay at the back.

## Test plan

For each demo, three or more invocation modes are exercised
against both C and spim:

- Default invocation (bare or hardcoded params).
- With argv args.
- With `-` for stdin (where applicable).
- Edge cases per demo (e.g., empty input, EOF mid-line).

The asm output and C output must be byte-for-byte identical
for stdin/file-input modes; for file-creation demos (cp,
touch), the resulting file must match.

Cross-check against system tools where possible:
- `cksum FILE | head` should produce the same bytes our
  `cp FILE OUT; cksum OUT` would.
- `seq 1 5` should match our `seq 1 5`.
- `factor 360` should match our `factor 360`.

## Decisions (confirmed)

- **Slotting:** Option A (append as Part 7, 28-39).  Existing
  extras get renumbered to 40-43.
- **`tac` memory:** **sbrk** (syscall 9).  Bill explicitly
  wants this — first demo to teach dynamic memory
  management.  The "growing buffer" pattern becomes the
  pedagogy.
- **`od` output:** `-c` only for v1 (character + escapes).
  Hex / octal modes would be ~20-line variants; defer.
- **`base64`:** encode-only for v1.  Decode is a symmetric
  follow-up.
- **`cp`:** single-source only (no multi-source / dest-dir).
- **`comm`:** doesn't check that inputs are sorted — matches
  real `comm`'s "garbage in, garbage out" behaviour.

## Other open questions
- **nologin and `touch` overlap?**  Both demonstrate
  `open`+`close`.  nologin is the read-side; `touch` is
  the create-side.  No conflict; they're complementary.
- **`tail -f`?**  Real `tail -f` watches for new data.
  Needs `lseek` and (ideally) `select`/`poll`.  spim has
  neither; skip.
- **Should `cp` support `cp file1 file2 dest_dir/`?**  Real
  `cp` can take multiple sources if the dest is a
  directory.  We don't have `stat` to check if dest is a
  dir; skip.  Single-source form only.
- **`comm` requires sorted input**.  Should we explicitly
  reject unsorted input?  Real `comm` doesn't either —
  garbage in, garbage out.  Match that.

## Out of scope

These are sbase tools that I'd NOT add to the curriculum:

- **`ls`, `find`** — need `readdir`.
- **`mv`** — needs `rename` (or unlink + cp), unlink missing.
- **`pwd`** — needs `getcwd`.
- **`stat`, `du`, `df`** — need `stat`.
- **`chmod`, `chown`** — need `chmod`/`chown` syscalls.
- **`grep`, `sed`, `awk`** — small languages of their own.
- **`sort`** — real sort is genuinely complex; bubble-sort
  covers the sort lesson on numeric arrays already.
- **`md5sum`/`sha256sum`** — same family as cksum but the
  algorithm is more complex without teaching a new asm
  concept.
- **`paste`** — multi-file input is more naturally taught
  via `comm`; `paste` adds the per-file column tracking on
  top.  Could be added later.
- **`fold`, `head -c`, `tee -a`, `tr -d`** — option variants
  of demos we already have.

## Order of work

1. **Confirm slotting** with Bill (Option A vs B).
2. **Write seq** (tiny, validates the "argv → loop → stdout"
   shape).
3. **Write touch** (tiny, validates the file-creation flags).
4. **Write factor** (small, pairs with sieve).
5. **Write cp** (open read + open write loop).
6. **Write uniq, nl, cut, od** in that order.
7. **Write tac, tail, comm, base64** (the four
   medium-difficulty demos).
8. **Update meson.build, READING-ORDER.md, PLAN-curriculum-
   order.md** with the new Part 7.
9. **Smoke-test every demo end-to-end** (C vs spim diff,
   cross-check against system tools where possible).

This is a substantial chunk of work — roughly the same effort
as the original Phase A + B + C combined.  Worth doing in 2-3
sittings rather than all at once.

## Out of scope (preserved across cleanup passes)

- Multi-file input forms (`cat file1 file2`, `paste`,
  multi-source `cp`).
- Decode side of `base64`.
- Real-Unix flag completeness (we port the canonical default
  behaviour of each tool, not the full POSIX flag set).
- `tail -f` and other watching modes.
