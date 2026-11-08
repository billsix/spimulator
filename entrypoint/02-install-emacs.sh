#!/usr/bin/env bash
#
# 02-install-emacs.sh -- Emacs + the Python LSP server. Corresponds to the Dockerfile's
# USE_EMACS flag; the Dockerfile runs this only when USE_EMACS=1. Emacs's packages are
# NOT installed here: they are vendored in entrypoint/dotfiles/.emacs.d/elpa, `make shell`
# mounts elpa/, and init.el byte-compiles it on Emacs's first start. No options -- see
# 01-install-base.sh for the design.
#
# Single dnf call, so its own exit status is this script's exit status.
set -uo pipefail

dnf install -y \
    emacs \
    emacs-gtk+x11 \
    emacs-pgtk \
    python3-lsp-server
