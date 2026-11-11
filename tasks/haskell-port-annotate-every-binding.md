# Give every named binding in the Haskell port an explicit type, then write a Haskell coding standard

**Status:** ready — decision made, implementation not started
**Priority:** 6
**Difficulty:** 5
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)

## BLUF

The Haskell port (`port/haskell/`) gives every top-level binding a type signature, which `-Wall
-Werror` already enforces. No local binding has a type: do-binds such as `s <- newSpim`, `let` and
`where` bindings, and lambda and case-pattern variables. Haskell can type all of them. The
maintainer chose to require a type on every do-bind and `let`/`where` binding, as the Python coding
standard does for Python, and to exempt pattern variables; the exempt forms are listed in Decisions.
Then,
from what the typing teaches, write a Haskell coding standard for github.com/billsix/runClaudeInContainer
and github.com/billsix/runCrushInContainer, modeled on their `python-coding-standard.md`. Done
means:
- every do-bind and `let`/`where` binding in the port has a type;
- a checker in the port's test suite keeps it that way;
- the port behaves as before;
- the standard exists in both sandbox repos, with a read-before-writing trigger in each `CLAUDE.md`.

## Context

- **The example that prompted this,** `port/haskell/src/Main.hs` `main`:

  ```haskell
  main :: IO ()
  main = do
    setupHandles
    s <- newSpim
    args <- getArgs
    listingRef <- newIORef NoListing
    let closeListing = do
          l <- readIORef listingRef
          case l of
            ListingTo h -> hClose h
            _ -> pure ()
    (spimMain s args listingRef `catch` \RunErrorJump -> exitSpim 1) `finally` closeListing
  ```

  `s`, `args`, `listingRef`, `closeListing`, `l` and `h` have no written type.
- **Every kind of binding can carry a type** with GHC 9.10.3's default language GHC2021, which
  includes `ScopedTypeVariables`. No `LANGUAGE` pragma is needed.
  - do-bind: `s :: Spim <- newSpim`, a pattern signature;
  - `let` or `where`: a signature line above the binding, `closeListing :: IO ()`;
  - lambda: `\(x :: Int) -> …`;
  - case pattern: `ListingTo (h :: Handle) -> …`.

  A local signature that mentions the enclosing function's type variables needs an explicit
  `forall` in the enclosing signature, which brings them into scope.
- **Size** (counted with `grep`, so approximate): 7,628 lines in 23 modules under `src/`.
  - 833 do-binds, none typed;
  - 128 lines starting a `let`, and 78 `where` blocks; 28 local signatures exist already;
  - 47 lambdas, plus the variables in case alternatives.
- **The build:** `port/haskell/build.sh` runs `ghc --make -O1 -Wall -Werror`. The port is not a
  cabal package (`port/haskell/README.md` says why).
- **Tools available:**
  - The GHC API is installed: `ghc -package ghc` works, and the package is `ghc-ghc-devel`. A
    program can parse each module, list every binder that has no type, and, after type checking,
    print the inferred type of each one with its source position. That makes a codemod (insert the
    inferred types) and a checker.
  - `hlint` 3.10 (linter) and `ormolu` 0.7.7 (formatter) are installed in the sandbox and in both
    sandboxes' `entrypoint/01-install-base.sh` lists (added 2026-11-11, at the maintainer's word:
    "if you need haskell linters and formatters, you can dnf install them"). They are candidates
    for the standard's tool-enforced tier. The spimulator image does not ship them; adding them to
    `entrypoint/05-install-ports.sh` is part of making either one a gate.
- **The model for the standard** is the `python-coding-standard.md` reference doc of
  github.com/billsix/runClaudeInContainer, mirrored in runCrushInContainer:
  - **(a)** what tools enforce;
  - **(b)** the judgment calls, and above all its "Type annotations — annotate every binding"
    section.

  That section's rule: every binding gets a type, and loop, unpack, `with`, and `except` targets are
  exempt by default because Python cannot annotate them inline. Its checker,
  `check_local_annotations.py`, is vendored into projects. Haskell can annotate its pattern
  variables, so the analog of that exemption is a choice, not a limit: Decisions.

