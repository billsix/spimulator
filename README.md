## spimulator

spimulator is a text-based MIPS32 simulator — a fork of SPIM (James Larus) by
William Emerison Six, used as a teaching tool. Works on Windows, Linux, and macOS.

On top of upstream SPIM, this fork adds:

- a **Meson/Ninja** build (replacing the legacy Make/xmkmf/Qt build);
- a **teaching mode** that explains each instruction as it runs (disassembly,
  register before/after, bit-layout diagrams, field decoding);
- an **examples curriculum** of paired C + MIPS-assembly demos (`examples/`);
- a MIPS port of the *Programming from the Ground Up* book (`pgu/`);
- a **tree-sitter** grammar for editor integration (`tree-sitter/`), with
  clang-tidy / clang-format configured.

### Building from source

spimulator is built with [Meson](https://mesonbuild.com/) and
[Ninja](https://ninja-build.org/).

#### Linux

* Install the toolchain.  On Fedora:
    * `dnf install meson ninja-build gcc libedit-devel`
* On Debian-based systems:
    * `apt install meson ninja-build gcc libedit-dev`
* Configure and build:
    * `meson setup builddir`
    * `meson compile -C builddir`
* Optionally install system-wide:
    * `meson install -C builddir`
* Run programs:
    * batch:
        * `./builddir/spimulator -f /path/to/01-helloworld.asm`
    * interactively:
        * `./builddir/spimulator`
            * `load "/path/to/01-helloworld.asm"`
            * `step`
            * `step`
            * `run`

#### macOS

* `brew install meson ninja gcc libedit`
* `meson setup builddir && meson compile -C builddir`

#### Container (any host)

* `podman build -t spimulator .` (or `docker build`).
* The included `Dockerfile` builds spim on Fedora 44, runs the regression and
  examples test suites, and fails the build on any test failure.
* The `Makefile` wraps this: `make shell` for a dev container, and
  `make docs` / `html` / `pdf` / `epub` to build the **pgu** book (needs the image
  built with `BUILD_DOCS=1`).

### Copyright

spimulator is Copyright (c) 2021, by William Emerison Six, starting from git commit e10b97408f6d2c405c36ab05cdffbf40828970fd
All rights reserved.

spimulator is distributed under a BSD license.  See LICENSE


### Original work

SPIM is Copyright (c) 1990-2020, by James R. Larus.
All rights reserved.


This project is derived from spim, https://sourceforge.net/projects/spimsimulator/.
