#!/usr/bin/env bash
#
# 02-install-emacs.sh -- Emacs + the Python LSP server. Corresponds to the Dockerfile's
# USE_EMACS flag; the Dockerfile runs this only when USE_EMACS=1. The MELPA-package
# bootstrap (`emacs --batch ...`) is config, so it stays in the Dockerfile. No options --
# see 01-install-base.sh for the design.
#
# Single dnf call, so its own exit status is this script's exit status.
set -uo pipefail

dnf install -y \
    emacs \
    emacs-gtk+x11 \
    emacs-pgtk \
    python3-lsp-server
