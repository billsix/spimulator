#!/usr/bin/env bash
#
# 05-install-ports.sh -- the toolchains of every port of the engine
# (tasks/reference/engine-ports-design.md): Python for the Python port, GHC for the Haskell port
# (the ghc package brings the libraries the port uses, parsec and haskeline
# among them), Rust and Cargo for the Rust port. The ports are part of the test
# gate, so the Dockerfile runs this unconditionally: no ARG, no `if`, and a
# lean (MINIMAL_IMAGE) image keeps it. GHC is about 1 GB of the image. No
# options -- see 01-install-base.sh for the design.
#
# Single dnf call, so its own exit status is this script's exit status.
set -uo pipefail

# Each port's lint tools, for the port suites' lint tests (port/<port>/lint.sh):
# ruff and ty (Python), hlint (Haskell), clippy and rustfmt (Rust); ormolu (Haskell)
# and the ruff/rustfmt formatters are also what `make format` runs on the ports.
dnf install -y python3 ruff ty ghc hlint ormolu rust cargo clippy rustfmt
