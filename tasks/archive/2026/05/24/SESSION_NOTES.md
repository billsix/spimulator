# Session notes — May 2026

Durable handoff for future Claude Code sessions.  The container is
ephemeral; commit history + this file + `docs/PORTING_QA.md` are
the only persistent memory.

## Future direction — PGU rewrite targeting MIPS-on-spim (2026-05-19)

Bill is considering rewriting PGU to use MIPS assembly running on
[[spimulator]] instead of x86 assembly running on Linux.  Pre-
requisite: spim must behave as a regular Unix process so PGU's
opening "set return status, inspect via `$?`, then pipeline" arc
still teaches what it's supposed to teach.

Investigation done 2026-05-19; four defects identified and written
up at [`/spimulator/tasks/unix-process-conformance.md`](../spimulator/tasks/unix-process-conformance.md):

1. **Banner "Loaded: …" on stdout** — corrupts pipelines.
   One-line fix (`message_out.f = stderr`).
2. **`__start` discards main's `$v0`** — `return 42` from
   `my_main` exits 0.  Two-line fix in `exceptions.s`.
3. **Parse / load / link failures exit 0** — CI can't detect
   broken builds.  Several edit points in `spim.c` / `spim-utils.c`.
4. **Runtime exceptions "ignored" and exit 0** — no way to
   detect a runtime crash from the shell.  Edit to
   `exceptions.s` + status flag readback in C wrapper.

Already works correctly: `syscall 17 (exit2)` exit code, SIGPIPE,
stdin reads, stdout flush, file syscalls, argv pass-through.  See
the spim task doc for the full evidence table and fix sketches.

**Update 2026-05-19 evening: all four defects landed.**  spim is
now a well-behaved Unix process.  PGU rewrite is unblocked from
the simulator side.  One follow-up: Defect 2 surfaced that
several /examples demos `jr $ra` from main without zeroing $v0,
so they inherit the last syscall number as exit status.  Curriculum
sweep pending; not yet authorized.

## Cumulative status (across multiple sessions)

### ✅ Completed

**A. Multi-arch C port of the book's assembly examples**
- Lives under `src/c/`; see [`src/c/README.md`](src/c/README.md).
- Covers i386 / x86_64 / arm / aarch64 / mips Linux via per-arch
  inline-asm syscall wrappers in `os.h`.
- `make check` runs every binary and verifies expected exit codes
  / stdout.  Cross-arch sanity: 17 `.c` × 5 arches, 0 errors.
- Apple Silicon was scoped out at the user's request.
- Detail / decisions / gotchas: see "C port details" below.

**B. Documentation porting QA** (RST in `docs/source/` vs upstream
DocBook XML in `upstreamSource/`)
- Initial review captured in [`docs/PORTING_QA.md`](docs/PORTING_QA.md).
- Phase 1 (`323201b claude mechanical fixes`) — sweep fixes for
  stray `;` after registers (`%cl;`), AT&T mangling (`&T`),
  entity leaks (`&ecx`, `ecx-indexed;`), escaped asterisks.
- Phase 2 (`44a415d phase 2 claude updates`) — targeted narrative
  fixes: `0x800x80`→`0x80`, `/libc.so.6`→`/lib/libc.so.6`,
  `imull` operand `%edx`→`%edx:%eax`, orphan lines deleted,
  italics restored, ASCII row 56 column-aligned, guidelines.rst
  heading dedented.
- Phase 3 (`285e10e updatde phase 3 claude`) — five raw HTML
  `<#anchor>` cross-refs converted to Sphinx `:ref:`.
- Phase 4a (rename) (`2c739c6 rename`) — `gdpapp.rst` →
  `gdbap.rst`; toctree updated in a follow-up commit.
- Phase 4b/4c (tables) — rebuilt the syscall table in
  `syscallap.rst` (full 6-column, all 14 syscalls) and the missing
  GDB quick-reference categories in `gdbap.rst` (5 categories,
  ~35 commands).  Added `syscallap.rst` to the toctree.
