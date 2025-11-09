cd /spimulator/ && \
    CC=clang CXX=clang++ meson setup builddir --buildtype=debug -Dwarning_level=3 && \
    meson configure builddir -Dcpp_args="-Wall" && \
    meson compile -C builddir  && \
    meson install -C builddir && \
    ln -s builddir/compile_commands.json
