FROM registry.fedoraproject.org/fedora:43

COPY helloworld.s meson.build /spimulator/
COPY src/ /spimulator/src
COPY tests/ /spimulator/tests

RUN sed -i -e "s@tsflags=nodocs@#tsflags=nodocs@g" /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf upgrade -y && \
    dnf install -y clang \
                   clang-tools-extra \
                   emacs \
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
    cd /spimulator/ && \
    meson setup builddir && \
    meson compile -C builddir && \
    meson install -C builddir && \
    echo 'set debuginfod enabled off' > /root/.gdbinit

COPY .clang-format /pgu/
