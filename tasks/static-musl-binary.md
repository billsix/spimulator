# A static spimulator binary linked against musl

**Status:** proposed — questions answered; ready to start on go-ahead
**Priority:** 8
**Difficulty:** 5
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Build `spimulator` as one statically linked binary against musl, with line editing, so it runs on
any Linux with no shared libraries. libedit and ncurses are built from source against musl for it.
Done means the image builds the static binary and fails unless the `regression` suite passes
against it, and `make static` copies it to `output/spimulator-static` on the host.

## Decision (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Link against musl" means (a) a static `spimulator`, "unless there's reasons not to". The other
readings (musl for the C examples, more musl source in the examples) are dropped.

- **Line editing stays: build libedit and ncurses against musl** (William Emerison Six
  <billsix@gmail.com>, 2026-11-14). The static binary keeps the interactive prompt's editing
  rather than shipping with `-Dline_editing=disabled`.
- **Vendor the libedit and ncurses sources in the repo** (William Emerison Six <billsix@gmail.com>,
  2026-11-14), each with a `make update-libedit` / `make update-ncurses` target and a
  `tools/update-<dep>.sh` script, the pattern for third-party C deps vendored by source (two deps,
  so the scripts source the shared `tools/update_dep_common.sh`; canonical in
  github.com/billsix/runClaudeInContainer `tasks/reference/update_dep_common.sh`, and
  `tasks/reference/vendoring-third-party-source-deps.md` there). The image builds them offline from
  the vendored tree.
- **The image build gates on the static binary** (William Emerison Six <billsix@gmail.com>,
  2026-11-14): the `regression` suite runs against it as it does against the glibc build, and a
  failure fails the build.
- **`make static` copies the binary to the host** (William Emerison Six <billsix@gmail.com>,
  2026-11-14), writing `output/spimulator-static`. `output/` is not gitignored in this repo today (only `output/pgu/` is
  written by `make html`), so the task adds `output/spimulator-static` to `.gitignore`.

## What stands in the way (checked 2026-11-14)

- **libedit.** The only shared library spimulator links besides libc and libm is libedit
  (`meson.build`, `libedit_dep`, the `line_editing` option). Fedora 45 ships no static libedit,
  and its glibc-built archives cannot link against musl anyway, so libedit and ncurses (libedit's
  terminal dependency) are built from source with `musl-gcc` into a separate prefix in the image.
- **Sanitizers.** ASan and UBSan runtimes target glibc, so the `RUN_SANITIZERS` gate stays on the
  glibc build; the static build is an extra build, not a replacement.
- **Available packages.** `musl-gcc`, `musl-devel` and `musl-libc-static` (1.2.6) are in Fedora 45,
  not installed in the image or the sandbox yet.
- **No GUI binary today.** A future GUI (`tasks/gui-and-cli-parity-investigation.md`) could not be
  fully static (OpenGL drivers load dynamically), so this is for the command-line binary only.

## What the repo does today (checked 2026-11-14)

- Nothing links against musl, and no musl package is installed in the sandbox.
- The C examples build with `-nostdlib` (`examples/src/meson.build`, `edu_link_args`) and their own
  small I/O helpers. Three example libraries are **source adapted from musl**, each with a
  `LICENSE-musl`: `examples/src/lib/libctype`, `libstdlib` and `libstr` (`examples/READING-ORDER.md`).
- When the examples were merged in from their own repository, its `musl/` directory was pruned as
  not needed (`ChangeLog`, the examples merge entry).

## Open questions

None.
