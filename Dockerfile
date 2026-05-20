FROM registry.fedoraproject.org/fedora:44

ARG USE_EMACS=0

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




RUN sed -i -e "s@tsflags=nodocs@#tsflags=nodocs@g" /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf upgrade -y && \
    dnf install -y clang \
                   clang-tools-extra \
                   g++ \
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

# Build from source.  --prefix sets the install root explicitly; meson's
# default on Unix is also /usr/local, but spelling it out keeps the
# Dockerfile self-documenting.
RUN cd ${SPIM_SRC_DIR} && \
    CC=clang CXX=clang++ meson setup ${SPIM_BUILD_DIR} \
        --buildtype=debug \
        --prefix=${SPIM_PREFIX} \
        -Dwarning_level=3 \
        -Dline_editing=enabled && \
    meson compile -C ${SPIM_BUILD_DIR} && \
    meson install -C ${SPIM_BUILD_DIR} && \
    ln -s ${SPIM_BUILD_DIR}/compile_commands.json ${SPIM_SRC_DIR}/compile_commands.json

# Execute the regression suite via `meson test`.  Fails the image
# build if any test fails.  The full inventory is enumerated in
# meson.build under `regression_tests`; driven by tests/run-test.sh.
RUN meson test -C ${SPIM_BUILD_DIR} --print-errorlogs


COPY .clang-format ${SPIM_SRC_DIR}/
COPY .clang-tidy   ${SPIM_SRC_DIR}/

RUN echo "exit() {" >> ~/.bashrc && \
    echo "    echo "Formatting on shell exit"" >> ~/.bashrc && \
    echo "    format.sh" >> ~/.bashrc && \
    echo "    lint.sh" >> ~/.bashrc && \
    echo "    builtin exit "$@"" >> ~/.bashrc && \
    echo "}" >> ~/.bashrc && \
    echo "PS1='\[\e[36m\]┌─(\t) \[\e[32m\]\u@\h:\w\n\[\e[36m\]└─λ \[\e[0m\]'" >> ~/.bashrc
