# Give every named binding in the Haskell port an explicit type, then write a Haskell coding standard

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branches:**
- spimulator `haskell-annotate-bindings`, off `opcodes-generator`;
- runClaudeInContainer `haskell-coding-standard`, off `master`;
- runCrushInContainer `haskell-coding-standard`, off `master`.

## Outcome

- **Every do-bind and `let`/`where` binding in `port/haskell/src/` has a written type:** 1,235
  bindings (882 do-binds, 329 local bindings, 24 local tuple bindings).
- **A meson test keeps it so:** `haskell-annotations` in the port-haskell suite runs
  `port/haskell/tests/check-annotations.sh`, which uses `port/haskell/tools/BindingTypes.hs`.
- **The port behaves as before:** port-haskell 72 OK, including the whole-tree comparison with the
  C engine.
- **The shared Haskell coding standard** is `tasks/reference/haskell-coding-standard.md` in
  github.com/billsix/runClaudeInContainer, mirrored twice in github.com/billsix/runCrushInContainer.
  Both sandboxes' `CLAUDE.md` files tell an agent to read it before writing Haskell. The port's
  README points to it.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

- **Which bindings carry a type: option (b) of two.**
  - **Required:** do-binds and `let`/`where` bindings, including local tuple bindings.
  - **Exempt by default:** pattern variables, as the Python standard exempts loop and unpack
    targets. The other option, (a), would have typed every pattern variable too; it was not
    chosen because a pattern variable's type is already fixed by the constructor or signature next
    to it. The exempt forms, all listed in the standard:
    - lambda parameters (47 lambdas);
    - variables bound by `case` alternatives (151 alternatives);
    - variables bound by function-argument patterns;
    - pattern-guard binds (6);
    - list-comprehension generators (none).
- **Linters and formatters:** the maintainer said to install what is needed and make sure both
  sandboxes always install it. `hlint` and `ormolu` went into both `01-install-base.sh` lists, and
  were committed by the maintainer before this branch.

## How

1. **The inferred types come from GHC, not from hand reading.**
   `port/haskell/tools/BindingTypes.hs` reads the `.hie` files of a `-fwrite-ide-info` build. They
   hold each identifier's binding kind, span and type. The tool lists every required binding that
   has no written type. The list before any edit is
   `tasks/adhoc/haskell-port-annotate-every-binding/data/missing-before.tsv`.
2. **`annotate.py` wrote the types in.** Then `typecheck-loop.sh` type-checked (`-fno-code`) and
   ran three fixers on GHC's errors until it was clean (all under
   `tasks/adhoc/haskell-port-annotate-every-binding/`):
   - `scope_foralls.py` added a `forall` where a local signature names the enclosing function's
     type variable.
   - `add_imports.py` added the imports and qualifiers new signatures needed.
   - `add_constraints.py` added the class constraints GHC asked for.
3. **The 41 polymorphic local types were done separately and reviewed by hand,** because a `.hie`
   type omits class constraints and can print two distinct type variables with the same name.
   Locals used at one type got that concrete type; the rest keep GHC's constraints, with
   redundant ones removed.
4. **By hand:**
   - a one-line `let` with four `;`-separated bindings was split into four typed bindings;
   - a pattern signature `n :: a`, which compiled only because a pattern signature may bind a
     fresh type variable, became `n :: CInt`.

What this taught is written into the standard ("Getting the types right" and "Enforce it with a
checker").

## Verification

- The port builds with `-Wall -Werror`, and port-haskell gives 72 OK, including `haskell-annotations`,
  every regression and demo test against the port, and the whole-tree comparison with the C engine.
- `check-annotations.sh` passes on the result. With one annotation removed it fails and names the
  binding.
- The two runCrushInContainer copies of the standard are byte-identical to the canonical.
