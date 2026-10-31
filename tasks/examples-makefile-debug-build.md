# Examples Makefile: a debuggable C build alongside the readable listings

**Status:** done + archived 2026-10-31 — `make debug` builds 75 debuggable
`bin-debug/<demo>` binaries; verified they run and carry DWARF, and `gdb bin-debug/factorial`
breaks on `my_main`, shows `factorial.c` source, and reads args. Image bake added to the
Dockerfile. Implemented under the maintainer's commit authorization (William Emerison Six
<billsix@gmail.com>, 2026-10-31).
**Priority:** 5
**Difficulty:** 3

## BLUF

`examples/src/Makefile` has one native flow today: `demo.c -> demo.s -> bin/<demo>`, with
`EDU_FLAGS` tuned to make the `.s` **book-readable** (no frame pointer, no CFI/unwind
tables, no `-g`). That same flag set makes the binaries **undebuggable**. Add a SECOND,
independent build — a `debug` target that compiles each demo straight from its `.c` with
`-g -O0 -fno-omit-frame-pointer` into a gitignored `bin-debug/`, so a student can step the
C in `gdb`/`lldb`. Keep the readable `listings`/`bin/` flow exactly as it is. Bake the
debug build into the container image. "Done" = `make debug` produces debuggable
`bin-debug/<demo>` binaries, the existing `make`/`listings`/`executables` are unchanged,
and the image carries `bin-debug/` so a pulled image is debuggable out of the box.

## Context

Two ideas the maintainer wants the examples Makefile to support (William Emerison Six
<billsix@gmail.com>, 2026-10-31):

1. compile all examples **in debug mode** so a student can debug the program in C;
2. what it does today — generate the **readable native assembly** listings.

They can't share one flag set: the readable `.s` deliberately strips exactly what a
debugger needs (`-fomit-frame-pointer`, no CFI/unwind tables, no `-g`), so they are two
artifact sets, not a `DEBUG=1` toggle on one. A student wants both present at once (read
the clean `.s` *and* step the matching binary), which is why this adds a parallel target
rather than a mode switch.

**Read first:**
- `examples/src/Makefile` — the current native flow. Targets `all` (= `listings`
  `executables`), `listings` (the `.s`), `executables` (`bin/<demo>`), `clean`. `EDU_FLAGS`
  is the readable-`.s` flag set; `LINK_FLAGS = -nostdlib` (each demo defines its own
  `_start` via `crt0.h` and reaches the kernel through `os.h` syscalls). It globs demos
  with `find . -name '*.c'` and filters out the IO/library helpers.
- `Dockerfile` line ~150: `RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang all` — the
  image already bakes the native Makefile. The debug build is baked by adding one more
  `RUN` (or extending that one to `all debug`).
- `entrypoint/01-install-base.sh` — already installs `gdb`, `lldb`, `valgrind`. No image
  package change is needed; there is an `entrypoint/dotfiles/.../.lldbinit`.

**Adjacent but DISTINCT — do not merge:** `tasks/examples-build-matrix.md` cross-compiles
each demo's `.c` to `.s` for five architectures for side-by-side comparison. That is
compiler *output* for reading; this task is debuggable *binaries*. No overlap.

## Settled design

- **New `debug` target**, independent of `all`/`listings`/`executables` (which stay
  byte-for-byte as they are).
- **Compile from the `.c` directly** (not via a debug `.s`): routing through an
  intermediate `.s` buys nothing for debugging and complicates the rule.
- **`DEBUG_FLAGS`** = the educational flags MINUS `-fomit-frame-pointer`, PLUS `-g`
  and `-fno-omit-frame-pointer`, i.e.
  `-std=gnu23 -g -O0 -fno-omit-frame-pointer -fno-asynchronous-unwind-tables
  -fno-unwind-tables -fno-stack-protector`. Still freestanding: `-nostdlib`, own `_start`.
  The student sets a breakpoint on `my_main` and steps the C against real DWARF line tables.
- **Output dir `bin-debug/`**, flat (demo basenames are unique across topic folders, as
  `bin/` and meson already rely on), gitignored like `bin/`.
- **Baked into the image**: extend the Dockerfile so the examples `make` run also builds
  `bin-debug/`.

## Decisions made during implementation

- **Debug helper archives (the defaulted choice):** parallel `libio-debug.a` /
  `libstdlib-debug.a` / `libstr-debug.a` / `libctype-debug.a`, compiled from the same
  helper `.c` with `DEBUG_FLAGS` (`%.debug.o: %.c` rule), so a student can step INTO
  `print_uint`/`atoi` as well as `my_main`. Mirrors the existing archive rules.
- **`DEBUG_FLAGS`** settled at `-std=gnu23 -g -O0 -fno-omit-frame-pointer -fno-stack-protector`
  (dropped the unwind-table suppressions so gdb backtraces work; still freestanding
  `-nostdlib`, own `_start`).
- **`*.debug.o` and `lib*-debug.a` need no new gitignore rule** — the existing `*.o` / `*.a`
  patterns in `examples/.gitignore` cover them; only `src/bin-debug/` was added.

## Verification (when implemented)

- `make debug` produces `bin-debug/<demo>` for every demo; `gdb bin-debug/factorial`
  breaks on `my_main` and shows C source lines.
- `make`, `make listings`, `make executables` output is unchanged (diff `bin/` + the `.s`
  against a pre-change build).
- `bin-debug/` is in `examples/.gitignore` (or the repo's gitignore covering it).
- The containerized `make image` gate is green and the built image carries `bin-debug/`.
