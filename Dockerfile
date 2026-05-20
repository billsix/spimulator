FROM registry.fedoraproject.org/fedora:44

ARG USE_EMACS=0


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

COPY helloworld.s meson.build meson_options.txt /spimulator/
COPY src/ /spimulator/src
COPY include/ /spimulator/include
COPY tests/ /spimulator/tests
COPY Documentation/ /spimulator/Documentation

# build from source
RUN cd /spimulator/ && \
    CC=clang CXX=clang++ meson setup builddir --buildtype=debug -Dwarning_level=3 -Dline_editing=enabled && \
    meson configure builddir -Dcpp_args="-Wall" && \
    meson compile -C builddir  && \
    meson install -C builddir && \
    ln -s builddir/compile_commands.json

# Execute the regression suite via `meson test`.  Fails the image
# build if any test fails.  The full inventory is enumerated in
# meson.build under `regression_tests`; driven by tests/run-test.sh.
RUN cd /spimulator && meson test -C builddir --print-errorlogs


COPY .clang-format /spimulator/
COPY .clang-tidy /spimulator/

RUN echo "exit() {" >> ~/.bashrc && \
    echo "    echo "Formatting on shell exit"" >> ~/.bashrc && \
    echo "    format.sh" >> ~/.bashrc && \
    echo "    lint.sh" >> ~/.bashrc && \
    echo "    builtin exit "$@"" >> ~/.bashrc && \
    echo "}" >> ~/.bashrc && \
    echo "PS1='\[\e[36m\]┌─(\t) \[\e[32m\]\u@\h:\w\n\[\e[36m\]└─λ \[\e[0m\]'" >> ~/.bashrc
