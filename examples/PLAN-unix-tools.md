# Plan: porting sbase/ubase tools into the examples tree

The `/examples/sbase` and `/examples/ubase` checkouts are suckless'
small Unix utilities.  Many are short enough that their core
algorithm — stripped of libc, argv flag parsing, error helpers,
and edge-case handling — fits the freestanding pgu-style C used
in `src/`.  Replacing or extending the demo curriculum with
recognisable Unix programs makes the lessons concretely
"this is how `cat` actually works."

## Hard constraint — spimulator's syscall surface

Each demo has TWO sides: a C version (runs on Linux via `os.h`)
and a MIPS `.asm` (runs on spimulator).  The asm side is the
limiting factor because spimulator implements only 17 syscalls
(see `/spimulator/include/spim-syscall.h`):

| # | Name | What it does |
|---|------|--------------|
| 1  | print_int      | print integer in `$a0` |
| 2–3 | print_float/double | — (we ignore for now) |
| 4  | print_string   | print NUL-terminated string at `$a0` |
| 5  | read_int       | read decimal integer to `$v0` |
| 6–7 | read_float/double | — |
| 8  | read_string    | read up to `$a1` bytes into buffer at `$a0` |
| 9  | sbrk           | grow data segment by `$a0` bytes; returns addr in `$v0` |
| 10 | exit           | exit with status 0 |
| 11 | print_char     | print byte in `$a0` |
| 12 | read_char      | read one byte from stdin into `$v0` |
| 13 | open           | open path at `$a0` with flags `$a1`, mode `$a2`; fd in `$v0` |
| 14 | read           | read `$a2` bytes from fd `$a0` into buffer at `$a1`; bytes in `$v0` |
| 15 | write          | write `$a2` bytes from buffer at `$a1` to fd `$a0`; bytes in `$v0` |
| 16 | close          | close fd in `$a0` |
| 17 | exit2          | exit with status in `$a0` |

What this **rules out** for the demo curriculum:

* `sync`, `mkfifo`, `link`, `unlink`, `chmod`, `chown`, `mkdir`,
  `truncate`, `readlink`, `dup`, `pipe`, `fork`, `exec`, signals,
  `getenv` / environ — none of these have a spimulator syscall.
* **argv** — spimulator does not load argv into the program's
  stack.  `main:` runs with no arguments.  Anything that needs to
  parse a command line (echo, cat <file>, head -n N, mkfifo, tee
  with file args) can't be ported as-is.
* `getcwd`, `stat`, anything that inspects filesystem metadata.

Hardcoded paths and hardcoded options are fine — they just live in
`.data`.  Any tool whose interesting behaviour reduces to "do this
specific thing" rather than "parse my command line" still ports.

(If we ever want to lift these limits, the path is to extend
spimulator with new syscalls.  That's a real option but outside the
scope of this plan.)

## Selection criteria

A program is a good candidate when ALL of these hold:

1. The core algorithm is < 50 lines in the original C, after
   stripping `ARGBEGIN`, `eprintf`, `fshut`, and ARG flag handling.
2. It needs only syscalls in the spimulator table above.
3. Its argv use can be replaced with hardcoded values in `.data`
   without losing the lesson.
4. It teaches something the existing 01–08 demos haven't already
   shown, OR demonstrates a familiar pattern in a recognisable
   Unix-utility wrapper.

## Tier 1 — trivial, slot in next to existing demos

### `true` / `false`

```c
__attribute__((noreturn)) void _start(void) { os_exit(0); }     /* true  */
__attribute__((noreturn)) void _start(void) { os_exit(1); }     /* false */
```

MIPS asm uses syscall 17 (exit2) with `$a0` = 0 or 1.  Already
exists as `/pgu/src/c/exit.c`.  **Teaches:** the exit code *is* the
program.

### `clear` (ubase)

```c
__attribute__((noreturn)) void _start(void) {
    static const char clr[] = "\x1b[2J\x1b[H";
    os_write(STDOUT, clr, sizeof(clr) - 1);
    os_exit(0);
}
```

MIPS asm uses syscall 4 (print_string).  **Slot:** between 01 and
02.  **Teaches:** the bytes you write don't have to be displayable
characters — escape codes are just bytes the terminal interprets.

### `yes`

```c
__attribute__((noreturn)) void _start(void) {
    static const char s[] = "y\n";
    for (;;) os_write(STDOUT, s, 2);
}
```

