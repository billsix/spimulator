# Plan: remove hardcoded input from the demos

## Status — landed 2026-05-18

All 6 demos in scope unhardcoded.  Verified end-to-end:

| Demo | Before | After |
|---|---|---|
| 06-fizzbuzz | hardcoded 1..100 | argv[1] = N (default 100) |
| 08-pascals-triangle | hardcoded 10 rows | argv[1] = rows (default 10, cap 34) |
| 27-queens | hardcoded N=8 | argv[1] = N (default 8, cap 12) |
| 09-sieve | hardcoded LIMIT=100, `.space 101` | argv[1] = limit, **sbrk-allocated** |
| 07-bubble-sort | hardcoded 10-int `.word` array | **reads ints from stdin** until EOF |
| 22-binary-search | hardcoded array, target from argv | **reads sorted ints from stdin**, target from argv |

New shared infrastructure added:

- **`/examples/src/read-int.c`** + **`read_int_from_stdin`**
  declaration in `io.h`.  C-side equivalent of spim's syscall
  5 (read_int), used by 07-bubble-sort and 22-binary-search.

New asm patterns introduced by this rollout:

- **sbrk via syscall 9** — first-ever use in the curriculum,
  in 09-sieve.  `addi $a0, $s1, 1; li $v0, 9; syscall;`
  returns the base of the new region in `$v0`.
- **read_int via syscall 5 + `$a3` EOF flag** — first-ever
  use, in 07-bubble-sort and 22-binary-search.  Pays back
  the eof-signaling spim change from earlier this session.

Verification highlights:

- 06-fizzbuzz: default + N=20 + diff N=30, all match.
- 08-pascals: default + N=5 + diff N=15, all match.
- 27-queens: default produces 92 solutions (canonical
  8-queens count); N=4 produces 2; N=6 diff matches.
- 09-sieve: default produces 25 primes up to 100; N=200 diff
  matches; N=1000 produces 168 primes (correct, π(1000)=168).
- 07-bubble-sort: random shuffles sort identically on C and
  spim sides; empty input → no output.
- 22-binary-search: 6 target probes (3, 41, 71, 50, 100, 11)
  all match including the "not found" cases.

## Goal

Make the demos that currently bake their input into source
code (a `.data` array, a literal N value, a fixed-path file)
take that input from argv, stdin, or a file instead.

The example Bill gave: **bubble sort should read numbers from
stdin or a file, not from a `static int data[10] = {9, 4,
…};`**.

The principle: a recognisable Unix tool / algorithm demo
should be able to run on the **user's** input.

## Inventory of hardcoded inputs

Scanning the current 27 main-path + 4 extras demos.  Each row
asks: where does the input come from today?

### Pure-output demos (no input — leave alone)

| Demo | Why |
|---|---|
| 01-helloworld | "hello world" string is the demo. |
| 02-print1through10 | The "1..10" IS the demo. |
| 03-increment-ints | Tiny arithmetic; no input concept. |
| 04-clear | ANSI escape sequence; no input. |
| 05-yes | Infinite "y" stream; no input. |
| 19-echo | Already takes argv. |

### Input-from-stdin demos (already correct — no change)

10-wc, 11-head, 12-rev, 13-tr, 14-rot13, 15-expand, 16-cat,
17-nologin (hardcoded file path is the lesson), 18-cksum.

### Already-take-argv demos (no change)

20-factorial, 21-gcd, 25-fibonacci, 26-hanoi, 23-tee.

### Demos with HARDCODED data that could be input

Five demos.  Each is a real candidate for unhardcoding.

| Demo | What's hardcoded | Proposed source |
|---|---|---|
| **06-fizzbuzz** | `1..100` range | `argv[1] = upper bound`; default 100 if absent |
| **07-bubble-sort** | 10-int array `{9, 4, 1, 7, …, 0}` | **stdin ints (one per line)**, EOF terminates |
| **08-pascals-triangle** | `10` rows | `argv[1] = rows`; default 10 |
| **09-sieve** | `LIMIT = 100` | `argv[1] = limit`; default 100 |
| **22-binary-search** | 10-int array `{3, 7, 11, …, 71}` | **stdin ints**, then `argv[1] = target` |
| **27-queens** | `N = 8` | `argv[1] = N`; default 8 (becomes "N-queens") |

### Demos to KEEP hardcoded (with reason)

