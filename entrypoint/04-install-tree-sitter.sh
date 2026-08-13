#!/usr/bin/env bash
#
# 04-install-tree-sitter.sh -- Node.js + npm, for building the editor-integration
# tree-sitter grammar. Corresponds to the Dockerfile's BUILD_TREE_SITTER flag; the
# Dockerfile runs this only when BUILD_TREE_SITTER=1. The grammar build itself
# (npm install / make / the Emacs shared-library compile) is not package installation,
# so it stays in the Dockerfile. No options -- see 01-install-base.sh for the design.
#
# Single dnf call, so its own exit status is this script's exit status.
set -uo pipefail

dnf install -y nodejs npm
