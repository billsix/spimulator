# Fix the Dockerfile's masked base-install status, and declare each ARG at first use

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §8.1, §8.3, §8.2 F14

## Outcome

- The base-install `RUN` is chained with `&&` through the `USE_EMACS` `if`. Before, it ended
  `… && echo … ; if …; fi ;`, so its status was the `if`'s, and a failed `01-install-base.sh` only
  surfaced later at an unrelated step (`sh -c 'false && echo x ; if [ 0 = 1 ]; then :; fi'` exits 0).
- `USE_EMACS`, `BUILD_DOCS`, `RUN_SANITIZERS` and `BUILD_TREE_SITTER` are each declared, with their
  comments, just before the first layer that reads them. A build-arg's value is part of every later
  layer's cache key, so toggling `BUILD_TREE_SITTER` no longer re-runs the package and build layers.
- The two comments that cited moved tasks now cite `tasks/archive/2026/05/24/PLAN-asm-listings-makefile.md`
  and `tasks/archive/2026/10/31/examples-makefile-debug-build.md`.

## Verification

The branch's final `make image` (default flags) builds the image. Its result is recorded in the
batch's last done-record.
