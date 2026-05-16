# Plan: multi-arch `_start` shim for argv-using C demos

## Goal

The six argv-using C demos (19-echo, 20-factorial, 21-cat-file,
22-gcd, 23-head-file, 24-tee) each carry an inline `_start` shim
that pulls `argc` / `argv` off the kernel-supplied stack and
calls `my_main(int, char **)`.  Today every one of those is
**x86_64-only** — the `#else` branch is a hard `#error`.

The goal is to extend the shim to support **every Linux arch
that `os.h` already covers**: x86_64, i386, ARM-32, AArch64,
and MIPS-32.  After this, a student on any of those hosts can
`./N-foo-1 args` directly, in addition to running the matching
`.asm` under spimulator.  It also unlocks the Dockerfile cross-
build matrix (see `PLAN-build-matrix.md`) — every demo gets
cross-compiled to all five targets at image build time.

## Why this is worth doing

The point of pairing each demo with a freestanding C version is
that the C side is the "real Linux executable" the student can
run on their machine.  Today that machine has to be x86_64
Linux.  ARM Linux is the second-most-common host in this
audience by a wide margin (every Pi-class board, every Apple
Silicon Mac running an ARM Linux VM/container, every recent
Chromebook in Linux mode).

The work is genuinely small per arch — the shim is 5–6
instructions in each case — and the lesson value of "here's how
process entry differs across CPUs" is itself pedagogically
useful at this point in the curriculum.

## State of the world today

The repeated shim in each affected file looks like:

```c
/* ------- x86_64 Linux crt0 shim (see 19-echo for explanation) -- */
#if defined(__x86_64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movq  (%rsp), %rdi\n"      /* rdi <- argc        (at [rsp+0]) */
    "    leaq  8(%rsp), %rsi\n"     /* rsi <- &argv[0]    (at rsp+8)   */
    "    call  my_main\n"
    "    movl  %eax, %edi\n"        /* status <- return value          */
    "    movl  $60, %eax\n"         /* NR_exit on x86_64               */
    "    syscall\n");
#else
#error "Need an inline _start shim for this arch"
#endif
```

`my_main` is then declared as `int my_main(int argc, char **argv)`
and returns a status code.  The shim treats that return value as
the kernel's exit status.

The kernel's initial-stack contract is the same on all three
target arches: at process entry, `sp` points to a stack frame
containing `argc`, then `argv[0]..argv[argc-1]`, then a `NULL`,
then `envp[]`, then a `NULL`, then auxv.  Only the cell size
and the calling convention differ.

## Per-arch implementation

**Reference implementation:**  the 5-arch shim already exists in
[`/pgu/src/c/toupper-nomm-simplified.c`](../pgu/src/c/toupper-nomm-simplified.c).
The code blocks below match that file's structure; if anything
in this plan disagrees with the /pgu version, the /pgu version
is canonical.

### x86_64 (already present)

- Initial stack: `argc` is at `0(%rsp)`, `&argv[0]` is at `8(%rsp)`
  (8-byte cells).
- Calling convention: args in `%rdi`, `%rsi`, ...; return in
  `%rax`.
- Exit syscall: `%rax = 60` (NR_exit), `%rdi = status`, `syscall`.

### i386 (32-bit x86 Linux)

- Initial stack: 4-byte cells.  `argc` at `(%esp)`, `&argv[0]` at
  `4(%esp)`.
- Calling convention: args passed on the STACK (cdecl).  Return
  in `%eax`.  Caller cleans up.
- Exit syscall: `%eax = 1` (NR_exit), `%ebx = status`, `int $0x80`.

```c
#elif defined(__i386__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movl  (%esp), %eax\n"     /* eax <- argc                       */
    "    leal  4(%esp), %ecx\n"    /* ecx <- &argv[0]                   */
    "    pushl %ecx\n"             /* push argv  (2nd cdecl arg)        */
    "    pushl %eax\n"             /* push argc  (1st cdecl arg)        */
    "    call  my_main\n"
    "    movl  %eax, %ebx\n"       /* status                            */
    "    movl  $1,   %eax\n"       /* NR_exit (i386)                    */
    "    int   $0x80\n");
```

