# Plan: native assembly listings + runnable executables for every C demo, via a Makefile, in the Docker build

Status: **Implemented — staged, awaiting commit (Bill commits).**

## Goal

For every C demo under `examples/src/`, via a hand-written
`Makefile` (not meson), produce two **native** artifacts and run
the Makefile as a step of the Docker image build so both ship in
the image:

1. a clean, readable assembly listing `<demo>.s` *beside* its
   source, and
2. that same `.s` assembled and linked into a runnable native
   executable at `examples/src/bin/<demo>`.

This is the full `/pgu/src/c/Makefile` flow: `<demo>.c -> <demo>.s
-> bin/<demo>`. (The first cut of this plan was listings-only;
extended to also build executables per Bill's follow-up — "the
makefile also compiles the assembly to an executable… that's what
I want.")

"Native" = the **build host's** arch, auto-detected by the
compiler. `cc -S` / `cc` with no `--target` follows the host:
x86_64 on an x86_64 host, AArch64 on an arm host, and so on. No
cross-compilation, no MIPS target.

## Why

The pedagogy of this tree is "one program, multiple vocabularies":

1. `<demo>.c` — the freestanding C the student reads/writes.
2. `<demo>.asm` — the hand-written MIPS for spim (the lesson).
3. **`<demo>.s`** — the compiler's translation of the C to the
   host's native assembly.
4. **`bin/<demo>`** — that `.s` assembled + linked into a running
   program, so "the assembly you read is literally what runs."

(3) and (4) were only implicit before: the student would have to
know to run `cc -S` / link with `-nostdlib` themselves, with the
right flags, in the right directory. The Makefile makes both an
ordinary build artifact.

## Relationship to existing plans (important — read before starting)

This area already has history; this plan deliberately narrows and
supersedes parts of it:

- **`tasks/archive/PLAN-asm-listings.md`** (landed 2026-05-23) made
  meson emit native `.s` via `-save-temps=obj` + `-fverbose-asm` in
  `examples/src/meson.build`'s `edu_args`. Those listings land at the
  ugly, hard-to-find `builddir/examples/src/<demo>.p/<munged>.c.s`
  and carry `.cfi_*` debug noise (because the meson build is
  `--buildtype=debug`, i.e. `-g`). **This plan replaces that
  mechanism** with a Makefile that writes a cleanly-named `<demo>.s`
  right beside the source, with no `-g` noise. → See "meson cleanup"
  below.
- **`tasks/PLAN-build-matrix.md`** (not started) is the *5-arch*
  cross-compile-to-`.s` matrix (x86_64/i386/arm/aarch64/mips) into an
  `asm-out/<arch>/` mirror tree via a shell script. This plan is the
  **native-only, beside-source, Makefile** slice. The Makefile here
  generalizes to the full matrix later by adding target triples, but
  that remains PLAN-build-matrix's job. Note the **layout differs**
  (beside-source here vs. `asm-out/` tree there); if the matrix later
  lands, decide whether to unify.

## Where the file lives

`examples/src/Makefile` — same location relative to its sources as
`/pgu/src/c/Makefile`, and the same directory the demos and their
`.asm` already sit in.

## The Makefile (as landed)

See `examples/src/Makefile` for the committed version. Shape:

- `.c -> .s` via `%.s: %.c` with the educational flag set (`-O0
  -fomit-frame-pointer -fno-asynchronous-unwind-tables
  -fno-unwind-tables -fno-stack-protector -fverbose-asm`), no `-g`.
- `.s -> .o` via `%.o: %.s` (assemble the listing we just emitted —
  the binary really comes from the readable `.s`, not a fresh `.c`
  compile).
- `bin/<demo>` links a demo's `.s` against three helper archives
  with `-nostdlib`.
- Targets: `all` (listings + executables), `listings`,
  `executables`, `clean`.

Notes on the design choices:

- **Glob + structural split, don't enumerate.** `meson.build` keeps
  a curated demo list with hand-written per-demo link deps; the
  Makefile instead derives everything from the tree so it can't
  drift. Helpers (no `_start`) are the top-level `*.c` (the IO
  routines) plus `lib/libctype/libctype.c` and
  `lib/libstdlib/libstdlib.c`; every other `.c` is a demo with its
  own `_start`. `$(filter-out …)` separates them.
- **Archive linking instead of per-demo dep lists.** The helpers
  become `libio.a` / `libctype.a` / `libstdlib.a`, and *every* demo
  links against all three. Archive semantics mean the linker pulls
  only the members a demo actually references, so there's no need to
  special-case "ctype-demo needs libctype, atoi-demo needs libstdlib
  + libctype" the way meson does. Link order `libio libstdlib
  libctype` satisfies libstdlib→libctype (atoi calls isspace).
- **`.s` beside source; executables collected in `bin/`.** The `.s`
  sits next to `<demo>.c`/`<demo>.asm` (the three-vocabularies read
  view). Executables go in a flat `examples/src/bin/` rather than
  strewn beside each source — demo basenames are unique (meson
  relies on this too), and a single `bin/` dir is trivially
  `.gitignore`'d, unlike extensionless binaries beside sources.
  *(This is the one deviation from /pgu, which puts binaries beside
  the source; flagged for Bill — easy to switch if he prefers.)*
- **`CC ?= cc`.** Host default; Docker passes `CC=clang` to match
  the rest of the image. Both are native and honor the flag set.
- **No `-g`.** Unlike the old meson `--buildtype=debug` `-save-temps`
  path, these listings are clean of `.cfi_*`/DWARF noise.

## Docker integration

Two changes to `/spimulator/Dockerfile`:

1. **Add `make` to the dnf install list** (the image builds with
   meson+ninja and didn't install `make`).
2. **Run the Makefile** after the meson build/install/test block:

   ```dockerfile
   RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang all
   ```

   Listings land beside their sources; executables land in
   `examples/src/bin/`. (meson also installs native demo binaries to
   `$PREFIX/bin`; the Makefile's `bin/` is the in-tree, /pgu-style
   "read the `.s`, run the result" artifact and is independent.)

## meson cleanup (done)

Dropped `-save-temps=obj` and `-fverbose-asm` from `edu_args` in
`examples/src/meson.build` (kept the four `-fno-*` /
`-fomit-frame-pointer` flags, which still shape the demo-*binary*
codegen meson's tests exercise). Listing generation now lives only
in the Makefile, with cleaner output and a sane path; meson no
longer emits the buried `builddir/.../<demo>.p/<munged>.c.s`.
Confirmed `meson test` still 29/29.

## .gitignore

Generated artifacts must not be committed. Added to
`examples/.gitignore`: `*.s`, `*.o`, `*.a`, and `src/bin/`. Safe
because every hand-written source in this tree is `.asm`.

Safe because every hand-written file in this tree is `.asm`, not
`.s` (verified: `find examples -name '*.s'` is currently empty).

## READING-ORDER.md

The "Comparing the C compiler's translation" section (added in
commit 3726f75) currently points students at
`builddir/examples/src/<demo>.p/<munged>.c.s`. Rewrite it to point
at the beside-source listing instead, e.g.:

> Every demo's C is also compiled to a native assembly listing
> beside it. After building (or in the container, already present),
> open `<demo>.s` next to `<demo>.c` and `<demo>.asm` to compare
> what the compiler emits for your host against the hand-written
> MIPS for spim. To regenerate locally:
> `make -C examples/src listings`.

## Test / verification (done locally)

1. **Build.** `make CC=clang all` → rc 0. 62 `.c` → 62 `.s`; 51
   demos → 51 executables in `bin/`. Incremental rebuild is a
   proper no-op; `make clean` leaves the tree pristine (0 stray
   `.s`/`.o`/`.a`, no `bin/`).
2. **Readability spot-check.** `intro/helloworld/helloworld.s` is
   clean (no `.cfi_*`), carries `-fverbose-asm` annotations, and
   the `os.h` inline `syscall` appears verbatim.
3. **Executables run** (the key new check):
   - `bin/exit` → rc 0
   - `bin/helloworld` → `hello world`
   - `bin/wc` on piped stdin → `12 bytes, 2 lines`
   - `bin/ctype-demo` (io + libctype) → runs
   - `bin/atoi-demo` (io + libstdlib + libctype) → `atoi("42") = 42`
   These cover all three link shapes (io-only, +libctype,
   +libstdlib+libctype), proving the archive-linking strategy.
4. **meson suite unaffected.** Full `meson test -C builddir` =
   **29/29**, and meson no longer emits `.p/*.c.s`.

**Not yet verified (needs Bill / a real Docker host):** the
`docker build` itself — that `make` installs and
`make … all` runs as a layer with `bin/` populated in the image.
Verified by construction only.

## Open questions / minor

- **Executables in `bin/` vs beside source.** Chose flat `bin/`
  for clean `.gitignore` + unique basenames; /pgu strews them
  beside the source. One-line change if Bill prefers /pgu parity.
- **No `check` target.** /pgu's Makefile has a `check` that runs
  every binary against expected output; here only the 6 library
  demos have golden `.expected` files, and meson already
  run-tests those. Didn't invent goldens for the other 45 demos.
  A `check` (reusing the existing `.expected` files) is an easy
  follow-up if wanted.
- **MIPS / full matrix.** Out of scope; lives in
  `PLAN-build-matrix.md`.

## What landed

1. `examples/src/Makefile` — `.c → .s → bin/<demo>`; archive
   linking; `all` / `listings` / `executables` / `clean`.
2. `examples/.gitignore` — `*.s`, `*.o`, `*.a`, `src/bin/`.
3. `Dockerfile` — `make` added to dnf; `make … CC=clang all` after
   the meson build/test.
4. `examples/READING-ORDER.md` — "Comparing…" section rewritten for
   beside-source `.s` + `bin/<demo>`.
5. `examples/src/meson.build` — dropped `-save-temps=obj` +
   `-fverbose-asm`.
6. `ChangeLog` — dated entry.
