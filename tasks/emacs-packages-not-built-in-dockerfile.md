# Emacs packages are not installed or compiled in the Dockerfile

Done 2026-11-08 (William Emerison Six <billsix@gmail.com> asked for it). Part of github.com/billsix/runClaudeInContainer
`tasks/emacs-contract-conformance.md` (the build-time MELPA install and the run-time compile items).

## What was wrong

`entrypoint/02-install-emacs.sh` installed the Emacs binary + `python3-lsp-server`, and the Makefile
already mounted only the vendored `elpa/` (with the config baked by the dotfiles `COPY`, and
`.dockerignore` keeping `elpa/` out). But the install `RUN` still ran
`emacs --batch --load install-melpa-packages.el` under `USE_EMACS=1`: a network download plus
byte-compile of the MELPA set at image build, hidden at `make shell` by the `elpa/` mount.

## What changed

- `Dockerfile`: the `emacs --batch` step is gone; the install comment and `entrypoint/02-install-emacs.sh`'s
  header say the packages are vendored and compiled at run time.
- `init.el` runs `(byte-recompile-directory package-user-dir 0)` after `(package-initialize)`; the first
  Emacs start in `make shell` compiles the vendored packages into the mounted host `elpa/`.
- `.gitignore` now ignores `*.elc`, `*.eln` and `eln-cache/`. It did not before, so the first run-time
  compile left 314 untracked `.elc` files in `elpa/`.
- The Makefile's `ELPA_MOUNT` and `update-emacs-packages` comments describe this.
- The tree-sitter grammar the image writes to `/root/.emacs.d/tree-sitter/` is outside the `elpa/` mount
  and unchanged.

## Verification (2026-11-08, nested podman in the runClaudeInContainer sandbox)

- `make image` (default flags) succeeded with no MELPA fetch; meson test 59 ok / 0 fail, sanitizer
  gate (UBSan trap, ASan) 25 ok / 0 fail each. The image has `/usr/bin/emacs`, the baked config, the
  tree-sitter grammar, and no `/root/.emacs.d/elpa`.
- Emacs in batch with the Makefile's `elpa/` mount, `--network=none`
  (runClaudeInContainer `tasks/adhoc/emacs-no-build-time-compile/emacs-start-check.sh`): exit 0, 314 `.elc`
  in the host `elpa/`; with the `.gitignore` fix, `git status` showed only this change.
- `tools/check_helm_file_at_point.sh` (runClaudeInContainer): 7/7.
- Found, not fixed (contract-level): the run-time compile logs `Cannot open load file … compat-macs.el`
  for compat's `compat-26.el`…`compat-30.el` on every start; harmless on Emacs 31.
