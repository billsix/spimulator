FROM registry.fedoraproject.org/fedora:44

ARG USE_EMACS=0

# Build the editor-integration tree-sitter grammar?  Adds Node.js +
# tree-sitter-cli (~90 MB) to the image but doesn't change spim itself.
# Off by default since most spim users don't need it.
ARG BUILD_TREE_SITTER=0

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
