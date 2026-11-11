# Added a debuggable C build to the examples Makefile, alongside the readable listings

**Completed:** 2026-10-31 (under the maintainer's commit authorization, William Emerison Six
<billsix@gmail.com>, 2026-10-31)

`examples/src/Makefile` had one native flow, `demo.c -> demo.s -> bin/<demo>`, whose `EDU_FLAGS`
make the `.s` book-readable (no frame pointer, no CFI/unwind tables, no `-g`) and therefore leave
the binaries undebuggable. A second, independent `make debug` target was added that compiles each
demo straight from its `.c` into a gitignored `examples/src/bin-debug/`, so a student can step the C
in `gdb`/`lldb`. The readable `all`/`listings`/`executables` flow was left as it was.

## Why two builds, not a toggle

The maintainer wanted the examples Makefile to support both a debug build and the readable native
listings (2026-10-31). They cannot share a flag set, since the readable `.s` strips exactly what a
debugger needs, and a student wants both at once (read the clean `.s` and step the matching binary).
So `debug` is a parallel target rather than a `DEBUG=1` mode switch.

`tasks/examples-build-matrix.md` (cross-compiling each demo's `.c` to `.s` for five architectures)
is a separate concern: compiler output for reading, not debuggable binaries.

## What was built

- A `debug` target, independent of `all`/`listings`/`executables`, compiling each demo directly from
  its `.c`; going through an intermediate `.s` would add nothing for debugging.
- `DEBUG_FLAGS = -std=gnu23 -g -O0 -fno-omit-frame-pointer -fno-stack-protector`, still
  freestanding (`-nostdlib`, own `_start` via `crt0.h`). The design first specified the educational
  flags minus `-fomit-frame-pointer` plus `-g`, which kept
  `-fno-asynchronous-unwind-tables -fno-unwind-tables`; this was revised during implementation to
  drop the unwind-table suppressions so gdb backtraces work.
- Parallel debug helper archives `libio-debug.a`, `libstdlib-debug.a`, `libstr-debug.a` and
  `libctype-debug.a`, built from the same helper `.c` files with `DEBUG_FLAGS` (a `%.debug.o: %.c`
  rule), so a student can step into `print_uint`/`atoi` as well as `my_main`. This was the defaulted
  choice and mirrors the existing archive rules.
- Output in a flat `examples/src/bin-debug/` (a gitignored build output; demo basenames are unique
  across topic folders, as the readable `bin/` and meson already rely on). Only a `src/bin-debug/`
  pattern was added to `examples/.gitignore`; the existing `*.o` and `*.a` patterns already cover
  `*.debug.o` and `lib*-debug.a`.
- The image bake: the `Dockerfile` line became
  `RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang all debug`, so a pulled image carries the debug
  binaries. No package change was needed, since `entrypoint/01-install-base.sh` already installs
  `gdb`, `lldb` and `valgrind`.

## Results

`make debug` built 75 `bin-debug/<demo>` binaries. They ran and carried DWARF, and
`gdb bin-debug/factorial` broke on `my_main`, showed `factorial.c` source and read the arguments.