Note: cdecl needs the args pushed in reverse order (right to
left), so argv goes on the stack first, then argc.  We don't
pop the call frame before the exit syscall because we're about
to terminate.

### ARM-32 (EABI)

- Initial stack: 4-byte cells.  `argc` at `[sp]`, `&argv[0]` at
  `[sp, #4]`.
- Calling convention: args in `r0`, `r1`, `r2`, `r3`; return in
  `r0`.
- Exit syscall: `r7 = 1` (NR_exit), `r0 = status`, `svc #0`.

```c
#elif defined(__arm__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr  r0, [sp]\n"           /* r0 <- argc                       */
    "    add  r1, sp, #4\n"         /* r1 <- &argv[0]                   */
    "    bl   my_main\n"
    "    mov  r7, #1\n"             /* NR_exit                          */
    "    svc  #0\n");
```

`my_main`'s return is already in `r0`, which is also where the
exit syscall wants `status` — no extra move needed.

### AArch64 (ARM64)

- Initial stack: 8-byte cells.  `argc` at `[sp]`, `&argv[0]` at
  `[sp, #8]`.
- Calling convention: args in `x0`, `x1`, ...; return in `x0`/`w0`.
- Exit syscall: `x8 = 93` (NR_exit), `x0 = status`, `svc #0`.

```c
#elif defined(__aarch64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr  x0, [sp]\n"           /* x0 <- argc (low 32 bits matter)  */
    "    add  x1, sp, #8\n"         /* x1 <- &argv[0]                   */
    "    bl   my_main\n"
    "    mov  x8, #93\n"            /* NR_exit                          */
    "    svc  #0\n");
```

Same return-value-is-already-in-x0 simplification.

### MIPS-32 (o32 Linux)

- Initial stack: 4-byte cells.  `argc` at `0($sp)`, `&argv[0]`
  at `4($sp)`.
- Calling convention: args in `$a0`..`$a3`; return in `$v0`.
- Exit syscall: `$v0 = 4001` (NR_exit, o32 series 4000+NR), `$a0
  = status`, `syscall`.
- Branch-delay slot: `jal` has one — fill with `nop`.

```c
#elif defined(__mips__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    lw    $a0, 0($sp)\n"      /* $a0 <- argc                       */
    "    addiu $a1, $sp, 4\n"      /* $a1 <- &argv[0]                   */
    "    jal   my_main\n"
    "    nop\n"                    /* branch-delay slot                 */
    "    move  $a0, $v0\n"         /* status                            */
    "    li    $v0, 4001\n"        /* NR_exit (o32 = 4000 + 1)          */
    "    syscall\n");
```

