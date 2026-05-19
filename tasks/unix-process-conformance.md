# Task: spim should behave like a Unix process

## Goal

Make `spimulator -f foo.asm` behave the same way a compiled
Linux binary would when used in a shell pipeline.  Concretely,
a student should be able to write the PGU-style "set a return
status and exit" demo and have `echo $?` work, pipe spim's
output through other Unix tools without garbage in the data
stream, and detect failure via `$?` in a Makefile or CI script.

This is motivated by Bill considering a rewrite of Programming
from the Ground Up (PGU) targeting MIPS-on-spim instead of x86.
PGU's very first example sets a return code and inspects it via
`$?`.  That pattern only teaches what it's supposed to teach if
the simulator behaves as a regular process in a pipeline.

## Why

PGU students will run:

```sh
$ spimulator -f exit.asm
$ echo $?
0
$ spimulator -f exit.asm | wc -l
0
```

Today the second one prints `1` (the "Loaded: …" banner is
counted), and several other failure modes don't propagate to
`$?` at all.  Each divergence undermines the lesson the next
chapter is trying to teach.

## Status — all four defects landed (2026-05-19)

All four defects fixed and verified with regression tests.
Summary of changes:

- **Defect 1:** `spim.c:351` — `message_out.f = stderr`.
  Banner and undefined-symbol messages now stay off the data
  stream.  Regression: `tests/tt.stderr_split.s`.
- **Defect 2:** `src/exceptions.s` — `__start` now does
  `move $a0, $v0; li $v0, 17; syscall` instead of
  `li $v0, 10; syscall`.  `return N` from main is now the
  shell's `$?`.  Regression: `tests/tt.return_value.s`.
- **Defect 3:** `spim.c` + `spim-utils.c` + `parser.y` +
  headers.  Added `parse_errors_seen` counter; main() now
  returns 2 on parser error or load failure, 1 on undefined
  symbols.  Regressions: `tests/tt.parse_error.s`,
  `tests/tt.missing_main.s`.
- **Defect 4:** `src/run.c` + `src/spim-utils.c` + `inst.h` +
  `spim.c`.  Added `first_bad_exception` latch; main()
  returns `128 + ExcCode` if any non-syscall, non-breakpoint,
  non-interrupt exception fired and the program didn't
  override via explicit `exit2(N != 0)`.  Regression:
  `tests/tt.unaligned.s` (expects exit 132 = 128+4).

### Consequence surfaced by Defect 2

`__start` now propagates `$v0` as-is via syscall 17.  Demos
that `jr $ra` from main without first setting `$v0 = 0`
inherit whatever was last loaded into `$v0` — typically the
syscall number of the final operation (4 for the demos whose
last act is `print_string`, 11 for `print_char`, etc.).

Sweep of `/examples/src` shows several demos affected:
01, 02, 16, 32 happen to exit 0 (last-syscall is `read` etc.);
03 → 2, 06-fizzbuzz → 4, 21-gcd → 11, 26-hanoi → 1,
36-tac → 3, etc.

This is the right behavior — it makes "set $v0 before
returning from main" a real, observable invariant rather than
a silent convention.  But the curriculum needs a sweep to add
`li $v0, 0` (or an explicit `syscall 17`) before each `jr $ra`
in main.  **Tracked as a separate follow-up task; not in scope
of this plan.**

### Exit-status convention now in force

| Cause | Shell `$?` |
|---|---|
| Normal exit via `exit2(N)` | N |
| `jr $ra` from main with `$v0 = N` | N |
| Undefined symbols (e.g., `main` missing) | 1 |
| Parser error in .asm | 2 |
| `Cannot open file` (`-file` given) | 2 |
| Runtime exception (alignment fault, etc.) | 128 + ExcCode |
| Pipeline downstream closed (SIGPIPE) | 141 (existing) |

Explicit `exit2(N != 0)` always wins over the exception
latch; the program's stated intent beats the simulator's
inference.

## Investigation summary — what works, what doesn't

### Already correct

| Property | Mechanism |
|---|---|
| Exit status via `syscall 17 (exit2)` | `$a0` is captured into `spim_return_value` (`syscall.c:195`), returned from main (`spim.c:550`) |
| SIGPIPE | Default behavior — spim dies with 141 when downstream pipe closes early |
| stdin via `read(fd=0)` | `read_input` in `spim.c:1396` is raw `read()`, no libedit interference |
| stdout flush | `write_output` (`spim.c:1355`) `fflush()`es after every write |
| File syscalls 13/14/15/16 | Call kernel `open`/`read`/`write`/`close` directly with real fds |
| argv pass-through | `$a0=argc, $a1=argv` at main entry (post argv-command-line fix) |
| `-quiet` flag | Suppresses banner — but requiring it for every invocation isn't Unix-y |

