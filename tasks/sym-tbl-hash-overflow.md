# Fix signed-int overflow in symbol-table hash

## Status — not started

Filed during the post-C23-sweep ASan/UBSan audit (May 2026).
Pre-existing — predates the C23 work, surfaces immediately for
any non-trivial symbol name.

## Bug

`src/sym-tbl.c:76`:

```c
for (i = 0; i < len; i++) hi = ((hi * 613) + (unsigned)(name[i]));
```

`hi` is declared `int`.  For symbol names of even modest length,
`hi * 613` overflows a 32-bit signed int.  Signed integer
overflow is undefined behavior in C — the program happens to
work because `-fwrapv` semantics are usually what GCC gives
under `-O0`, but `-O2` is free to assume "this never overflows"
and miscompile the surrounding code.

UBSan flag at every spim startup:

```
src/sym-tbl.c:76:39: runtime error: signed integer overflow:
   1187488384 * 613 cannot be represented in type 'int'
```

The hash is computed for `__start` (and several other built-in
labels) during `initialize_world`, so every spim invocation
hits it.

## Root cause

`hi` should be `unsigned`.  Hash functions traditionally use
unsigned arithmetic precisely because overflow is defined to
wrap.  The cast on the right operand (`(unsigned)(name[i])`)
shows the original author was thinking about it but didn't
follow through to the accumulator.

Code was written by jameslarus, June 2023 — predates the
hand-written parser migration, the AST work, and the C23 sweep.

## Fix

Change `hi`'s type from `int` to `unsigned int` (or `uint32_t`),
and drop the now-unnecessary `(unsigned)` cast on the name byte:

```c
unsigned hi;
...
for (i = 0; i < len; i++) hi = (hi * 613u) + name[i];
```

Audit the function's other callers — `get_hash` takes `hi` as
an `int*` out-parameter that's used to index `label_hash_table`.
Change the out-param type to `unsigned*` (or keep `int*` but
cast at assignment).  Check `lookup_label`, `label_is_defined`,
`record_label`, `for_each_label` for consistency.

Probably a 5-line change overall.

## Verification

1. Rebuild under ASan+UBSan
   (`meson setup -Db_sanitize=address,undefined builddir-asan`).
2. Run `./builddir-asan/spimulator -f tests/tt.argv.s ...`.
3. Confirm no `signed integer overflow` finding from `sym-tbl.c`.
4. Confirm 22/22 regression tests still pass under both normal
   and ASan builds.

There's no existing regression test that specifically exercises
the hash function — the test would have to depend on
implementation details.  The before/after test is "ASan no
longer reports this site."

## Why this matters

The hash function is called on every label lookup — every time
the parser sees an identifier, every label-resolution call,
every breakpoint lookup, every REPL completion. The current UB
is masked by GCC's pragmatic `-fwrapv`-ish behavior at `-O0`
but could silently miscompile at `-O2` if the optimizer decides
to use the "overflow can't happen" assumption to elide a check
elsewhere.

Not currently causing user-visible breakage; this is a
correctness-hygiene fix that closes a UB landmine.

## Effort

Trivial — 5 lines including caller-signature updates.  Risk:
none if the audit covers all `get_hash` callers.
