# Upgrade the container base from Fedora 44 to Fedora 45 (done)

Done 2026-11-05 (William Emerison Six <billsix@gmail.com>). Part of a fleet-wide
Fedora 44 → 45 bump.

## What changed

- `Dockerfile` and `pgu/Dockerfile`: `registry.fedoraproject.org/fedora:44` →
  `fedora:45`.
- `src/instruction.c`: `enable_text_auto_alignment` marked `[[maybe_unused]]`
  (see finding below).

`make image` is green nested on the 45 base — the full gate passed: build +
`meson test` (regression + examples suites) + the UBSan-trap/ASan sanitizer gate
(`RUN_SANITIZERS=1` default) + the examples native artifacts + tree-sitter +
emacs. Image `localhost/spimulator:latest`.

## Toolchain finding (LLVM 23)

Fedora 45 ships clang from **LLVM 23**, which adds `-Wunused-but-set-global`.
Under spimulator's core `-Werror` the build failed on `src/instruction.c:67`:
`enable_text_auto_alignment` is written in `align_text` (the `.align 0` case) and
the `text_begins_at_point` / `k_text_begins_at_point` pair, but is **read nowhere
in the repo** (confirmed by grep over all `*.c`/`*.h`).

Fix applied: the C23 `[[maybe_unused]]` attribute on the declaration, with an
in-line comment — narrow, in-language, and it preserves the code rather than
deleting a flag that carries evident `.align 0` intent. It was the only
`-Werror` breakage from the toolchain bump across the whole tree.

**Open question left for the maintainer** (not addressed here — a
simulator-semantics decision, out of scope for a base-image bump): should
`.align 0` actually disable automatic text alignment, i.e. should the
instruction-store path consult `enable_text_auto_alignment`? It currently does
not, so `.align 0` has no effect on auto-alignment.
