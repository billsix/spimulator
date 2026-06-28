FROM registry.fedoraproject.org/fedora:44

ARG USE_EMACS=0

# Build the editor-integration tree-sitter grammar?  Adds Node.js +
# tree-sitter-cli (~90 MB) to the image but doesn't change spim itself.
# Off by default since most spim users don't need it.
ARG BUILD_TREE_SITTER=0

# Install the toolchain to build the PGU book (the MIPS-on-spim port
# under pgu/) into HTML/PDF/EPUB?  Adds Sphinx + furo + a full TeX
# Live (~hundreds of MB), so it is a build-time opt-out.  The book
# itself is built at runtime by the `docs`/`html`/`pdf`/`epub`
# Makefile targets, which mount the repo and run sphinx-build inside
# this image — nothing is built during `docker build`.
ARG BUILD_DOCS=1

# Run the sanitizer gate at image-build time?  Builds spim a second/third
# time under UndefinedBehaviorSanitizer (trap mode) and AddressSanitizer and
# runs the regression suite under each, FAILING the image if any UB or memory
# error fires (the same way the plain `meson test` step gates the build).
# Catches integer-UB / memory-safety regressions before they ship.  Off by
# default for a bare `podman build` (a second/third instrumented compile of
# src/ costs build time); `make image` turns it on.  See
# tasks/archive/2026/06/16/ubsan-sweep.md for the primer + rationale.
ARG RUN_SANITIZERS=0

# Build + install locations.  Override at build time with
# `--build-arg SPIM_BUILD_DIR=...` or `--build-arg SPIM_PREFIX=...`.
# `ENV` (not `ARG`) so subsequent RUN layers see them via $VAR
# expansion without re-declaring.
ENV SPIM_SRC_DIR=/spimulator
ENV SPIM_BUILD_DIR=/spimulator/builddir
ENV SPIM_PREFIX=/usr/local


COPY entrypoint/dotfiles/ /root/
COPY entrypoint/buildDebug.sh /usr/local/bin
COPY entrypoint/format.sh /usr/local/bin
COPY entrypoint/lint.sh /usr/local/bin
COPY entrypoint/shell.sh /usr/local/bin




RUN --mount=type=cache,target=/var/cache/libdnf5 \
    --mount=type=cache,target=/var/lib/dnf \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    sed -i -e "s@tsflags=nodocs@#tsflags=nodocs@g" /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf upgrade -y && \
    dnf install -y clang \
                   clang-tools-extra \
                   diffutils \
                   gcc \
                   gdb \
                   git \
                   libedit-devel \
                   lldb \
                   make \
                   meson \
                   ninja \
                   nano \
                   pkgconfig \
                   tmux \
                   valgrind \
                   which && \
    echo 'set debuginfod enabled off' > /root/.gdbinit ; \
    if [ "$USE_EMACS" = "1" ]; then \
      dnf install -y \
                  emacs \
                  emacs-gtk+x11 \
                  emacs-pgtk \
                  python3-lsp-server && \
      emacs --batch --load /root/.emacs.d/install-melpa-packages.el; \
    fi ;

# PGU book toolchain (gated by BUILD_DOCS).  Mirrors the deps in
# pgu/Dockerfile: Sphinx + the furo theme for HTML/EPUB, latexmk +
# TeX Live for PDF, inkscape to rasterize the book's SVG figures,
# aspell for the docs spellcheck target, pandoc as a doc converter.
RUN --mount=type=cache,target=/var/cache/libdnf5 \
    --mount=type=cache,target=/var/lib/dnf \
    if [ "$BUILD_DOCS" = "1" ]; then \
      dnf install -y \
                  aspell \
                  aspell-en \
                  inkscape \
                  latexmk \
                  pandoc \
                  python3-furo \
                  python3-pip \
                  python3-sphinx \
                  python3-sphinx-latex \
                  python3-sphinx_rtd_theme \
                  texlive \
                  texlive-anyfontsize \
                  texlive-dvipng \
                  texlive-dvisvgm \
                  texlive-standalone ; \
    fi ;

COPY helloworld.s meson.build meson_options.txt ${SPIM_SRC_DIR}/
COPY src/             ${SPIM_SRC_DIR}/src
COPY include/         ${SPIM_SRC_DIR}/include
COPY tests/           ${SPIM_SRC_DIR}/tests
COPY Documentation/   ${SPIM_SRC_DIR}/Documentation
# Examples curriculum — paired C + MIPS-asm teaching demos.
# Built and tested as part of the unified meson setup below
# (the `meson test` step runs the 'examples' suite alongside
# spim's 'regression' suite).
COPY examples/        ${SPIM_SRC_DIR}/examples

# Build from source.  --prefix sets the install root explicitly; meson's
# default on Unix is also /usr/local, but spelling it out keeps the
# Dockerfile self-documenting.
RUN cd ${SPIM_SRC_DIR} && \
    CC=clang meson setup ${SPIM_BUILD_DIR} \
        --buildtype=debug \
        --prefix=${SPIM_PREFIX} \
        -Dwarning_level=3 \
        -Dline_editing=enabled && \
    meson compile -C ${SPIM_BUILD_DIR} && \
    meson install -C ${SPIM_BUILD_DIR} && \
    ln -s ${SPIM_BUILD_DIR}/compile_commands.json ${SPIM_SRC_DIR}/compile_commands.json

