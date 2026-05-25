# Plan: native assembly listings for every C demo, via a Makefile, in the Docker build

Status: **Not started — awaiting review.**

## Goal

For every C demo under `examples/src/`, emit a clean, readable
**native** assembly listing `<demo>.s` *beside* its source, using
a hand-written `Makefile` (not meson), and run that Makefile as a
step of the Docker image build so the listings ship in the image.

"Native" = the **build host's** arch, auto-detected by the
compiler. `cc -S` with no `--target` follows the host: x86_64 on
an x86_64 host, AArch64 on an arm host, and so on. No
cross-compilation, no MIPS target — just "what does a real
compiler emit from this C, here, on this machine."

This is a direct port of the approach `/pgu/src/c/Makefile`
already uses (its default `make` compiles the book's C ports to
host-native `.s` with educational flags).

## Why

The pedagogy of this tree is "one program, multiple vocabularies":

1. `<demo>.c` — the freestanding C the student reads/writes.
2. `<demo>.asm` — the hand-written MIPS for spim (the lesson).
3. **`<demo>.s`** — the compiler's translation of the C to the
   host's native assembly. *This is the new artifact.*

(3) lets a student open all three in one directory and see how the
same program looks as their own host's instruction set next to the
hand-written MIPS. Today (3) is only implicit: the student would
have to know to run `cc -S` themselves with the right flags in the
right directory. Most won't.

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

## The Makefile (proposed, complete)

```makefile
# examples/src/Makefile — generate clean, readable native assembly
# listings from every C demo.
#
# For each <demo>.c this emits <demo>.s: the host compiler's
# translation of the C to the BUILD HOST's native assembly
# (x86_64 on an x86_64 host, AArch64 on an arm host, ...).  No
# --target override -- `cc -S` follows the host, exactly like
# /pgu/src/c/Makefile's default `make`.
#
# Listings-only build: it never links or runs anything.  The
# runnable MIPS binaries + their tests are owned by meson
# (meson.build / ../tests/run-demo.sh).  This Makefile exists only
# to materialize the "what does a real compiler emit from this C?"
# artifact beside each .c / hand-written .asm pair.

CC ?= cc

# Educational flags -- keep the generated .s book-readable: no
# frame pointer, no CFI/unwind tables, no stack canaries.
# -fverbose-asm annotates each instruction with its C source line
# + variable names.  Same set meson uses in edu_args (note: NOT
# gcc's -fno-dwarf2-cfi-asm, which clang rejects).
EDU_FLAGS = -O0 -S \
            -fomit-frame-pointer \
            -fno-asynchronous-unwind-tables \
            -fno-unwind-tables \
            -fno-stack-protector \
            -fverbose-asm

# Demos include io.h/os.h/crt0.h via -I.; the lib demos also need
# their library headers.
INCLUDES = -I. -Ilib/libctype -Ilib/libstdlib

# Every C translation unit gets a listing -- demos AND the
# io/libctype/libstdlib helpers.  Each .c compiles on its own (-S,
# no link), so demo-vs-library doesn't matter here.
CSRC := $(shell find . -name '*.c')
LISTINGS := $(CSRC:.c=.s)

.PHONY: all listings clean
all: listings
listings: $(LISTINGS)

%.s: %.c
	$(CC) $(EDU_FLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f $(LISTINGS)
```

Notes on the design choices:

- **Glob, don't enumerate.** `meson.build` keeps a curated list of
  demo *executables*; this Makefile instead globs every `.c` so it
  can never drift out of sync with meson, and because a per-TU `-S`
  listing is wanted for the library helpers (`io`, `libctype`,
  `libstdlib`) too, not just the linkable demos.
- **Plain `<demo>.s`, beside source.** Matches /pgu. Hand-written
  files are `.asm`, so `.s` never collides. Full source paths are
  preserved (`intro/helloworld/helloworld.s`), so identical
  basenames in different topic folders don't collide either.
- **`CC ?= cc`.** Host default. In Docker we'll call it as
  `make CC=clang` so the listings match the clang the rest of the
  image is built with (see below). Overridable by the student.
- **No `-g`.** Unlike the meson `--buildtype=debug` path, these
  listings are clean of `.cfi_*`/DWARF directives from the start.

