FROM registry.fedoraproject.org/fedora:43

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
                   lldb \
                   meson \
                   ninja \
                   nano \
                   rlwrap \
                   tmux \
                   which && \
    echo 'set debuginfod enabled off' > /root/.gdbinit

COPY helloworld.s meson.build /spimulator/
COPY src/ /spimulator/src
COPY include/ /spimulator/include
COPY tests/ /spimulator/tests

# build from source
RUN cd /spimulator/ && \
    meson setup builddir && \
    meson compile -C builddir && \
    meson install -C builddir

# execute tests, fail building the image if any tests fail
RUN  cd /spimulator/tests ; \
     spimulator -delayed_branches -delayed_loads -noexception -file tt.bare.s >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -ef ../src/exceptions.s -file tt.core.s < tt.in >& test.out; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1; \
     spimulator -ef ../src/exceptions.s -file tt.le.s  >& test.out ; tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1;


COPY .clang-format /pgu/
