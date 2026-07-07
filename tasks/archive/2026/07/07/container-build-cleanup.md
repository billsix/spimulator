# Fix bashrc exit() trap + stale dnf cache path

**Status:** DONE — archived 2026-07-07. Exit-trap echoes single-quoted so
`builtin exit "$@"` lands literally (verified: `exit 7` from a container
shell now propagates 7); Fedora version factored into one Makefile
`FEDORA_VERSION ?= 44` variable feeding both the dnf cache path and the
Dockerfile `FROM` tag via `--build-arg` (the open question below, answered
yes). Verified via a full nested `make image` (suite + sanitizer gate green).
The same exit-trap bug in texExpToPng is tracked in that repo, not here.
**Created:** 2026-06-13

## Goal

Two container-plumbing fixes in spimulator: the `exit()` trap baked into
`~/.bashrc` swallows the shell's exit code, and the dnf package-cache mount
points at a Fedora 43 path while the image is Fedora 44.

## Plan

- [ ] **`exit()` trap drops the exit code.** The Dockerfile builds the function
      with nested double-quotes
      (`echo "    builtin exit "$@"" >> ~/.bashrc`); `$@` is expanded at
      image-build time (empty), so the written function is `builtin exit` with
      no argument — `exit 1` from the shell becomes `exit 0`. **Fix:** quote the
      echoes so `"$@"` lands literally in `~/.bashrc`, or (cleaner) `COPY` a real
      bashrc fragment / function file instead of assembling it with `echo`.
      The `echo "Formatting on shell exit"` line survives by luck (words end up
      unquoted) — fold it into the same fix.
- [ ] **Stale cache path.** `PACKAGE_CACHE_ROOT = ~/.cache/packagecache/fedora/43`
      while the base image is `fedora:44` → cold cache / 43-vs-44 metadata
      mismatch. **Fix:** bump to `…/fedora/44` (or derive the version from a
      single variable shared with the base image tag).

## Notes / decisions

- Same `exit()`-quoting bug exists in texExpToPng (tracked separately) — fix
  both the same way.

## Open questions

- Worth factoring the Fedora version into one Make/Docker variable so the cache
  path and `FROM` tag can't drift again?