### Diverges from Unix-process behavior

Four distinct defects, smallest first:

#### Defect 1 — banner pollutes stdout

`spim.c:351` initializes `message_out.f = stdout`.  Everything
written via `write_output(message_out, …)` therefore lands in
the data stream:

- "Loaded: /usr/local/share/spimulator/exceptions.s\n" (from
  `spim-utils.c:116`)
- "The following symbols are undefined:\n<list>\n" (from
  `spim.c:538-540`, `:753-755`)
- "(spim) " prompt (from `spim.c:670`) — only when REPL active
- Breakpoint-encountered messages (`spim.c:760`, `:773`, `:792`)
- The interactive help text (`spim.c:965+`)
- "\nExecution interrupted\n" (`spim.c:684`)

Demonstration:

```sh
$ spimulator -f hello.asm | wc -l   # hello.asm prints one line
2                                   # off-by-one because of the banner
```

**Fix:** change line 351 to `message_out.f = stderr`.  These
messages are informational, not data.  Real warnings/errors
(`run_error`, `fatal_error`, `error`) already use `stderr`
directly (`spim.c:1313`, `:1330`, `:1347`), so this change
brings the message channel in line with the existing error
channel.

#### Defect 2 — main's return value is discarded

`exceptions.s:181-192`:

```mips
__start:
    lw $a0, 0($sp)        # argc
    addiu $a1, $sp, 4     # argv
    addiu $a2, $a1, 4     # envp
    sll $v0, $a0, 2
    addu $a2, $a2, $v0
    jal main
    nop
    li $v0, 10            # syscall 10 = exit (no status)
    syscall
```

After `main` returns, `__start` always issues syscall 10, which
ignores `$v0`.  A C-on-MIPS program that does `return 42` from
`my_main` gets exit status 0.

Pure-PGU code sidesteps this — PGU teaches you to write the
exit syscall explicitly with the status in a register before
issuing it — but the paired-C-side of the curriculum is
asymmetric.

**Fix:** rewrite the tail of `__start`:

```mips
    move $a0, $v0         # carry main's return value through
    li $v0, 17            # exit2: status in $a0
    syscall
```

Two-line change in `src/exceptions.s`.  Mirrors the
`_start → main → exit(rv)` chain in real glibc / musl `_start`
stubs.  Existing demos that already call exit2 explicitly are
unaffected (they never reach the `__start` tail).

#### Defect 3 — load/parse/link failures exit 0

| Failure mode | spim's exit code | Should be |
|---|---|---|
| Parser error in .asm | 0 | non-zero |
| `Cannot open file: foo.asm` | 0 | non-zero |
| Undefined symbol (`main` missing) | 0 | non-zero (link-equivalent failure) |

None of these set `spim_return_value`.  A `make check` or CI
job can't detect that a student's program failed to assemble.

