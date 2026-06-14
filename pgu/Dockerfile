FROM registry.fedoraproject.org/fedora:44

ARG BUILD_DOCS=1
ARG USE_GRAPHICS=1

RUN --mount=type=cache,target=/var/cache/libdnf5 \
    --mount=type=cache,target=/var/lib/dnf \
    sed -i -e "s@tsflags=nodocs@#tsflags=nodocs@g" /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf upgrade -y && \
    dnf install -y --skip-unavailable \
                glibc-devel.i686 \
                glibc.i686 \
                libgcc.i686 \
                libatomic.i686 && \
    dnf install -y --skip-unavailable \
                   clang \
                   clang-tools-extra \
                   emacs \
                   g++ \
                   gcc \
                   gdb \
                   gtk4 \
                   gtk4-demo \
                   lldb \
                   man \
                   man-db \
                   man-pages \
                   nano \
                   python3 \
                   strace \
                   tmux ; \
    if [ "$BUILD_DOCS" = "1" ]; then \
       dnf install -y \
                      aspell \
                      aspell-en \
                      latexmk \
                      libreoffice \
                      inkscape \
                      pandoc \
                      python3-furo \
                      python3-pip \
                      python3-sphinx-latex \
                      python3-sphinx_rtd_theme \
                      texlive \
                      texlive-anyfontsize \
                      texlive-dvipng \
                      texlive-dvisvgm \
                      texlive-standalone ; \
         fi ; \
    if [ "$USE_GRAPHICS" = "1" ]; then \
       dnf install -y \
                      mesa-dri-drivers  \
                      libXScrnSaver \
                      libXtst \
                      libXcomposite \
                      libXcursor \
                      libXdamage \
                      libXfixes \
                      libXft \
                      libXi \
                      libXinerama \
                      libXmu \
                      libXrandr \
                      libXrender \
                      libXres \
                      libXv \
                      libXxf86vm \
                      libglvnd-gles \
                      mesa-demos \
                      vulkan-tools  ; \
         fi ; \
    echo 'set debuginfod enabled off' > /root/.gdbinit

COPY .clang-format /pgu/

RUN echo "source ~/.extrabashrc" >> ~/.bashrc
RUN echo "settings set target.disable-aslr false" >> ~/.lldbinit

ENTRYPOINT ["/entrypoint.sh"]
