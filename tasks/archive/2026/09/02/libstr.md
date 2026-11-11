# Added libstr, the string and memory teaching library

**Completed:** 2026-09-02 (William Emerison Six <billsix@gmail.com>; one commit on
branch `overnight-2026-09-02-examples`)

Ten `<string.h>` functions were ported from musl as a teaching library at
`examples/src/lib/libstr/`. The MIPS side trades musl's optimizations (alignment-aware
word-at-a-time loops, SIMD, Two-Way string matching) for naive implementations that fit on one
screen. It is library 2 of three in the curriculum (libctype → libstr → libstdlib) and sits beside
libctype, whose alphabetic data makes the compare demos feel complete.

## Functions

Chosen as useful, teaching a distinct idea, and within spim's syscall surface; nothing that
needs malloc.

| Function | C signature | Pedagogical hook |
|---|---|---|
| `strlen`   | `size_t strlen(const char *s)`                | byte loop with NUL sentinel |
| `strcmp`   | `int strcmp(const char *a, const char *b)`    | parallel byte loop, three-way exit |
| `strncmp`  | `int strncmp(const char *a, const char *b, size_t n)` | as strcmp + bounded counter |
| `strcpy`   | `char *strcpy(char *dst, const char *src)`    | byte copy until NUL |
| `strncpy`  | `char *strncpy(char *dst, const char *src, size_t n)` | bounded copy + NUL-fill |
| `strchr`   | `char *strchr(const char *s, int c)`          | byte search, returns ptr |
| `memchr`   | `void *memchr(const void *s, int c, size_t n)` | bounded byte search |
| `memcpy`   | `void *memcpy(void *dst, const void *src, size_t n)` | byte copy by count |
| `memset`   | `void *memset(void *s, int c, size_t n)`      | byte fill by count |
| `memmove`  | `void *memmove(void *dst, const void *src, size_t n)` | overlap-aware copy direction |

The teaching rationale: the sentinel byte loop is the foundational MIPS pattern, and strlen, strcmp,
strcpy and strchr side by side show it as variations on one shape. `memcpy` vs `memmove` is a real
bug source, and the overlap-direction check teaches signed comparison with `slt`. musl's
`return (unsigned char)*a - (unsigned char)*b;` is worth contrasting with the `-1/0/1` chain
students usually write.

Every asm function is a leaf: inputs in `$a0..$a3`, result in `$v0`, `$t*` clobbered, `$s*` and
`$ra` preserved, no `jal`, no `.data`, and each ends with `jr $ra`.

## What was delivered

- `examples/src/lib/libstr/`: `libstr.h`, `libstr.c` (naive C mirroring the asm), `libstr.asm` and
  `LICENSE-musl`. The planned `libstr.md` (contract and idiom catalogue) was not written; the
  contract lives in the source comments.
- `examples/src/lib/libstr-demo/`: `str-demo.c`, `str-demo.asm` and `str-demo.expected`, with 24
  hardcoded subcases (equal/unequal/prefix and bounded compares, copy and NUL-padded bounded
  copy, forward/reverse and bounded search, sized memcpy, memset fill/clear, and memmove
  forward-overlap, backward-overlap and non-overlap), one `name=PASS` line each. It runs as
  `spimulator -f src/lib/libstr/libstr.asm -f src/lib/libstr-demo/str-demo.asm` from
  `examples/`; the multi-`-f` load was verified on 2026-07-07 (`tasks/multi-file-load.md`) and
  is pinned by `tests/tt.multifile.s`.
- Wiring: `libstr_lib`/`libstr_inc` and the `str-demo` entry in `examples/src/meson.build`,
  `str-demo` in `lib_demo_tests`, a `str-demo)` case in `examples/tests/run-demo.sh`, and a Part
  9 (teaching libraries) entry plus concept-index rows in `examples/READING-ORDER.md`. The
  examples Makefile was not updated at the time; that gap broke `make image` later and was fixed
  in `tasks/archive/2026/10/28/fix-examples-listing-std-flag.md`.

## Results

`meson compile` was clean. The golden was generated from the compiled C binary as oracle, and the
asm under spim reproduced it byte for byte (`meson test str-demo` OK, direct diff empty). The full
suite passed 34/34 (up from 33), and clang-format `--dry-run --Werror` was clean on the new
`.c`/`.h` files. One benign `-Wstringop-truncation` warning remains on the deliberate
`strncpy(buf,"hello",3)` subcase, which is the gotcha the demo teaches; `edu_args` has no `-Werror`.

## Not done

- The planned `examples/src/count-chars.c` → `strlen` consolidation (rename `count_chars`, delete
  the file, repoint its consumers) was deferred as risky. `count_chars` is part of `io_lib` and
  called by `examples/src/print-string.c`, which backs `print_string` in every demo, so it belongs
  with the demos' de-duplication sweep tracked in `tasks/multi-file-load.md`.
- Left out of scope: `strdup`/`strndup` (malloc); `strstr`/`memmem` (Two-Way, about 150 lines in
  musl); `strspn`, `strcspn`, `strpbrk`; `strtok`/`strtok_r` (stateful API); wide-char
  `wcs*`/`wmem*` (locale); musl's word-at-a-time optimizations; and `strerror`, since spim has no
  errno surface.
