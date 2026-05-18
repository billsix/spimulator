# Task: well-defined EOF signaling for read_char / read_int / read_string

## Goal

Make spim's input syscalls (5 = read_int, 8 = read_string,
12 = read_char) signal end-of-file in a way the assembly
caller can distinguish from a legitimate input value.

Today neither syscall can.  `read_int` returns `0` on EOF
(indistinguishable from the user typing `0`); `read_char`
returns `'\n'` forever at EOF (a leftover xspim-compatibility
hack that the source code itself flags as "makes xspim =
spim").  The curriculum has been working around this by using
sentinel characters (`'~'` or `'z'`) — see 10-wc, 11-head,
12-rev, 13-tr, 14-rot13, 15-expand.  Each of those demos
carries a NOTE explaining that the sentinel is a workaround,
not real EOF detection.

After this change, the asm side can detect EOF properly and
the curriculum can drop the sentinel workarounds.

## Why this is worth doing

Three concrete wins:

1. **The asm side stops lying about EOF.**  Real Unix tools
   detect EOF — every `getchar()` C program does — and the
   asm port doesn't get to.  That's a curriculum gap.
2. **Bubble-sort and similar demos become honest.**  The
   natural "read N ints from stdin until EOF, then sort" loop
   needs a clean EOF signal.  Without it, the demo has to be
   length-prefixed (first int = N) or sentinel-based (some
   magic value terminates input) — both technically work but
   neither is what `sort(1)` actually does.
3. **The C side and asm side converge.**  The C versions of
   filter demos already use `os_read returning 0` as EOF.
   The asm versions can match that exactly instead of
   pretending newlines come forever.

## State of the world today

### `read_int` (syscall 5)

`/spimulator/src/syscall.c:132-138`:

```c
case READ_INT_SYSCALL: {
  static char str[256];
  read_input(str, 256);
  R[REG_RES] = atol(str);
  break;
}
```

`read_input` (in `src/spim.c:1390`) reads bytes until newline
or until host `read()` returns ≤ 0, then null-terminates the
buffer.  At EOF the buffer is empty, and `atol("")` returns
`0`.  That's the same value the user would get by typing `0`
on a line by itself.  Indistinguishable.

### `read_char` (syscall 12)

`/spimulator/src/syscall.c:174-181`:

```c
case READ_CHARACTER_SYSCALL: {
  static char str[2];
  read_input(str, 2);
  if (*str == '\0') *str = '\n'; /* makes xspim = spim */
  R[REG_RES] = (long)str[0];
  break;
}
```

The `*str = '\0'` case fires when `read_input` returned with
nothing.  The follow-on hack assigns `'\n'`, so **on EOF,
read_char returns `'\n'` forever**.  The comment cites
xspim/spim compatibility from a previous era; nothing in
the modern curriculum depends on it.

### `read_string` (syscall 8)

`/spimulator/src/syscall.c:156-160`:

```c
case READ_STRING_SYSCALL: {
  read_input((char*)mem_reference(R[REG_A0]), R[REG_A1]);
  data_modified = true;
  break;
}
```

At EOF the destination buffer is left empty (just a NUL
terminator).  Same indistinguishability problem as read_int.

### `read_input` itself

`/spimulator/src/spim.c:1390-1416`.  Doesn't currently return
anything — it's `void`-typed.  Detecting how many bytes it
read requires changing its signature.

## The change

Two coordinated edits:

### A.  `read_char` returns `-1` on EOF

Drop the `*str = '\0' -> '\n'` xspim hack.  When read_input
fills nothing, return `-1` in `$v0`:

```c
case READ_CHARACTER_SYSCALL: {
  static char str[2];
  int n = read_input(str, 2);
  if (n == 0) {
    R[REG_RES] = -1;                   /* EOF */
  } else {
    R[REG_RES] = (long)str[0];
  }
  break;
}
```

Pedagogically this matches `getchar()` in C: `int ch =
getchar(); if (ch == EOF) break;`.  Student asm code reads
naturally as:

```
        li $v0, 12
        syscall
        bltz $v0, eof_done            # -1 = EOF
        # ... $v0 has the byte ...
```

### B.  `read_int` (and `read_string`) set `$a3` on EOF

The standard MIPS convention is `$v0` = value, `$a3` = flag
(0 = success, non-zero = error/EOF).  This is exactly how
`os.h`'s MIPS-Linux branch folds host errno into the
negative-return convention used elsewhere.

```c
case READ_INT_SYSCALL: {
  static char str[256];
  int n = read_input(str, 256);
  R[REG_RES] = atol(str);
  R[REG_A3] = (n == 0) ? 1 : 0;        /* EOF flag */
  break;
}
case READ_STRING_SYSCALL: {
  int n = read_input((char*)mem_reference(R[REG_A0]), R[REG_A1]);
  R[REG_A3] = (n == 0) ? 1 : 0;
  data_modified = true;
  break;
}
```

The asm caller checks `$a3`:

```
        li $v0, 5
        syscall
        bnez $a3, eof_done             # $a3 != 0 => EOF
        # ... $v0 has the int ...
```

`$v0` still carries whatever `atol("")` returned (0 for
read_int; empty buffer for read_string) — so existing demos
that DON'T check `$a3` see the same `$v0` they always saw.
That's the backward-compatibility argument.

### Supporting change: `read_input` returns a count

Change `read_input`'s signature from `void` to `int` (bytes
actually read, 0 on EOF).  Touch all four call sites in
syscall.c.  Header declaration in `include/spim.h:224` needs
updating too.