This is the only target where the curriculum already has a
native runtime (spim's `exceptions.s`).  The crt0 above is for
**real MIPS Linux hardware/qemu**, NOT for spim — the asm
demos continue to use spim's own runtime.  That said, the
flow mirrors spim's: argc/argv in `$a0`/`$a1` at the boundary.

### Combined block

```c
#if defined(__x86_64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movq  (%rsp), %rdi\n"
    "    leaq  8(%rsp), %rsi\n"
    "    call  my_main\n"
    "    movl  %eax, %edi\n"
    "    movl  $60, %eax\n"
    "    syscall\n");
#elif defined(__i386__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    movl  (%esp), %eax\n"
    "    leal  4(%esp), %ecx\n"
    "    pushl %ecx\n"
    "    pushl %eax\n"
    "    call  my_main\n"
    "    movl  %eax, %ebx\n"
    "    movl  $1,   %eax\n"
    "    int   $0x80\n");
#elif defined(__aarch64__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr  x0, [sp]\n"
    "    add  x1, sp, #8\n"
    "    bl   my_main\n"
    "    mov  x8, #93\n"
    "    svc  #0\n");
#elif defined(__arm__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr  r0, [sp]\n"
    "    add  r1, sp, #4\n"
    "    bl   my_main\n"
    "    mov  r7, #1\n"
    "    svc  #0\n");
#elif defined(__mips__)
__asm__(
    ".global _start\n"
    "_start:\n"
    "    lw    $a0, 0($sp)\n"
    "    addiu $a1, $sp, 4\n"
    "    jal   my_main\n"
    "    nop\n"
    "    move  $a0, $v0\n"
    "    li    $v0, 4001\n"
    "    syscall\n");
#else
#error "Need an inline _start shim for this arch"
#endif
```

## Layout choice — per-file duplication vs shared header

The shim block is identical in every demo that uses it.  Two
options:

### Option A — per-file (current pattern)

Keep the `__asm__()` block inline in every `.c` file.  Each demo
remains a single self-contained source.  Adding a new arch means
editing 6 files.

Pros: the demo's source explicitly shows the kernel→C boundary;
no implicit `#include` magic; consistent with how the curriculum
has explained the shim so far.

Cons: 6× the maintenance for any arch addition / fix; visual
weight at the top of every argv demo.

### Option B — shared header (`crt0.h`)

Create `src/crt0.h` with the multi-arch shim block above.
Demos do:

```c
#include "io.h"
#include "crt0.h"   /* provides _start that calls my_main(argc, argv) */
```

and lose the inline `__asm__()` block entirely.

Pros: one place to maintain the shim; adding an arch is one
edit; demos look cleaner.

Cons: the demo source no longer literally shows the boundary —
a curious student has to chase down the header.  Slightly
breaks the "each demo is one file" pattern that 19/20/21/22/
23/24 already established.

**Recommendation:** Option B.  The shim is now genuinely
shared infrastructure — six demos already carry an identical
copy and any future argv demo will need it too.  Mitigation
for the "where did `_start` go?" question: leave a one-line
pointer comment in each demo (`/* _start shim provided by
crt0.h; see there for kernel-stack -> my_main conventions */`)
so the boundary is still discoverable.

Bill to confirm before edits begin.

## Affected files

The six demos that today carry the inline x86_64-only shim:

| Demo | C file |
|------|--------|
| 19-echo       | `19-echo/19-echo-1.c` |
| 20-factorial  | `20-factorial/20-factorial-1.c` |
| 21-cat-file   | `21-cat-file/21-cat-file-1.c` |
| 22-gcd        | `22-gcd/22-gcd-1.c` |
| 23-head-file  | `23-head-file/23-head-file-1.c` |
| 24-tee        | `24-tee/24-tee-1.c` |

Plus going forward, every new argv demo (PLAN-cs-demos.md
entries like `fizzbuzz N`, future `head -n N <file>` variants,
etc).

## Test plan

Each demo has to do the right thing on each of {x86_64, arm,
aarch64} Linux.  The actual `_start` boot is the most
arch-specific code in the curriculum — both compile-time AND
run-time validation matters.

### Step 1 — compile

Easy to do without target hardware.  On Fedora 43 x86_64:

```sh
# arm32 (EABI)
sudo dnf install -y gcc-arm-linux-gnu glibc-arm-linux-gnu
arm-linux-gnu-gcc -static -nostdlib -fomit-frame-pointer \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-stack-protector \
    -I . src/19-echo/19-echo-1.c <io-lib sources> \
    -o /tmp/19-echo-arm

# aarch64
sudo dnf install -y gcc-aarch64-linux-gnu glibc-aarch64-linux-gnu
aarch64-linux-gnu-gcc <same flags> -o /tmp/19-echo-aarch64
```

(meson can cross-compile via a cross file; see the meson docs
for `meson setup --cross-file ...`.  Or just `add_languages` and
a `cross-armhf.txt` per arch.)

Compile passing per arch = shim's syntax and constants are
plausible.

### Step 2 — run under user-mode QEMU

```sh
sudo dnf install -y qemu-user-static
qemu-arm-static     /tmp/19-echo-arm     one two three
qemu-aarch64-static /tmp/19-echo-aarch64 one two three
```

Expected output: `one two three\n`.  Compare against the
already-known-good x86_64 native run.

`qemu-arm-static` and `qemu-aarch64-static` translate user-mode
syscalls on the fly — so we can verify both the `_start` shim
AND the os.h syscall wrappers in one shot without needing real
ARM hardware.

This is the load-bearing test.  If the qemu run produces
identical output to the x86_64 run, the multi-arch shim is
working.

### Step 3 — run on real hardware (Bill verifies)

For final confidence, the same binaries should be smoke-tested
on actual hardware Bill has access to (a Pi class board, an
Apple Silicon Linux VM, etc.).  qemu-user is faithful but not
perfect.

Per the build/runtime split this session has been working
under: I can do steps 1–2 locally; Bill verifies step 3 (since
it needs hardware I don't have).

### Step 4 — Dockerfile or meson test

Optional but valuable: add a `meson test` invocation that
cross-compiles each demo for each arch and runs them under
qemu-user.  Means CI catches a shim regression as soon as it
happens.

If we want this in the Dockerfile, the docker image has to
include the cross-compilers and qemu-user-static.  Modest size
increase (~150 MB).

## Order of work

1. Write `src/crt0.h` containing the combined three-arch shim.
2. Smoke-test on x86_64 by including `crt0.h` from 19-echo,
   removing the inline shim, rebuilding, re-running the existing
   "one two three" check.  No behavior change expected.
3. Add the arm32 and aarch64 cross-compilers + qemu-user to the
   build env.
4. Cross-compile 19-echo for arm32 + aarch64; run under qemu-user
   with the same args; verify output.
5. Roll the `#include "crt0.h"` substitution across the remaining
   five demos (20-factorial, 21-cat-file, 22-gcd, 23-head-file,
   24-tee).
6. Smoke-test each on x86_64 + qemu-arm + qemu-aarch64.
7. (Optional) add a meson test target that runs steps 4 and 6 on
   every demo.

## Open questions

- **Static vs dynamic linking under cross-compile.**  The
  freestanding builds we're producing are statically linked with
  `-nostdlib` and don't need glibc at runtime — but the cross
  toolchains often DO pull in glibc headers for `<stddef.h>`.
  Worth verifying that the freestanding-safe headers (`stddef.h`,
  `stdint.h`) cross-compile cleanly without dragging in a libc
  dependency.
- **Should `crt0.h` also expose a CPP macro for the kernel's
  stack-cell size?**  e.g. `#define KERNEL_STACK_CELL 8` on
  x86_64/aarch64, `4` on arm.  Useful for any demo that wanted
  to walk argv pointer arithmetic in C portably.  Probably not
  needed today — every demo accesses argv via the `char **`
  pointer, which is already correctly sized by the C compiler.
- **Other Unixes.**  FreeBSD / macOS use different exit syscall
  numbers and (on macOS) different syscall instructions.
  Currently the whole tree is Linux-only via the matching `#elif
  defined(__linux__)` in `os.h`.  If we ever target FreeBSD,
  `crt0.h` would need a parallel set of `#elif` branches.
  Punt.

## Out of scope

- MIPS native crt0.  `os.h` has a MIPS-Linux syscall branch but
  no demo currently needs to run on real MIPS Linux — the MIPS
  side runs under spimulator, which provides its own start
  code via `exceptions.s`.
- i386 32-bit.  Could add later; not commonly the host arch of
  current students.
- A non-trivial process-init shim (constructing `__environ`,
  parsing auxv, etc.).  Demos don't need any of that.
