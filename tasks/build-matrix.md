# Plan: clang cross-compile-to-assembly matrix in the Docker image

## Goal

At Docker image build time, cross-compile **every C port** in
`src/c/` to **assembly listings** (not linked binaries) for all
five Linux architectures that `os.h` supports, and ship those
`.s` files in the image under a predictable naming scheme.

The point is **for the reader to study** — to see the same C
source rendered as five different ISAs side-by-side, alongside
the book's original hand-written x86-32 assembly.  Not to ship
runnable cross-arch binaries.

The five target Linux architectures, matching `src/c/os.h`'s
syscall branches:

- **x86_64**    (the build host's native arch)
- **i386**      (32-bit x86; same ISA family as the book's originals)
- **arm**       (ARM-32 EABI, hard-float)
- **aarch64**   (ARM-64)
- **mips**      (MIPS-32 o32)

## Why this is worth doing

The whole pedagogical arc of /pgu is:

1. Start from "Programming from the Ground Up", a book whose
   examples are hand-written x86-32 Linux assembly.
2. Add C ports that produce the *same observable behavior* on
   any of five Linux arches, via the `os.h` syscall abstraction.

What's missing is letting the reader **see** the second half.
Today the C ports are claims about portability; the reader has
to trust that `clang --target=aarch64-linux-gnu -S` would
produce something sensible.  Materializing all five `.s`
listings up front:

- **Closes the loop on the portability lesson.**  The reader
  opens `asm-out/x86_64/exit.s` next to `asm-out/aarch64/exit.s`
  and physically sees that the *same C source* becomes two
  very different sequences of native instructions — both of
  which match the book's hand-written x86-32 assembly in
  structure.
- **Provides material for direct compare-and-contrast with the
  book's originals.**  Open the book's `exit.s` (the x86-32
  hand-written version), open `asm-out/i386/exit.s` (clang's
  output for the C port targeting the same ISA), and
  `asm-out/aarch64/exit.s` (clang's output for a totally
  different ISA).  The three together teach (i) what compilers
  produce vs what humans write, and (ii) what the ISA matters
  for vs what it doesn't.
- **Catches portability regressions at image-build time.**

## Dependencies

None — /pgu's `_start` shim already covers all five arches.
See [`pgu/src/c/toupper-nomm-simplified.c`](../pgu/src/c/toupper-nomm-simplified.c)
for the canonical 5-arch shim; the other freestanding demos in
`pgu/src/c/` use `void _start(void)` directly (no argv) and
cross-compile cleanly on any arch.

(This is unlike `/examples`, which has six demos that currently
`#error` on non-x86_64 and need
`/examples/PLAN-multiarch-shim.md` to land first.)

## Toolchain choice — clang + `-S` only

Use **clang with `--target=<triple>` and `-S`**.  No linker
needed (we're not linking).

The /pgu Makefile already documents the recipe:

```
make EXTRA_CFLAGS=--target=aarch64-linux-gnu
```

and already passes `-S` to produce `.s` listings as an
intermediate step (see `GEN_FLAGS` in `src/c/Makefile`).  All
that's missing is the loop over the five target triples.

Target triples (same as `/examples/PLAN-build-matrix.md` for
consistency):

| Arch    | `--target=`                | Extra flags        |
|---------|----------------------------|--------------------|
| x86_64  | `x86_64-linux-gnu`         | —                  |
| i386    | `i386-linux-gnu`           | —                  |
| arm     | `arm-linux-gnueabihf`      | `-mfloat-abi=hard` |
| aarch64 | `aarch64-linux-gnu`        | —                  |
| mips    | `mipsel-linux-gnu` (LE)    | `-mabi=32`         |

The `mips` target is the one most likely to have surprises —
its `os.h` branch is the most complex (`$a3` failure-signaling
fold-back), so its `.s` is the most informative comparison.

## Output layout

Per-arch subdirectories under `asm-out/`:

```
asm-out/
├── x86_64/
│   ├── exit.s
│   ├── maximum.s
│   ├── helloworld-nolib.s
│   ├── power.s
│   ├── factorial.s
│   ├── conversion-program.s
│   ├── toupper-nomm-simplified.s
│   ├── read-records.s
│   ├── write-records.s
│   ├── add-year.s
│   ├── helloworld-lib.s
│   ├── printf-example.s
│   ├── count-chars.s
│   └── integer-to-string.s
├── i386/
├── arm/
├── aarch64/
└── mips/
```

Same names across arches lets the reader script side-by-side
diffs:

```sh
diff -u asm-out/x86_64/exit.s asm-out/mips/exit.s | less
```

## Freestanding vs libc-using demos

`src/c/Makefile` splits the demos into two groups:

```make
FREESTANDING = exit maximum helloworld-nolib power factorial
               conversion-program toupper-nomm-simplified
               read-records write-records add-year

LIBC_PROGRAMS = helloworld-lib printf-example
```

For **`-S` cross-compile**, the distinction matters less than
it did for linking:

- The freestanding demos `#include "os.h"` and cross-compile to
  `.s` cleanly on every arch with no sysroot.
- The libc-using demos `#include <stdio.h>` — they need a
  per-arch libc *header* path to preprocess.  Clang's bundled
  headers don't ship `<stdio.h>`.

Three options for the libc-using two:

1. **Install per-arch glibc headers** (`glibc-headers.x86_64`,
   `glibc-aarch64-linux-gnu`, etc).  Real, modest size.
2. **Skip them on cross arches** — only generate their `.s` for
   x86_64 native (where the host's glibc-devel is available).
3. **Stub `<stdio.h>` for cross builds** — replace the
   `#include <stdio.h>` with the minimum `puts`/`printf`
   declarations needed.  Most surgical but most invasive to
   the demo source.

**Recommendation:** option 1.  The Dockerfile already has
`glibc-devel.i686` for the i386 case; extending to per-arch
cross packages is small.  Trade-off is ~100-200 MB of cross
glibc headers across all 5 arches; that's still less than
`lld+llvm` would have been if we were also linking.

Fedora packages to install (verify names against Fedora 44):

```
glibc-headers-x86_64
glibc-headers-i686 (already present as glibc-devel.i686)
glibc-arm-linux-gnu (or sysroot-arm-linux-gnu-glibc)
glibc-aarch64-linux-gnu
glibc-mips-linux-gnu (may not exist; fall back to option 2 for mips)
```

If MIPS glibc headers aren't packaged, libc-using demos punt
to x86_64-only on that arch.  Document it.

## Educational compiler flags

Inherit from the existing `src/c/Makefile`'s `GEN_FLAGS`:

```
-S -O0
-fomit-frame-pointer
-fno-asynchronous-unwind-tables
-fno-unwind-tables
-fno-dwarf2-cfi-asm        # drops .cfi_* directives
-fno-stack-protector
```

Strips CFI / DWARF / canary noise so the generated `.s` is
short enough to read top-to-bottom.  Same flag set as the
/examples build matrix uses; consistency between the two trees
matters here.

## Build integration

Add `src/c/build-asm-matrix.sh` that loops over the five
targets:

```sh
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

declare -A targets=(
    [x86_64]="x86_64-linux-gnu"
    [i386]="i386-linux-gnu"
    [arm]="arm-linux-gnueabihf -mfloat-abi=hard"
    [aarch64]="aarch64-linux-gnu"
    [mips]="mipsel-linux-gnu -mabi=32"
)

flags="-S -O0 -fomit-frame-pointer -fno-asynchronous-unwind-tables \
       -fno-unwind-tables -fno-dwarf2-cfi-asm -fno-stack-protector \
       -I ."

FREESTANDING="exit maximum helloworld-nolib power factorial \
              conversion-program toupper-nomm-simplified \
              read-records write-records add-year \
              count-chars integer-to-string"
LIBC_PROGRAMS="helloworld-lib printf-example"

for arch in "${!targets[@]}"; do
    triple="${targets[$arch]}"
    out="../../asm-out/$arch"
    mkdir -p "$out"

    for src in $FREESTANDING; do
        clang --target=$triple $flags "$src.c" -o "$out/$src.s"
    done

    if [ "$arch" = "x86_64" ] || [ "$arch" = "i386" ]; then
        for src in $LIBC_PROGRAMS; do
            clang --target=$triple $flags "$src.c" -o "$out/$src.s"
        done
    fi
    # libc-using demos on arm/aarch64/mips: see "Open questions"
done
```

Polish: pass `EXTRA_CFLAGS` through if set, so the existing
Makefile's interface still works.

## Dockerfile changes

The current /pgu Dockerfile already installs clang.  Adding
the asm matrix:

```dockerfile
# After the existing dnf install block, add cross headers if
# we want libc-using demos for all arches (per "Open questions"):
# RUN dnf install -y glibc-arm-linux-gnu glibc-aarch64-linux-gnu

# After the COPY ./src /src/ line (if not already there):
WORKDIR /src/c
RUN ./build-asm-matrix.sh
RUN mv ../../asm-out /usr/local/share/pgu-asm
```

(Or fold the loop directly into the Dockerfile RUN.)

## Test plan

One layer only (no runtime execution since we don't link):

1. **Cross-compile to .s succeeds** — `docker build` succeeds.
   Every demo's `.c` produces a `.s` for every arch.  Failure
   surfaces a missing `os.h` syscall, a broken `_start` shim,
   or a libc-headers gap.

2. **Sanity-grep the `.s`** — spot-check that each arch's
   listing actually uses that arch's instructions:

   ```sh
   grep -l "syscall"     asm-out/x86_64/*.s   # x86_64
   grep -l "int.*0x80"   asm-out/i386/*.s     # i386
   grep -l "svc"         asm-out/arm/*.s asm-out/aarch64/*.s
   grep -l "syscall"     asm-out/mips/*.s     # MIPS uses syscall too
   ```

   Catches "all five came out as x86_64 because --target got
   dropped" failure modes.

3. **Spot-check vs the book's hand-written x86-32 assembly.**
   Open `asm-out/i386/exit.s` (clang's output) next to
   `src/exit.s` (Bill's hand-written port of the book's
   original).  They should be doing the same thing in
   recognizably the same idiom — that's the whole pedagogy.
   If they look unrelated, something's wrong with either the
   `.c` port or the clang invocation.

## Image-size concern

Plain text `.s` listings, ~few MB total across 24+ files × 5
arches.  Negligible.

Toolchain: clang alone is ~250 MB (no lld needed).  Per-arch
glibc headers add ~100-200 MB if we install them for the
libc-using demos.

**Recommendation:** keep the toolchain in /pgu's image.  The
/pgu container is positioned as a learning environment (the
existing Dockerfile installs emacs/gdb/lldb/tmux), so having
clang available for the student's own experiments is
on-mission.

## Order of work

1. Audit which freestanding demos currently compile clean on
   each target arch (likely all 10 of `$(FREESTANDING)`).
2. Write `src/c/build-asm-matrix.sh` per the script sketch.
3. Run it locally on Fedora 43; verify the `asm-out/` tree
   populates correctly; spot-check via the sanity-grep test.
4. Add the script invocation to `Dockerfile`.
5. Decide on libc-using demos: punt on cross or install per-
   arch glibc headers.
6. Test full docker build end-to-end.
7. Update README.md to point students at `/usr/local/share/
   pgu-asm/` and explain how to use the comparison.

## Open questions

- **MIPS endianness.**  `mipsel` (LE) vs `mips` (BE).  Pick one
  matching `/examples/PLAN-build-matrix.md` for consistency.
  Lean `mipsel`.
- **Libc-using demos on cross arches.**  Worth installing the
  per-arch glibc headers, or punt for v1?  My lean is "install
  them" since the headers are modest size and the comparison
  story is more complete if every demo materializes for every
  arch.
- **Should the book's existing `.s` originals also be linked
  into this view?**  Could symlink `src/exit.s` into
  `asm-out/i386-book/exit.s` (or similar), creating a "side-
  by-side: hand-written vs clang -S" pair.  Pedagogically
  strong; mechanically simple.  Worth a follow-up.
- **Reproducibility.**  Pin the clang version (`dnf install
  clang-<VER>`) so a re-build produces byte-identical `.s`.

## Out of scope

- Linking the `.s` into binaries.  Rejected as the wrong
  pedagogy — the `.s` listing IS the artifact a reader wants
  here, not a binary.
- Real cross-arch execution.  Same reason.
- Non-Linux targets (FreeBSD, macOS).
- The original `.s` ports under `src/` — they stay hand-
  written, x86-32-only by design.  Their relationship to the
  matrix is comparative, not generative.
- Optimization-level studies.  Stay at `-O0`.

## Relationship to /examples

`/examples/PLAN-build-matrix.md` has the same shape and the
same goal.  The two trees should land their matrices with
consistent:

- Target triples (especially the MIPS endianness choice).
- Educational flag set (the `-fno-*` block).
- Output naming (`asm-out/<arch>/<demo>.s`).
- Libc-skip / install policy.

/pgu can start sooner since its `_start` shim is already
5-arch; /examples needs its shim plan to land first.
