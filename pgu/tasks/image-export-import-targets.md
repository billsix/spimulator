# Add `image-export` / `image-import` Makefile targets

**Status:** proposed — needs go-ahead
**Created:** 2026-06-13

## Goal

Add the OCI-image save/load convenience targets that **modelviewprojection** already has, so the built
image can be archived to a tar (to move it to an offline/air-gapped machine, or snapshot a known-good
build) and reloaded later without a rebuild. Part of standardizing this target pair across all the
container-template projects — see the "Makefile contract" in `~/.claude/CLAUDE.md`.

## Reference implementation

`modelviewprojection/modelviewprojection/Makefile` (the only project that had it as of 2026-06-13;
its copy hardcodes `podman`, lacks `.PHONY`, and doesn't gitignore the tar — fixed below):

```make
image-export: ## export the OCI image
	podman save $(CONTAINER_NAME) -o $(CONTAINER_NAME)-$(shell date +%m-%d-%Y_%H-%M-%S).tar
image-import: ## import the OCI image, "make image-import FILE=foo.tar"
	podman load -i $(FILE)
```

## Current state

`spimulator/pgu` has neither target. It already defines `CONTAINER_CMD` and `CONTAINER_NAME` (= `programmingfromthegroundup`),
so the standard form drops straight in.

## Proposed targets (improved over mvp's)

Improvements over the mvp original: use `$(CONTAINER_CMD)` (not a hardcoded `podman`, matching the rest
of the Makefile), mark both `.PHONY`, and gitignore the exported tars.

```make
.PHONY: image-export
image-export: ## export the OCI image to a timestamped tar in the repo root
	$(CONTAINER_CMD) save $(CONTAINER_NAME) -o $(CONTAINER_NAME)-$(shell date +%m-%d-%Y_%H-%M-%S).tar

.PHONY: image-import
image-import: ## import an OCI image tar: make image-import FILE=foo.tar
	$(CONTAINER_CMD) load -i $(FILE)
```

## Notes / decisions

- **Gitignore the artifacts:** add `$(CONTAINER_NAME)-*.tar` (i.e. `programmingfromthegroundup-*.tar`) — or just `*.tar` —
  to `.gitignore`; the exported tars are large and must never be committed.
- **No `image` prerequisite by default:** `save` needs the image to already exist, so leaving
  `image-export` without an `image` dep makes it error cleanly rather than silently triggering a long
  rebuild. Add `image-export: image` if you'd rather it always builds first.
- **Nested podman:** `save`/`load` start no container, so they need no `--cgroups=disabled` and run
  fine nested.
- **`image-import` requires `FILE=`** — `make image-import FILE=programmingfromthegroundup-06-13-2026_12-00-00.tar`.
  Optional guard: `@test -n "$(FILE)" || { echo 'set FILE=foo.tar'; exit 1; }`.

## Acceptance

- `make help` lists `image-export` and `image-import` with `##` descriptions.
- `make image-export` writes `programmingfromthegroundup-<timestamp>.tar`; `make image-import FILE=…` reloads it.
- The exported tar pattern is gitignored.
