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
    dnf install -y bison \
                   clang \
                   clang-tools-extra \
                   flex \
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

# execute tests, fail building the image if any tests fail
# (spimulator has been installed via `meson install` above, so the
# default exception-handler path resolves and we don't need `-ef ...`
# on every line — tt.bare.s is the one exception since it runs in
# bare mode with `-noexception`.)
RUN  cd /spimulator/tests ; \
     spimulator -delayed_branches -delayed_loads -noexception -f tt.bare.s >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -f tt.core.s < tt.in >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -f tt.le.s >& test.out ; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -f tt.argv.s alpha beta gamma >& test.out ; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     printf 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber\nrun\nquit\n' | spimulator >& test.out ; grep -q "argv1=z" test.out && grep -q "argv1=a" test.out || exit 1;


COPY .clang-format /spimulator/
COPY .clang-tidy /spimulator/

RUN echo "exit() {" >> ~/.bashrc && \
    echo "    echo "Formatting on shell exit"" >> ~/.bashrc && \
    echo "    format.sh" >> ~/.bashrc && \
    echo "    lint.sh" >> ~/.bashrc && \
    echo "    builtin exit "$@"" >> ~/.bashrc && \
    echo "}" >> ~/.bashrc && \
    echo "PS1='\[\e[36m\]┌─(\t) \[\e[32m\]\u@\h:\w\n\[\e[36m\]└─λ \[\e[0m\]'" >> ~/.bashrc
