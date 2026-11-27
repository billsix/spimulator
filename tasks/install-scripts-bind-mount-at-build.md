# Bind-mount build-only install scripts into their RUN instead of COPYing them

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 2
**Created:** 2026-11-26 (William Emerison Six <billsix@gmail.com>)
**Source:** github.com/billsix/runClaudeInContainer `tasks/reference/build-time-bind-mounts-for-install-scripts.md` (the research and its measurements)

## BLUF

Replace each `COPY` of a build-only script (and, where nothing later needs it, a build-only input
file) in this repo's Dockerfile(s) with a `RUN --mount=type=bind` on the `RUN` that uses it. Done
means the candidates below are each converted or explicitly kept with a reason, `make image` builds,
and the repo's gate passes against the new image.

## Context

Read first: github.com/billsix/runClaudeInContainer `tasks/reference/build-time-bind-mounts-for-install-scripts.md`, and the "Dockerfile contract" section of the maintainer's personal overlay
template (github.com/billsix/dotfiles `.ai-coding-conventions.personal.template.md`), which states the
rule. Worked example: github.com/billsix/geometricalgebra `Dockerfile`.

The shape, one mount per script, target path unchanged so the `RUN` body stays as it is:

```dockerfile
RUN --mount=type=bind,source=entrypoint/01-install-base.sh,target=/usr/local/bin/01-install-base.sh \
    --mount=type=cache,target=/var/cache/libdnf5 \
    --mount=type=cache,target=/var/lib/dnf \
    /usr/local/bin/01-install-base.sh
```

Rules that came out of the research:

- Never mount the whole `entrypoint/` directory: every layer that mounts it re-runs when any file in
  it changes.
- No `z`, `Z` or `relabel` option on the mount. They are podman-only (Docker rejects them), and a
  plain mount passed on an SELinux-enforcing host with a `:Z`-labelled context under both podman
  and Docker.
- The script must be committed executable (a mount keeps the host mode), or be run as `bash <script>`.
- `source` is relative to the build context, which for a Dockerfile in a subdirectory is whatever
  directory its `make image` passes to the build.
- Keep `COPY` for anything the running container uses: dotfiles, an entrypoint or `shell.sh` baked
  as a fallback, baked source, and any file the Makefile or another script names by its in-image path.

## Candidates (surveyed 2026-11-26, a text match: read each before converting)

A row is listed when a `RUN` in the same Dockerfile names the copied file's in-image path and no
Makefile, script or workflow in the repo does. A script that is only *mentioned* inside a string the
`RUN` writes is a false lead.

**`Dockerfile`**

| Copied file | Destination |
| --- | --- |
| `entrypoint/01-install-base.sh` | `/usr/local/bin` |
| `entrypoint/02-install-emacs.sh` | `/usr/local/bin` |
| `entrypoint/03-install-docs.sh` | `/usr/local/bin` |
| `entrypoint/04-install-tree-sitter.sh` | `/usr/local/bin` |
| `entrypoint/05-install-ports.sh` | `/usr/local/bin` |

## Plan

- [ ] Read each candidate's `RUN` and confirm the file is not needed in the finished image.
- [ ] Convert (the conversion script used for the worked example handles the
      `COPY entrypoint/<script>.sh /usr/local/bin/` case:
      github.com/billsix/runClaudeInContainer
      `tools/convert_install_copy_to_bind_mount.py`).
- [ ] Update the Dockerfile comments that describe the removed `COPY` lines, and any `.dockerignore`
      `!` re-include that existed only for a now-mounted file.
- [ ] `make image`, then the repo's gate. If a workflow under `.github/` builds the image with
      Docker, check its first run after the change (Docker 29.9.3 built the plain mount form and
      keyed its cache on the script's content in the research).

## Notes / decisions


## Open questions