## Decisions

- **Which bindings must carry a type** (William Emerison Six <billsix@gmail.com>, 2026-11-11, option
  (b) of two). Do-binds and `let`/`where` bindings always carry one. Pattern variables are exempt
  by default, as the Python standard exempts loop and unpack targets. The other option was (a),
  typing every pattern variable too. It was not chosen because a pattern variable's type is already
  fixed by the constructor or signature beside it, so writing it again adds noise to every `case`.
  The checker enforces the required forms only. The written standard lists the exempt forms, as
  agreed, so a reader knows they were left out on purpose.

  **Required** (the checker fails without a type):
  - **do-binds:** `s :: Spim <- newSpim`. A tuple or constructor pattern takes one signature for
    the whole pattern, `(a, b) :: (Int, Int) <- action`; there are 26 tuple do-binds today.
  - **`let` and `where` bindings,** functions and values alike, by a signature line above:
    `closeListing :: IO ()`.
  - **Local pattern bindings,** `(hi, lo) = …` in a `let` or `where` (22 today), by one signature line
    per variable above the binding.

  **Omitted** (exempt by default; typed only where that helps a reader, and the checker does not
  ask):
  - **Lambda parameters,** as in `\RunErrorJump -> exitSpim 1` and `\x -> …` (47 lambdas). The
    function the lambda is passed to fixes their type.
  - **Variables bound by a `case` alternative,** as in `ListingTo h -> hClose h` (151 alternatives
    bind variables). The constructor fixes their type.
  - **Variables bound by a function's argument patterns,** top-level or local. The function's own
    signature types them.
  - **Pattern-guard binds,** `| Just x <- lookup k m` (6 today). The guarded expression fixes the
    type.
  - **List-comprehension generators,** `[x | x <- xs]` (none today). The list fixes the type.

## Plan

1. **Write the inferred-type tool** in `port/haskell/tools/`, using the GHC API. It lists every
   untyped binder of the required forms (Decisions), with the type GHC inferred, so annotations come from the
   compiler, not by hand.
2. **Annotate module by module**, one commit each, smallest first. Write the inferred type exactly:
   a different type can change behavior, such as a numeric literal defaulting to `Integer` instead
   of `Int`, or a lost polymorphism. Add `forall` where a local signature needs the outer type
   variables.
3. **Verify each commit:** `port/haskell/build.sh` (`-Wall -Werror`), `meson test --suite
   port-haskell`, and `port/harness/compare_all_programs.py` against the C engine. Behavior must
   not change.
4. **Make the tool a checker** (`--check`, exit 1 on any untyped binder) and register it in
   `port/meson.build` as a `haskell-annotations` test.
5. **Write the Haskell coding standard** as `tasks/reference/haskell-coding-standard.md` in
   runClaudeInContainer:
   - the same (a)/(b) split as the Python doc, and a type-annotation section in the Python
     section's words, changed only where Haskell differs (the forms listed in Context, the `forall`
     rule, and the omitted forms from Decisions, listed as exempt);
   - the other judgment calls the typing turns up.

   Mirror it in runCrushInContainer, at `tasks/reference/` and the client's
   `client/entrypoint/dotfiles/.config/crush/reference/`. Add a "read before writing or reviewing
   Haskell" trigger to each `CLAUDE.md` (runClaude's `entrypoint/dotfiles/.claude/CLAUDE.md`
   reference list and "Python, C++, and C" section; runCrush's client `CLAUDE.md` "Code
   conventions"). If `hlint` or `ormolu` becomes a gate, add it to
   spimulator's `entrypoint/05-install-ports.sh` too.
6. **Record in the done-record** what the typing taught that went into the standard, and any
   binding left untyped with its reason.