# Execute the full test suite via `meson test`.  Fails the image
# build if any test fails.  Two suites run here:
#   - 'regression' (spim itself): inventory in meson.build under
#     `regression_tests`; driven by tests/run-test.sh.
#   - 'examples' (curriculum demos): inventory in
#     examples/src/meson.build under `lib_demo_tests`; driven by
#     examples/tests/run-demo.sh.  Each demo runs BOTH the C
#     binary AND the spim-asm version, diffs both stdouts against
#     the pinned `<demo>.expected` golden, and (for exit-demo and
#     atexit-demo) verifies the host shell exit status matches
#     `<demo>.expected-status`.  Any drift on either side fails
#     the build.
RUN meson test -C ${SPIM_BUILD_DIR} --print-errorlogs

# Sanitizer gate (opt-in via RUN_SANITIZERS=1; `make image` sets it).  Build
# spim — and ONLY spim (the `spimulator` target; the -nostdlib example demos
# must not be sanitized) — under UBSan-trap and ASan, and run the regression
# suite under each with --no-rebuild.  A surviving UB traps (SIGILL); a memory
# error aborts; either fails the image.  Scoped to src/; see the UBSan-sweep
# task doc for why diagnostic UBSan under-reports and trap mode is the gate.
# ASan leak detection is defaulted off in spim.c (__asan_default_options) — the
# gate is for corruption, not spim's intentional exit-time leaks.
RUN if [ "$RUN_SANITIZERS" = "1" ]; then \
      set -e; \
      echo '== UBSan (trap) gate =='; \
      CC=clang meson setup /tmp/san-ubsan ${SPIM_SRC_DIR} --buildtype=debug \
          -Dwarning_level=3 \
          -Dc_args='-fsanitize=undefined -fsanitize-trap=undefined' \
          -Dc_link_args='-fsanitize=undefined -fsanitize-trap=undefined'; \
      meson compile -C /tmp/san-ubsan spimulator; \
      meson test -C /tmp/san-ubsan --no-rebuild --suite regression --print-errorlogs; \
      echo '== ASan gate =='; \
      CC=clang meson setup /tmp/san-asan ${SPIM_SRC_DIR} --buildtype=debug \
          -Dwarning_level=3 -Db_sanitize=address; \
      meson compile -C /tmp/san-asan spimulator; \
      meson test -C /tmp/san-asan --no-rebuild --suite regression --print-errorlogs; \
      rm -rf /tmp/san-ubsan /tmp/san-asan; \
    fi

# Materialize the native teaching artifacts beside each C demo:
# <demo>.s (the compiler's translation of the C to this host's
# native arch, next to the hand-written MIPS <demo>.asm) and a
# linked, runnable native executable in examples/src/bin/.  Lets a
# student read all three vocabularies and run the result, the same
# /pgu-style flow.  This is the *native* path; the MIPS binaries
# that run under spim + their golden tests are owned by the meson
# build above.  CC=clang to match the rest of the image.
# See examples/tasks/PLAN-asm-listings-makefile.md.
RUN make -C ${SPIM_SRC_DIR}/examples/src CC=clang all

# Optional: build and test the editor-integration tree-sitter grammar.
# Gated by --build-arg BUILD_TREE_SITTER=1 because Node + tree-sitter-cli
# add ~90 MB and aren't needed to build spim itself.
#
# When BUILD_TREE_SITTER=1 AND USE_EMACS=1, the grammar is additionally
# compiled to a shared library and dropped into Emacs's tree-sitter
# load path, so M-x mips-spim-ts-mode (auto-bound to .s/.asm/.mips)
# just works inside the container.
COPY tree-sitter/ ${SPIM_SRC_DIR}/tree-sitter
RUN --mount=type=cache,target=/var/cache/libdnf5 \
    --mount=type=cache,target=/var/lib/dnf \
    if [ "$BUILD_TREE_SITTER" = "1" ]; then \
      dnf install -y nodejs npm && \
      cd ${SPIM_SRC_DIR}/tree-sitter && \
      npm install && \
      make && \
      if [ "$USE_EMACS" = "1" ]; then \
        mkdir -p /root/.emacs.d/tree-sitter && \
        cc -O2 -fPIC -shared -I src src/parser.c \
           -o /root/.emacs.d/tree-sitter/libtree-sitter-mips_spim.so ; \
      fi ; \
    fi


COPY .clang-format ${SPIM_SRC_DIR}/
COPY .clang-tidy   ${SPIM_SRC_DIR}/

RUN echo "exit() {" >> ~/.bashrc && \
    echo "    echo "Formatting on shell exit"" >> ~/.bashrc && \
    echo "    format.sh" >> ~/.bashrc && \
    echo "    lint.sh" >> ~/.bashrc && \
    echo "    builtin exit "$@"" >> ~/.bashrc && \
    echo "}" >> ~/.bashrc && \
    echo "PS1='\[\e[36m\]┌─(\t) \[\e[32m\]\u@\h:\w\n\[\e[36m\]└─λ \[\e[0m\]'" >> ~/.bashrc