MIPS asm uses syscall 4 in an infinite loop.  **Slot:** between 02
and 03.  **Teaches:** the tightest possible loop; how `Ctrl+C` and
pipe SIGPIPE terminate it; that an "infinite" loop on Unix is
usually fine because something downstream eventually closes the
pipe.  (Note: on spimulator the loop really does run forever —
no SIGPIPE.  Worth calling out as a difference from Linux.)

## Tier 2 — stdin-only versions of real Unix tools

These all share the same shape: read from stdin (syscall 12 or
14 with `$a0`=0), do work, write to stdout (syscall 4/11/15 with
fd 1).  Hardcoded inputs only — no argv.

### `cat` (stdin only)

```c
__attribute__((noreturn)) void _start(void) {
    static char buf[4096];
    long n;
    while ((n = os_read(STDIN, buf, sizeof(buf))) > 0)
        os_write(STDOUT, buf, n);
    os_exit(n < 0 ? 1 : 0);
}
```

MIPS asm uses syscalls 14 (read from fd 0) and 15 (write to fd 1)
with a static buffer in `.data` or sbrk'd memory.  **Slot:** a new
chapter after 08, "file and pipe I/O."  **Teaches:** block I/O
(versus the byte-at-a-time loops in 04 and 06); the universal
"filter" shape.

### `wc -c` and `wc -l` (stdin only)

Read each byte (syscall 12), bump a byte counter; when the byte is
`'\n'`, bump a line counter too.  At EOF, print counters with
syscall 1 + 4.

**Slot:** immediately after 06 (commaAndPeriodCounter), since it's
the same shape with one extra counter.  **Teaches:** that 06 was
already `wc` in disguise.

### `head -n 10` (stdin only, N hardcoded)

Read byte (syscall 12), echo with syscall 11, count newlines, stop
after 10.

**Slot:** after `cat`.  **Teaches:** early termination of an I/O
loop.

### `rev` (per line, stdin only)

Read into a line buffer until `'\n'` or EOF, then walk the buffer
backwards writing each byte.  Static buffer in `.data` to avoid
malloc.

**Slot:** after `head`.  **Teaches:** buffer-then-reverse pattern;
length-tracking; what happens when the buffer is full (truncate or
flush partial?).

### `nologin` (ubase)

```c
__attribute__((noreturn)) void _start(void) {
    static const char path[] = "/etc/nologin.txt";
    static char buf[256];
    int fd = (int)os_open(path, OS_O_RDONLY, 0);
    if (fd >= 0) {
        long n;
        while ((n = os_read(fd, buf, sizeof(buf))) > 0)
            os_write(STDOUT, buf, n);
        os_close(fd);
    } else {
        static const char msg[] =
            "The account is currently unavailable.\n";
        os_write(STDOUT, msg, sizeof(msg) - 1);
    }
    os_exit(1);
}
```

MIPS asm uses syscalls 13 (open), 14 (read), 15 (write), 16
(close).  **Slot:** **first multi-file demo** — needs the student
to have seen `cat` (so the read/write loop is familiar) and
introduces one new idea: `open` + `close`.  **Teaches:** how a
program reads a file that isn't stdin.

## Tier 3 — algorithmic exercises (no argv, stdin only)

### `tr 'a-z' 'A-Z'` (stdin only, hardcoded translation)

Read each byte (syscall 12), if it's in `'a'..'z'` subtract 32,
write (syscall 11).  ~20 lines.  Same shape as
`/pgu/src/c/toupper-nomm-simplified.c` but **without** argv — pure
filter.  **Teaches:** byte-level conditional transformation.

### `expand` (tabs → spaces, stdin only)

Column-tracking state machine over stdin: read byte, if it's `'\t'`
emit (8 - col % 8) spaces, else emit it; update col.  **Teaches:**
state across the input stream.

## Tier 4 — defer indefinitely (need spimulator changes)

These would teach valuable concepts but require new syscalls /
features in spimulator first.  Not worth doing unless Bill wants
to extend spimulator:

* `cat <file>`, `echo <args>`, `tee`, `head -n N <file>`,
  `mkfifo`, `truncate` — all need argv.
* `sleep` — needs an `alarm`/`pause` syscall.
* `printenv`, `env` — need environ.
* `sync` — needs a sync syscall.
* `pwd` — needs `getcwd`.
* `find`, `du` — need `readdir`/`stat`.

## Selection summary

| Tier | Programs | New asm syscalls | argv? |
|------|----------|------------------|-------|
| 1 | true, false, clear, yes | none | no |
| 2 | cat, wc, head, rev, nologin | 13/14/15/16 (open/read/write/close) | no |
| 3 | tr, expand | none beyond Tier 2 | no |
| 4 | echo, cat<file>, head<file>, tee, sync, mkfifo, sleep, ... | various | **yes** — blocked on spimulator changes |

