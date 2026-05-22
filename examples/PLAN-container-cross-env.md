# Plan: cross-compilers + qemu-user in the container

## Goal

Bake into the `/examples` container the tooling needed to
build and run any C demo for any of the five target Linux
architectures (`x86_64`, `i386`, `arm`, `aarch64`, `mips`).
That makes the multiarch `_start` shim (`crt0.h`) verifiable
inside the dev/CI environment, not just on Bill's hardware.

Concretely:

```sh
# inside the container
$ clang --target=aarch64-linux-gnu -static -nostdlib \
        -I src -ffreestanding \
        src/arguments/echo/echo.c <io-lib sources> \
        -fuse-ld=lld -o /tmp/echo-aarch64
$ qemu-aarch64-static /tmp/echo-aarch64 one two three
one two three
```

…all using nothing but what `dnf install` puts in `/usr/bin`.

## Why this is worth doing

Three reasons:

1. **Multiarch shim verification.**  `crt0.h` has been rolled
   out across ~30 demos but only the x86_64 branch is actually
   exercised by the existing builds.  Without an in-container
   way to cross-compile + run, an arm/aarch64/mips/i386
   regression goes undetected until Bill notices it on real
   hardware — slow loop.
2. **The container is the dev environment.**  Bill's reaction
   when I said "this needs cross-binutils installed" was
   essentially "that's what the container is for."  Right.
   Anything a future session needs to do its job should be
   installed by the Dockerfile, not assumed to be present on
   the host.
3. **Unlocks two adjacent plans.**
   - `PLAN-build-matrix.md` (cross-compile every demo to .s on
     all 5 ISAs at image build) — needs the cross toolchains.
   - Future "CI runs every demo on every arch via qemu-user"
     work — needs qemu-user-static.

## State of the world today

`/examples/Dockerfile` installs only: aspell, git, make, sphinx,
texlive.  No compilers, no qemu.  The actual build environment
this session has been using (Fedora 43 with meson, clang,
flex/bison, qemu-user-static) is a richer container that
isn't checked in here — it lives in some upstream parent image
or was manually provisioned.

The build container Bill develops in has:

- `clang 22.x` (multi-target capable; lacks per-target sysroots)
- `qemu-{arm,aarch64,i386,mips}-static` (the user-mode emulators
  are already there)
- `ld.bfd` (host-arch only — no cross-emulation modes)
- No cross-gcc / cross-binutils for any non-x86 target

So one thing is missing: a *cross-capable linker* (lld or
cross-binutils) and per-target tooling to drive it.

## Two paths

### Option A — clang + lld + qemu-user-static (lightest)

Add to the Dockerfile:

```dockerfile
dnf install -y \
    clang \
    lld \
    qemu-user-static
```

Total disk: ~150 MB above the base.

**How it works:** clang already supports all our targets
out-of-the-box.  `lld` is LLVM's linker, also multi-target.
For our **freestanding** builds (`-nostdlib -ffreestanding`)
we don't need per-arch sysroots or per-arch libc — the demos
write their own `_start`, call kernel syscalls directly, and
don't link against libc.  So clang + lld is *sufficient*:

```sh
clang --target=aarch64-linux-gnu -static -nostdlib \
      -ffreestanding -fuse-ld=lld \
      -I src src/arguments/echo/echo.c <...> -o /tmp/out
```

Then `qemu-aarch64-static /tmp/out one two three` runs it.

**Pros:**

- Smallest footprint by far.
- One toolchain (clang) targets all five arches; no per-arch
  package proliferation.
- Matches how PGU's `/pgu/src/c/*.c` are already being built
  (clang-multi-target is a familiar idiom in Bill's other
  repos).

**Cons:**

