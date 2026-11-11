# Fixed `make image`: the examples listing build now uses gnu23 and builds libstr

**Completed:** 2026-10-28 (work branch `claude`)

`make image` had two independent blockers in `examples/src/Makefile`, the hand-maintained build that
emits the `.s` teaching listings and native executables (the Dockerfile's
`RUN make -C examples/src CC=clang all`). The first masked the second. Both were fixed in the
Makefile, mirroring `examples/src/meson.build`, with no teaching `.c` changed (the demos are
deliberately un-modernized; see the "Modernization scope" note in `CLAUDE.md`).

## Root cause

Two build systems compile the demos: meson (`examples/src/meson.build`, under the top-level
`meson.build`'s `default_options: ['c_std=gnu23', 'warning_level=3']`) for the runnable binaries
and golden tests, and `examples/src/Makefile` for the side-by-side C / native asm / MIPS asm
teaching flow.

1. **Missing `-std=`, so C17.** `EDU_FLAGS` set no standard, so clang used its default C17
   (`clang -dM -E -x c /dev/null` ⇒ `__STDC_VERSION__ 201710L`), where `true`/`false`/`bool` are not
   keywords. The C23 modernization (`tasks/archive/2026/05/22/c23-modernization.md`,
   `tasks/archive/2026/10/16/c23-modernization-pass2.md`) deliberately uses those keywords and
   `nullptr` with no `<stdbool.h>`, which holds only if every build path selects C23. Commit
   `fb24ee2` ("while true") changed `for (;;)` to `while (true)` in several demos, and the build
   died with `intro/yes/yes.c:35:10: error: use of undeclared identifier 'true'`. Reproduced with
   host clang-22: no `-std` fails, `-std=gnu23` and `-std=c23` both succeed.
2. **`libstr` was never added to the Makefile.** Commit `8f0f38a` added `libstr` and `str-demo` to
   meson only. The Makefile finds demos with `find . -name '*.c'` minus a hand-listed `HELPER_C`, so
   `examples/src/lib/libstr/libstr.c` was mis-classed as a demo, `str-demo.c` had no `-Ilib/libstr`
   (`fatal error: 'libstr.h' file not found`), and no `libstr.a` existed to link. `make image` had
   been broken since `8f0f38a` at the latest; blocker 1 hid it.

## Fix

Rule text below is quoted from the Makefile, so its paths are relative to `examples/src/`; `bin/`
and the `.o`/`.a` files are gitignored build outputs.

1. `EDU_FLAGS`: prepended `-std=gnu23`, matching meson. Its comment, which claimed the "same set
   meson uses in `edu_args`", was updated to note the standard now matches meson's
   `default_options` too.
2. `HELPER_C`: added `lib/libstr/libstr.c`, which removed the bogus `bin/libstr` demo binary.
3. `INCLUDES`: added `-Ilib/libstr`.
4. `ARCHIVES`: added `libstr.a` with a `libstr.a: lib/libstr/libstr.o` rule, linked in the
   `bin/%` recipe (the linker pulls only referenced members, matching the "every demo links all
   archives" design).
5. `clean`: also removes `lib/libstr/libstr.o`.

## Decisions

- **gnu23, not strict c23.** The maintainer confirmed "gnu23 is fine." Strict `-std=c23` also breaks
  the simulator core (`src/spim-utils.c`, `src/string-stream.c`), because
  `fmemopen`/`open_memstream` are POSIX and hidden under `__STRICT_ANSI__` without a feature-test
  macro. The demos would not have broken, since `examples/src/os.h` already uses `__asm__` rather
  than bare `asm`.
- **The flag, not `<stdbool.h>`.** Adding the header to the demos would contradict the documented
  "C23 keywords, no `<stdbool.h>`" design; the flag removes the drift instead of patching each file.

## Verification

- Host: `clang -std=gnu23` compiled `examples/src/intro/yes/yes.c`,
  `examples/src/lang/calc/calc-sdt.c` and `examples/src/print-string.c`, and
  `make -C examples/src CC=clang clean all` was green, with `str-demo` built and no `libstr`
  demo binary.
- Container gate (PASS): a lean `podman build`
  (`BUILD_DOCS=0 USE_EMACS=0 BUILD_TREE_SITTER=0 RUN_SANITIZERS=1`) built a clean image. The
  `make -C examples/src CC=clang all` step compiled `examples/src/intro/yes/yes.c` and
  `examples/src/lib/libstr-demo/str-demo.c` with `-std=gnu23 … -Ilib/libstr` and linked the `yes`
  and `str-demo` binaries against `libstr.a`; meson tests ran `Ok: 25, Fail: 0`; the UBSan-trap and
  ASan gates ran.
- A full `make image` with all features on was not re-run. The lean flags only drop the TeX,
  Emacs and tree-sitter layers, which are orthogonal to this fix; the failing code path was
  fully exercised.
