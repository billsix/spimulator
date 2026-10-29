# Fix `make image`: examples `.s`-listing build uses clang's default C17, not gnu23

**Status:** done 2026-10-28 (fix applied + verified; see Verification)
**Priority:** 2  (image build is broken on a clean checkout — blocks `make image`)
**Difficulty:** 1  (one compiler flag)
**Owner work branch:** `claude`

## BLUF

`make image` has **two independent blockers**, both in `examples/src/Makefile` (the
hand-maintained build that emits the `.s` teaching listings + native executables at
Dockerfile line 150: `RUN make -C examples/src CC=clang all`). The first masked the
second:

1. **Missing `-std=` → C17 default.** The Makefile passed no `-std=`, so clang used its
   default (C17), where `true`/`false`/`bool` are not keywords. Commit `fb24ee2`
   ("while true") swapped `for (;;)` → `while (true)` in several demos, so the build died
   with `intro/yes/yes.c:35:10: error: use of undeclared identifier 'true'`. The meson
   build sets `c_std=gnu23` project-wide and compiled the same source fine — a latent
   drift between the two build systems.
2. **`libstr` never integrated into the Makefile.** Commit `8f0f38a` added the `libstr`
   helper library + `str-demo` to the *meson* build but not to this Makefile's inventory,
   so `str-demo.c` fails with `fatal error: 'libstr.h' file not found` (no `-Ilib/libstr`),
   there is no `libstr.a` to link, and `libstr.c` was wrongly classed as a demo. This was
   hidden behind blocker 1 and surfaces the moment blocker 1 is fixed.

**Done** means both are fixed in `examples/src/Makefile` so `make image` builds again,
with no change to any teaching `.c` (the demos are deliberately un-modernized — see the
CLAUDE.md "Modernization scope" note).

## Context (cold-start)

- Two build systems compile the `examples/` demos:
  1. **meson** (`examples/src/meson.build`, driven by the top-level `meson.build` with
     `default_options: ['c_std=gnu23', 'warning_level=3']`) — builds the runnable demo
     binaries and their golden tests. Uses **gnu23**.
  2. **`examples/src/Makefile`** — a *separate* native teaching flow that emits, beside
     each `<demo>.c`, the host compiler's `.s` assembly listing and a `-nostdlib`
     executable, so a student reads C, native asm, and hand-written MIPS asm side by
     side. This is the build that `make image` invokes at Dockerfile line 150 and that
     fails. It defines `EDU_FLAGS` (its compile flags) but **never set `-std=`**, so it
     inherited clang's default C17.
