-- | List the bindings of the Haskell port that must carry a written type and
-- do not, with the type GHC inferred for each.
--
-- The rule (tasks/archive/2026/11/11/haskell-port-annotate-every-binding.md, Decisions): every
-- do-bind, every @let@ or @where@ binding, and every local pattern binding has
-- a written type; lambda, case-alternative, argument-pattern, pattern-guard
-- and list-comprehension variables are exempt.  The facts come from the
-- @.hie@ files GHC writes with @-fwrite-ide-info@: each identifier's binding
-- context, source span and type.
--
-- Usage:
--
-- > binding-types LIBDIR HIEDIR          -- list what is missing
-- > binding-types LIBDIR HIEDIR --check  -- also exit 1 if anything is
-- >                                      -- missing
--
-- LIBDIR is @ghc --print-libdir@.  Each line printed is tab-separated: the
-- kind (@do@, @local@ or @pattern@), the source file, the start line and
-- column, the end line and column, the name (for @pattern@, the bound names
-- separated by spaces), and the inferred type (for @pattern@, the names'
-- types separated by @;;@).  Columns are 1-based; an end column is one past
-- the last character.
module Main (main) where

import Data.Array (Array)
import Data.List (intercalate, isSuffixOf, nub, sortOn)
import qualified Data.Map.Strict as Map
import Data.Maybe (listToMaybe)
import qualified Data.Set as Set
import GHC (getSessionDynFlags, runGhc)
import GHC.Data.FastString (unpackFS)
import GHC.Driver.Session (DynFlags)
import GHC.Iface.Ext.Binary (HieFileResult (..), readHieFile)
import GHC.Iface.Ext.Types
import GHC.Iface.Ext.Utils (recoverFullType, renderHieType)
import GHC.Types.Name (Name, nameOccName)
import GHC.Types.Name.Cache (NameCache, initNameCache)
import GHC.Types.Name.Occurrence (occNameString)
import GHC.Types.SrcLoc (srcSpanEndCol, srcSpanEndLine, srcSpanStartCol, srcSpanStartLine)
import System.Directory (listDirectory)
import System.Environment (getArgs)
import System.Exit (exitFailure)
import System.FilePath ((</>))

-- | One binding (or pattern) without a written type.
data Missing = Missing
  { missingKind :: String
  , missingFile :: FilePath
  , missingSpan :: Span
  , missingName :: String
  , missingType :: String
  }

main :: IO ()
main = do
  args :: [String] <- getArgs
  (libdir, hieDir, check) :: (FilePath, FilePath, Bool) <- case args of
    [l, h] -> pure (l, h, False)
    [l, h, "--check"] -> pure (l, h, True)
    _ -> fail "usage: binding-types LIBDIR HIEDIR [--check]"
  files :: [FilePath] <- listDirectory hieDir
  cache :: NameCache <- initNameCache 'h' []
  dflags :: DynFlags <- runGhc (Just libdir) getSessionDynFlags
  found :: [[Missing]] <-
    mapM
      (\file -> missingIn dflags . hie_file_result <$> readHieFile cache (hieDir </> file))
      (filter (".hie" `isSuffixOf`) files)
  let missing :: [Missing]
      missing = sortOn position (concat found)
  mapM_ (putStrLn . render) missing
  if check && not (null missing)
    then do
      putStrLn (show (length missing) ++ " binding(s) without a written type")
      exitFailure
    else pure ()
  where
    position :: Missing -> (FilePath, Int, Int)
    position m = (missingFile m, srcSpanStartLine (missingSpan m), srcSpanStartCol (missingSpan m))
    render :: Missing -> String
    render m =
      intercalate
        "\t"
        [ missingKind m
        , missingFile m
        , show (srcSpanStartLine s)
        , show (srcSpanStartCol s)
        , show (srcSpanEndLine s)
        , show (srcSpanEndCol s)
        , missingName m
        , missingType m
        ]
      where
        s :: Span
        s = missingSpan m

-- | An AST node with the annotations of every node above it, nearest first.
data Located = Located
  { locNode :: HieAST TypeIndex
  , locAbove :: [Set.Set NodeAnnotation]
  }

-- | Every node of a tree, each with its ancestors' annotations.
nodes :: HieAST TypeIndex -> [Located]
nodes = go []
  where
    go :: [Set.Set NodeAnnotation] -> HieAST TypeIndex -> [Located]
    go above node =
      Located node above : concatMap (go (annotations node : above)) (nodeChildren node)

-- | The node infos of a node, from every origin GHC recorded.
infos :: HieAST TypeIndex -> [NodeInfo TypeIndex]
infos = Map.elems . getSourcedNodeInfo . sourcedNodeInfo

-- | The annotations of a node.
annotations :: HieAST TypeIndex -> Set.Set NodeAnnotation
annotations = Set.unions . map nodeAnnotations . infos

-- | Whether a set of annotations has one with this constructor name.
hasConstr :: String -> Set.Set NodeAnnotation -> Bool
hasConstr constr = any ((== constr) . unpackFS . nodeAnnotConstr) . Set.toList

-- | The names a node binds or uses directly, with their details, without
-- repeats (a node records each name once per origin).
identifiers :: HieAST TypeIndex -> [(Name, IdentifierDetails TypeIndex)]
identifiers node =
  Map.toList
    ( Map.fromListWith
        (<>)
        [(name, details) | info <- infos node, (Right name, details) <- Map.toList (nodeIdentifiers info)]
    )

