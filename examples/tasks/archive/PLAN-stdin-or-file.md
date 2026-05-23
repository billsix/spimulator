# Plan: stdin-or-file fallback for the Unix filter demos

## Status — landed 2026-05-18

Implemented with the **real-Unix argv-with-dash convention**.
Six filter demos now accept either stdin (bare or via the
explicit `-` arg) or a named filename; two previously
file-only demos were deleted as redundant; the curriculum
renumbered to fill the gap.

### Convention used

```sh
demo                    # reads stdin
demo -                  # reads stdin (explicit '-')
demo FILE               # opens FILE and reads it
demo anything-else      # usage error
```

(Plus head's optional `-n N` flag before the filename.)

`tr` and `rot13` stay as pure stdin filters because real
`tr` is a pure stdin filter — it never accepts file args.

`nologin` stays with its hardcoded `/etc/nologin.txt` path
— the demo's lesson is the open/close pattern, not argv
parsing.

### Demos upgraded

- **`wc`** — stdin or file.  C and asm both rewritten
  around `my_main(argc, argv)` + `crt0.h` and a single
  `os_read(fd, &c, 1)` byte loop.
- **`head`** — accepts the full `head [-n N] [FILE|-]`
  shape with str_eq + atoi.  Subsumes the old `head-file`.
- **`rev`** — stdin or file; line buffer unchanged.
- **`expand`** — stdin or file; column counter unchanged.
- **`cat`** — stdin or file; block I/O unchanged.
  Subsumes the old `cat-file`.
- **`cksum`** — stdin or file; prints filename column
  in file mode (`<crc> <bytes> <filename>`), matching real
  `cksum`.

### Demos deleted

- `23-cat-file` (subsumed by cat).
- `24-head-file` (subsumed by head).

### Renumbering

The deletes left slots 23 and 24 empty.  Demos 25-33 shifted
down by 2 to fill them:

| Was | Now |
|---|---|
| 25-tee | tee |
| 26-get-char-from-user | get-char-from-user |
| 27-fibonacci | fibonacci |
| 28-hanoi | hanoi |
| 29-queens | queens |
| 30-print-out-ascii | print-out-ascii (extras) |
| 31-commaAndPeriodCounter | commaAndPeriodCounter (extras) |
| 32-subrountines | subrountines (extras) |
| 33-testStringsForEquality | testStringsForEquality (extras) |

Total: 31 directories on disk (27 main-path + 4 extras),
verified building clean as 32 meson targets (31 demo
executables + io_lib).

### Verified modes

For each upgraded demo: bare stdin, explicit `-`, FILE, and
usage error all produce identical output between the C
binary and spim on the asm.  `cksum`'s output was
cross-checked against the system `cksum` and matches
byte-for-byte.

## Out of scope / deferred

- **The `-` convention as a generic stdin sentinel.**  Done.
- **Multi-file support** (`cat file1 file2 file3`, `cksum *`).
  Real Unix tools take multiple file args; ours don't.  Adds
  a per-file outer loop + per-file formatting.  Punted.
- **`tr` and `rot13` learning to take a file.**  Real `tr`
  doesn't take files — we matched that behaviour.  Could be
  reconsidered if the curriculum needs a "byte-transform that
  also takes a file" demo.
- **`-` as a filename argument in tee.**  `tee`'s argv is
  output files, not input.  No change.

## Related plans

- `PLAN-curriculum-order.md` — Part 3 / 4 / 5 labels carried
  through this change; "Files" (Part 4) now contains demos
  that accept argv-with-dash, which is conceptually a Part 5
  shape.  The reading order narrative still works because
  argv arrives gradually starting at Part 3 (filter demos)
  rather than abruptly at Part 5.  Worth a one-sentence
  acknowledgement in the plan doc.
- `READING-ORDER.md` — updated.  The old "stdin-or-file gap"
  warning section is gone.
