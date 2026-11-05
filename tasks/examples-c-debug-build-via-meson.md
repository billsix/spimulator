# Build the C examples once, with meson, as normal debug binaries on PATH; drop the native listings

**Status:** proposed — decisions made, ready to implement on a go-ahead
**Priority:** 4
**Difficulty:** 3
**Created:** 2026-11-05 (William Emerison Six <billsix@gmail.com>)

## BLUF

The C versions of the examples should be ordinary debug builds that a student runs and steps through in
gdb/lldb, next to the MIPS `.asm` they run under spimulator. This project doesn't teach the host
machine's assembly. So: meson becomes the only build of the C demos, with normal debug flags (`-g -O0`,
frame pointer kept). They install into `bin/` next to `spimulator` as `spimulator-example-<demo>`, and
`examples/src/Makefile` (the native `.s` listings plus the `.c → .s → bin` flow plus `make debug`) is
deleted. Done means: `meson install` puts every demo on PATH; `gdb spimulator-example-helloworld` can
break on `my_main` and step the C with a full backtrace; `meson test` (the examples suite) passes; a
nested `make image` builds without the Makefile step; no doc mentions `<demo>.s`, `bin/` or `bin-debug/`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-05)

1. **Meson only**, not the Makefile. Meson already compiled every demo straight from the `.c` (as
   `spimulator-example-<demo>`, `examples/src/meson.build`), so one build was already there. The
   Makefile was a second and third native build of the same demos.
2. **Install into `bin/`, next to `spimulator`.** This reverses
   `tasks/archive/2026/07/07/examples-install-location.md`, which moved the demos to
   `libexec/spimulator/` because example binaries don't belong on PATH under the FHS and Fedora packaging
   rules. The maintainer chose PATH because running and debugging the C demos is now a main activity,
   not a side artifact. The `spimulator-example-` prefix means they can't collide with real commands.
   The cost: about 50 extra completions after typing `spimulator`.
3. **Drop the readable native `.s` listings.** In the maintainer's words: "if they want to compile to
   assembly, they can do that themselves. I want to prioritize running c code in a debugger, and the
   spimulator examples, they can learn their architecture's assembly sometime else, not in this project,
   or at least, not how I teach it."

## Context (read first)

- `examples/src/meson.build`: `edu_args` (`-O0 -fomit-frame-pointer -fno-asynchronous-unwind-tables
  -fno-unwind-tables -fno-stack-protector`), `edu_link_args` (`-nostdlib`), `example_install_dir =
  libexecdir/spimulator`, and the `foreach demo : demos` / `lib_demos` / individual `executable(...)`
  blocks.
- The top-level `meson.build` includes `subdir('examples/src')`. The Dockerfile sets it up with
  `--buildtype=debug` (which adds `-g` and `-O0`), then runs `meson install` with prefix `/usr/local`.
- `examples/src/Makefile`: the targets `all`/`listings`/`executables`/`debug`/`clean`, and
  `DEBUG_FLAGS` (`-std=gnu23 -g -O0 -fno-omit-frame-pointer -fno-stack-protector`). This is the flag set
  to adopt.
- `examples/tests/run-demo.sh` takes `C_BIN_DIR` as `$1` and runs
  `$C_BIN_DIR/spimulator-example-$NAME`. Check that the meson test registration passes the build dir,
  not an install path. If so, the tests aren't affected by the install move.

## Plan

1. **Flags** (`examples/src/meson.build`): replace `edu_args` with a normal debug set. Drop
   `-fomit-frame-pointer`, `-fno-asynchronous-unwind-tables` and `-fno-unwind-tables`, so gdb backtraces
   and stepping work. Keep `-fno-stack-protector`, because the demos are freestanding (`-nostdlib`, own
   `_start`) and have no `__stack_chk_fail`. Add `-fno-omit-frame-pointer` explicitly. Let
   `--buildtype=debug` supply `-g -O0` rather than hard-coding them. Rename the variable (e.g.
   `demo_c_args`) and rewrite its comment to say what it does now ("debuggable C, stepped in
   gdb/lldb"). The libraries (`examples_io`, `libctype`, `libstr`, `libstdlib`) get the same flags, so a
   student can step into `print_string`/`atoi`. Check that the freestanding link still works with unwind
   tables on (no `.eh_frame` link error under `-nostdlib`), using both gcc and clang.
2. **Install location**: remove `install_dir: example_install_dir` (and the variable plus its comment),
   so the demos install into `bindir`.
3. **Delete `examples/src/Makefile`** and remove its entries from `examples/.gitignore` (`*.s`, `*.o`,
   `*.a`, `src/bin/`, `src/bin-debug/`). Keep only entries meson or other tools still produce.
4. **Dockerfile**: delete the `RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang all debug` step and its
   comment block.
5. **Docs**, describing the present only:
   - `examples/READING-ORDER.md`: the `<demo>.s` / `make -C examples/src` section becomes "run it,
     and step it in a debugger" (`spimulator-example-helloworld`, `gdb spimulator-example-helloworld`,
     `break my_main`, `run`, `next`).
   - `CLAUDE.md`: the examples bullet that mentions the Makefile's listings and `bin-debug/`,
     including the "two build systems ... must be kept in sync" warning, which goes away. Also the
     "Modernization scope" bullet, which says students read the C "next to the generated assembly".
   - `tasks/reference/example-c-teaching-style.md` (around its opening rationale) says the same. The
     teaching-style rule still holds, because the C mirrors the hand-written MIPS `.asm`. Reword the
     reason so it no longer cites a host listing.
   - `tasks/README.md` §Ordering & dependencies mentions `examples-build-matrix`. Update it to match
     the answer to open question 1.
   - The README, if it mentions either.
   - Add a `ChangeLog` entry. Moving the demos onto PATH and removing the listings are user-visible.
6. **Related tasks that conflict with decision 3** need the maintainer's call (see open questions):
   - `tasks/examples-build-matrix.md`: cross-compile every C demo to `.s` for five architectures.
   - `tasks/container-cross-env.md`: cross compilers + qemu-user to run the demos on five
     architectures.
   - `tasks/pgu-build-matrix.md`: the same idea for the separate `pgu/` tree, which has its own
     `.c → .s` Makefile.
7. **Verify**:
   - `meson setup --buildtype=debug` + `ninja` + `meson test` (examples suite) under both gcc and
     clang.
   - `meson install --destdir` lists every `spimulator-example-*` under `bin/`.
   - `gdb -batch -ex 'break my_main' -ex run -ex bt spimulator-example-helloworld` shows the C source
     line and a full backtrace.
   - The sanitizer gate still passes.
   - A nested `make image` builds, and in the container `which spimulator-example-helloworld` resolves.
   - `git grep` finds no `bin-debug`, `make listings` or `examples/src/Makefile` outside
     `tasks/archive`.

## Open questions

1. `tasks/examples-build-matrix.md` (cross-compile every demo to `.s` for 5 architectures) contradicts
   decision 3. Retire it, archived with a note citing this decision? Recommendation: retire.
2. `tasks/container-cross-env.md` (cross compilers + qemu to *run* the demos on 5 architectures) is about
   running, not reading assembly. Keep it, or retire it too? Recommendation: keep it but lower its
   priority; it tests `crt0.h`'s portability, which this project still cares about.
3. The `pgu/` tree has its own `.c → .s → bin` Makefile and `tasks/pgu-build-matrix.md`. Should decision
   3 apply there too, or does pgu (Programming from the Ground Up) teach x86 assembly on purpose?
   Recommendation: leave pgu alone, since that book is about x86 assembly.