**Fix sketch:** in `spim.c`'s main loop, after the
`read_assembly_file` and symbol-resolution steps, check for
the error conditions already detected (the code already prints
the messages — it just doesn't set the exit code) and set
`spim_return_value = 1` (or a distinct small value per
category if we want make-style "1 = warnings, 2 = errors")
before falling through.

Specific edit points to investigate:

- `spim.c:538-541` — "The following symbols are undefined"
  block.  Set return value here.
- `spim.c:753-755`, `:759` — same in the REPL path.
- `spim-utils.c` — wherever `Cannot open file` is emitted.
  The function calling `read_assembly_file` already gets a
  bool back; propagate it.
- `scanner.l`/`parser.y` — parser already counts errors
  (`bison`'s `yynerrs`); plumb that through.

Open question: should an assembly error during `-file` parsing
be fatal (skip `run_program` entirely) or just non-zero exit
after best-effort run?  Real assemblers refuse to produce
output on error; matching that would mean fatal, exit 2.

#### Defect 4 — runtime exceptions are "ignored" with exit 0

Running this:

```mips
        .text
        .globl main
main:
        lw $t0, 1($zero)      # misaligned load
        li $v0, 10
        syscall
```

prints `Exception 4 [Address error in inst/data fetch] occurred
and ignored` and exits 0.  Same for divide-by-zero (silent, exit
0) and other faults.  Real Linux: SIGSEGV/SIGBUS, process dies,
shell `$?` = 139 or 138.

The "ignored" behavior comes from `exceptions.s`'s handler
sequence: on exception, it prints a message and jumps back to
the next instruction.  This is pedagogically defensible —
beginners can read the message and keep stepping — but it
breaks Unix-process semantics.

**Fix options, increasing scope:**

- **A. Status flag from handler.**  Have `exceptions.s` write a
  sentinel (e.g. 1) to a known kernel-segment word on exception,
  and have the C wrapper read it post-`run_program` and OR it
  into `spim_return_value`.  Continues to "ignore" exceptions
  for stepping clarity, but the final exit code reflects the
  fact that an exception fired.
- **B. Terminate on exception.**  Change `exceptions.s` to issue
  `syscall 17` with a status convention (e.g. 128 + exception
  number, mirroring shell signal exit codes) instead of
  returning.  Matches real Unix more strictly, but breaks the
  "continue stepping after fault" pedagogy.
- **C. Make it a runtime flag.**  Default A, opt into B with
  `-strict-exceptions` for CI / pipeline use.

Recommend option A (least disruption to the existing teaching
mode).  If Bill wants the strict shell-status convention as
well, option C is cheap on top.

## Recommended implementation order

Smallest blast radius first:

1. **Defect 1 — `message_out` → stderr.**  One-line edit in
   `spim.c:351`.  Add a regression test (`tests/tt.stderr.s`)
   that runs a tiny program and verifies stdout contains only
   the program's own output, stderr contains the banner.

2. **Defect 2 — `__start` propagates `$v0`.**  Two-line edit in
   `src/exceptions.s`.  Add a regression test
   (`tests/tt.return_value.s`) that does `li $v0, 42; jr $ra`
   and verifies shell `$?` = 42.

3. **Defect 3 — load/parse/link failures set exit code.**
   Larger but mechanical.  Several edits in `spim.c` and
   `spim-utils.c`.  Add regression tests for each category
   (`tt.parse_error.s` with deliberate garbage,
   `tt.missing_main.s` with no `main` label).

4. **Defect 4 — runtime exceptions reflected in exit code.**
   Option A: edit `exceptions.s` + small read-back in C wrapper.
   Regression test (`tt.unaligned.s`) that performs an
   unaligned load and verifies `$?` != 0.

After all four: PGU's curriculum can be rewritten with the
working assumption that "spim is a Unix process."

## Why this matters for the PGU rewrite

The first PGU chapter is built around the idea that
`./program; echo $?` is the simplest possible Unix interaction.
If spim violates that, every subsequent PGU example inherits a
small lie — and chapters like the error-handling and pipeline
examples become much harder to teach because the simulator
behaves differently from the surrounding shell.

The PGU `int 0x80` / `%eax = 1` / `%ebx = N` pattern maps
directly to MIPS `li $v0, 17` / `li $a0, N` / `syscall`.  Most
of PGU's examples will translate without spim changes once the
four defects above are fixed:

- `exit.s` → trivial; already works modulo banner pollution
- `error-exit.s` → needs Defect 3 fix to show "real" failure
- `count-chars.s` → file syscalls work today
- `read-record.s`, `write-record.s` → file syscalls work today
- `linux.s` (syscall-number header) → becomes a `.equ` block
  giving 1/4/5/8/10/11/12/13/14/15/16/17

## Out of scope

- Signal delivery from spim to its child of itself (e.g.
  `kill(getpid(), SIGSEGV)` instead of `exit(139)`).  The
  exit-status convention is the practical equivalent and
  doesn't require touching POSIX signal plumbing.
- Pseudo-TTY emulation / curses programs.  PGU doesn't use
  these.
- Memory-mapped I/O (`-mapped_io`).  Stays as-is.

## Related

- `tasks/argv-command-line-handling.md` — argv pass-through
  was a prerequisite; that's already landed.
- `tasks/octal-escape-fix.md` — recent example of a small
  spim-correctness fix.  This task is similar in spirit but
  larger in surface area.
- `/examples/READING-ORDER.md` — once Defect 2 lands, the
  curriculum's C side could `return 0` / `return N` from
  `my_main` and have it Just Work.