- LLDB Quick-Reference (`3319dab updated for lldb`) — new
  section in `gdbap.rst` mirroring the GDB reference structure
  (6 categories, label `_lldbquickref:`).
- Phase 5 (`9543d28 next phase claude code complete`) — ported
  `dedicationap.rst` and `historyap.rst`; both added to toctree
  in upstream-XML order.
- Phase 6 (verify) — discovered 4 "Malformed table" errors in
  `instructionsap.rst`; root cause was earlier `;`-stripping that
  shortened cells without re-padding.  Fixed all 31 misaligned
  rows (`396fcb5 fixed tables`).
- Phase 8 (inline-asm fidelity) — three parallel agents compared
  every inline `::` asm snippet across all 17 chapters against
  `src/*.s`.  Most chapters clean.  Nine substantive bugs found
  and fixed in one pass (4 won't-assemble: phantom `$reg` typos
  in `ctranslationap.rst`, stray `.lcomm,` comma in
  `memoryint.rst`, missing `ST_` prefix in `robust.rst`; 4 wrong
  values: `BRK`/`SYS_BRK` mismatch, C/asm immediate mismatch
  44 vs 30, Intel-syntax demo with `%ebx` and wrong multiplier,
  `$hello` vs `$helloworld`; 1 inline-only example in
  `files.rst` missing `$` immediate-mode prefix).  Worth noting
  the agents found *no* drift in functions, firstprog, records,
  files-asm-portions, counting, gdbap, intro, memory, cch,
  otherlang, guiap.

**C. Build infrastructure**
- Top-level `Makefile`: `docs:` now builds html + pdf + epub
  (was html + pdf).  `pdf` target now invokes `pdf.sh` (was
  invoking `epub.sh`, so "PDF" output was actually a second EPUB).
- `entrypoint/{html,pdf,epub}.sh`: each format now writes to its
  own subfolder under `/output/pgu/` (`html/`, `pdf/`, `epub/`)
  instead of all overwriting `/output/pgu/`.
- `entrypoint/epub.sh`: added `-r` to its `cp` (was failing because
  EPUB output has subdirectories — the cause of the make-level
  EPUB failure user originally hit).
