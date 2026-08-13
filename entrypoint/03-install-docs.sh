#!/usr/bin/env bash
#
# 03-install-docs.sh -- the PGU book toolchain: Sphinx + furo (HTML/EPUB), latexmk +
# TeX Live (PDF), inkscape (rasterize SVG figures), aspell (docs spellcheck), pandoc.
# Corresponds to the Dockerfile's BUILD_DOCS flag; the Dockerfile runs this only when
# BUILD_DOCS=1. No options -- see 01-install-base.sh for the design.
#
# Single dnf call, so its own exit status is this script's exit status.
set -uo pipefail

dnf install -y \
    aspell \
    aspell-en \
    inkscape \
    latexmk \
    pandoc \
    python3-furo \
    python3-pip \
    python3-sphinx \
    python3-sphinx-latex \
    python3-sphinx_rtd_theme \
    texlive \
    texlive-anyfontsize \
    texlive-dvipng \
    texlive-dvisvgm \
    texlive-standalone
