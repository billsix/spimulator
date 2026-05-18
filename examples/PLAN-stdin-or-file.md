# Plan: stdin-or-file fallback for the Unix filter demos

## Goal

Make the Unix-filter demos behave like the real Unix tools they
port: read from `argv[1]` if a filename was given, otherwise
fall back to stdin.

```sh
wc < file       # stdin path
wc file         # opens file directly
wc              # blocks on stdin if no arg
```

Today each demo is **either** stdin-only **or** filename-via-
argv, never both.  Real `wc` is one program that does both;
ours are two programs that each do half.

This plan is deferred — written down so we don't forget.
Picked up when the curriculum wants to upgrade from "either
shape, taught separately" to "both shapes, one demo."

## Affected demos

Currently the curriculum has these stdin-only filters that
would naturally take a filename:

- `wc` (currently stdin-only)
- `head` (currently stdin-only; `head-file` exists as a
  separate demo but it doesn't fall back to stdin)
- `cat` (currently stdin-only; `cat-file` exists as a separate
  demo but it doesn't fall back to stdin)
- `rev` (currently stdin-only)
- `tr` (currently stdin-only)
- `expand` (currently stdin-only)
- `rot13` (currently stdin-only)

And these filename-only demos that would naturally fall back
to stdin:

- `cat-file` (currently fails if no argv[1])
- `head-file` (currently fails if no `-n N FILE`)

## Implementation sketch

The C-side change is ~6 lines per demo:

```c
int fd = STDIN;
if (argc > 1) {
  fd = (int)os_open(argv[1], OS_O_RDONLY, 0);
  if (fd < 0) {
    print_string("foo: cannot open ");
    print_string(argv[1]);
    print_char('\n');
    return 1;
  }
}
/* … existing read loop, but reading from `fd` instead of STDIN … */
if (fd != STDIN) os_close(fd);
```

The asm-side change is ~10 instructions:

```
        # fd = STDIN
        li $s3, 0                    # fd starts at stdin
        # if (argc > 1)
        li $t0, 1
        ble $a0, $t0, no_arg
        # fd = open(argv[1], 0, 0)
        lw $a0, 4($a1)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_err
        move $s3, $v0
no_arg:
        # ... existing read loop, but with fd = $s3 ...
```

Plus a `close` at exit time if we opened anything.

## Pedagogical value

Adding stdin-fallback to the filter demos teaches one specific
generalisation: **STDIN is just fd 0; any open file gives you
another fd; the rest of the program doesn't care which**.
That's a load-bearing concept in Unix-tool design and the
curriculum doesn't currently demonstrate it explicitly.  Today
the curriculum implicitly suggests "stdin demos use syscall
14 with `$a0=0`; file demos use syscall 13 first and then
syscall 14 with the returned fd."  The stdin-or-file unification
makes those the SAME demo with a conditional setup.

## Design decisions to make before starting

- **Modify existing demos in place** or **add new
  "wc-or-file" companions**?  My lean: modify in place.  The
  existing demos already exist for the right reasons; making
  them more faithful to the real Unix tools makes them
  better, not worse.  Existing curriculum chapters that wanted
  to show "the simplest stdin filter" still work — they just
  skip the file path in the explanation.
- **What happens to `23-cat-file` and `24-head-file`?**  Under
  the "modify in place" plan, the `-file` suffixed demos
  become redundant with the upgraded base demos.  We could:
    - merge them (each upgraded base demo IS the file-capable
      version)
    - keep both (the `-file` demo as a "look how this single-
      purpose tool ports" preview before showing the unified
      version)
  Decide based on the eventual book chapter structure.
- **What happens to `17-nologin`?**  It hardcodes a path
  (`/etc/nologin.txt`).  Could either keep that hardcoded path
  (the demo's lesson is "open + close" not "argv parsing")
  or generalise to take argv.  My lean: keep hardcoded — the
  demo is short and focused; generalising would dilute the
  open/close lesson.

## Out of scope

- Adding stdout-redirection support (the demos already
  inherit that from the shell since they `os_write(STDOUT,
  ...)` unconditionally).
- The `-` convention for "stdin as a filename" (treats `-` as
  STDIN).  Some real Unix tools do this; nice to have, not
  load-bearing.
- Adding multi-file support (`wc file1 file2 file3`).  That's
  a bigger change — needs a per-file loop wrapping the read
  loop, plus per-file output formatting.  Out of scope for v1.

## Relationship to other plans

- `PLAN-cs-demos.md` — orthogonal.
- `PLAN-curriculum-order.md` — affects which demos appear in
  Part 3 and Part 5.  If we merge the `-file` demos into
  their base counterparts, Part 5 shrinks by 2-3 demos and
  some Part 3 demos grow slightly.  Worth re-balancing
  Part 3 and Part 5 after this plan lands.
- `PLAN-unix-tools.md` — this is the natural "Phase D"
  follow-up to Phase 1-3 (stdin) and Phase C (argv+file).
