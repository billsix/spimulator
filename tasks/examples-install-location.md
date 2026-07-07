# Move example binaries off PATH (RPM-conventional install location)

**Status:** in progress — research done, fix being applied (2026-07-07)
**Created:** 2026-07-07

## Problem

The Dockerfile's `meson install` (Dockerfile:124, prefix `/usr/local`) installs
**every native example binary onto PATH**. `examples/src/meson.build` marks all
~35 demo executables (`spimulator-example-*`) `install: true` with no
`install_dir`, so meson defaults them to `${prefix}/bin` → `/usr/local/bin`.
Example programs are teaching artifacts, not user-facing commands; they should
not be on PATH.

(The `spimulator-example-` name prefix means no collision risk with real
commands, but PATH is still the wrong place for them.)

## Research: where would an RPM put these?

Per the FHS and the Fedora Packaging Guidelines:

- **Example *sources*** (`.c`, `.asm`) are documentation content: they ship as
  `%doc`, landing in `/usr/share/doc/<package>/examples/`. Files under `%doc`
  must not be required at runtime, and packages are expected not to ship
  *executables* there.
- **Compiled binaries not intended for users' PATH** belong in
  `%{_libexecdir}/<package>/` → `/usr/libexec/spimulator/`. FHS 3.0 codifies
  `/usr/libexec` for "internal binaries not intended to be executed directly
  by users or shell scripts."
- **Arch-independent runtime data** (e.g. the `.asm` demos, if spim loaded them
  from an installed location) would go in `/usr/share/spimulator/`.
- Most distro packages would ship the example **sources** only (as `%doc`) and
  not the compiled demo binaries at all — the student builds them.

So the RPM answer: sources → `/usr/share/doc/spimulator/examples/`; if the
compiled demos are shipped at all → `/usr/libexec/spimulator/`.

## Fix

In `examples/src/meson.build`, give every demo `executable()` an
`install_dir` of `get_option('libexecdir') / 'spimulator'`, so
`meson install` puts them at `${prefix}/libexec/spimulator/`
(`/usr/local/libexec/spimulator/` in the image) — off PATH, matching the RPM
convention. Defined once as `example_install_dir` at the top of the file and
referenced at each of the 7 `executable()` sites (the two `foreach` loops plus
the five libstdlib demos).

No test impact: the `examples` meson test suite runs the *build-dir* binaries
via `examples/tests/run-demo.sh`, not the installed copies. The
`make -C examples/src all` native artifacts (`examples/src/bin/`) are a
separate, uninstalled path and are unaffected.

## Verification

- `meson setup` + `meson install --destdir <scratch>` locally: confirm the
  `spimulator-example-*` binaries land under `libexec/spimulator/`, not `bin/`,
  and that `spimulator` itself still lands in `bin/`.
- Next `make image` rebuild: `command -v spimulator-example-helloworld` inside
  the container should find nothing; `/usr/local/libexec/spimulator/` should
  hold the demos.

## Open questions

- Ship the example *sources* to `/usr/share/doc/spimulator/examples/` via
  `install_subdir()` as well, to fully mirror what an RPM would do? Deferred —
  the container already carries the full source tree at `/spimulator/examples`.