- The project's C23 modernization (archived: `tasks/archive/2026/05/22/c23-modernization.md`,
  `tasks/archive/2026/10/16/c23-modernization-pass2.md`) deliberately uses C23 keywords
  `bool`/`true`/`false`/`nullptr` **with no `<stdbool.h>` includes** (see the archived
  doc's "`static_assert`, `bool`/`true`/`false`, `nullptr`" section). That design only
  holds if every build path actually selects C23 — which the listing Makefile did not.

## Root cause

**Blocker 1 — C17 default:**
- clang's default is C17 (`clang -dM -E -x c /dev/null` ⇒ `#define __STDC_VERSION__ 201710L`).
- In C17, `true` is a macro from `<stdbool.h>`, not a keyword. The demos carry no
  `<stdbool.h>` (by project design), so under C17 `true` is undeclared.
- `EDU_FLAGS` in `examples/src/Makefile` lacks `-std=`, so the listing build is C17 even
  though the meson build is gnu23. The two drifted; `fb24ee2`'s `while (true)` tripped it.
- Verified by reproduction (host clang-22): `clang -S intro/yes/yes.c` with no `-std`
  errors on `true`; `clang -std=gnu23 -S …` and `-std=c23 -S …` both succeed.

**Blocker 2 — libstr not in the Makefile inventory:**
- The Makefile discovers demos by `find . -name '*.c'` minus a hand-listed `HELPER_C`
  (`*.c` + libctype + libstdlib). `lib/libstr/libstr.c` was not in `HELPER_C`, so it was
  mis-classed as a demo, and `str-demo.c` had no `-Ilib/libstr` and nothing building/linking
  `libstr.a`. meson (`examples/src/meson.build`) handles it: `libstr_lib` +
  `libstr_inc = include_directories('lib/libstr')`, and `str-demo` links `libstr_lib`.
- So `make image` has been broken since `8f0f38a` (libstr) at the latest; `fb24ee2`
  (while true) added blocker 1 on top and is what the maintainer noticed.

## Fix (applied)

All in `examples/src/Makefile`, mirroring what `examples/src/meson.build` already does;
no teaching `.c` touched.

1. `EDU_FLAGS`: prepend `-std=gnu23` so the `.s`-listing build selects the same standard
   as the meson build and C23 keywords resolve without `<stdbool.h>`.
2. `HELPER_C`: add `lib/libstr/libstr.c` (so it is a helper, not a mis-classed demo —
   removes the bogus `bin/libstr`).
3. `INCLUDES`: add `-Ilib/libstr` (so `str-demo.c` finds `libstr.h`).
4. `ARCHIVES`: add `libstr.a`, with a `libstr.a: lib/libstr/libstr.o` rule, and link it in
   the `bin/%` recipe (the linker pulls only referenced members, matching the existing
   "every demo links all archives" design).
5. `clean`: also remove `lib/libstr/libstr.o`.

Decisions logged:
- **gnu23, not strict c23.** The maintainer confirmed "gnu23 is fine." Strict `-std=c23`
  additionally breaks the *simulator core* (`src/spim-utils.c`, `src/string-stream.c`):
  `fmemopen`/`open_memstream` are POSIX, hidden under `__STRICT_ANSI__` unless a
  feature-test macro (`_POSIX_C_SOURCE`/`_GNU_SOURCE`) is defined. gnu23 exposes them.
  Matching the project standard (gnu23) is both the minimal fix and the correct one.
- **`-std=gnu23`, not `<stdbool.h>`.** Adding `<stdbool.h>` to the demos would contradict
  the documented "C23 keywords, no `<stdbool.h>`" design (archived c23 task). The flag is
  the root-cause fix: it removes the drift rather than papering over it per-file.
- The examples' inline-asm (`examples/src/os.h`) already uses `__asm__` (not bare `asm`),
  so even strict c23 would not have broken the demos — the core was the blocker. Recorded
  for the record; not acted on.

## Verification

- Host: `clang -std=gnu23 …` compiles `intro/yes/yes.c`, `lang/calc/calc-sdt.c`,
  `print-string.c` (all exit 0).
- Host: `make -C examples/src CC=clang clean all` — full listing + executable build is
  green after both fixes (exit 0; all demos incl. `str-demo` in `bin/`; no `bin/libstr`).
- Container gate: a lean `podman build` (BUILD_DOCS=0 USE_EMACS=0 BUILD_TREE_SITTER=0
  RUN_SANITIZERS=1) must get past the previously-failing `make -C examples/src` step and
  through the meson build, the full `meson test` suite, and the UBSan+ASan gates to a
  clean image. (Lean flags only trim the docs/emacs/tree-sitter *layers*; they do not
  touch the code build, test, or sanitizer steps that gate correctness.)
- **Container gate result (2026-10-28, PASS):** the lean `podman build` built a clean
  image. STEP 29/34 `make -C examples/src CC=clang all` compiled `intro/yes/yes.c` and
  `lib/libstr-demo/str-demo.c` with `-std=gnu23 … -Ilib/libstr` and linked `bin/yes` /
  `bin/str-demo` against `libstr.a`; the meson suite ran `Ok: 25, Fail: 0`; the UBSan-trap
  and ASan gates ran; `Successfully tagged localhost/spimulator-leantest`.

**Scope note:** this gate ran with `BUILD_DOCS=0 USE_EMACS=0 BUILD_TREE_SITTER=0` (the
heavy TeX/Emacs/tree-sitter *layers*, orthogonal to this fix and network-dependent). The
maintainer's reported failure — the examples `true` error — is in the code path this gate
fully exercises. A full `make image` (all features on) was not re-run here; if it fails, it
would be in those unrelated layers, a separate task.

## Follow-ups (optional)

- **Optional:** the `EDU_FLAGS` comment claims it is the "Same set meson uses in
  `edu_args`". That was only true for the per-target args, not the project-wide
  `c_std=gnu23`. The added `-std=gnu23` makes the claim accurate; consider a one-line
  comment noting the std now matches meson's `default_options` too. (Done as part of the
  fix.)