## Status (overnight 2026-05-17/18)

**Phases 1, 2, 3 done.  Phase 5 started.**  Demos 09–18 live in `src/`:

| # | demo | C | asm | spim status | new concept |
|---|---|---|---|---|---|
| 09 | clear   | ✓ | ✓ | ✓ | ANSI escape bytes |
| 10 | yes     | ✓ | ✓ | ✓ | infinite syscall loop |
| 11 | cat     | ✓ | ✓ | ✓ | block I/O (syscalls 14/15) |
| 12 | wc      | ✓ | ✓ | ✓ | byte + line counters |
| 13 | head    | ✓ | ✓ | ✓ | early termination |
| 14 | rev     | ✓ | ✓ | ✓ | line buffer + reverse |
| 15 | nologin | ✓ | ✓ | ✓ | open + close + non-zero exit |
| 16 | tr      | ✓ | ✓ | ✓ | byte-conditional transform |
| 17 | expand  | ✓ | ✓ | ✓ | stream-state counter |
| 18 | cksum   | ✓ | ✓ | ✓ | bitwise ops + 256-entry .data table + private subroutine ($ra save) + unsigned uint32 print |
| 19 | echo      | ✓ | ✓ | ✓ | argv loop + per-element print (no atoi) |
| 20 | factorial | ✓ | ✓ | ✓ | atoi + numeric argv driving the algorithm |
| 21 | cat-file  | ✓ | ✓ | ✓ | argv + open (Phase C) |
| 22 | gcd       | ✓ | ✓ | ✓ | two atoi calls + Euclidean div/mfhi (Phase C) |
| 23 | head-file | ✓ | ✓ | ✓ | flag-check str_eq + atoi + open + per-byte read (Phase C) |
| 24 | tee       | ✓ | ✓ | ✓ | variable argc + fd array + per-block fan-out write (Phase C) |

Resolved questions:

- **Renumber-vs-append**: append.  Directory order is no longer
  pedagogical order; the eventual Sphinx book will reconcile.
- **L5 (encoding prefix) on the new demos**: NO.  These are
  pgu-paced — single-word pseudos and obvious encodings.  The
  encoding column doesn't reveal anything new.
- **Sentinel collisions**: 16/17 use `~` instead of `z` because
  `z` is inside the a..z range that tr modifies, which would be
  confusing.

