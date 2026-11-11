# Retired: a lean image for nested-podman builds

**Created:** 2026-09-10 (William Emerison Six <billsix@gmail.com>)
**Retired:** 2026-11-11 (William Emerison Six <billsix@gmail.com>; decision 1 of
`tasks/archive/2026/11/11/backlog-rework-after-ports.md`)

## Summary

The task planned to make a nested `make image` build lean by keying `USE_EMACS` and `BUILD_DOCS` off
`NESTED_PODMAN`. It was never implemented. On 2026-09-27 the fleet replaced that idiom with the
opt-in `MINIMAL_IMAGE` (the minimal-nested-images.md reference doc from
github.com/billsix/runClaudeInContainer): a nested build is full by default, because the inner store
is on disk. The maintainer retired the task rather than rewriting it against `MINIMAL_IMAGE`.

The survey's durable findings still hold:

- The image's optional flags are `USE_EMACS`, `BUILD_TREE_SITTER`, `BUILD_DOCS` and
  `RUN_SANITIZERS`, all Makefile-default 1.
- If a lean variant is ever added, only `USE_EMACS` and `BUILD_DOCS` may drop.
  `BUILD_TREE_SITTER` and `RUN_SANITIZERS` are gates. An `opcodes.h` rename once passed three
  `BUILD_TREE_SITTER=0` gates, which is why that flag must stay on.
