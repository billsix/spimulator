#!/usr/bin/env bash
#
# 01-install-base.sh -- the always-needed Fedora packages: the C toolchain spim builds
# with.
#
# One package group per script, no options: WHICH optional groups also get installed is
# decided by the Dockerfile's ARG `if` blocks (or by a human choosing which scripts to
# run). Same packages during `podman build`, on a bare Fedora host, or in a guest with
# no container runtime. Run base first, then any 0N-install-*.sh group you want.
#
# Two dnf calls (upgrade + install), so accumulate a non-zero exit if either fails.
set -uo pipefail

if ! command -v dnf >/dev/null 2>&1; then
    echo "01-install-base.sh: needs 'dnf' (this installs Fedora packages), not found." >&2
    echo "Run on a Fedora host/guest, or inside the project's Fedora-based image." >&2
    exit 1
fi

status=0

dnf upgrade -y || status=1

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
              which || status=1

exit $status
