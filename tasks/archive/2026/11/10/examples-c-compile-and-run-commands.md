# Every example C program documents a one-line native compile and a spimulator run

**Priority:** 3
**Difficulty:** 3
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-10
**Branch:** `c23-pass3-and-port-study`

## Outcome

All 74 teaching `.c` files under `examples/src/` (everything except the nine IO helpers, the three
library sources and the template) carry a BUILD AND RUN block just before their first `#include`:
a one-line native `gcc` command (`-std=gnu23 -nostdlib`, the `-I` directories, and exactly the
helper/library sources the demo links), the native run line, and one spimulator line per paired
`.asm`, each with the argv/stdin the demo needs. Only the header comments changed; no code. The
convention and the checker are documented in `tasks/reference/example-c-teaching-style.md`
("Every demo documents how to build and run it").

`tools/check_example_commands.py` runs the documented lines verbatim from each demo's directory (in a
scratch copy) and requires the compile to succeed and every spimulator run to match the native run's
stdout and exit status; a teaching `.c` with no block is a failure. It takes about a second, so per
the maintainer's decision it joined the examples meson suite as `documented-commands`.

Verified in the image: run against the built image with the repo bind-mounted, the build/test
stages passed 60/60 (the new test included), and the checker run with the image's installed
`/usr/local/bin/spimulator` and `/usr/bin/gcc` on PATH reported 74/74. The first full `make image`
after the commit failed, though: the Dockerfile copied `src/`, `include/`, `tests/`,
`Documentation/` and `examples/` but not `tools/`, so the new test could not find its script (the
bind-mounted runs had hidden that). A follow-up commit added `COPY tools/` to the Dockerfile, and
`make image` then passed.

## How it was done

- `tasks/adhoc/examples-c-compile-and-run-commands/discover.sh` (since deleted) compiled every demo
  and helper, read undefined/defined symbols with `nm`, and wrote each demo's transitive helper set
  to `data/inventory.tsv`.
- `insert_headers.py` (since deleted) generated the blocks from that inventory plus a hand-written
  argv/stdin table; it was idempotent (a second run changed nothing).
- Arguments and stdin: the demo's own `.input` file where one exists (as a FILE argument for demos
  that take one, so the file-open path runs), the golden test's input otherwise, a short `printf`
  for stdin filters with no input file, and `| head -3` for `yes`.

## Findings for the maintainer (not fixed: code was out of scope)

- `recursion/get-char-from-user/get-char-from-user-2.asm` (the "fixed" version) prints
  `ch was 97, value a` where the C prints `ch was a, value 97`: the two fields are swapped. Neither
  this demo nor the other three below has a golden test, which is why it went unnoticed.
- `extras/testStringsForEquality/testStringsForEquality-1.asm` prints only `1`/`0`/`1`, without the
  C version's `str1 compared to str2 is` labels.
- `extras/subrountines/subrountines-1.asm` prints the right numbers but exits with status 126: its
  hand-rolled `main` never sets `$v0`, and `__start` passes `$v0` to exit2.
- `get-char-from-user-1.asm` faults on purpose (the misaligned-slot lesson).

Each of the four carries a student-facing note in its header, and the checker lists the four `.asm`
files in `DIFFERS_ON_PURPOSE`, with the reason. Remove an entry once that asm is fixed.

## Decisions

- The checker for the documented commands joins the examples meson suite if it adds under a minute; otherwise it stays a hand-run tool (William Emerison Six <billsix@gmail.com>, 2026-11-10). It takes ~1 s, so it joined.
- The blocks are wrapped in `/* clang-format off/on */`, because clang-format's comment reflow
  (run by `format.sh` on every shell exit) would split the one-line command.
- Commands that write files (`tee`, `cp`, `touch`) write into the demo directory, as a student
  running them would; the checker runs them in a scratch copy.
