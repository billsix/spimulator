# Plan: classic CS algorithm demos

Once `PLAN-unix-tools.md`'s Phase 5 has produced as many
algorithmic-stdin sbase ports as Bill wants, the next pedagogical
direction is **classic CS exercises shown in MIPS**.

> **Note (post-argv-fix):** spim now actually passes argv to user
> code (see `/spimulator/tasks/argv-command-line-handling.md`),
> so several of these demos become more natural with their
> "N" as a command-line argument instead of hardcoded.  Example:
> `factorial 5` rather than a hardcoded N=5 in `.data`.  Each
> entry below notes whether argv is the natural form.  Audience:
the college student who already knows programming (Fibonacci,
Hanoi, sort algorithms) but not assembly.  Each demo lands a
familiar algorithm in a new light — the asm shows what really
happens.

These would slot in as demos 25–34, in the order below
(easier → harder, each building on the previous).  Each is a
sketch; numbers and details may shift when ported.

> **Numbering note:** the section headers below still say
> "Demo 19" etc. from when this plan was written.  Phase C of
> `PLAN-unix-tools.md` consumed slots 19–24, so the actual
> filesystem slot is **section number + 6**.  Fizzbuzz lands
> at `25-fizzbuzz`, GCD (already done as part of Phase C) was
> `22-gcd`, Fibonacci will be `26-fibonacci`, and so on.

## Demos in recommended order

### 19. Fizzbuzz (1 → 100) — **DONE as `25-fizzbuzz`**

**What's new pedagogically:** modulo as an algorithm primitive
(`div`/`mfhi`), multi-way branching, output formatting.

**Landed:** `void _start(void)` on the C side (no argv, no
crt0.h needed); `$t0 = i`, `$t1 = divisor-then-remainder` on
the asm side.  Verified byte-for-byte between C and spim
output for the full 1..100 sweep.

**spim notes:** integer-only, no stdin.  Fits in registers
($t0 = counter, $t1 = scratch for div/mod).  Output is via
syscall 1 (print_int) and syscall 4 (print_string) for "Fizz",
"Buzz", "FizzBuzz".

**Sketch:**

```c
for (int i = 1; i <= 100; i++) {
  if (i % 15 == 0)      print_string("FizzBuzz");
  else if (i % 3 == 0)  print_string("Fizz");
  else if (i % 5 == 0)  print_string("Buzz");
  else                  print_int(i);
  print_string("\n");
}
```

### 20. GCD (Euclidean) — **DONE as `22-gcd`** (during Phase C)

**What's new:** tight integer loop with modulo, the "two
variables dance" pattern.

**spim notes:** inputs hardcoded in `.data` or in immediates;
output a single line.  ~6 instructions in the loop body.

**Sketch:**

```c
unsigned int a = 1071, b = 462;
while (b != 0) { unsigned int t = a % b; a = b; b = t; }
print_uint(a);
```

### 21. Fibonacci (BOTH variants in one demo) — **DONE as `26-fibonacci`**

**What's new:** the same algorithm two ways — iterative (5
instructions in a loop) vs recursive (the call tree fans out
visibly).  An invitation to ask "why is one of these so much
faster?" and find the answer in the call trace.

**spim notes:** recursive Fib is the first demo where the
subroutine needs to save **n** *and* `$ra` across each recursive
call — saving them in `$s` registers is no longer enough because
deeper calls overwrite the same `$s`.  This forces the
**stack-frame-per-call** pattern: every recursive entry pushes
`$ra` and the live argument onto a frame, restores on exit.

**Landed:** 12-byte per-call frame in `fib_rec` (`0($sp)` =
saved `$ra`, `4($sp)` = saved `n`, `8($sp)` = intermediate
`fib(n-1)`).  `fib_iter` keeps everything in `$t*`-regs, no
frame.  N from argv; both results printed (`iter:` and `rec:`).
Verified byte-for-byte between C and spim for N = 0, 1, 2,
10, 20.

**Sketch:**

