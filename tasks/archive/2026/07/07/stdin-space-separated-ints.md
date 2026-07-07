# Task: piped space-separated ints on stdin

## Goal

Make space-separated piped input work for the `read_int`
(syscall 5) loop pattern, so that

```
echo 43 3 12 | spimulator -f examples/src/07-bubble-sort/07-bubble-sort.asm
```

reads all three values and runs the demo.  Today it reads
only the first one.

## What works now

Newline-separated input works:

```
$ printf '43\n3\n12\n' | spimulator -f .../07-bubble-sort.asm
3
12
43
```

`07-bubble-sort.asm` loops calling syscall 5 (`read_int`) and
branches on `$a3` (1 = EOF) to terminate.

## What doesn't work

Space-separated on a single line:

```
$ echo 43 3 12 | spimulator -f .../07-bubble-sort.asm
43
```

Only the first int is read; the loop exits after one
iteration even though stdin still has `3 12\n` buffered.

## Things to investigate

### 1. Where does syscall 5's int-reader live?

Likely `src/syscall.c`'s `READ_INT_SYSCALL` (or similar)
case.  Currently almost certainly uses `fgets()` /
`scanf("%d", ...)` against `stdin`, or reads a whole line
and `atoi`s the front.  The latter would explain why only
the leading int is consumed.

Check whether `$a3 = 1` is set after the FIRST successful
read with leftover whitespace+digits in the buffer, vs only
on true EOF.

### 2. Three candidate behaviors to choose from

The chosen behavior needs to match what students will type
at the prompt AND what they pipe.

**Option A — `scanf("%d", ...)`-style**: read the next int
from stdin, skipping leading whitespace (spaces, tabs,
newlines).  Leftover input stays in the buffer for the next
syscall 5.  `$a3 = 1` only when the next read fails (EOF or
non-digit garbage).  This is what a C student would expect
from `scanf` and matches typical Unix tool input.

**Option B — line-buffered, one int per line**: current
behavior.  Each syscall 5 reads a full line and returns the
first int.  Simple but doesn't compose with shell pipelines
like `seq 1 100 | program`.

**Option C — line-buffered, multi-int per line**: read a
full line on the first call, then return ints one at a
time from the buffer; refill the buffer when exhausted.
Middle ground; preserves "one int per syscall" while
accepting space-separated lines.

Recommendation in advance of investigation: probably A.
It matches `scanf` (which is what `src/syscall.c`'s
`READ_INT` likely already calls in some form), matches
the principle of least surprise for piped input, and
needs the smallest tweak — possibly just switching to
`fscanf(stdin, " %d", &v)` and reading `$a3` from
`feof(stdin)` AFTER the read attempt.

### 3. Read_char vs read_int interaction

If multiple syscalls happen in one program (a read_int
loop followed by a read_char prompt), make sure the
chosen behavior doesn't strand bytes the user expected
read_char to see.  Specifically: if read_int leaves the
trailing newline in the buffer, the next read_char gets
'\n' before the user's first real keypress.  This bites
interactive sessions but not piped ones.

### 4. The `$a3` EOF flag contract

Currently `$a3 = 1` means EOF; `$a3 = 0` means
success.  Make sure this stays consistent regardless of
which option above is picked.  Whatever `07-bubble-sort.asm`'s
`bnez $a3, after_read` does today must keep working.

### 5. /examples impact audit

After fixing, audit which /examples demos use syscall 5
in a loop:

```
grep -lE 'li \$v0, 5\b' examples/src/*/*.asm
```

For each, verify it still terminates correctly with the
new behavior under both newline-separated AND
space-separated stdin.  At minimum:
- 07-bubble-sort (the motivating case)
- 22-binary-search (also reads ints from stdin)
- anything else the audit turns up.

### 6. Test coverage

Add a regression test `tests/tt.read_int_space_sep.s`
mirroring `tt.read_int_eof.s` but piping space-separated
input.  Pin the expected behavior.

## Out of scope

- read_string (syscall 8) — different syscall, different
  user-facing semantics; not the bug Bill hit.
- Interactive REPL `print` of `$a3` — unrelated.
- Changing the assembly-side API (syscall numbers,
  register conventions).  This is a C-side fix in
  `src/syscall.c`.

## First concrete step

Read `src/syscall.c` for the syscall 5 (`READ_INT`) case
and inspect what it currently does with stdin.  Write up
the finding here before changing any code, so the design
choice between Options A/B/C is explicit.