Phase 4 was originally deferred ("spim doesn't pass argv to user
code") but this was a misread of the runtime — spim's
`exceptions.s` already calls `main` with `$a0=argc, $a1=argv`,
and `initialize_run_stack()` lays argv on the stack the standard
way.  What was actually broken was spim's command-line **parser**:
non-dash tokens after `-file foo.asm` re-entered the file-name
branch and overwrote `program_argc/argv` down to argc=1.

That parser bug was fixed in
`/spimulator/tasks/argv-command-line-handling.md` (3-line patch
in `src/spim.c` + new `tests/tt.argv.s` regression test added to
the Dockerfile).  Phase 4 is therefore **unblocked once the spim
patch ships**.

## Post-argv-fix rollout plan

In dependency order:

**Phase A — atoi foundation.**  Add `parse_int` to the io
library (`string-to-int.c`, declared in `io.h`, included in
`meson.build`).  The asm side replicates a small `atoi:`
subroutine per demo (matching pgu's self-contained pattern).
Without this, every numeric-argv demo would reinvent decimal
parsing.

**Phase B — argv smoke + first arithmetic demo.**

- **19-echo**: print `argv[1..N-1]` space-separated, newline.
  Validates the argv pipeline; no atoi needed.
- **20-factorial**: `n = parse_int(argv[1])`; iterative
  factorial; print result.  First demo where a numeric
  command-line arg drives the algorithm.

**Phase C — broader rollout** (priority order, after B lands):

- `cat <file>` — `os_open(argv[1])` + the existing block-I/O
  loop from 16-cat.  **DONE — 16-cat.**
- `gcd a b` — two-arg numeric, Euclidean.  **DONE — 21-gcd.**
- `head -n N <file>` — combines atoi for `-n` with file open.
  **DONE — 11-head** (the richest argv demo: combines `-n`
  flag-check via str_eq, atoi on `N`, and open on the filename).
- `tee <file ...>` — multiple output fds + stdin read loop.
  **DONE — 23-tee** (variable argc, fd-array, per-block fan-out
  write, capped at MAX_OUT=8 files).
- `fizzbuzz N`, `fibonacci N`, etc. — `PLAN-cs-demos.md`
  entries whose hardcoded N becomes more natural via argv.

Subtleties to remember when porting:

- **C side needs an inline `_start` crt0 shim** for argv (per
  `/pgu/src/c/toupper-nomm-simplified.c`): pulls `argc` and
  `argv` off the kernel-supplied stack into registers and
  calls `my_main(int, char **)`.  Linux x86_64 is the only
  arch the examples target today.
- **spim side**: `$a0 = argc`, `$a1 = argv` at entry to `main`.
  No syscall needed — exceptions.s puts them there.

**Phase 5** (already started overnight, separate track) —
stdin-only algorithmic ports of sbase tools that aren't argv-
bound:

- **18-cksum** done.  Computes the POSIX CRC32 of stdin in both
  C and spim asm, verified byte-for-byte against system `cksum`
  on empty / "a" / "hello world".  Carries a 256-entry `.word`
  table for the polynomial lookups, uses `srl`/`sll`/`xor`/`nor`
  for the bit math, calls a private `print_uint` subroutine
  (which forced the `$ra`-save lesson — first stdin-only demo
  that needs it).  A new `print-uint.c` helper was added to the
  io library since C `print_int` is signed and would render CRC
  values > INT_MAX as negative.

After Phase 5 wraps, the next direction is **classic CS
algorithm demos** (Fibonacci, Hanoi, 8 Queens, sieve, etc.) —
see [`PLAN-cs-demos.md`](PLAN-cs-demos.md) for the 10-demo
roadmap.  Those are a different pedagogical track from sbase
ports: familiar algorithms in MIPS, aimed at the college
student who knows programming but not assembly.

Candidate next Phase-5 demos (all stdin-only, no argv blocker):

- `strings`: scan stdin for ASCII runs ≥ N (hardcode N=4); state
  machine over the byte stream.  Small, ~30 lines.
- `tail -n N`: ring-buffer the last N lines, print on EOF.
  Hardcode N=10.  Introduces the ring-buffer pattern.
- `od`: dump stdin in some fixed format (octal/hex/c-style).
  Column-tracking like 15-expand, but with numeric formatting.
- A hash demo (`md5sum`/`sha256sum`): heavy on the algorithm
  side, but worth one for symmetry with cksum.  Big
  lookup-free implementation.

## Order of work

**Phase 0 — finish current asm work first.**  Layer-5
(`-explain=4`) annotation across 01–08 per `PLAN-asm-comments.md`.
Each new demo brings more `.asm`, and rolling layer 5 over a
static set is cleaner.

**Phase 1 — Tier 1 (trivial inserts):**

1. `clear` (slot 01/02).
2. `yes` (slot 02/03).

Each is < 1 hour of work for both C and asm.  No new syscalls
needed.  Defer `true`/`false` — they basically already exist as
the exit lessons inside other demos.

**Phase 2 — Tier 2 (the file/pipe chapter):**

3. `cat` (stdin only) — introduces block I/O via syscalls 14/15.
4. `wc` — generalises 06 with an additional counter.
5. `head -n 10` — early termination of an I/O loop.
6. `rev` — line buffering.
7. `nologin` — adds syscalls 13/16 (open/close).

**Phase 3 — Tier 3 (algorithmic):**

8. `tr 'a-z' 'A-Z'`.
9. `expand`.

**Phase 4 — defer.**  Tier 4 demos.  Revisit if/when spimulator
gains argv loading.

## Open questions

* **Renumbering vs append.**  Inserting `clear` between 01 and 02
  means renumbering: it becomes 02, the old 02 becomes 03, etc.
  That breaks every reference to current demo numbers.
  Alternative: append the new demos as 09, 10, 11… and let
  pedagogical order live in the book/index rather than the
  directory names.  Bill to decide before the first port.
* **Where do the originals live?**  sbase/ubase trees are large.
  After porting we may want to move the originals out of the
  example tree (e.g. into `/examples/upstream/`) so they don't
  show up in tab-completion when working on demos.
* **`os_read` / `os_write` with explicit fd in `.asm`.**  Today's
  asm tree only uses syscalls 4 (print_string) and 11/12 (per-
  character).  Adding block I/O (syscalls 14/15) is a new concept
  for the asm side — worth a paragraph in the chapter that
  introduces it.
* **Helpers in `io.h`.**  `cat`, `nologin`, etc. all want a "copy
  fd to fd" helper.  Worth adding `copy_fd(in, out)` or keeping
  each demo standalone for explicitness?