- `docs/source/conf.py`:
  - `highlight_language = 'none'` — fixes apostrophes in narrative
    `::` blocks getting Python-string-colored (e.g. "Customer's
    name" turned green).
  - `version = release` — silences `WARNING: conf value "version"
    should not be empty for EPUB3`.
  - `suppress_warnings = ['misc.highlighting_failure']` — silences
    the four cosmetic Pygments-GAS-lexer warnings (apostrophe in
    `'0'`/`'a'` char literals; `+` in `RECORD_FIRSTNAME +
    record_buffer`).  Lexer retries in relaxed mode and still
    renders correctly.

After all of the above, latest build produces clean HTML + real
PDF + working EPUB, exit 0, 0 user-visible warnings (Inkscape
WARNINGs from PangoFT2FontMap/GtkRecentManager are environmental
and pre-existing — not anything we can fix).

### 🟡 Still open / future work

1. **`inkscape inkscape ndcSpace.svg --export-filename=inkscape ndcSpace.spng`**
   — typo in some pre-Sphinx image-conversion step doubling the
   word `inkscape` and producing a broken output filename with a
   space.  Cosmetic; doesn't fail the build.  Need to find which
   script generates that line (not in `entrypoint/`; probably in
   the Sphinx `Makefile` or a custom rule).

2. **GFDL verbatim cleanup** in `docs/source/fdlap.rst` (still
   listed as 🟢 low-priority in `docs/PORTING_QA.md`):
   - Smart curly `"…"` substituted into the GFDL text (canonically
     distributed with straight ASCII quotes — paraphrase risk for
     the license's own "verbatim copying" clauses).
   - `Copyright YEAR YOUR NAME.` lost the `©` symbol at line 416.

3. **Optional: pre-existing upstream issues** preserved verbatim
   in the RST (faithful port, not regressions):
   - `functions.rst:389` — truncated sentence "Note that in Linux
     assembly language, functions are".  Upstream has the same
     dangling clause.
   The user may want to fix this as a port-time correction or
   leave it.  (The `BRK`/`SYS_BRK` mismatch that used to live
   here was fixed as part of Phase 8.)

4. **Possible future work flagged but never asked for:**
   - Apple Silicon branch in `src/c/os.h` (libc-backed, since
     macOS has no stable raw-syscall ABI).
   - Cross toolchains in `Dockerfile` so the container can
     actually link non-x86 binaries (currently only i686 multilib
     is installed).
   - Wire `error-exit.c` / `alloc.c` into demo programs that
     exercise them end-to-end.
   - Fix four upstream typos that survived the port: "give" file
     descriptor (syscallap), "perserverance" (dedicationap),
     "editting"/"cleared" (historyap).  Fidelity vs. polish call.

---

## C port details

This file records the decisions, gotchas, and *why* behind the C
port — none of that lives in the code or the README.

---

## What got built in `src/c/`

C translations of every `.s` from `src/`, generating book-readable
assembly via `clang -O0 -S` with frame-pointer / unwind-table /
stack-protector all disabled. See `src/c/Makefile` for the exact
flag list.

Programs split into three categories:

* **Freestanding** (`-nostdlib`, define their own `_start`):
  `exit`, `maximum`, `helloworld-nolib`, `power`, `factorial`,
  `conversion-program`, `read-records`, `write-records`,
  `add-year`, `toupper-nomm-simplified`.
* **libc-linked** (`int main(void)`, link with libc):
  `helloworld-lib`, `printf-example`. Only these two — they
  exist specifically to demonstrate calling C library functions.
* **Library helpers** (no `_start`, linked into other programs):
  `count-chars.c`, `integer-to-string.c`, `write-newline.c`,
  `read-record.c`, `write-record.c`, `error-exit.c`, `alloc.c`.

Two header files: `os.h` (multi-arch syscall layer) and
`record-def.h` (the records-chapter struct).

---

## Architecture of `os.h`

Provides six syscall wrappers as `ALWAYS_INLINE static inline`
functions — `os_write`, `os_read`, `os_open`, `os_close`,
`os_exit`, `os_brk` — with arch-specific inline assembly behind
`#if defined(__i386__)` / `__x86_64__` / `__arm__` / `__aarch64__`
/ `__mips__`.

`ALWAYS_INLINE` is a one-line `__attribute__((always_inline))`
macro. The `static inline` keyword pair is written explicitly at
each declaration so the inlining intent is visible at the use
site:

```c
ALWAYS_INLINE static inline long os_write(int fd, ...) { ... }
```

I went with this over GNU statement-expression macros because:
real type checking, real argument names in the debugger, real
locals (`long ret;`). At `-O0` the result is identical to a
macro expansion — verified the `int $128` / `syscall` / `svc 0`
lands at every call site, no `call os_write` boundary.

### Arch-specific subtleties baked in

* **AArch64 has no `open` syscall.** `os_open` is built on top
  of `openat(AT_FDCWD, ...)`.
* **MIPS error convention is different.** Failure is signaled
  via `$a3 != 0` rather than negative return; the wrapper folds
  that into the standard negative-return convention.
* **MIPS `O_CREAT` / `O_TRUNC` differ** from the asm-generic
  values; `OS_O_*` constants in `os.h` ifdef around it.
* **x86_64 `syscall` clobbers `%rcx` and `%r11`** — listed in
  the clobber set on every wrapper.

### `_start` vs argc/argv

Most programs use the user's existing `_start(void)` style and
call `os_exit(N)` at the end. Only `toupper-nomm-simplified.c`
needs argv, so it has a per-arch file-scope `__asm__()` shim
(i386/x86_64/arm/aarch64/mips) that pulls argc and argv off the
raw stack and calls a regular C `int my_main(int argc, char
**argv)`. This is the same trick a minimal libc's `crt0` does;
it's inline in that one file rather than a separate `crt0.c`
because no other program needs it.

---

## Decisions made (and why)

* **Linux-only.** User explicitly dropped Apple Silicon. The
  Apple branch would have required falling back to libc (Apple
  has no stable raw-syscall ABI), no `brk()` (mmap-only), and
  `main` instead of `_start` — basically a different program
  shape. Easy to add later if needed: branch in `os.h` on
  `__APPLE__`, route to libc, exclude `alloc.c`.

* **`ALWAYS_INLINE` not `OS_INLINE`.** "OS_INLINE" sounded like
  it described the OS layer rather than an inlining strategy.
  Renamed and made the `static inline` visible in each
  declaration.

* **Header renamed `linux.h` → `os.h`.** Once the wrappers
  cover any OS (initially Linux + Apple, now just Linux), the
  Linux-specific name was misleading. Kept it `os.h` even
  after dropping Apple — same logic applies if/when you add it
  back.

* **`record-def.h` uses default alignment, not `__packed`.**
  The struct (`firstname[40]`, `lastname[40]`, `address[240]`,
  `int age`) has all offsets at multiples of 4 and total size
  324, which equals the on-disk size. `__packed` would be
  unnecessary and would generate worse code on RISC arches.

* **Built-in build verification.** `make check` runs every
  binary and matches exit codes / stdout exactly. Caught a
  `set -e` bug during development — `maximum`/`factorial`/
  `power` deliberately exit non-zero (their result *is* the
  exit code), so the recipe can't use `set -e` and instead
  does `[ $rc -eq N ] || exit 1` per case.

* **`helpers` Makefile target.** Library files like
  `error-exit.c` and `alloc.c` aren't pulled in by any current
  program, so without a `helpers` target nothing in `make`
  would touch them. The target compiles each to `.s`, surfacing
  any breakage that would otherwise sit dormant.

* **`-fno-dwarf2-cfi-asm` etc.** Without these, even `-O0`
  emits `.cfi_*` directives that obscure the generated `.s`.
  The whole pedagogical point of the C port is that students
  can read the generated assembly and recognize the patterns
  from the book.

---

## Verification done before end of session

* Native x86_64: `make check` passes — all 13 binaries match
  expected exit codes and output.
* Cross-compile sanity: 17 `.c` files × 5 arches
  (`i686-linux-gnu`, `x86_64-linux-gnu`, `arm-linux-gnueabi`,
  `aarch64-linux-gnu`, `mipsel-linux-gnu`) = 85 compilations,
  zero errors, zero warnings. Does not exercise linking on
  non-host arches — that needs sysroots installed.
* `i386` flag path (`make EXTRA_CFLAGS=-m32`) compiles to `.s`
  cleanly. Linking needs `glibc-devel.i686`, which the
  `Dockerfile` already installs.

---

## Cleanup also done

* `.gitignore` in `src/c/` covers binaries, `*.s`,
  `test.dat`, `testout.dat`.
* Stale 19-line "TODO" comment list at the bottom of
  `src/Makefile` replaced with a one-line pointer to
  `c/README.md`.
* Old `linux.h` removed (replaced by `os.h`).

---

## Open work

See [`docs/PORTING_QA.md`](docs/PORTING_QA.md) for the
documentation porting punchlist — that's the most concrete
unfinished work. The C port itself is done.

Possible future C-port additions, none requested:

* Apple Silicon branch in `os.h` (libc-backed).
* Cross toolchains in the `Dockerfile` (`gcc-arm-linux-gnueabi`,
  etc.) so the container can actually link non-x86 binaries,
  not just compile to `.s`. Currently the Dockerfile only
  installs `i686` multilib.
* Wire `error-exit.c` into a program that actually uses it
  (the book originally had `toupper.s` use `error_exit` for
  bad args; the simplified version we ported skipped that).
* Add an `alloc`-using demo program so the allocator is
  exercised end-to-end, not just compile-tested.
