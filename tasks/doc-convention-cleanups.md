# Fixed doc-convention drift in open tasks and reference docs

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §1.4, §1.5, §9.4, §10.6 F23

## Summary

- **Stamps:** the bare "Bill" stamps in `string-equality-audit`, `mini-c-compiler`,
  `container-cross-env`, `multi-file-load`, `timing-model`, `pgu-build-matrix` and
  `entrypoint/format.sh` became `William Emerison Six <billsix@gmail.com>`; prose mentions became
  "the maintainer". `entrypoint/format.sh` kept its executable bit.
- **Links:** `ast-column-tracking`, `multi-file-load`, `mini-c-compiler` and
  `tasks/reference/inherited-idiosyncrasies.md` now cite repo-root-relative paths, pointing at the
  archived tasks (`mips-asm-teaching-style.md` was fixed earlier in this batch).
- **Present tense:** the "History" sections of `tasks/reference/opcode-table-generator.md` and
  `tasks/reference/parser-ast-emit.md` became short "Why" paragraphs that keep the design reasons.
  `tasks/reference/inherited-idiosyncrasies.md` was trimmed to the items still open (decision 1).
  `CLAUDE.md` no longer says the asm rollout "is COMPLETE"; it states the rule.
- **Task headers:** `ast-column-tracking`, `container-cross-env`, `examples-build-matrix`,
  `pgu-build-matrix` and `software-alu` got a Status line; those five plus `mini-c-compiler`,
  `multi-file-load`, `string-equality-audit` and `timing-model` got a BLUF and Context.
  `codebase-cleanup-plan` was rewritten earlier in this batch. In `ast-column-tracking`, the stale
  "Ordering"/"UNBLOCKED" paragraphs became its Context. The three build-matrix tasks carry a
  `Depends on:` line naming `tasks/examples-c-debug-build-via-meson.md`.
- `tasks/reference/incremental-example-decomposition.md` cites gacalc's teaching-order doc as
  `tasks/reference/algebra-teaching-order.md` in github.com/billsix/geometricalgebra (checked from
  its `github` remote).

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `tasks/reference/inherited-idiosyncrasies.md` is trimmed to the items still open (option (b)).
