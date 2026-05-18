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

(Plus 11-head's optional `-n N` flag before the filename.)

`13-tr` and `14-rot13` stay as pure stdin filters because real
`tr` is a pure stdin filter — it never accepts file args.

`17-nologin` stays with its hardcoded `/etc/nologin.txt` path
— the demo's lesson is the open/close pattern, not argv
parsing.

### Demos upgraded

- **`10-wc`** — stdin or file.  C and asm both rewritten
  around `my_main(argc, argv)` + `crt0.h` and a single
  `os_read(fd, &c, 1)` byte loop.
- **`11-head`** — accepts the full `head [-n N] [FILE|-]`
  shape with str_eq + atoi.  Subsumes the old `head-file`.
- **`12-rev`** — stdin or file; line buffer unchanged.
- **`15-expand`** — stdin or file; column counter unchanged.
- **`16-cat`** — stdin or file; block I/O unchanged.
  Subsumes the old `cat-file`.
- **`18-cksum`** — stdin or file; prints filename column
  in file mode (`<crc> <bytes> <filename>`), matching real
  `cksum`.

### Demos deleted

- `23-cat-file` (subsumed by 16-cat).
- `24-head-file` (subsumed by 11-head).

### Renumbering

The deletes left slots 23 and 24 empty.  Demos 25-33 shifted
down by 2 to fill them:

| Was | Now |
|---|---|
| 25-tee | 23-tee |
| 26-get-char-from-user | 24-get-char-from-user |
| 27-fibonacci | 25-fibonacci |
| 28-hanoi | 26-hanoi |
| 29-queens | 27-queens |
| 30-print-out-ascii | 40-print-out-ascii (extras) |
| 31-commaAndPeriodCounter | 41-commaAndPeriodCounter (extras) |
| 32-subrountines | 42-subrountines (extras) |
| 33-testStringsForEquality | 43-testStringsForEquality (extras) |

Total: 31 directories on disk (27 main-path + 4 extras),
verified building clean as 32 meson targets (31 demo
executables + io_lib).

### Verified modes

For each upgraded demo: bare stdin, explicit `-`, FILE, and
usage error all produce identical output between the C
binary and spim on the asm.  `18-cksum`'s output was
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
- **`-` as a filename argument in 23-tee.**  `tee`'s argv is
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
