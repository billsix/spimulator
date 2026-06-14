# Add a `make update-emacs-packages` target (vendored Emacs refresh)

**Status:** proposed — needs go-ahead
**Created:** 2026-06-13

## Goal

Give spimulator the same one-command "refresh + re-vendor the Emacs `elpa/` tree" workflow that
**geometricalgebra** already has: a `make update-emacs-packages` target that rebuilds the image,
wipes + reinstalls the MELPA packages into the host's bind-mounted `elpa/`, strips the
machine-specific compiled artifacts, and force-stages the tree so it's ready to commit.

Right now spimulator vendors a real `elpa/` tree (569 files tracked in git) and has the
`USE_EMACS`/`ELPA_MOUNT` plumbing, but there is **no ergonomic way to update the vendored packages** —
you'd have to do it by hand inside `make shell USE_EMACS=1` and remember the strip + `git add -f`
dance. This task adds the convenience target.

## Reference implementation (copy from here)

geometricalgebra is the worked precedent. Read, in that repo:
- `Makefile` — the `update-emacs-packages` target (the `$(MAKE) image USE_EMACS=1` → in-container
  wipe+reinstall → host strip + `git add -A -f` recipe), and the comment block above `ELPA_MOUNT`.
- `tasks/archive/2026/06/07/emacs-package-install-strategy.md` — the full rationale, including the
  decision to strip `*.elc` **and** `*.eln`, and the reconciliation with the Dockerfile's build-time
  install.

## How spimulator currently stands (what differs from the reference)

- `Makefile:3` — `USE_EMACS ?= 1` (note: defaults **on** here, unlike geometricalgebra's `?= 0`).
- `Makefile:21-25` — `ELPA_MOUNT` mounts **just `elpa/`**: `-v $(CURDIR)/entrypoint/dotfiles/.emacs.d/elpa:/root/.emacs.d/elpa:U,z`. **Same shape as geometricalgebra**, so the reference recipe ports almost verbatim.
- `Dockerfile:3` — `ARG USE_EMACS=0` (the standard Makefile-`1`/Dockerfile-`0` mirror).
- `Dockerfile:27` — `COPY entrypoint/dotfiles/ /root/` copies the **whole** dotfiles tree, *including*
  the 17M vendored `elpa/`, into the image — **and** `Dockerfile:59-66` *also* runs
  `emacs --batch --load /root/.emacs.d/install-melpa-packages.el` at build time when `USE_EMACS=1`.
  So packages are shipped **twice** (vendored copy + build-time reinstall on top). This is the exact
  redundancy geometricalgebra resolved (see "Decision 2" below).
- `Dockerfile:146-165` — when `BUILD_TREE_SITTER=1 && USE_EMACS=1`, a compiled grammar `.so` is dropped
  into `/root/.emacs.d/tree-sitter/`. **This is why the elpa-only mount matters**: the update target
  must touch only `elpa/`, never the whole `.emacs.d/`, or it would clobber the tree-sitter artifact.
  spimulator's `ELPA_MOUNT` already scopes to `elpa/`, so this is naturally safe — keep it that way.
- No `.dockerignore` excluding the tree (geometricalgebra added one).
- `install-melpa-packages.el` lives at `entrypoint/dotfiles/.emacs.d/install-melpa-packages.el`.

## Proposed target (tailored to spimulator)

Add after the existing `image`/`shell` targets. Because `ELPA_MOUNT` here is elpa-only, this is a
near-verbatim port of geometricalgebra's:

```make
.PHONY: update-emacs-packages
update-emacs-packages: ## USE_EMACS=1: rebuild image, wipe+reinstall elpa, strip *.elc/*.eln, git add -f
	$(MAKE) image USE_EMACS=1
	$(CONTAINER_CMD) run --rm \
		-v $(CURDIR)/entrypoint/dotfiles/.emacs.d/elpa:/root/.emacs.d/elpa:U,z \
		-v $(CURDIR)/entrypoint/dotfiles/.emacs.d/install-melpa-packages.el:/root/.emacs.d/install-melpa-packages.el:ro,z \
		--entrypoint /bin/bash \
		$(CONTAINER_NAME) \
		-c 'set -e; find /root/.emacs.d/elpa -mindepth 1 -delete; \
		    emacs --batch --load /root/.emacs.d/install-melpa-packages.el'
	cd $(CURDIR)/entrypoint/dotfiles/.emacs.d/elpa && \
		find . \( -iname '*.elc' -o -iname '*.eln' \) -delete && \
		git add -A -f .
	@echo "Done: reinstalled packages, stripped *.elc/*.eln, staged elpa -- review and commit."
```

Notes:
- The install script is bind-mounted **read-only** so you can tweak the package list in
  `install-melpa-packages.el` and re-run without rebuilding the image.
- `git add -A -f .` overrides any `*.elc`/`*.eln` gitignore patterns so the full tree stages.
- Mirror geometricalgebra's `ELPA_MOUNT` comment block update so the use-vs-refresh split is documented
  at the mechanism.

## Decisions to make (do not implement until chosen)

1. **Scope.** Just the `update-emacs-packages` target, or also the Dockerfile reconciliation (#2)?
   The target alone is self-contained and low-risk; recommend doing it first.
2. **Dockerfile redundancy (the "ship twice" question).** geometricalgebra ended up: add
   `entrypoint/dotfiles/.emacs.d/elpa` to `.dockerignore` (stop copying the 17M tree into the image)
   **and** drop the build-time `emacs --batch --load …` install — so the image carries no Emacs
   packages and the vendored tree is the single source, mounted at runtime. Should spimulator follow
   suit? Caveat unique to spimulator: confirm nothing in the build (e.g. the tree-sitter step, or an
   image that's expected to have Emacs packages without a mount) depends on the build-time install
   before removing it. If kept, the redundancy is harmless but the build stays online w.r.t. MELPA.
3. **`USE_EMACS` default.** spimulator defaults `USE_EMACS ?= 1`; the target forces `USE_EMACS=1`
   anyway, so this doesn't matter for the target — noted only so the port isn't mistaken for a bug.

## Operational notes

- **Nested podman (running inside the sandbox):** the in-container `podman run` needs
  `--cgroups=disabled` to work nested, and the `$(MAKE) image` step builds fine. Per the standing
  arrangement this flag is added transiently at run time, not committed into the Makefile.
- **Off-limits:** the vendored `elpa/` *contents* are build artifacts — don't read/edit/reformat them;
  this task only adds the *mechanism* that regenerates them. The author runs the target and commits the
  result deliberately (it rewrites a ~17M tree).
- **Not executed as part of this task** — like the reference, it should be parse/dry-run-verified only;
  actually running it rebuilds the image and rewrites the vendored tree, which is the author's call.

## Acceptance

- `make help` lists `update-emacs-packages` with its `##` description.
- The target parses and (dry-run) issues the expected `podman run` + host strip/`git add` steps.
- `ELPA_MOUNT` comment documents the use (`make shell USE_EMACS=1`) vs refresh
  (`make update-emacs-packages`) split.
- If decision #2 is "yes": `.dockerignore` excludes the elpa tree and the build-time install is gone,
  verified by a throwaway build showing `/root/.emacs.d` without `elpa/`.