```c
/* New signature */
int read_input(char* str, int n);
```

```c
int read_input(char* str, int str_size) {
  ...
  int count = 0;
  while (1 < str_size) {
    char buf[1];
    if (read((int)console_in.i, buf, 1) <= 0) break;
    *ptr++ = buf[0];
    str_size -= 1;
    count += 1;
    if (buf[0] == '\n') break;
  }
  if (0 < str_size) *ptr = '\0';
  ...
  return count;
}
```

Trivial change — no caller currently uses the return value,
so adding one is additive.

## Behavior after the change

| Syscall | At EOF |
|---|---|
| `read_int` (5) | `$v0 = 0` (unchanged), `$a3 = 1` (new) |
| `read_string` (8) | buffer = empty + NUL (unchanged), `$a3 = 1` (new) |
| `read_char` (12) | `$v0 = -1` (changed; was `'\n'`) |
| `read_float`, `read_double` (6, 7) | unchanged; out of scope (curriculum doesn't use them) |

## Backward compatibility

- `read_int`: backward-compatible.  Existing demos see the same `$v0` as before.  Only NEW demos that read `$a3` benefit.
- `read_char`: **breaking change** at EOF.  Old code that consumed the `'\n'`-forever behavior would loop forever or read the wrong byte.  In the current curriculum, NO demo depends on the xspim-compat hack — they all use `'~'` or `'z'` sentinels because they couldn't trust the EOF behavior anyway.  Audit confirms zero existing breakage.
- `read_string`: backward-compatible same as read_int.

The breaking-change concern for `read_char` is purely
theoretical — every user of `read_char` in the curriculum
already works around the broken behavior, so fixing it can
only help.

## Test plan

### Spim-side regression tests

Add to `tests/`:

- **`tt.read_char_eof.s`** — reads chars from stdin into a
  buffer until `read_char` returns `-1`; prints the byte
  count; expects an exact match.  Driven via piped stdin
  with known content.
- **`tt.read_int_eof.s`** — reads ints in a loop, checks
  `$a3` after each call, accumulates the sum; verifies that
  the sum matches the expected value AND that the loop
  exited via the `$a3` flag (not after a hypothetical zero
  input).

Both wire into the Dockerfile test block next to the
existing `tt.argv.s` / `tt.args-cmd.s` lines.

### Existing-curriculum smoke tests

After the spim change, every byte-loop demo in `/examples/`
should still produce identical output to its current
behavior (since they all terminate on `'~'`/`'z'` and never
reach the actual EOF path).  Verify by running the existing
docker test invocations:

```sh
echo "Hello, World!~" | spimulator -f /examples/src/14-rot13/14-rot13.asm
# expected: Uryyb, Jbeyq!
```

Should be unchanged.

### New-behavior verification

A small new test that does the same thing but WITHOUT the
sentinel — the program should read all bytes from stdin and
exit on the real EOF:

```sh
echo "Hello, World!" | spimulator -f tests/tt.eof_char_loop.s
# expected: same output, exits cleanly when stdin closes
```

## Curriculum update — /examples

After the spim change ships, six demos in `/examples/src/`
currently use sentinel characters as EOF workarounds.  All
six should be updated to use real EOF detection:

| Demo | Current sentinel | After |
|---|---|---|
| 10-wc | `'z'` | EOF via `bltz $v0, done` after `read_char` |
| 11-head | `'z'` | same |
| 12-rev | `'z'` | same |
| 13-tr | `'z'` (sidebar) | same |
| 14-rot13 | `'~'` | same |
| 15-expand | `'~'` | same |

Each demo's body change is small (one `bltz` instead of
one `beq <sentinel>`); the `#NOTES` blocks that explain the
sentinel workaround can be dropped entirely.  The `#PURPOSE`
comments can mention "uses standard EOF detection (read_char
returns -1)" instead.

Roughly 6 .asm files × 10 lines of diff each = 60 lines.

