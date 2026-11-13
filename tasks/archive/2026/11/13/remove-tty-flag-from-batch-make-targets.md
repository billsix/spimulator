# Remove `-it` from the batch `make` targets that run a container

**Done:** 2026-11-13. **Created:** 2026-11-13 (William Emerison Six <billsix@gmail.com>).
**Needed by:** `tasks/archive/2026/11/13/github-actions-format-and-test-ci.md`. A GitHub runner has no TTY, so
`make format` could not run there with `-it`.

## Outcome

`-it` was removed from the targets that run a batch job and exit on their own:
- `Makefile` `format`;
- `Makefile`'s `run_in_container` macro, which the top-level book targets `html`, `pdf` and `epub`
  call;
- `pgu/Makefile` `format`.

It stays on:
- `shell` in both Makefiles (interactive);
- **`pgu/Makefile`'s `html`, `pdf` and `epub`**, contrary to the task's first list. Their scripts
  (`pgu/entrypoint/{html,pdf,epub}.sh`) run `make` in `pgu/docs`, and that Makefile's catch-all rule
  `%: Makefile spellcheck` runs `aspell check` interactively on every `.rst`. So they need a
  terminal. A comment above `pgu/Makefile`'s `docs` target records this. The top-level Makefile's
  book targets call `sphinx-build` directly and skip aspell, which is why they could lose the flag.

The general rule (the maintainer's, 2026-11-13): `-it` stays on targets that read the keyboard and
on long-running GUI/server targets, where it is what makes Ctrl-C reach the program. spimulator
has no GUI or server targets.

## Verification

Each run had no terminal attached: stdin from `/dev/null`, output to a file.
- `make format` exited 0. Its `: image` prerequisite rebuilt the image, which passed the gate
  (348 ok, 0 failed in the main meson run; 53 and 53 in the UBSan and ASan regression runs).
  clang-format changed no file.
- `make html` exited 0 ("build succeeded, 1 warning"), exercising `run_in_container`.
- `pgu/Makefile`'s `format` recipe, run directly against the existing `programmingfromthegroundup`
  image (its `: image` would rebuild a TeX image), exited 0.
