# Plan: clang cross-compile-to-assembly matrix in the Docker image

## Goal

At Docker image build time, cross-compile **every C demo** in
`src/` to **assembly listings** (not linked binaries) for all
five Linux architectures that `os.h` supports, and ship those
`.s` files in the image under a predictable naming scheme.

The point is **for the student to read and compare** — to see
the same C source rendered as five different ISAs side-by-side.
Not to ship runnable cross-arch binaries.

The five target Linux architectures, matching `os.h`'s syscall
branches:

- **x86_64**    (the build host's native arch)
- **i386**      (32-bit x86)
- **arm**       (ARM-32 EABI, hard-float)
- **aarch64**   (ARM-64)
- **mips**      (MIPS-32 o32)

## Why this is worth doing

The whole pedagogy of this tree is "one C source, multiple
assembly languages."  Today the student gets:

- The hand-written `.asm` for spim (MIPS).
- The native `gcc -S` output for whatever host arch they're on
  (usually x86_64), if they bother to run it themselves.

What's missing is the **comparison artifact**: opening the same
demo's `.s` file for all five arches in a tiled editor view and
seeing how `os_write` becomes `syscall` on x86_64, `int $0x80`
on i386, `svc #0` on arm/aarch64, and `syscall` (with $v0=4004)
on mips.  That comparison is exactly the lesson `os.h` already
exists to teach — but right now it's an abstract claim, not
something the reader can hold in their hand.

Materializing the .s matrix at image build time:

- **Makes the comparison concrete and immediate.**  The
  student opens `asm-out/x86_64/echo-1.s` and
  `asm-out/aarch64/echo-1.s` and sees the difference,
  with no toolchain install.
- **Catches portability regressions at image-build time.**  A
  new demo that breaks the cross-compile on any arch fails the
  docker build, the same way `meson test` catches native
  regressions today.
- **Saves the student a toolchain install** for the
  comparison.  Especially valuable since the cross-compile
  takes ~500 MB of toolchain to install for a one-off look.

## Dependencies

Soft prerequisite: [`PLAN-multiarch-shim.md`](PLAN-multiarch-shim.md)
needs to land first for the six argv-using demos (echo,
factorial, cat, gcd, head, tee).
Those carry an inline `_start` shim that's currently gated as
`#error` on non-x86_64.  Without the shim plan applied, the
preprocessor halts on the unsupported `#elif` branch and
`clang -S` fails.

Demos 01–18 don't have the shim and should generate `.s`
cleanly on all five arches today.

## Toolchain choice — clang + `-S` only

Use **clang with `--target=<triple>` and `-S`**.  No linker
needed (we're not linking).

Rationale (same as before):

- One package (`clang` in Fedora) covers all five targets.
- The demos are `-nostdlib` freestanding, so no libc sysroot
  is required to generate the `.s` — clang's bundled
  `<stddef.h>` / `<stdint.h>` are enough.
- No `lld` install needed (no link step).
- No `qemu-user-static` needed (no runtime execution).

Concrete invocation per target:

```sh
clang --target=<triple> -S -O0 \
      -fomit-frame-pointer -fno-asynchronous-unwind-tables \
      -fno-unwind-tables -fno-stack-protector \
      -I src \
      src/N-name/N-name-1.c \
      -o asm-out/<arch>/N-name-1.s
```

Note: we generate `.s` per individual source file.  The io-lib
sources (`count-chars.c`, `integer-to-string.c`, etc.) also get
their own per-arch `.s` listings.  The student can compare them
too — they're often shorter and clearer comparisons than the
demos themselves.

Target triples:

| Arch    | `--target=`                | Extra flags        |
|---------|----------------------------|--------------------|
| x86_64  | `x86_64-linux-gnu`         | —                  |
| i386    | `i386-linux-gnu`           | —                  |
| arm     | `arm-linux-gnueabihf`      | `-mfloat-abi=hard` |
| aarch64 | `aarch64-linux-gnu`        | —                  |
| mips    | `mipsel-linux-gnu` (LE)    | `-mabi=32`         |

## Output layout

Per-arch subdirectories under `asm-out/`:

```
asm-out/
├── x86_64/
│   ├── helloworld-1.s
│   ├── print1through10-1.s
│   ├── ...
│   ├── tee-1.s
│   ├── count-chars.s
│   ├── integer-to-string.s
│   └── ... (io-lib sources)
├── i386/
├── arm/
├── aarch64/
└── mips/
```

Same `.s` name across arches lets the student script
side-by-side diffs:

```sh
diff -u asm-out/x86_64/echo-1.s asm-out/aarch64/echo-1.s | less
```

or open both in a tiled editor.

## Educational compiler flags

The /pgu tree's existing Makefile sets a useful set of flags
specifically tuned for **readable** generated `.s`.  Inherit
that set rather than reinventing it:

```
-O0
-fomit-frame-pointer
-fno-asynchronous-unwind-tables
-fno-unwind-tables
-fno-dwarf2-cfi-asm        # drops .cfi_* directives
-fno-stack-protector
```

These strip out the metadata that a "real" production `.s`
file would carry (CFI unwind tables, DWARF debug, stack
canaries, frame-pointer setup) so the `.s` is short enough to
read top-to-bottom.

## Meson integration

Two options (same trade-off as the earlier executables plan,
but each is cheaper now that we're not linking):

### Option A — five meson cross files

`cross/x86_64.txt`, `cross/i386.txt`, etc.  Each points meson
at the right clang invocation with `-S`.  Build each:

```sh
meson setup builddir-aarch64 --cross-file cross/aarch64.txt
meson compile -C builddir-aarch64
```

### Option B — a shell script that loops

Add `build-asm-matrix.sh` that loops over the five target
triples and invokes `clang -S` directly per source file.
Simpler; no need to teach meson about cross-compilation for
something we're not linking.

**Recommendation:** Option B for this case.  Since we never
link, meson's executable() / library() machinery isn't really
buying anything — we're just running clang once per
(arch, source).  A shell script is more honest about that.

Shell script sketch:

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

for arch in "${!targets[@]}"; do
    triple="${targets[$arch]}"
    out="../asm-out/$arch"
    mkdir -p "$out"
    for c in $(find . -name '*.c' -not -path './builddir*/*'); do
        rel="${c#./}"
        name="${rel##*/}"
        clang --target=$triple $flags "$c" -o "$out/${name%.c}.s"
    done
done
```

## Dockerfile changes

The current `examples/Dockerfile` is the Sphinx-book builder.
Adding the asm matrix needs:

```dockerfile
# After the existing dnf install line, add:
RUN dnf install -y clang

# After the COPY ./src /src/ line, add:
WORKDIR /src
RUN ./build-asm-matrix.sh
RUN mv /asm-out /usr/local/share/examples-asm
```

The resulting `/usr/local/share/examples-asm/` tree is what
students access in the image.

## Test plan

One layer only (no runtime execution since we don't link):

1. **Cross-compile to .s succeeds** — `docker build` succeeds.
   Every demo's `.c` produces a `.s` for every arch.  Failure =
   a portability issue (missing `os.h` branch, broken `_start`
   shim on a target).

2. **Sanity-grep the `.s`** — spot-check that the produced
   assembly actually contains arch-specific instructions:

   ```sh
   grep -l syscall asm-out/x86_64/*.s   # should match x86_64-using ones
   grep -l "int.*0x80" asm-out/i386/*.s
   grep -l "svc" asm-out/arm/*.s asm-out/aarch64/*.s
   grep -l "syscall" asm-out/mips/*.s   # MIPS uses its own `syscall` mnemonic
   ```

   Verifies that each arch's `.s` is actually the right ISA,
   not e.g. all five being x86_64 because the cross flag got
   silently dropped.

(Per the build/runtime split this session has been working
under: this is pure build-time work, so I can drive it
end-to-end locally.)

## Image-size concern

Five `.s` listings per source.  Each demo source produces a
short `.s` (a few hundred lines at most).  Across 24 demos +
io-lib sources × 5 arches that's a few MB of plain text —
negligible.

Toolchain footprint: clang in Fedora is ~250 MB (no need for
lld, since we're not linking).  Could be trimmed post-build:

```dockerfile
RUN dnf remove -y clang
```

**Recommendation:** trim it.  The matrix is the artifact; once
generated, the toolchain doesn't need to ride in the image.

## Order of work

1. Land `PLAN-multiarch-shim.md` first (six demos otherwise
   fail to preprocess on non-x86_64).
2. Write `src/build-asm-matrix.sh`.
3. Smoke-test locally on Fedora 43 — verify all 5 arch
   subdirs populate with `.s` files; spot-check the content
   per the sanity-grep test.
4. Add the `dnf install clang` line + the script invocation +
   the move-to-/usr/local/share/ in `Dockerfile`.
5. Test full docker build end-to-end.
6. Add the trim-toolchain step.

## Open questions

- **MIPS endianness.**  `mipsel` vs `mips`.  Pick one for
  consistency with `/pgu/PLAN-build-matrix.md`.  Likely
  `mipsel` (matches Debian's mainline MIPS port).
- **Include the matrix in the Sphinx book?**  The whole point
  of generating the .s files is for the student to compare —
  rendering a couple of representative diffs into the book
  could be valuable.  Out of scope for the initial plan, but
  worth flagging.
- **Reproducibility.**  Pin the clang version (`dnf install
  clang-<VER>`) so a re-build of the image produces
  byte-identical `.s` listings.  Without pinning, a clang
  update could shuffle register allocation in ways unrelated
  to the source.

## Out of scope

- Linking the `.s` files into binaries.  See the previous
  iteration of this plan — that was rejected as the wrong
  pedagogy.  The `.s` is the artifact.
- Real cross-arch execution (qemu-user, etc).  Same reason.
- Non-Linux targets (FreeBSD, macOS).
- The `.asm` files in `examples/src/*/*.asm` — those are
  hand-written for spim and are not part of this matrix.
- Optimization-level studies (`-O0` vs `-O2`).  Stay at `-O0`
  for readability.  A later `book/` chapter could compare
  `-O0`-vs-`-O2` for one demo if useful.

## Relationship to /pgu

`/pgu/PLAN-build-matrix.md` has the same shape and the same
goal.  The two trees should land their matrices with
consistent:

- Target triples (especially the MIPS endianness choice).
- Educational flag set (the `-fno-*` block).
- Output naming (`asm-out/<arch>/<demo>.s`).
- Toolchain-trim policy.

/pgu can start sooner since its `_start` shim is already
5-arch (the canonical reference); /examples needs its shim
plan to land first.
