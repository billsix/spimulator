# Fix lldb ASLR-personality failure in the container

**Status:** DONE — archived 2026-07-07. Added
`settings set target.disable-aslr false` to `entrypoint/dotfiles/.lldbinit`.
Verified in the rebuilt image: lldb launches spimulator, hits a breakpoint at
`main`, no "personality set failed" error. (Reference repos weren't mounted
this session; the setting is the standard fix and verified working here.)
**Created:** 2026-07-07

## Problem

Running `lldb` inside the spimulator container fails at launch with an
ASLR/personality error (typically `personality set failed: Operation not
permitted`, or the process failing to launch). Cause: lldb *disables ASLR by
default* for debuggee launches via `personality(ADDR_NO_RANDOMIZE)`, and the
container's seccomp policy blocks that `personality()` call for rootless
podman.

Bill has already fixed this in other projects (not mounted this session —
`lldbassemblyhelper` is the most likely reference; also check `apue`). The fix
there reportedly lives in the `.lldbinit` dotfile.

## Fix

Add to `entrypoint/dotfiles/.lldbinit` (currently it has only the two
`stop-line-count-*` settings):

```
settings set target.disable-aslr false
```

This tells lldb not to attempt the `personality()` call at all — the debuggee
just runs with ASLR on, which is fine for our debugging (spim is a
non-PIE-sensitive workflow; breakpoints resolve regardless).

Before applying, diff against the working `.lldbinit` in the reference project
to pick up the exact prior wording/any companion settings.

## Alternatives considered

- `podman run --security-opt seccomp=unconfined` (or a custom seccomp profile
  allowing `personality`): heavier hammer, weakens the sandbox, and has to be
  threaded through every Makefile target. The `.lldbinit` line is local and
  sufficient.

## Verification

Inside `make shell`:

```sh
lldb /spimulator/builddir/spimulator -o 'b main' -o run -o 'bt 3' -o quit
```

should hit the breakpoint with no `personality set failed` error. Also confirm
plain `run` of a demo under lldb works.