-- | Every variable a pattern binds, in source order, with its type.
patternVariables :: HieAST TypeIndex -> [(Name, TypeIndex)]
patternVariables pat =
  nub
    [ (name, ix)
    | l <- nodes pat
    , (name, details) <- identifiers (locNode l)
    , any isPatternBind (identInfo details)
    , Just ix <- [identType details]
    ]
  where
    isPatternBind :: ContextInfo -> Bool
    isPatternBind (PatternBind {}) = True
    isPatternBind _ = False

-- | The bindings in one module that need a type and have none.
missingIn :: DynFlags -> HieFile -> [Missing]
missingIn dflags hie = doBinds ++ patternBinds ++ localBinds
  where
    file :: FilePath
    file = hie_hs_file hie
    types :: Array TypeIndex HieTypeFlat
    types = hie_types hie
    everything :: [Located]
    everything = concatMap nodes (Map.elems (getAsts (hie_asts hie)))
    -- A type as source text: on one line, with String for [Char].
    typeText :: TypeIndex -> String
    typeText ix = replace "[Char]" "String" (unwords (words (renderHieType dflags (recoverFullType ix types))))
    nameText :: Name -> String
    nameText = occNameString . nameOccName
    -- Names with a type signature somewhere in the module.
    signed :: Set.Set Name
    signed =
      Set.fromList
        [ name
        | l <- everything
        , (name, details) <- identifiers (locNode l)
        , TyDecl `Set.member` identInfo details
        ]

    -- do-binds: the pattern (first child) of a BindStmt whose nearest
    -- statement container is a do block (not a guard), unless the pattern is
    -- a SigPat already or a wildcard.
    doBinds :: [Missing]
    doBinds =
      [ Missing "do" file (nodeSpan pat) (unwords (map (nameText . fst) (patternVariables pat))) (typeText ix)
      | l <- everything
      , hasConstr "BindStmt" (annotations (locNode l))
      , inDoBlock (locAbove l)
      , Just pat <- [listToMaybe (nodeChildren (locNode l))]
      , not (isSigned pat)
      , not (hasConstr "WildPat" (annotations pat))
      , Just ix <- [listToMaybe (concatMap nodeType (infos pat))]
      ]
    -- A SigPat, or a parenthesized one: (Just x :: Maybe Int).
    isSigned :: HieAST TypeIndex -> Bool
    isSigned pat =
      hasConstr "SigPat" (annotations pat)
        || (hasConstr "ParPat" (annotations pat) && any (hasConstr "SigPat" . annotations) (nodeChildren pat))
    inDoBlock :: [Set.Set NodeAnnotation] -> Bool
    inDoBlock above =
      maybe False (hasConstr "HsDo") (listToMaybe (filter (\a -> hasConstr "HsDo" a || hasConstr "GRHS" a) above))

    -- Local pattern bindings, (hi, lo) = e in a let or where: every bound
    -- variable without a signature, reported at the binding's start.
    patternBinds :: [Missing]
    patternBinds =
      [ Missing
          "pattern"
          file
          (nodeSpan (locNode l))
          (unwords (map (nameText . fst) unsigned))
          (intercalate ";;" (map (typeText . snd) unsigned))
      | l <- everything
      , hasConstr "PatBind" (annotations (locNode l))
      , not (null (locAbove l)) -- a top-level pattern binding has a signature by -Wall
      , Just pat <- [listToMaybe (nodeChildren (locNode l))]
      , let unsigned :: [(Name, TypeIndex)]
            unsigned = filter ((`Set.notMember` signed) . fst) (patternVariables pat)
      , not (null unsigned)
      , not (isVariable pat)
      ]
    -- A PatBind whose pattern is a single variable is a value binding,
    -- reported by localBinds.
    isVariable :: HieAST TypeIndex -> Bool
    isVariable pat = hasConstr "VarPat" (annotations pat)

    -- let/where bindings: local value bindings (functions and variables)
    -- without a signature, each reported once, at its first clause.
    localBinds :: [Missing]
    localBinds =
      Map.elems
        ( Map.fromListWith
            earlier
            [ (name, Missing "local" file (nodeSpan (locNode l)) (nameText name) (typeText ix))
            | l <- everything
            , (name, details) <- identifiers (locNode l)
            , name `Set.notMember` signed
            , any isLocalValue (identInfo details)
            , Just ix <- [identType details]
            ]
        )
    isLocalValue :: ContextInfo -> Bool
    isLocalValue (ValBind RegularBind (LocalScope _) _) = True
    isLocalValue _ = False
    earlier :: Missing -> Missing -> Missing
    earlier a b =
      if (srcSpanStartLine (missingSpan a), srcSpanStartCol (missingSpan a))
        <= (srcSpanStartLine (missingSpan b), srcSpanStartCol (missingSpan b))
        then a
        else b

-- | Replace every occurrence of a substring.
replace :: String -> String -> String -> String
replace old new = go
  where
    go :: String -> String
    go [] = []
    go s@(c : rest)
      | take (length old) s == old = new ++ go (drop (length old) s)
      | otherwise = c : go rest