| Demo | What's hardcoded | Why keep |
|---|---|---|
| 17-nologin | `/etc/nologin.txt` | The demo's whole point is "this specific file." |
| 18-cksum | 256-entry CRC table | The table IS the lesson; not user input. |
| 22-binary-search's array (alternative) | Could keep | The algorithm is the point; reading a sorted array from stdin is more code. |
| 24-get-char-from-user | (no hardcoded input; takes stdin) | — |
| Extras 40-43 | Various | Not in main reading order; leave alone. |

## Per-demo upgrade design

### `06-fizzbuzz` → `fizzbuzz [N]`

```c
int my_main(int argc, char **argv) {
  int n = (argc == 2) ? parse_int(argv[1]) : 100;
  for (int i = 1; i <= n; i++) { ... }
}
```

Trivial change.  C and asm each grow by ~10 lines.  Default
preserves existing behaviour (run with no args still produces
the canonical 1..100 output).

### `07-bubble-sort` → reads stdin ints, sorts, prints

```c
int my_main(int argc, char **argv) {
  /* argv-less; reads stdin.  Could also take a filename later. */
  static int a[MAX];
  int n = 0;
  while (n < MAX) {
    /* read_int returns 0 with $a3=1 at EOF after our spim
     * change; for the C side, we hand-tokenize digits from
     * stdin since there's no os_read_int. */
    int v;
    if (read_int_from_stdin(&v) != 0) break;
    a[n++] = v;
  }
  bubble_sort(a, n);
  for (int i = 0; i < n; i++) { print_int(a[i]); print_char('\n'); }
}
```

Subtleties:

- **Input format**: one int per line (matches `seq 100 |
  shuf | bubble-sort`).  Whitespace between ints also fine
  but per-line is the canonical Unix shape.
- **Sentinel for the C side**: the C `read_char` returns -1
  at EOF (same as spim post-EOF-change), so a small
  read-int-from-chars helper reads digit-by-digit and returns
  an EOF flag.
- **The asm side** uses spim's `read_int` (syscall 5) +
  the new `$a3` EOF flag.  Convergent with the
  `tt.read_int_eof.s` test we already have.
- **Static cap**: 256 ints.  Larger inputs truncate.  No
  sbrk in this demo — keep the focus on the sort algorithm.

Output format: one int per line.

### `08-pascals-triangle` → `pascals-triangle [N]`

Same shape as fizzbuzz.  Default N=10; argv[1] overrides.
Need to bump the working array's size or sbrk to scale.

Subtleties:

- **Overflow**: row 33 column 16 = `C(33,16) ≈ 1.16 × 10^9`
  — still fits in int32.  Row 34+ overflows.  Default 10 is
  far below.  Can document the cap.
- **Static array size**: pre-allocate room for, say, 35 rows;
  cap N at 34.  Or sbrk if we want unbounded.  Static cap is
  simpler.

### `09-sieve` → `sieve [LIMIT]`

```c
int my_main(int argc, char **argv) {
  int limit = (argc == 2) ? parse_int(argv[1]) : 100;
  /* sieve buffer needs to be limit+1 bytes; sbrk it */
}
```

The current sieve uses `.space 101` (101 bytes BSS).  With
argv-driven LIMIT we need dynamic allocation.

Two options:

- **Static cap** (1024 bytes = primes up to 1023).  Simpler.
- **sbrk-allocated** of `LIMIT+1` bytes.  Dynamic, teaches
  another sbrk use.

Lean: sbrk-allocated.  09-sieve becomes the second sbrk demo
(36-tac is the first).  Pedagogically the sieve is a natural
fit for "allocate exactly what we need" since LIMIT
determines memory directly.

### `22-binary-search` → reads stdin ints, target from argv

```c
int my_main(int argc, char **argv) {
  if (argc != 2) usage;
  int target = parse_int(argv[1]);
  static int data[MAX];
  int n = 0;
  while (n < MAX) { ... read ints from stdin ... }
  /* assume input is sorted; don't enforce */
  int idx_linear = linear_search(data, n, target);
  int idx_binary = binary_search(data, n, target);
  print "linear: ...";
  print "binary: ...";
}
```

Subtlety: real `look(1)` (the binary-search Unix tool) also
takes a sorted file argument.  We could allow stdin OR file
following the now-established argv-with-dash convention.  My
lean: just stdin for v1; matches `07-bubble-sort`'s shape.

### `27-queens` → `queens [N]`

```c
int my_main(int argc, char **argv) {
  int n = (argc == 2) ? parse_int(argv[1]) : 8;
  /* columns[N] needs to scale.  Static cap at 12 (any N
   * larger blows up exponentially anyway). */
}
```