```c
int fib_iter(int n) {
  int a = 0, b = 1;
  for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; }
  return a;
}
int fib_rec(int n) {
  if (n < 2) return n;
  return fib_rec(n - 1) + fib_rec(n - 2);
}
```

### 22. Binary search — **DONE as `27-binary-search`**

**What's new:** array element access (`addr + i*4`), divide-and-
conquer (`mid = (lo+hi)/2` is `srl` by 1), termination on `lo > hi`.
Pair with a linear-search version to compare instruction counts.

**spim notes:** hardcoded sorted array of ~10 ints in `.data`,
hardcoded target.  Output: `mid` index, or "not found".

**Landed:** both `linear_search` and `binary_search` in one
demo; target from argv[1] (more flexible than hardcoded for
classroom use; the array stays hardcoded since the *array* is
the lesson).  10-element `.word` array via `data:.word 3, 7,
…, 71`.  Print helper `print_idx_or_nf` handles the
"index OR not-found" output, avoiding two copies of the
bltz/print-int/print-string sequence inline.  Verified
byte-for-byte between C and spim for targets {3, 41, 71,
50, 100, 11} (mix of found-at-boundary, found-interior, and
not-found-above/within range).

**Sketch:**

```c
static const int data[] = { 3, 7, 11, 19, 23, 31, 41, 53, 67, 71 };
int target = 41;
int lo = 0, hi = 9;
while (lo <= hi) {
  int mid = (lo + hi) / 2;
  if (data[mid] == target) { print_int(mid); break; }
  if (data[mid] < target)  lo = mid + 1;
  else                     hi = mid - 1;
}
```

### 23. Bubble sort — **DONE as `28-bubble-sort`**

**What's new:** nested loops, in-place mutation, swap (which
needs a temp register).  Array element access via `lw`/`sw` at
`base + i*4`.

**spim notes:** small array (10 ints) in `.data`, sort in place,
print the result.  Naive O(n²) — the point is the asm, not the
algorithm.

**Landed:** 10-int `.word data` initialised `9, 4, 1, 7, 6, 2,
8, 3, 5, 0`.  `bubble_sort` and `print_array` as separate
subroutines (both call-free internally — no per-call frames
needed).  Swap reads a[j+1] via `4($t4)` rather than computing
`&a[j+1]` separately.  Output is before+after, one line each:
`before: 9 4 1 7 6 2 8 3 5 0` / `after:  0 1 2 3 4 5 6 7 8 9`.
Verified byte-for-byte between C and spim.

**Sketch:**

```c
int a[10] = { 9, 4, 1, 7, 6, 2, 8, 3, 5, 0 };
for (int i = 0; i < 9; i++)
  for (int j = 0; j < 9 - i; j++)
    if (a[j] > a[j+1]) { int t = a[j]; a[j] = a[j+1]; a[j+1] = t; }
for (int i = 0; i < 10; i++) { print_int(a[i]); print_char(' '); }
print_char('\n');
```

### 24. Caesar cipher / ROT13 — **DONE as `29-rot13`**

**What's new:** modular arithmetic on bytes ("wrap around" past
'z' back to 'a').  Same byte-stream shape as 16-tr, but the
transformation is non-trivial: shift by 13 then test for wrap.

**spim notes:** stdin → stdout filter.  Use `~` sentinel like
16-tr/17-expand.  ROT13 is self-inverse, so running the program
twice gives back the input.

**Landed:** stdin → stdout filter, `~` sentinel.  `(ch - base
+ 13) % 26` becomes `addi, addi, li 26, div, mfhi` — first
demo where the modulus operation is non-trivially the lesson
(in earlier demos the modulo was for a small constant like
10 in print_uint).  Two parallel branches for [a..z] and
[A..Z].  **Gotcha discovered:** spim's assembler does NOT
accept `-'a'` as an immediate (negation breaks the char
literal); had to use the numeric `-97` with a comment.  Worth
adding to the `REFERENCE-encodings.md` "Two spim gotchas"
section.  Self-inverse property verified: piping
`Hello, World!` through twice produces `Hello, World!`.

