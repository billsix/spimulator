FROM registry.fedoraproject.org/fedora:43


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
                   emacs \
                   flex \
                   g++ \
                   gcc \
                   gdb \
                   git \
                   lldb \
                   meson \
                   ninja \
                   nano \
                   pkgconfig \
                   rlwrap \
                   tmux \
                   valgrind \
                   which && \
    echo 'set debuginfod enabled off' > /root/.gdbinit && \
    emacs --batch --load /root/.emacs.d/install-melpa-packages.el


COPY helloworld.s meson.build /spimulator/
COPY src/ /spimulator/src
COPY include/ /spimulator/include
COPY tests/ /spimulator/tests

# build from source
RUN cd /spimulator/ && \
    CC=clang CXX=clang++ meson setup builddir --buildtype=debug -Dwarning_level=3 && \
    meson configure builddir -Dcpp_args="-Wall" && \
    meson compile -C builddir  && \
    meson install -C builddir && \
    ln -s builddir/compile_commands.json

# execute tests, fail building the image if any tests fail
RUN  cd /spimulator/tests ; \
     spimulator -delayed_branches -delayed_loads -noexception -file tt.bare.s >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -ef ../src/exceptions.s -file tt.core.s < tt.in >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -ef ../src/exceptions.s -file tt.le.s  >& test.out ; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1;


COPY .clang-format /spimulator/
COPY .clang-tidy /spimulator/

RUN echo "exit() {" >> ~/.bashrc && \
    echo "    echo "Formatting on shell exit"" >> ~/.bashrc && \
    echo "    format.sh" >> ~/.bashrc && \
    echo "    lint.sh" >> ~/.bashrc && \
    echo "    builtin exit "$@"" >> ~/.bashrc && \
    echo "}" >> ~/.bashrc && \
    echo "PS1='\[\e[36m\]┌─(\t) \[\e[32m\]\u@\h:\w\n\[\e[36m\]└─λ \[\e[0m\]'" >> ~/.bashrc
