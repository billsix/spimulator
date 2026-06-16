# Add `image-export` / `image-import` Makefile targets

**Status:** done — implemented 2026-06-16
**Created:** 2026-06-13

## Goal

Add the OCI-image save/load convenience targets that **modelviewprojection** already has, so the built
image can be archived to a tar (e.g. to move it to an offline/air-gapped machine, or snapshot a known-good
build) and reloaded later without a rebuild.

## Reference implementation

`modelviewprojection/modelviewprojection/Makefile`:

```make
image-export: ## export the OCI image
	podman save $(CONTAINER_NAME) -o $(CONTAINER_NAME)-$(shell date +%m-%d-%Y_%H-%M-%S).tar

image-import: ## import the OCI image, "make image-import FILE=foo.tar"
	podman load -i $(FILE)
```

## Current state

spimulator has **neither** target. `CONTAINER_CMD = podman`, `CONTAINER_NAME = spimulator`.

## Proposed targets (improved over mvp's)

Add near the other image targets. Three improvements over the mvp original: use `$(CONTAINER_CMD)`
(not a hardcoded `podman`, matching the rest of the Makefile), mark both `.PHONY`, and tighten the
help text.

```make
.PHONY: image-export
image-export: ## export the OCI image to a timestamped tar in the repo root
	$(CONTAINER_CMD) save $(CONTAINER_NAME) -o $(CONTAINER_NAME)-$(shell date +%m-%d-%Y_%H-%M-%S).tar

.PHONY: image-import
image-import: ## import an OCI image tar: make image-import FILE=foo.tar
	$(CONTAINER_CMD) load -i $(FILE)
```

## Notes / decisions

- **Gitignore the artifacts.** `podman save` drops a large `spimulator-MM-DD-YYYY_HH-MM-SS.tar` in the
  repo root. Add `$(CONTAINER_NAME)-*.tar` (i.e. `spimulator-*.tar`) — or just `*.tar` — to
  `.gitignore` so these never get committed. (mvp's copy omits this; don't inherit that gap.)
- **No `image` dependency by default.** `podman save` needs the image to already exist. Leaving
  `image-export` without an `image` prerequisite (as mvp does) means it errors clearly if you haven't
  built yet, rather than silently triggering a long rebuild (spimulator's `image` also builds + tests
  spim, so an accidental rebuild here is especially costly). Optionally make it `image-export: image`.
- **Nested podman:** `podman save`/`load` don't start a container, so they need **no**
  `--cgroups=disabled` and run fine nested.
- **`image-import` requires `FILE=`** — `make image-import FILE=spimulator-06-13-2026_12-00-00.tar`.
  A guard (`@test -n "$(FILE)" || { echo 'set FILE=foo.tar'; exit 1; }`) is a nice-to-have.

## Acceptance

- `make help` lists `image-export` and `image-import` with `##` descriptions.
- `make image-export` writes `spimulator-<timestamp>.tar`; `make image-import FILE=…` reloads it.
- The exported tar pattern is gitignored.