**Sketch:**

```c
int ch;
while ((ch = read_char()) != -1) {
  if (ch >= 'a' && ch <= 'z') ch = 'a' + (ch - 'a' + 13) % 26;
  else if (ch >= 'A' && ch <= 'Z') ch = 'A' + (ch - 'A' + 13) % 26;
  print_char((char)ch);
}
```

### 25. Towers of Hanoi — **DONE as `30-hanoi`**

**What's new:** recursion that PRODUCES output (move sequences),
not just a value.  Three lines of pseudocode that look magic
until the student sees the call tree.

**spim notes:** depth N=4 or N=5 keeps the output manageable
(2^N − 1 moves).  Recursive function takes 4 args (n, src, dst,
tmp) — first demo where 4 input registers are all in play.

**Landed:** N from argv; `hanoi(n, 'A', 'C', 'B')` from main.
**20-byte per-call stack frame** (saved `$ra` + all four args
at `0/4/8/12/16($sp)`).  The frame is allocated AFTER the
`n==0` base-case test, so leaf calls don't touch the stack.
Between the two recursive calls, `$a0..$a3` are fully
clobbered, so the second call reloads all four args from the
frame.  Output is `Move disk from <src> to <dst>` per line.
Verified byte-for-byte between C and spim for N = 1, 2, 3, 4
(producing 1, 3, 7, 15 moves respectively — `2^N - 1` as
expected).

**Sketch:**

```c
void hanoi(int n, char src, char dst, char tmp) {
  if (n == 0) return;
  hanoi(n - 1, src, tmp, dst);
  print_string("Move disk from ");
  print_char(src); print_string(" to "); print_char(dst); print_char('\n');
  hanoi(n - 1, tmp, dst, src);
}
```

### 26. Pascal's triangle (10 rows) — **DONE as `31-pascals-triangle`**

**What's new:** in-place array update for each row (the new row
overwrites the old), formatted output with variable spacing.
Combines an algorithmic core (binomial coefficients) with the
print-formatting from 17-expand.

**spim notes:** working array of 11 ints in `.data`; build each
row in place by walking right-to-left so old values aren't
clobbered before they're consumed.

**Landed:** 11-cell `.word row` initialised to `1, 0, 0, ...,
0`.  Outer loop `n=0..9`; inner update loop walks `j=n..1`
right-to-left; print loop walks `j=0..n` left-to-right.
The asm reads `row[j-1]` as `-4($t1)` after computing
`&row[j]` — the inverse offset trick from 28-bubble-sort's
`4($t4)`.  Hardcoded 10 rows (no argv); the algorithm is the
lesson here, not the parameter.  Verified byte-for-byte
between C and spim.

**Sketch:**

```c
int row[11] = {1};
for (int n = 0; n < 10; n++) {
  for (int j = n; j > 0; j--) row[j] += row[j-1];
  for (int j = 0; j <= n; j++) { print_int(row[j]); print_char(' '); }
  print_char('\n');
}
```

### 27. Sieve of Eratosthenes (primes up to 100) — **DONE as `32-sieve`**

**What's new:** the `.data` section as working memory (rather
than as constants).  Allocate a 100-byte flag array with
`.space`; nested loops mark composites; final pass prints
survivors.  The "240 BC algorithm" framing lands well.

**spim notes:** `.space 100` for the flag array, then read/write
flags by computing `(base + i)` and using `lb`/`sb`.

**Landed:** `.space 101` (BSS-zeroed) flag array.  Outer loop
stops at `i*i > 100`; inner marking loop starts at `i*i`,
strides by `i`.  Byte-level access via `lbu`/`sb` — first
demo where the array stride is 1 (no `sll by 2` needed).
$s3 caches `la sieve` and $s4 caches the LIMIT constant
across both phases so they're not recomputed each iteration.
Output: 25 primes (`2 3 5 ... 89 97`) on one line.  Byte-
for-byte match between C and spim.