Plus: the `os.h` MIPS-Linux branch needs no change (it
already folds `$a3` into negative returns for the real
MIPS-Linux case).

## Order of work

1. **Modify `read_input` to return a count** (`src/spim.c`,
   `include/spim.h`).  Trivial; no test impact.
2. **Update `read_int`, `read_string` to set `$a3` on EOF**
   (`src/syscall.c`).  Trivial; backward-compatible.
3. **Update `read_char` to return `-1` on EOF** — drop the
   xspim hack.  Audit confirms no existing curriculum demo
   depends on the old behavior.
4. **Add `tests/tt.read_char_eof.s` and
   `tests/tt.read_int_eof.s`**.  Wire into Dockerfile.
5. **Rebuild + re-run the full test suite.**  Existing tests
   must still pass.
6. **Update the 6 byte-loop demos in /examples** to use
   real EOF detection.  Test each: confirm identical output
   on inputs that previously used the sentinel, AND confirm
   the demos now also work on input that doesn't carry the
   sentinel.
7. **Drop the sentinel-workaround prose** from `#PURPOSE`,
   `#NOTES`, and `READING-ORDER.md` once the demos are
   updated.

## Status — landed 2026-05-18

A + B applied to spim source:
- `read_input` now returns `int` (byte count).
- `read_int` (syscall 5) sets `$a3 = 1` on EOF.
- `read_string` (syscall 8) sets `$a3 = 1` on EOF.
- `read_char` (syscall 12) returns `-1` on EOF; the
  xspim `*str = '\n'` compatibility hack is gone.
  The byte value is now widened via `(unsigned char)` so
  bytes ≥ 0x80 don't alias `-1`.
- New tests `tests/tt.read_char_eof.s` and
  `tests/tt.read_int_eof.s` cover the new signals; wired
  into the Dockerfile test block.
- Curriculum (in `/examples/src/`): 10-wc, 11-head,
  12-rev, 13-tr, 14-rot13, 15-expand, and 31-comma-and-
  period-counter switched from sentinel-character (`z` /
  `~`) to `bltz $tN, done` EOF detection.  Sentinel
  prose dropped from `#NOTES` blocks.
- 26-get-char-from-user-2.asm also gained an EOF check
  (`bltz $t0, loopEnd`) alongside its original 'a'
  terminator, so piped input that doesn't contain 'a'
  exits cleanly rather than spinning on -1.

## Open questions

- **Should `read_string` get a NULL-terminator-at-EOF
  convention too?**  Today it null-terminates the buffer
  anyway (just empty).  `$a3` is enough signal.
- **Do we want a `feof()` query syscall?**  Option C from
  the original design discussion.  Not strictly needed
  now that read_char and read_int both signal EOF in-band,
  but it'd be useful for "is more input coming?" without
  consuming a byte — e.g., a tail-like demo that wants
  to know whether to keep reading or print buffered
  output.  **Deferred** (Bill's call — write down but
  don't implement).  When this is picked up:
    - Suggested syscall number: 18 (next free after the
      curriculum's existing range).
    - Return value in `$v0`: 1 if stdin is at EOF,
      0 otherwise.
    - Implementation sketch: peek at the host's stdin
      with a non-blocking `recv(MSG_PEEK)` or maintain a
      one-byte lookahead buffer in `read_input` so
      `feof()` can answer without consuming the byte.
      The lookahead approach is portable; the
      non-blocking peek isn't on plain pipes.
    - The lookahead approach has a small invariant cost:
      every successful read consumes one byte of
      lookahead and possibly fetches the next.  Adds
      ~10 lines of state to `read_input`.
- **Should we ever produce a "pretend EOF" REPL command
  for tt tests?**  e.g., a way to inject EOF on stdin
  mid-session in the REPL so the tt suite can test
  EOF-then-keep-typing scenarios.  Probably overkill.
- **What about EOF detection for syscall 14 (read on
  arbitrary fd)?**  That syscall already returns the byte
  count in `$v0`, and `0` is the standard POSIX EOF signal.
  Existing demos (16-cat, 18-cksum, 23-cat-file, etc.) check
  `blez $v0, done` and work correctly.  No change needed.

## Out of scope

- Float / double input syscalls (6, 7).  Could get the same
  treatment, but the curriculum doesn't use them.
- Changing the `read_int` ATOL behavior (e.g., to return
  `INT_MIN` on EOF as a sentinel).  The `$a3` approach is
  cleaner.
- Renaming or removing the `'~'`/`'z'` sentinel characters
  from documentation as a historical artifact.  After the
  curriculum update they'll still be mentioned briefly
  ("before spim signaled EOF, demos used a sentinel char")
  but no longer load-bearing.
- Pedagogical reordering of demos that read input.  The
  6-Part order from `PLAN-curriculum-order.md` doesn't shift
  due to this change.