## Docker integration

Two changes to `/spimulator/Dockerfile`:

1. **Add `make` to the dnf install list.** The image builds with
   meson+ninja and does not currently install `make` (verify: it's
   absent from the `dnf install` block around line 34–48). Add
   `make` to that list.
2. **Run the Makefile** after the meson build/install/test block
   (after the current `meson test` line, before the tree-sitter
   block):

   ```dockerfile
   # Materialize native assembly listings beside each C demo so a
   # student can compare <demo>.c, the hand-written <demo>.asm, and
   # the compiler's native <demo>.s in one directory.  CC=clang to
   # match the compiler used for the rest of the image.
   RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang listings
   ```

   The listings land beside their sources at
   `/spimulator/examples/src/<topic>/<demo>/<demo>.s` in the image.

## meson cleanup (recommended, but a decision point)

Now that the Makefile owns listing generation with cleaner output
and a better path, the interim meson mechanism is redundant.
**Recommend** removing `-save-temps=obj` and `-fverbose-asm` from
`edu_args` in `examples/src/meson.build` (leaving the four
`-fno-*` / `-fomit-frame-pointer` flags, which still shape the
*binary* codegen the tests exercise). This avoids two competing
native-listing mechanisms with different paths and quality.

If you'd rather keep both for now, that's fine — they don't
conflict; the Makefile's `.s` and meson's `.p/<...>.c.s` just
coexist. Flagging it so the redundancy is a conscious choice.

## .gitignore

The listings are build artifacts (they change with compiler
version / host arch), so they should **not** be committed. Add to
`examples/src/.gitignore` (or `examples/.gitignore`):

```
# Compiler-generated native assembly listings (see src/Makefile)
*.s
```

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

## Test / verification plan

1. **Local generation.** From `examples/src`, run
   `make listings` (and `make CC=clang listings`). Exit 0; every
   `.c` produces a sibling `.s`. `make clean` removes them all.
2. **Readability spot-check.** `less intro/helloworld/helloworld.s`
   — confirm it's clean (no `.cfi_*`), has `-fverbose-asm`
   source/variable annotations, and is recognizably the same
   program as `helloworld.asm` beside it.
3. **Sanity-grep** (catches a silently-wrong compiler invocation):
   on an x86_64 host the listings should contain `syscall` /
   `%rdi`; the inline-asm `_start` from `crt0.h` should appear
   verbatim. `grep -rl syscall examples/src --include='*.s'`.
4. **meson suite unaffected.** `meson test -C builddir` still
   passes the `examples` suite (currently 29/29) — the Makefile
   touches no binary the tests run.
5. **Docker build.** `docker build` succeeds with `make` added;
   `docker run ... ls examples/src/intro/helloworld/` shows
   `helloworld.s` present.

## Open questions / minor

- **`.s` vs arch-suffixed name.** Plain `<demo>.s` matches /pgu and
  is unambiguous on a given host (a student only builds on their
  own machine). If you'd prefer the arch baked into the name
  (`<demo>.x86_64.s`) so a checked-out image is self-describing,
  it's a one-line change to the pattern rule + var. Lean: plain
  `.s`, per your "just native" steer and /pgu parity.
- **gcc vs clang default.** `CC ?= cc` (gcc on Fedora) for a bare
  `make`; Docker pins `CC=clang` to match the image. Both are
  native and both honor the flag set. No action needed unless you
  want a single canonical compiler in both places.
- **MIPS / full matrix.** Explicitly out of scope here per the
  "just native" decision; lives in `PLAN-build-matrix.md`.

## Order of work

1. Add `examples/src/Makefile` (above). Run `make listings`
   locally; verify steps 1–3.
2. Add `*.s` to `examples/src/.gitignore`.
3. Dockerfile: add `make` to dnf list + the `make … listings` RUN
   step. Build the image; verify steps 4–5.
4. Rewrite the READING-ORDER.md "Comparing…" section.
5. (Recommended) Drop `-save-temps=obj` + `-fverbose-asm` from
   meson `edu_args`; re-run `meson test` to confirm 29/29.
6. ChangeLog entry (user-visible: new in-tree `.s` listings +
   Makefile + Docker step).
