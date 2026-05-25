# C ports of the book's assembly examples

This directory holds C translations of the assembly programs in
`../`. Each `.c` file is compiled to a `.s` with educational
flags (no frame pointer, no CFI/unwind tables, no stack canaries)
so the generated assembly stays book-readable, then assembled and
linked into a binary.

The freestanding programs (everything except `helloworld-lib` and
`printf-example`) build with `-nostdlib`, define their own
`_start`, and reach the kernel through the inline-asm syscall
wrappers in `os.h`.

## Supported architectures

| Arch | Target triple | Trap | nr reg | arg regs |
|---|---|---|---|---|
| i386 | `i686-linux-gnu` | `int $0x80` | `eax` | `ebx, ecx, edx` |
| x86_64 | `x86_64-linux-gnu` | `syscall` | `rax` | `rdi, rsi, rdx` |
| ARM 32 | `arm-linux-gnueabi(hf)` | `svc 0` | `r7` | `r0..r3` |
| AArch64 | `aarch64-linux-gnu` | `svc #0` | `x8` | `x0..x5` |
| MIPS 32 | `mipsel-linux-gnu` | `syscall` | `$v0` | `$a0..$a3` |

`os.h` selects the right inline assembly based on `__i386__`,
`__x86_64__`, `__arm__`, `__aarch64__`, `__mips__` — no special
flag needed beyond what `clang -m32` / `--target=` already sets.

The wrappers are tagged `__attribute__((always_inline))` so the
syscall instruction is emitted at every call site even at `-O0`,
not behind a `call os_write` boundary.

A few gotchas baked into `os.h`:

* AArch64 has no plain `open` syscall; `os_open` synthesizes it
  via `openat(AT_FDCWD, ...)`.
* MIPS signals errors in `$a3` rather than as a negative return;
  the wrapper folds that back into the standard negative-return
  convention.
* `OS_O_CREAT` / `OS_O_TRUNC` differ on MIPS; `os.h` ifdefs them.

## Build — native arch

```sh
make
```

That uses whichever clang/gcc is on `$PATH` and builds for the
host arch. Run the binaries:

```sh
./exit; echo $?              # -> 0
./maximum; echo $?           # -> 222
./factorial; echo $?         # -> 24
./power; echo $?             # -> 33   (2^3 + 5^2)
./helloworld-nolib           # -> hello world
./conversion-program         # -> 824
./write-records              # creates test.dat with 3 records
./read-records               # -> Fredrick / Marilyn / Derrick
./add-year                   # creates testout.dat, ages +1
echo input | ./toupper-nomm-simplified /dev/stdin out.txt && cat out.txt
./helloworld-lib             # libc-linked
./printf-example             # libc-linked
```

## Build — i386 from an x86_64 host (the book's original target)

```sh
make EXTRA_CFLAGS=-m32
```

Requires the 32-bit toolchain and runtime:

* Fedora: `dnf install glibc-devel.i686 glibc.i686 libgcc.i686`
* Debian/Ubuntu: `apt install gcc-multilib`

This is what the project's `Dockerfile` already installs, so
`make shell` from the repo root drops you into a container where
`make EXTRA_CFLAGS=-m32` Just Works.

## Build — cross compile

The `EXTRA_CFLAGS=--target=...` form drives clang's built-in
cross-compiler. You still need a sysroot with libc + crt files
for the target if you want to *link* (compiling `.c` to `.s` is
toolchain-free).

### x86_64 explicitly

```sh
make EXTRA_CFLAGS=--target=x86_64-linux-gnu
```

### ARM 32-bit (EABI)

```sh
make CC="clang --sysroot=/usr/arm-linux-gnueabihf" \
     EXTRA_CFLAGS="--target=arm-linux-gnueabihf"
```

Install:

* Fedora: `dnf install gcc-arm-linux-gnu glibc-arm-linux-gnu`
* Debian/Ubuntu: `apt install gcc-arm-linux-gnueabihf`

Run with `qemu-arm-static` if you're on an x86_64 host.

### AArch64

```sh
make CC="clang --sysroot=/usr/aarch64-linux-gnu" \
     EXTRA_CFLAGS="--target=aarch64-linux-gnu"
```

Install:

* Fedora: `dnf install gcc-aarch64-linux-gnu glibc-aarch64-linux-gnu`
* Debian/Ubuntu: `apt install gcc-aarch64-linux-gnu`

Run with `qemu-aarch64-static`.

### MIPS 32-bit

```sh
make CC="clang --sysroot=/usr/mipsel-linux-gnu" \
     EXTRA_CFLAGS="--target=mipsel-linux-gnu"
```

Install:

* Debian/Ubuntu: `apt install gcc-mipsel-linux-gnu`
* Fedora: build a cross toolchain via `crosstool-ng` or use a
  prebuilt one — Fedora doesn't ship a MIPS cross.

Run with `qemu-mipsel-static`.

## What's where

| File | Purpose |
|---|---|
| `os.h` | per-arch syscall wrappers (`os_write`, `os_read`, `os_open`, `os_close`, `os_exit`, `os_brk`) — `always_inline`'d |
| `record-def.h` | `struct record` shared by the records-chapter programs |
| `count-chars.c`, `integer-to-string.c`, `write-newline.c` | Library helpers (no `_start`) |
| `read-record.c`, `write-record.c` | One-syscall record helpers |
| `error-exit.c` | `error_exit(code, msg)` helper |
| `alloc.c` | `brk`-based allocator (`allocate_init` / `allocate` / `deallocate`) |
| `helloworld-nolib.c`, `power.c`, `factorial.c` | Single-file freestanding programs |
| `conversion-program.c`, `read-records.c`, `write-records.c`, `add-year.c` | Multi-file freestanding programs |
| `toupper-nomm-simplified.c` | Has its own per-arch `_start` shim — the kernel hands argv on the raw stack with no return address, which the C calling convention can't express |
| `helloworld-lib.c`, `printf-example.c` | libc-linked programs (use `<stdio.h>`); not freestanding |
| `exit.c`, `maximum.c` | The book's first two examples |

## Reading the generated `.s`

After `make`, every `.s` file sits next to its `.c`. Open them
together — the C is on the left, the asm on the right, and with
the educational flags above it stays close to what the book's
hand-written assembly looks like. The `os.h` syscall site shows
up as a single inline `int $0x80` / `syscall` / `svc 0` block,
which is the part you'd hand-write in pure assembly.
