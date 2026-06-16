.DEFAULT_GOAL := shell

USE_EMACS ?= 1
BUILD_TREE_SITTER ?= 1
BUILD_DOCS ?= 1

CONTAINER_CMD = podman
CONTAINER_NAME = spimulator
FILES_TO_MOUNT = -v .:/spimulator/:Z \
                 -v ./entrypoint/shell.sh:/usr/local/bin/shell.sh:Z \
                 -v ./entrypoint/format.sh:/usr/local/bin/format.sh:Z \
                 -v ./entrypoint/lint.sh:/usr/local/bin/lint.sh:Z \
                 -v ./entrypoint/dotfiles/.tmux.conf:/root/.tmux.conf:Z

PACKAGE_CACHE_ROOT = ~/.cache/packagecache/fedora/43

DNF_CACHE_TO_MOUNT = -v $(PACKAGE_CACHE_ROOT)/var/cache/libdnf5:/var/cache/libdnf5:Z \
	             -v $(PACKAGE_CACHE_ROOT)/var/lib/dnf:/var/lib/dnf:Z


# USE_EMACS=1 (the default) bind-mounts just the vendored elpa/ tree into the
# container so an interactive `make shell` can *use* the vendored packages (:U
# chowns it to the container user, :z relabels for SELinux). Mounting ONLY elpa/
# (not the whole .emacs.d/) keeps the build-time tree-sitter grammar dropped into
# /root/.emacs.d/tree-sitter/ intact. Set USE_EMACS=0 to skip the mount. To
# *refresh* the vendored packages, use `make update-emacs-packages` below.
ifeq ($(USE_EMACS), 1)
  ELPA_MOUNT= -v $(CURDIR)/entrypoint/dotfiles/.emacs.d/elpa:/root/.emacs.d/elpa:U,z
else
  ELPA_MOUNT=
endif



.PHONY: all
all: shell ## Build the image and get a shell in it

.PHONY: image
image: ## Build podman image to run the examples
	# cache rpm packages
	mkdir -p $(PACKAGE_CACHE_ROOT)/var/cache/libdnf5
	mkdir -p $(PACKAGE_CACHE_ROOT)/var/lib/dnf
	# build the container
	$(CONTAINER_CMD) build \
                         -t $(CONTAINER_NAME) \
                         --build-arg USE_EMACS=$(USE_EMACS) \
                         --build-arg BUILD_TREE_SITTER=$(BUILD_TREE_SITTER) \
                         --build-arg BUILD_DOCS=$(BUILD_DOCS) \
                         $(DNF_CACHE_TO_MOUNT) \
                         $(ELPA_MOUNT) \
                         .


.PHONY: shell
shell: format ## Get Shell into a ephermeral container made from the image
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
                $(ELPA_MOUNT) \
		$(CONTAINER_NAME) \
		/usr/local/bin/shell.sh


# Refresh the vendored Emacs packages. Forces USE_EMACS=1 and rebuilds the image
# first. Then, in the container, wipes the elpa tree and reinstalls from MELPA
# into the host's bind-mounted entrypoint/dotfiles/.emacs.d/elpa (the
# install-melpa-packages.el is mounted read-only, so edits to it take effect
# without a rebuild). Mounting ONLY elpa/ leaves the build-time tree-sitter
# grammar (/root/.emacs.d/tree-sitter/) untouched. Finally strips compiled
# *.elc/*.eln (regenerated, machine-specific artifacts) and force-stages the elpa
# tree (git add -A -f overrides .gitignore's *.elc/*.eln/...) so it is ready to
# commit. Needs network access.
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


.PHONY: format
format: image ## Format the C code
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(CONTAINER_NAME) \
		/usr/local/bin/format.sh


# ---------------------------------------------------------------------------
# PGU book (pgu/) — build the MIPS-on-spim port into HTML/PDF/EPUB.
#
# The repo is mounted at /spimulator inside the container, so the book
# source lives at $(DOCS_SRC) and output is copied to ./output/pgu/ on
# the host (via the /spimulator mount).  We invoke sphinx-build
# directly rather than `make -C pgu/docs`, because the copied
# pgu/docs/Makefile routes every target through an INTERACTIVE
# `aspell check` (would hang a build) and a known-buggy inkscape line;
# that Makefile gets adapted in a later phase of the port (see
# tasks/port-pgu.md).  Requires the image built with BUILD_DOCS=1.
# ---------------------------------------------------------------------------
DOCS_SRC   = /spimulator/pgu/docs/source
DOCS_BUILD = /spimulator/pgu/docs/build

define run_in_container
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(ELPA_MOUNT) \
		$(CONTAINER_NAME) \
		-c '$(1)'
endef

# Rasterize the book's SVG figures to PNG before building.  We loop
# over whatever .svg are present rather than hardcoding a list, so it
# never drifts (and we sidestep PGU's hardcoded, partly-broken
# inkscape lines).  PNGs land in _static beside the source bitmaps;
# they are git-ignored (see pgu/.gitignore).
RENDER_FIGURES = for f in $(DOCS_SRC)/_static/*.svg; do inkscape "$$f" --export-filename="$${f%.svg}.png"; done

.PHONY: docs
docs: html pdf epub ## Build the PGU book in HTML, PDF, and EPUB form

.PHONY: html
html: image ## Build the PGU book in HTML form
	$(call run_in_container,$(RENDER_FIGURES) && sphinx-build -M html $(DOCS_SRC) $(DOCS_BUILD))

.PHONY: pdf
pdf: image ## Build the PGU book in PDF form
	$(call run_in_container,$(RENDER_FIGURES) && sphinx-build -M latexpdf $(DOCS_SRC) $(DOCS_BUILD))

.PHONY: epub
epub: image ## Build the PGU book in EPUB form
	$(call run_in_container,$(RENDER_FIGURES) && sphinx-build -M epub $(DOCS_SRC) $(DOCS_BUILD))



.PHONY: image-export
image-export: ## export the OCI image to a timestamped tar in the repo root
	$(CONTAINER_CMD) save $(CONTAINER_NAME) -o $(CONTAINER_NAME)-$(shell date +%m-%d-%Y_%H-%M-%S).tar

.PHONY: image-import
image-import: ## import an OCI image tar: make image-import FILE=foo.tar
	$(CONTAINER_CMD) load -i $(FILE)

.PHONY: help
help:
	@grep --extended-regexp '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'