`safe(row, col)` and `solve(row)` are already parameterised
on N implicitly through the array size — just need to lift
the `8` constant.

Subtlety: number of solutions grows wildly with N (1, 0, 0,
2, 10, 4, 40, 92, 352, 724, …).  N=12 already produces 14,200
solutions.  Cap recommended.

## Implementation order

Sorted easy → hard:

1. **06-fizzbuzz** — tiny argv addition, trivial.
2. **08-pascals-triangle** — tiny argv addition + size scaling
   via static cap.
3. **27-queens** — tiny argv addition + size scaling via
   static cap.
4. **09-sieve** — first sbrk integration (after 36-tac if
   that lands first).
5. **07-bubble-sort** — stdin int reader + bubble sort.
   Biggest change.
6. **22-binary-search** — stdin int reader (sorted) + target
   from argv.

## Subtleties for the asm side

The new pieces all six demos need are some subset of:

- **Reading argv with an optional N** (already familiar from
  20-factorial / 21-gcd / 26-hanoi).
- **Reading ints from stdin** (`read_int` syscall 5, with
  the new `$a3` EOF flag introduced in
  `/spimulator/tasks/eof-signaling.md`).  First curriculum
  demos that actually use syscall 5.
- **Reading ints from a file fd** — `read_int` only reads
  stdin.  For a file-input variant, we'd parse digits
  ourselves from `read(fd, ...)` bytes.  Punt for v1.
- **sbrk** — `li $v0, 9; li $a0, <bytes>; syscall` returns
  the previous data-segment break in `$v0`.  Allocate
  through it, treat the returned address as the base of an
  array.

## Out of scope

- **Hardcoded `.data` lookup tables** (18-cksum's 256-entry
  CRC table, etc).  Those are constants, not user input.
- **Hardcoded paths** in 17-nologin.  The demo's lesson IS
  that specific file.
- **Argv-as-data demos** (e.g., `echo args`, `seq M N`,
  `factor N`) — they already take input from argv.
- **Reading from a file in addition to stdin.**  Consistent
  with `PLAN-stdin-or-file.md` Scope A: stdin-or-file is
  for the FILTER demos (wc, cat, etc).  These algorithm
  demos don't need the file path AND would need to switch
  from `read_int` to `read` + manual digit parsing for file
  inputs.  Defer; revisit if a student asks.

## Pedagogical framing

The shift turns 5-6 algorithm demos from "the program
demonstrates the algorithm" into "the program is a Unix tool
that implements the algorithm."  Which is the natural
progression:

- **Today's `07-bubble-sort`**: shows the algorithm with a
  hardcoded example.  Student reads the asm and sees what
  sort looks like; the data was chosen by the author.
- **Tomorrow's `07-bubble-sort`**: pipe ANY ints in,
  receive sorted output.  Student pipes their own data and
  watches it sort.

The asm of the sort itself is unchanged.  All the new code
is "how to receive your data."  Which is, conveniently, a
real-world skill.

## Open questions

- **Input format for the int-readers**: one per line, or
  whitespace-separated, or both?  My lean: one per line for
  Unix-tool feel (`seq 100 | shuf | bubble-sort | head`),
  but `read_int` actually handles both naturally because it
  skips whitespace.  So the demos accept both for free.
- **Static cap vs sbrk for bubble-sort and binary-search**:
  static cap (256 ints).  These demos' lesson is the
  algorithm; memory management is a different lesson.
  sbrk shows up in 09-sieve and 36-tac.
- **27-queens upper bound on N**: cap at 12 (still produces
  14k solutions; anything larger generates output you'd
  never scroll through).  Or no cap, let the user wait.
- **Should 07-bubble-sort print "before/after" or just
  "after"?**  Real `sort` only prints the sorted output.
  The current demo prints both (instructive but not
  Unix-like).  Probably switch to "after only" when the
  input comes from stdin.

## Relationship to other plans

- `PLAN-cs-demos.md`: this plan UNHARDCODES the five
  algorithmic demos that PLAN-cs-demos.md authored with
  fixed inputs.  Doesn't conflict; just upgrades.
- `PLAN-stdin-or-file.md`: this plan extends the same
  Unix-tool convention to algorithm demos that don't
  currently take input.
- `PLAN-tier1-tier2-tools.md`: orthogonal — this plan touches
  algorithm demos; that plan adds NEW utility demos.  Both
  can land independently.
- `/spimulator/tasks/eof-signaling.md`: this plan's
  stdin-int-reader pattern depends on the `$a3` EOF flag
  that already landed.