- Students who want to use `gcc` instead of `clang` for
  comparison (a real lesson: "look how the two compilers
  translate this") would need the gcc cross toolchains in
  addition.
- No sysroot means anything that includes a libc header (any
  non-freestanding code Bill might add later) breaks.  Today
  none does.

### Option B — clang + lld + GNU cross toolchains + qemu-user-static (full)

Same as A, plus per-target GNU toolchains:

```dockerfile
dnf install -y \
    clang \
    lld \
    gcc-arm-linux-gnu \
    gcc-aarch64-linux-gnu \
    gcc-mips-linux-gnu \
    gcc-i686-linux-gnu \
    glibc-arm-linux-gnu \
    glibc-aarch64-linux-gnu \
    glibc-mips-linux-gnu \
    glibc-i686-linux-gnu \
    qemu-user-static
```

(Exact Fedora package names need checking — `dnf search
gcc-arm-linux` to confirm.)

Total disk: ~500 MB – 1 GB above the base.

**Pros:**

- Two compilers per target = the "compare GCC vs clang
  codegen" lesson becomes possible.
- Per-arch sysroots come along; non-freestanding code paths
  (if/when Bill adds them) Just Work.
- More "standard" — matches the toolchain a real distribution
  would ship.

**Cons:**

- Big.  Each cross-gcc is 100-200 MB.  Pulls in glibc devel
  for each target.
- Slower to build the container image.
- Most demos don't need it — they're freestanding.

## Recommendation

**Option A**, with a notation that Option B is a "future
extension if dual-compiler comparison becomes a curriculum
goal."

Reasoning:

- The freestanding-only setup matches the curriculum's stated
  philosophy ("one program, three vocabularies: C, asm
  by hand, native asm by compiler" — nothing in there requires
  two different C compilers).
- 150 MB is a reasonable cost; 1 GB is not.
- Easy to upgrade A → B later when there's a concrete reason.

## Test plan

Once the Dockerfile gains clang + lld + qemu-user-static:

### Step 1 — does freestanding cross-compile work at all?

Smallest possible smoke test (no demo dependencies):

```sh
cat > /tmp/hello-aarch64.c <<'EOF'
__attribute__((noreturn)) void _start(void) {
  asm volatile(
    "mov x0, #42\n"
    "mov x8, #93\n"   // NR_exit
    "svc #0\n");
  __builtin_unreachable();
}
EOF
clang --target=aarch64-linux-gnu -static -nostdlib \
      -fuse-ld=lld /tmp/hello-aarch64.c -o /tmp/hello-aarch64
qemu-aarch64-static /tmp/hello-aarch64; echo $?
# Expected: 42
```

Repeat for arm, mips, i386.  If all four exit 42, the build
env is good.

### Step 2 — cross-compile a representative argv demo

```sh
cd /examples/src
clang --target=aarch64-linux-gnu -static -nostdlib \
      -ffreestanding -fuse-ld=lld -I. \
      echo/echo.c read-int.c <other io-lib sources as needed> \
      -o /tmp/echo-aarch64
qemu-aarch64-static /tmp/echo-aarch64 one two three
# Expected: one two three
```

If this works on all four non-host arches, `crt0.h` is verified
multi-arch.

### Step 3 — wrap as a meson target or CI script

After steps 1+2 confirm the cross-compile works at all, add
something callable from CI:

```sh
meson setup builddir-aarch64 --cross-file cross/aarch64.txt
meson compile -C builddir-aarch64
# Then optionally: meson test -C builddir-aarch64 (using qemu-user as exe_wrapper)
```

The meson cross-file can name qemu-aarch64-static as the
`exe_wrapper`, which makes `meson test` Just Run binaries that
target a different arch — quite elegant.

This step ties back to PLAN-multiarch-shim.md (verify) and
PLAN-build-matrix.md (cross-compile-to-asm).  Both become much
easier once the in-container env is provisioned.

## Order of work

1. Add `dnf install -y clang lld qemu-user-static` to
   `/examples/Dockerfile`.  Single-line change.
2. (Optional but recommended) Pin the package versions so the
   container build is reproducible:
   `dnf install -y clang-22.x.y lld-22.x.y qemu-user-static-X.Y.Z`.
   Or trust Fedora's rolling.
3. Rebuild the container; verify `which clang lld qemu-aarch64-static`
   all resolve.
4. Run Step 1 smoke test above.  If it passes, the env is good.
5. Run Step 2 (the echo cross test) for each non-host arch.
   If it passes, `crt0.h` is verified multi-arch.
6. (Optional) Add a meson cross-file under `src/cross/` per
   arch.  Add a `meson test` invocation in the Dockerfile that
   exercises each.

## Open questions

- **lld vs ld.gold vs ld.bfd.**  Going with lld for multi-target.
  Cross-binutils' `aarch64-linux-gnu-ld` would also work but
  requires the GNU cross packages (Option B territory).
- **Static linking.**  `-static -nostdlib` is the right
  combination for these freestanding demos.  Mixing in a
  dynamic linker would add a per-arch dependency we don't
  need.
- **mips vs mipsel vs n32 vs o32.**  spim is o32.  Real MIPS
  Linux is mostly mipsel-o32 in user-mode QEMU's defaults.
  The `crt0.h` mips block uses syscall number 4001 which is
  o32; should be portable.  Verify in step 1.
- **Should `qemu-user-static` be registered with binfmt_misc
  so `./aarch64-bin` Just Works without explicit
  `qemu-aarch64-static` prefix?**  Adds complexity (need
  `/proc/sys/fs/binfmt_misc` mounted in the container) and
  isn't needed for our usage — we always invoke qemu
  explicitly in scripts.  Skip.

## Relation to other plans

This plan is **infrastructure**, supporting the work of:

- [`PLAN-multiarch-shim.md`](PLAN-multiarch-shim.md) —
  "verify the shim works on every arch" was previously
  blocked on having a way to do that in-container.  This
  unblocks step 2 of that plan.
- [`PLAN-build-matrix.md`](PLAN-build-matrix.md) — "produce
  cross-arch .s listings at image-build time" likewise.
  After this plan lands, the build-matrix plan has a working
  toolchain to drive.
- [`PLAN-asm-listings.md`](PLAN-asm-listings.md) — native-arch
  .s listings.  Independent of this plan; doesn't need cross
  tools.

The natural rollout order is:

1. This plan (env)
2. PLAN-asm-listings (native .s) — quickest pedagogical win.
3. PLAN-multiarch-shim verification (use env to verify).
4. PLAN-build-matrix (use env to produce cross .s artifacts).

## Out of scope

- Real-hardware verification on Bill's actual ARM/aarch64
  devices.  qemu-user is close but not perfect; Bill's
  hardware is the final arbiter.  This plan only addresses
  "in-container CI-able verification."
- Other Unixes (FreeBSD, macOS).  No cross-compile for
  non-Linux targets; the curriculum is Linux-syscalls-only.
- Native-arch toolchain bake-in (gcc, clang, meson, flex,
  bison).  Those should already be in the container the
  Dockerfile inherits from / installs.  This plan adds
  *cross* tools on top.