**Sketch:**

```c
static unsigned char sieve[101];   // sieve[i] = 1 means composite
for (int i = 2; i * i <= 100; i++)
  if (!sieve[i])
    for (int j = i * i; j <= 100; j += i)
      sieve[j] = 1;
for (int i = 2; i <= 100; i++)
  if (!sieve[i]) { print_int(i); print_char(' '); }
print_char('\n');
```

### 28. 8 Queens — **DONE as `33-queens`**

**What's new:** backtracking — the "try, check, recurse on
success, undo on failure" pattern.  The deepest algorithmic
demo in this list; the student sees fail-and-retry as a
first-class algorithm shape.

**spim notes:** 8-element `int columns[8]` representing which
column the queen on each row sits in.  Recursion depth max 8.
Print the first solution (or all 92, depending on appetite).
Like Hanoi this needs per-call stack frames — Fib introduces
the pattern, Queens stresses it.

**Landed:** prints all 92 solutions (one per line, eight
space-separated column indices).  Per-call 12-byte frame in
solve() holds `$ra` + saved `row` + the `col` loop counter.
The col counter HAS to be stack-resident because each
recursive call has its own col; an `$s*` register would get
overwritten by the recursive entry.  safe() and
print_solution() are both call-free internally, so neither
gets a frame.  First solution is `0 4 7 5 2 6 1 3` (the
canonical one).  C and spim produce byte-for-byte identical
output across all 92 lines.

## Plan complete

All 10 entries above have landed.  Demos 25–33 now exist in
`src/`, each with a paired C source and MIPS asm.  See
`PLAN-curriculum-order.md` for the proposed pedagogical
reading order that interleaves these with the earlier 24
demos for the eventual book.

**Sketch:**

```c
int columns[8];
int safe(int row, int col) {
  for (int i = 0; i < row; i++) {
    int c = columns[i];
    if (c == col || c - col == row - i || col - c == row - i) return 0;
  }
  return 1;
}
void solve(int row) {
  if (row == 8) { /* print solution */ return; }
  for (int col = 0; col < 8; col++)
    if (safe(row, col)) { columns[row] = col; solve(row + 1); }
}
solve(0);
```

## Pedagogical arc

Reading 19 → 28 in order, the student picks up:

- **modulo / integer division** (19, 20, 24, fizzbuzz logic)
- **iteration ↔ recursion comparison** (21)
- **call stack growth across recursive calls** (21, 25, 28)
- **array indexing with strides** (22, 23, 26, 27)
- **in-place mutation** (23, 26, 27)
- **modular arithmetic / wrap-around** (24)
- **multi-arg recursion + stack-saved args** (25)
- **`.data` as working memory, not just constants** (27)
- **backtracking** (28)

By the time they finish 28, they've seen every major intro-CS
algorithm pattern in MIPS form.

## Open questions to resolve before porting

* **`print_int` for negative numbers.**  fib(45) overflows
  int32 to a negative value.  Hanoi / sieve / Pascal only
  produce small positives.  Decision: print as signed (current
  `print_int`) and let the student see the overflow at
  fib(46) — that's pedagogy in itself.
* **Stack discipline for recursive demos.**  Fib-recursive
  (21) introduces the per-call stack frame.  Should `io.h` /
  the demos grow a small `save_ra`/`restore_ra` helper, or
  should each demo do the prologue/epilogue inline so the
  student SEES the manual save/restore?  Probably the latter,
  matching pgu pacing — the explicit pattern is the lesson.
* **Recursion depth.**  Spim's stack is finite (default 64
  KiB).  Fib(40)-recursive needs ~40 frames; safe.  Queens
  needs only 8 frames; trivial.  Worth a sentence in the
  files that "this depth fits" so students don't worry.
* **stdin-or-not for each demo.**  19/20/21/22/23/26/27/28
  are stdin-free (hardcoded inputs).  Only 24 (Caesar) reads
  stdin.  Less sentinel grief than Phase 5.
