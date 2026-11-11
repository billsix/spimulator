## spimulator

spimulator is a text-based MIPS32 simulator — a fork of SPIM (James Larus) by
William Emerison Six, used as a teaching tool. Builds on Linux and macOS.

On top of upstream SPIM, this fork adds:

- a **Meson/Ninja** build (replacing the legacy Make/xmkmf/Qt build);
- a **teaching mode** that explains each instruction as it runs (disassembly,
  register before/after, bit-layout diagrams, field decoding);
- an **examples curriculum** of paired C + MIPS-assembly demos (`examples/`);
- a MIPS port of the *Programming from the Ground Up* book (`pgu/`);
- a **tree-sitter** grammar for editor integration (`tree-sitter/`), with
  clang-tidy / clang-format configured;
- the engine **ported to Python, GHC Haskell and Rust** (`port/`), each tested to
  behave the same as the C engine: `spimulator-python`, `spimulator-haskell` and
  `spimulator-rust` take the same flags (`port/README.md`).

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
        * `./builddir/spimulator -f helloworld.s`
    * interactively:
        * `./builddir/spimulator`
            * `load "helloworld.s"`
            * `disasm` — print a listing of the loaded program
            * `step`
            * `step`
            * `run`

#### macOS

* `brew install meson ninja gcc libedit`
* `meson setup builddir && meson compile -C builddir`

#### Container (any host)

* `podman build -t spimulator .` (or `docker build`).
* The included `Dockerfile` builds spim on Fedora 45, runs the regression,
  examples and port test suites, and fails the build on any test failure.
* The `Makefile` wraps this: `make shell` for a dev container, and
  `make docs` / `html` / `pdf` / `epub` to build the **pgu** book (needs the image
  built with `BUILD_DOCS=1`).

#### Changing the opcode table

The table of every instruction, pseudo-instruction, directive and REPL command
is `include/opcodes.tsv`, and the operand shapes are in
`include/opcode-types.tsv`. `tools/gen_opcodes.py` generates from them every
table built on them: the C engine's, each port's, and the tree-sitter
grammar's keyword lists. The generated files are committed; never edit one by
hand.

* [HOST] Have an image (`make image`) from before the edit: `make generate`
  runs on it.
* [HOST] Edit `include/opcodes.tsv`. Keep the rows in alphabetical order.
* [HOST] `make generate`: regenerate every table, in the existing image.
* [HOST] Commit the data file together with the files `make generate` rewrote.
* [HOST] `make image`: build and test. The build fails, with a diff, if a
  committed generated file is out of date.

> Between the edit and `make generate`, `make image`, `make shell` and
> `make format` fail: the image build checks the tables. Without an image,
> run `python3 tools/gen_opcodes.py` on the host instead; it needs
> `clang-format` (the image's major version, or the C output's layout differs).

> A new instruction also needs its execution in the C engine and in each
> port; the port test suites fail until each port has it.

### Copyright

spimulator is Copyright (c) 2021, by William Emerison Six, starting from git commit e2d4f30
All rights reserved.

spimulator is distributed under a BSD license.  See LICENSE


### Original work

SPIM is Copyright (c) 1990-2020, by James R. Larus.
All rights reserved.


This project is derived from spim, https://sourceforge.net/projects/spimsimulator/.
