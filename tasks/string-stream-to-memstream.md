# Replace custom `str_stream` with POSIX `open_memstream`

## Status — not started

Filed after the variable-rename branch.  Separate from the
rename phases.  Do after the variable-name expansion (and
file-rename phase 10) lands on master.

## Summary

`src/string-stream.c` (~80 lines) implements a hand-rolled
append-only growable char buffer with printf-style formatting
— `ss_init` / `ss_printf` / `ss_to_string` / etc.  The code
predates POSIX.1-2008, which standardized exactly this
concept as `open_memstream`.  Replace the custom code with
the POSIX function; delete the custom file.

## Why this exists / why replace

The `str_stream` type was the simulator's way of building up
strings incrementally before printing them (used heavily by
disassembly formatters, the `-explain` narration code, error
message construction, etc.).  In the 1990s when this was
written, the standard C library had no concept of an
in-memory `FILE*`, so projects rolled their own.

POSIX.1-2008 added `open_memstream(char **bufp, size_t *sizep)`
which returns a `FILE*` backed by a dynamically-growing
buffer.  All of `fprintf`, `fputs`, `fputc`, `fwrite`, etc.
work against it directly — no per-project re-implementation
needed.

## Platform floor

`open_memstream` is available on:

- Linux/glibc: forever (since glibc 1.0.x).
- macOS: since 10.13 High Sierra (Sept 2017).
- FreeBSD: since 9.2 (2013).
- NetBSD: since 7.0 (2015).
- OpenBSD: not yet (last checked).  Would need a fallback if
  OpenBSD support matters.
- Windows: not available.  The project dropped Windows in
  Phase 1 of the May 2026 portability cleanup; not a concern.

**The platform floor doesn't change**: the C23 phase 12 work
already requires macOS 10.13+ for `fmemopen` (the read-side
sibling).  Adopting `open_memstream` adds nothing new to the
support matrix.

## API mapping

| Custom `str_stream` API | `open_memstream` equivalent |
|---|---|
| `ss_init(ss)` | `FILE* f = open_memstream(&buf, &size);` |
| `ss_printf(ss, fmt, ...)` | `fprintf(f, fmt, ...)` |
| `ss_to_string(ss)` | `fflush(f); return buf;` |
| `ss_length(ss)` | `ftell(f)` (after flush), or read the `size_t` out-param |
| `ss_clear(ss)` | `rewind(f);` |
| `ss_erase(ss, n)` | `fseek(f, -n, SEEK_CUR);` |
| free | `fclose(f); free(buf);` |

Mapping is nearly 1:1.  Most call sites are
`ss_printf(ss, "fmt", args)` → `fprintf(stream, "fmt", args)` —
mechanical sed.

## Mechanics

Suggested phasing:

1. **Decide on the wrapping abstraction.**  Two options:
   - **(a)** Replace `str_stream*` with bare `FILE*` everywhere.
     Need to also pass the `char**` and `size_t*` to recover
     the buffer post-`fclose`.  Most idiomatic, but `FILE*`
     alone doesn't carry the buffer info; you'd need extra
     locals.
   - **(b)** Keep a small wrapper struct
     ```c
     typedef struct {
       FILE* stream;
       char* buf;
       size_t size;
     } mem_stream;
     ```
     with `ms_init` / `ms_flush` / `ms_free` helpers.  Less
     idiomatic but matches the existing call-site shape and
     avoids forcing every caller to manage three locals.

   Recommendation: (b) for ergonomic compatibility, then later
   consider whether to flatten to bare `FILE*` for fully-idiomatic
   stdio usage.

2. **Replace the implementation.**  `src/string-stream.c`
   becomes a thin wrapper over `open_memstream`, or gets
   deleted if you go with (a).

3. **Sweep call sites.**  ~160 `ss_*` calls across the
   codebase, almost all `ss_printf` → `fprintf`.  Mechanical
   sed.  `ss_to_string` callers need to add an explicit
   `fflush` before reading the buffer.

4. **Update `include/string-stream.h`**.  Either delete or
   reduce to the wrapper struct + helpers.

5. **Verify.**  22 regression tests; the explain-mode golden
   output and the AST-print JSON tests in particular exercise
   the string-building paths heavily.

## Scope

- `src/string-stream.c` — ~80 lines, delete or thin to wrapper.
- `include/string-stream.h` — header, delete or thin.
- ~160 call sites across `src/inst.c`, `src/explain.c`,
  `src/display-utils.c`, `src/syscall.c`, possibly others.
- `meson.build` — drop `src/string-stream.c` from
  `source_files` if deleted entirely.

## What you'd gain

- ~80 lines of custom code deleted.
- Standard `stdio.h` APIs throughout (`fprintf`, `fputs`,
  `fwrite`, `fseek`) — more familiar to anyone reading the
  code cold.
- Richer functionality available for free.  E.g. the
  explain-mode formatters could use `fputc` for single
  characters instead of `ss_printf(ss, "%c", c)`.
- Consistency with the C23 phase 12 work, which already uses
  `fmemopen` (the input-side sibling) for the embedded
  exception handler.

## What you'd lose

- Slightly more ceremony at every "give me the current
  contents" point: needs `fflush(stream)` before reading
  `buf`/`size`.  The custom `ss_to_string` did this implicitly.
- The buffer is owned by the `FILE*` during streaming; you
  can't pass `buf` around to another owner without
  `fclose`-ing the stream first.
- The OpenBSD support story (if you care about it) gets
  slightly worse — they don't have `open_memstream`.

## Effort

~3-4 hours including the careful audit of all 160 call sites.
The variable rename plus the wrapper-struct design is the
heavier part; the sed sweep is mechanical.

**Risk:** medium.  The string-building paths are exercised
by the explain-mode tests so regressions surface clearly.
The bigger risk is subtle changes in flush semantics — the
custom code returns a NUL-terminated buffer at any point;
`open_memstream` only guarantees the buffer state after
`fflush` or `fclose`.

## Pairs with which other work

- Independent of the C23 modernization (already merged).
- Independent of the variable-rename branch.
- Could land before or after either.
- Pairs naturally with any future refactor of `src/explain.c`
  (the heaviest `ss_printf` user) — combine the two if you
  end up touching explain anyway.
