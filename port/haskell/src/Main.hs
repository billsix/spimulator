{-# LANGUAGE ForeignFunctionInterface #-}

-- | The command line and the REPL (C: src/spim.c).
--
-- @spimulator-haskell@ takes the C engine's flags: @-f file@ (repeatable)
-- assembles files and runs them; with no file it starts the interactive
-- REPL.  The teaching mode (@-explain@) is not part of this port.
module Main (main) where

import Control.Exception (catch, finally)
import Control.Monad (forM_, unless, when)
import Control.Monad.IO.Class (liftIO)
import Data.Bits (shiftR, (.&.))
import Data.Char (chr, ord, toLower)
import Data.Int (Int32)
import Data.IORef (IORef, newIORef)
import Data.List (isPrefixOf)
import Data.Word (Word32, Word8)
import qualified Data.ByteString.Char8 as B
import AsmCommon (currentTextPc)
import Assemble (userKernelTextSegment)
import CFormat (formatG)
import DataSegment (userKernelDataSegment)
import DisplayUtils
import Foreign.C.Error (getErrno, Errno (..))
import Foreign.C.String (CString, peekCAString)
import Foreign.C.Types (CInt (..))
import GHC.Float (float2Double)
import InstFormat
import Instruction (instEncodeIO)
import Memory
import Output
import Parser (parseFile)
import Registers
import Scanner
import SpimUtils
import State
import SymbolTable
import System.Directory (doesFileExist)
import System.Environment (getArgs, getExecutablePath, lookupEnv)
import System.FilePath (takeDirectory, (</>))
import System.IO (Handle, IOMode (..), hClose, hFlush, hIsTerminalDevice, hSetBinaryMode, openFile, stderr, stdin)
import qualified System.Console.Haskeline as H
import Text.Printf (printf)
import Data.IORef (readIORef, writeIORef)

foreign import ccall unsafe "strerror" c_strerror :: CInt -> IO CString

-- | Where the listing goes, if @-listing@ was given.
data Listing = NoListing | ListingTo Handle | ListingStderr

-- | What the command line asked for.
data Options = Options
  { exceptionFile :: Maybe String -- ^ Nothing: the default handler
  , loadHandler :: Bool
  , dumpUser :: Bool
  , dumpAll :: Bool
  , assembleOnly :: Bool
  }

main :: IO ()
main = do
  setupHandles
  s :: Spim <- newSpim
  args :: [String] <- getArgs
  listingRef :: IORef Listing <- newIORef NoListing
  let closeListing :: IO ()
      closeListing = do
        l :: Listing <- readIORef listingRef
        case l of
          ListingTo h -> hClose h
          _ -> pure ()
  (spimMain s args listingRef `catch` \RunErrorJump -> exitSpim 1) `finally` closeListing

-- | The default exception handler: src/exceptions.s, found by walking up
-- from the directory of this program until a directory holds it.  Its
-- name is shown as the C engine shows its built-in copy.
defaultHandler :: IO ExceptionFiles
defaultHandler = do
  exe :: String <- getExecutablePath
  found :: Maybe String <- search (takeDirectory exe) (12 :: Int)
  case found of
    Just path -> do
      text :: String <- B.unpack <$> B.readFile path
      pure (DefaultHandler text "<embedded exceptions.s>")
    Nothing -> fatalError "Cannot parse embedded exception handler\n"
  where
    search :: String -> Int -> IO (Maybe String)
    search _ 0 = pure Nothing
    search dir n = do
      let candidate :: String
          candidate = dir </> "src" </> "exceptions.s"
      ok :: Bool <- doesFileExist candidate
      if ok
        then pure (Just candidate)
        else
          let up :: String
              up = takeDirectory dir
           in if up == dir then pure Nothing else search up (n - 1)

-- | The handler to load, from the options.
handlerFor :: Options -> IO ExceptionFiles
handlerFor o
  | not (loadHandler o) = pure NoHandler
  | otherwise = case exceptionFile o of
      Nothing -> defaultHandler
      Just "" -> pure NoHandler
      Just f -> pure (HandlerFiles f)

-- | The usage message for a bad flag.
usage :: String
usage =
  "Usage: spim\n\
  \\t-bare\t\t\tBare machine (no pseudo-ops, delayed branches and loads)\n\
  \\t-asm\t\t\tExtended machine (pseudo-ops, no delayed branches and loads) (default)\n\
  \\t-delayed_branches\tExecute delayed branches\n\
  \\t-delayed_loads\t\tExecute delayed loads\n\
  \\t-exception\t\tLoad exception handler (default)\n\
  \\t-noexception\t\tDo not load exception handler\n\
  \\t-exception_file <file>\tSpecify exception handler in place of default\n\
  \\t-quiet\t\t\tDo not print warnings\n\
  \\t-noquiet\t\tPrint warnings (default)\n\
  \\t-explain [N]\t\tNarrate every instruction (teaching mode).\n\
  \\t\t\t\tLevel N selects detail: 1 = semantic, 2 = + interactive,\n\
  \\t\t\t\t3 = + bit-layout (default when N omitted; historical behavior),\n\
  \\t\t\t\t4 = + progressive decoder walkthrough.\n\
  \\t\t\t\tAlso accepts -explain=N. Short form: -x.\n\
  \\t-noexplain\t\tDisable teaching mode (default)\n\
  \\t-mapped_io\t\tEnable memory-mapped IO\n\
  \\t-nomapped_io\t\tDo not enable memory-mapped IO (default)\n\
  \\t-file <file> [-file <file> ...] <args>\n\
  \\t\t\t\tAssembly code file(s) and arguments to program.\n\
  \\t\t\t\tMultiple -file (or -f) options accumulate into one\n\
  \\t\t\t\tsymbol table; forward references resolve across files.\n\
  \\t-assemble\t\tWrite assembled code to <file>.out\n\
  \\t-listing <file>\t\tWrite assemble-time event trace to <file> (use - for stderr)\n\
  \\t-print-ast\t\tParse to AST, print it to stderr, and exit without emitting any code\n\
  \\t-print-ast-json\t\tParse to AST, dump JSON to stdout (for tooling), and exit\n\
  \\t-show-expansion\t\tParse to AST, print just the pseudo-op wrappers + their expansion, and exit\n\
  \\t-dump\t\t\tWrite user data and text segments into files\n\
  \\t-full_dump\t\tWrite user and kernel data and text into files.\n"

-- | Loading state while the arguments are read.
data Loading = Loading
  { loaded :: Bool
  , attempted :: Bool
  , worldReady :: Bool
  , lastFile :: Int
  , programArgs :: [String]
  , badUsage :: Bool
  }

-- | Read the arguments in order, loading files as they come, then act
-- (C: @main@).
spimMain :: Spim -> [String] -> IORef Listing -> IO ()
spimMain s argv listingRef = do
  envHandler :: Maybe String <- lookupEnv "SPIM_EXCEPTION_HANDLER"
  let opts0 :: Options
      opts0 = Options envHandler True False False False
      n :: Int
      n = length argv
      arg :: Int -> Maybe String
      arg k = if k < n then Just (argv !! k) else Nothing
      setB :: (Spim -> IORef a) -> a -> IO ()
      setB ref v = set (ref s) v
      int :: Int -> Int
      int k = maybe 0 atoi (arg k)
      sizes :: ((Int, Int, Int, Int, Int, Int, Int, Int) -> (Int, Int, Int, Int, Int, Int, Int, Int)) -> IO ()
      sizes f = modify (initialSizes s) f
      loop :: Int -> Options -> Loading -> IO (Options, Loading)
      loop i o ld
        | i >= n = pure (o, ld)
        | otherwise = do
            let a :: String
                a = argv !! i
                next :: Options -> IO (Options, Loading)
                next o' = loop (i + 1) o' ld
                -- A flag that takes a value, given last: say so, and show
                -- the usage (C: flag_value_missing).
                withValue :: IO (Options, Loading) -> IO (Options, Loading)
                withValue act
                  | i + 1 < n = act
                  | otherwise = do
                      cError ("spimulator: " ++ a ++ " requires a value\n")
                      loop (i + 1) o ld {badUsage = True}
                is :: [String] -> Bool
                is xs = a `elem` xs
            case () of
              _
                | is ["-asm", "-a"] -> setB optBare False >> setB optDelayedBranches False >> setB optDelayedLoads False >> next o
                | is ["-bare", "-b"] -> setB optBare True >> setB optDelayedBranches True >> setB optDelayedLoads True >> setB optQuiet True >> next o
                | is ["-delayed_branches", "-db"] -> setB optDelayedBranches True >> next o
                | is ["-delayed_loads", "-dl"] -> setB optDelayedLoads True >> next o
                | is ["-exception", "-e", "-trap", "-t"] -> next o {loadHandler = True}
                | is ["-noexception", "-ne", "-notrap", "-nt"] -> next o {loadHandler = False}
                | is ["-exception_file", "-ef", "-trap_file", "-tf"] ->
                    withValue $ loop (i + 2) o {exceptionFile = Just (maybe "" id (arg (i + 1))), loadHandler = True} ld
                | is ["-mapped_io", "-mio"] -> setB optMappedIo True >> next o
                | is ["-nomapped_io", "-nmio"] -> setB optMappedIo False >> next o
                | is ["-pseudo", "-p"] -> setB optAcceptPseudo True >> next o
                | is ["-nopseudo", "-np"] -> setB optAcceptPseudo False >> next o
                | is ["-quiet", "-q"] -> setB optQuiet True >> next o
                | is ["-noquiet", "-nq"] -> setB optQuiet False >> next o
                | is ["-noexplain", "-nx"] -> next o
                | is ["-explain", "-x"] || "-explain=" `isPrefixOf` a || "-x=" `isPrefixOf` a -> do
                    writeErr "spimulator-haskell: the teaching mode (-explain) is not supported by this port\n"
                    exitSpim 2
                | is ["-stext", "-st"] -> withValue $ sizes (\(_, b, c, d, e, f, g, h) -> (int (i + 1), b, c, d, e, f, g, h)) >> loop (i + 2) o ld
                | is ["-sdata", "-sd"] -> withValue $ sizes (\(t, _, c, d, e, f, g, h) -> (t, int (i + 1), c, d, e, f, g, h)) >> loop (i + 2) o ld
                | is ["-ldata", "-ld"] -> withValue $ sizes (\(t, b, _, d, e, f, g, h) -> (t, b, int (i + 1), d, e, f, g, h)) >> loop (i + 2) o ld
                | is ["-sstack", "-ss"] -> withValue $ sizes (\(t, b, c, _, e, f, g, h) -> (t, b, c, int (i + 1), e, f, g, h)) >> loop (i + 2) o ld
                | is ["-lstack", "-ls"] -> withValue $ sizes (\(t, b, c, d, _, f, g, h) -> (t, b, c, d, int (i + 1), f, g, h)) >> loop (i + 2) o ld
                | is ["-sktext", "-skt"] -> withValue $ sizes (\(t, b, c, d, e, _, g, h) -> (t, b, c, d, e, int (i + 1), g, h)) >> loop (i + 2) o ld
                | is ["-skdata", "-skd"] -> withValue $ sizes (\(t, b, c, d, e, f, _, h) -> (t, b, c, d, e, f, int (i + 1), h)) >> loop (i + 2) o ld
                | is ["-lkdata", "-lkd"] -> withValue $ sizes (\(t, b, c, d, e, f, g, _) -> (t, b, c, d, e, f, g, int (i + 1))) >> loop (i + 2) o ld
                | (is ["-file", "-f"] && i + 1 < n) || take 1 a /= "-" -> do
                    let explicit :: Bool
                        explicit = take 1 a == "-"
                    if not explicit && loaded ld
                      then pure (o, ld)
                      else do
                        let fileI :: Int
                            fileI = if explicit then i + 1 else i
                        unless (worldReady ld) $ do
                          h :: ExceptionFiles <- handlerFor o
                          quiet :: Bool <- get (optQuiet s)
                          initializeWorld s h (not quiet)
                        let ld1 :: Loading
                            ld1 = ld {worldReady = True, attempted = True}
                        ok :: Bool <- readAssemblyFile s (argv !! fileI)
                        if not ok
                          then pure (o, ld1)
                          else do
                            let ld2 :: Loading
                                ld2 = ld1 {loaded = True, lastFile = fileI}
                            if explicit
                              then loop (fileI + 1) o ld2
                              else pure (o, ld2 {programArgs = drop fileI argv})
                | a == "-assemble" -> next o {assembleOnly = True}
                | a == "-print-ast" -> setB printAstFlag True >> setB printAstOnly True >> next o
                | a == "-show-expansion" -> setB showExpansionFlag True >> setB printAstOnly True >> next o
                | a == "-print-ast-json" -> setB printAstJsonFlag True >> setB printAstOnly True >> next o
                | a == "-listing" ->
                    case arg (i + 1) of
                      Nothing -> do
                        cError "\n-listing requires a filename argument\n"
                        loop (i + 1) o ld {badUsage = True}
                      Just path -> do
                        opened :: Maybe Listing <-
                          if path == "-"
                            then pure (Just ListingStderr)
                            else (Just . ListingTo <$> openListing path) `catch` \e -> const (pure Nothing) (e :: IOError)
                        case opened of
                          Nothing -> do
                            cError ("\nCannot open listing file '" ++ path ++ "'\n")
                            loop (i + 2) o ld {badUsage = True}
                          Just l -> do
                            writeIORef listingRef l
                            set (asmObserver s) (Just (listingObserver s l))
                            loop (i + 2) o ld
                | a == "-dump" -> next o {dumpUser = True}
                | a == "-full_dump" -> next o {dumpAll = True}
                | otherwise -> do
                    cError ("\nUnknown argument: " ++ a ++ " (ignored)\n")
                    loop (i + 1) o ld {badUsage = True}
  (o, ld0) :: (Options, Loading) <- loop 0 opts0 (Loading False False False (-1) [] False)
  let ld :: Loading
      ld = if loaded ld0 && null (programArgs ld0) then ld0 {programArgs = drop (lastFile ld0) argv} else ld0
  when (badUsage ld) $ cError usage
  ret :: Int32 <- get (spimReturnValue s)
  let orStatus :: Int -> IO a
      orStatus k = exitSpim (if ret /= 0 then fromIntegral ret else k)
  if not (loaded ld)
    then
      if attempted ld
        then orStatus 2
        else do
          h :: ExceptionFiles <- handlerFor o
          quiet :: Bool <- get (optQuiet s)
          initializeWorld s h (not quiet)
          initializeRunStack s (programArgs ld)
          topLevel s o (programArgs ld)
    else do
      errs :: Int <- get (parseErrorsSeen s)
      when (errs > 0) $ orStatus 2
      only :: Bool <- get (printAstOnly s)
      when only $ exitSpim 0
      if assembleOnly o
        then do
          bad :: Bool <- writeAssembledCode s (head' (programArgs ld))
          exitSpim (if bad then 1 else 0)
        else
          if dumpUser o || dumpAll o
            then do
              dumpDataSeg s (not (dumpUser o))
              dumpTextSeg s (not (dumpUser o))
              get (spimReturnValue s) >>= exitSpim . fromIntegral
            else do
              initializeRunStack s (programArgs ld)
              undefs :: Maybe String <- undefinedSymbolString s
              case undefs of
                Just names -> do
                  writeErr "The following symbols are undefined:\n"
                  writeErr names
                  writeErr "\n"
                  orStatus 1
                Nothing -> do
                  start :: Word32 <- findSymbolAddress s "__start"
                  _ <- runProgram s start maxBound False False `catch` \RunErrorJump -> pure (False, False)
                  pure ()
              first :: Int <- get (firstBadException s)
              rv :: Int32 <- get (spimReturnValue s)
              exitSpim (if first /= -1 && rv == 0 then 128 + first else fromIntegral rv)
  where
    head' :: [String] -> String
    head' (x : _) = x
    head' [] = ""
    openListing :: String -> IO Handle
    openListing path = do
      h :: Handle <- openFile path WriteMode
      hSetBinaryMode h True
      pure h

-- | C's @atoi@.
atoi :: String -> Int
atoi str =
  let t :: String
      t = dropWhile (`elem` " \t\n\v\f\r") str
      sign :: Int
      digits :: String
      (sign, digits) = case t of
        ('-' : r) -> (-1, r)
        ('+' : r) -> (1, r)
        _ -> (1, t)
   in fromIntegral (fromIntegral (sign * foldl (\acc c -> acc * 10 + (ord c - 48)) 0 (takeWhile (`elem` ['0' .. '9']) digits)) :: Int32)

-- | Write each assembler event to the listing (C: @listing_observer@).
listingObserver :: Spim -> Listing -> AsmEvent -> Int -> IO ()
listingObserver s listing ev line = do
  text :: String <- case ev of
    EvLabelDef addr name global ->
      pure (printf "line %4d  label  %-24s defined at 0x%08x%s\n" line name addr (if global then "  (global)" else "" :: String))
    EvTextInst addr enc -> do
      dis :: String <- instToString s addr
      pure (printf "line %4d  text   0x%08x  %-32s  0x%08x\n" line addr dis enc)
    EvDataByte addr v -> pure (printf "line %4d  data   0x%08x  byte    0x%02x\n" line addr (v .&. 0xff))
    EvDataHalf addr v -> pure (printf "line %4d  data   0x%08x  half    0x%04x\n" line addr (v .&. 0xffff))
    EvDataWord addr v -> pure (printf "line %4d  data   0x%08x  word    0x%08x\n" line addr (fromIntegral v :: Word32))
    EvDataDouble addr v -> pure (printf "line %4d  data   0x%08x  double  %s\n" line addr (formatG 18 v))
    EvDataString addr len nul ->
      pure (printf "line %4d  data   0x%08x  string  len=%d%s\n" line addr len (if nul then " (+\\0)" else "" :: String))
    EvForwardRef from name -> pure (printf "line %4d  fref   0x%08x  uses unresolved symbol '%s'\n" line from name)
    EvForwardResolved at name v ->
      pure (printf "line %4d  fres   0x%08x  patched '%s' = 0x%08x\n" line at name (fromIntegral v :: Word32))
  case listing of
    ListingTo h -> B.hPut h (B.pack text)
    ListingStderr -> writeErr text
    NoListing -> pure ()

-- | @-dump@: the user data segment, or with @-full_dump@ all data segments,
-- into data.asm (C: @dump_data_seg@).
dumpDataSeg :: Spim -> Bool -> IO ()
dumpDataSeg s kernelAlso = do
  text :: String <-
    if kernelAlso
      then formatDataSegs s
      else do
        top :: Word32 <- get (dataTop s)
        ("\tDATA\n" ++) <$> formatMem s dataBot top
  B.writeFile "data.asm" (B.pack text)

-- | The text segments into text.asm (C: @dump_text_seg@).
dumpTextSeg :: Spim -> Bool -> IO ()
dumpTextSeg s kernelAlso = do
  ttop :: Word32 <- get (textTop s)
  user :: String <- formatInsts s textBot ttop
  text :: String <-
    if kernelAlso
      then do
        kttop :: Word32 <- get (kTextTop s)
        kernel :: String <- formatInsts s kTextBot kttop
        pure (user ++ "\n\tKERNEL\n" ++ kernel)
      else pure ("\n\tUSER TEXT SEGMENT\n" ++ user)
  B.writeFile "text.asm" (B.pack text)

-- | @-assemble@: the user text and data as @.word@ lists in FILE.out
-- (C: @write_assembled_code@).  True means it failed.
writeAssembledCode :: Spim -> String -> IO Bool
writeAssembledCode s name = do
  err :: Bool <- get (parseErrorOccurred s)
  if err
    then pure True
    else do
      userKernelTextSegment s False
      start :: Word32 <- findSymbolAddress s "__eoth"
      end :: Word32 <- currentTextPc s
      let addrsTo :: Word32 -> Word32 -> [Word32]
          addrsTo a b = takeWhile (< b) [a, a + 4 ..]
      codes :: [Int32] <- mapM (\a -> memReadInst s a >>= maybe (pure 0) (\r -> get r >>= instEncodeIO)) (addrsTo start end)
      -- C prints ", " after every word but the one at end - 4.
      let list :: Word32 -> Word32 -> [Int32] -> String
          list from to xs = concat (zipWith (\a c -> printf "0x%x" (fromIntegral c :: Word32) ++ (if a /= to - 4 then ", " else "")) (addrsTo from to) xs)
          textPart :: String
          textPart = printf ".text # 0x%x .. 0x%x\n.word " start end ++ list start end codes ++ "\n"
      userKernelDataSegment s False
      bare :: Bool <- get (optBare s)
      let dstart :: Word32
          dstart = if bare then 0 else dataBot
      dend :: Word32 <- get (nextDataPc s)
      dataPart :: String <-
        if dend > dstart
          then do
            ws :: [Int32] <- mapM (memReadWord s) (addrsTo dstart dend)
            pure (printf ".data # 0x%x .. 0x%x\n.word " dstart dend ++ list dstart dend ws ++ "\n")
          else pure ""
      B.writeFile (name ++ ".out") (B.pack (textPart ++ dataPart))
      pure False

-- The REPL.

-- | The REPL's memory between commands (C: the statics of
-- @parse_spim_command@).
data Repl = Repl
  { prevCmd :: IORef Command
  , prevToken :: IORef Token
  , runAddr :: IORef Word32
  , stepCount :: IORef Int
  , printLoc :: IORef Int32
  , bkptAddr :: IORef Word32
  , argvRef :: IORef [String]
  }

data Command
  = UnknownCmd | ExitCmd | ReadCmd | RunCmd | StepCmd | PrintCmd | PrintSymCmd | PrintAllRegsCmd
  | ReinitializeCmd | AsmCmd | RedoCmd | NopCmd | HelpCmd | ContinueCmd | SetBkptCmd | DeleteBkptCmd
  | ListBkptCmd | DumpNativeCmd | DumpCmd | DisasmCmd | ArgsCmd
  deriving (Eq)

-- | The read-eval-print loop (C: @top_level@).  On a terminal, lines are
-- read with haskeline (editing and history); otherwise straight from
-- standard input, byte by byte, so that a program run from the REPL reads
-- the input that follows its @run@ command, as in C.
topLevel :: Spim -> Options -> [String] -> IO ()
topLevel s o argv = do
  scannerInit s ""
  set (inputFileName s) (Just "<standard input>")
  repl :: Repl <- Repl <$> newIORef NopCmd <*> newIORef TokNL <*> newIORef 0 <*> newIORef 0 <*> newIORef 0 <*> newIORef 0 <*> newIORef argv
  tty :: Bool <- hIsTerminalDevice stdin
  if tty
    then do
      home :: Maybe String <- lookupEnv "HOME"
      let settings :: H.Settings IO
          settings = (H.defaultSettings :: H.Settings IO) {H.historyFile = fmap (</> ".spimulator-haskell_history") home}
      H.runInputT settings (ttyLoop repl False)
    else plainLoop repl False
  where
    plainLoop :: Repl -> Bool -> IO b
    plainLoop repl redo = do
      line :: Maybe String <- if redo then pure (Just "") else readLine
      case line of
        Nothing -> writeErr "\n" >> exitSpim 0
        Just text -> do
          redo' :: Bool <- oneCommand repl redo text
          plainLoop repl redo'
    ttyLoop :: Repl -> Bool -> H.InputT IO b
    ttyLoop repl redo = do
      line :: Maybe String <- if redo then pure (Just "") else H.getInputLine "(spim) "
      case line of
        Nothing -> liftIO (writeErr "\n" >> exitSpim 0)
        Just text -> do
          redo' :: Bool <- liftIO (oneCommand repl redo (map (chr . (`mod` 256) . ord) text))
          ttyLoop repl redo'
    oneCommand :: Repl -> Bool -> String -> IO Bool
    oneCommand repl redo text = do
      unless redo $ scannerPushSource s (text ++ "\n")
      r :: Bool <- parseSpimCommand s o repl redo `catch` \RunErrorJump -> pure False
      unless redo $ scannerPopSource s
      hFlush stderr
      pure r
    readLine :: IO (Maybe String)
    readLine = do
      let go :: String -> IO (Maybe String)
          go acc = do
            b :: Maybe Word8 <- readByte
            case b of
              Nothing -> pure (if null acc then Nothing else Just (reverse acc))
              Just 10 -> pure (Just (reverse acc))
              Just c -> go (chr (fromIntegral c) : acc)
      go []

-- | Take a token; the end of the input ends spim (C: @read_token@).
readToken :: Spim -> IO Token
readToken s = do
  t :: Token <- scannerAdvance s
  if t == TokEOF then exitSpim 0 else pure t

flushToNewline :: Spim -> IO ()
flushToNewline s = do
  t :: Token <- readToken s
  unless (t == TokNL) $ flushToNewline s

valInt :: Spim -> IO Int32
valInt s = do
  v :: TokValue <- get (scanValue s)
  pure (case v of VInt i -> i; _ -> 0)

valStr :: Spim -> IO String
valStr s = do
  v :: TokValue <- get (scanValue s)
  pure (case v of VStr t -> t; _ -> "")

-- | An optional integer argument, 0 if none (C: @get_opt_int@).
getOptInt :: Spim -> IO Int32
getOptInt s = do
  t :: Token <- readToken s
  case t of
    TokInt -> flushToNewline s >> valInt s
    TokNL -> pure 0
    _ -> flushToNewline s >> pure 0

-- | Is the typed word a prefix of the command, at least n characters long?
strPrefix :: String -> String -> Int -> Bool
strPrefix typed cmd n = typed `isPrefixOf` cmd && length typed >= n

-- | Which command a line starts with (C: @read_assembly_command@).
readAssemblyCommand :: Spim -> IO Command
readAssemblyCommand s = do
  t :: Token <- readToken s
  case t of
    TokNL -> pure RedoCmd
    TokId -> do
      w :: String <- valStr s
      pure $ case () of
        _
          | strPrefix w "exit" 2 -> ExitCmd
          | strPrefix w "quit" 2 -> ExitCmd
          | strPrefix w "print" 1 -> PrintCmd
          | strPrefix w "print_symbols" 7 -> PrintSymCmd
          | strPrefix w "print_all_regs" 7 -> PrintAllRegsCmd
          | strPrefix w "run" 2 -> RunCmd
          | strPrefix w "args" 2 -> ArgsCmd
          | strPrefix w "read" 2 -> ReadCmd
          | strPrefix w "load" 2 -> ReadCmd
          | strPrefix w "reinitialize" 6 -> ReinitializeCmd
          | strPrefix w "step" 1 -> StepCmd
          | strPrefix w "help" 1 -> HelpCmd
          | strPrefix w "continue" 1 -> ContinueCmd
          | strPrefix w "breakpoint" 2 -> SetBkptCmd
          | strPrefix w "delete" 1 -> DeleteBkptCmd
          | strPrefix w "list" 2 -> ListBkptCmd
          | strPrefix w "dumpnative" 5 -> DumpNativeCmd
          | strPrefix w "dump" 4 -> DumpCmd
          | strPrefix w "disasm" 3 -> DisasmCmd
          | take 1 w == "?" -> HelpCmd
          | take 1 w == "." -> AsmCmd
          | otherwise -> UnknownCmd
    _ -> pure UnknownCmd

-- | Run one command; True means "do the previous command again"
-- (C: @parse_spim_command@).
parseSpimCommand :: Spim -> Options -> Repl -> Bool -> IO Bool
parseSpimCommand s o repl redo = do
  cmd :: Command <- if redo then get (prevCmd repl) else readAssemblyCommand s
  let done :: Command -> IO Bool
      done c = set (prevCmd repl) c >> pure False
      tokenOrPrev :: IO Token
      tokenOrPrev = if redo then get (prevToken repl) else readToken s
      argv :: IO [String]
      argv = get (argvRef repl)
      breakMessage :: Bool -> IO ()
      breakMessage hit = when hit $ get (regPC s) >>= \pc -> writeErr (printf "Breakpoint encountered at 0x%08x\n" pc)
  case cmd of
    ExitCmd -> exitSpim 0
    ReadCmd -> do
      t :: Token <- tokenOrPrev
      unless redo $ flushToNewline s
      if t == TokStr
        then do
          name :: String <- valStr s
          _ <- readAssemblyFile s name
          scannerPopSource s
        else cError "Must supply a filename to read\n"
      done ReadCmd
    RunCmd -> do
      a0 :: Word32 <- if redo then get (runAddr repl) else fromIntegral <$> getOptInt s
      addr :: Word32 <- if a0 == 0 then findSymbolAddress s "__start" else pure a0
      set (runAddr repl) addr
      argv >>= initializeRunStack s
      when (addr /= 0) $ do
        undefs :: Maybe String <- undefinedSymbolString s
        forM_ undefs $ \names -> do
          writeErr "The following symbols are undefined:\n"
          writeErr names
          writeErr "\n"
        (hit, _) :: (Bool, Bool) <- runProgram s addr maxBound False False
        breakMessage hit
      done RunCmd
    ContinueCmd -> do
      pc :: Word32 <- get (regPC s)
      when (pc /= 0) $ runProgram s pc maxBound False True >>= breakMessage . fst
      done ContinueCmd
    StepCmd -> do
      n0 :: Int <- if redo then get (stepCount repl) else fromIntegral <$> getOptInt s
      let n :: Int
          n = if n0 == 0 then 1 else n0
      set (stepCount repl) n
      pc :: Word32 <- get (regPC s)
      addr :: Word32 <- if pc == 0 then findSymbolAddress s "__start" else pure pc
      when (addr /= 0) $ runProgram s addr n True True >>= breakMessage . fst
      done StepCmd
    PrintCmd -> do
      t :: Token <- tokenOrPrev
      consumed :: Bool <- case t of
        TokReg -> do
          loc :: Int32 <- if redo then (+ 1) <$> get (printLoc repl) else valInt s
          set (printLoc repl) loc
          v :: Int32 <- getGpr s (fromIntegral loc `mod` 32)
          writeErr (printf "Reg %d = 0x%08x (%d)\n" loc (fromIntegral v :: Word32) v)
          pure False
        TokFpReg -> do
          loc :: Int32 <- if redo then (+ 2) <$> get (printLoc repl) else valInt s
          set (printLoc repl) loc
          let r :: Int
              r = fromIntegral loc `mod` 32
          when (even loc) $ doubleWords s (r - r `mod` 2) >>= \d -> writeErr (printf "FP reg %d = %s (double)\n" loc (formatG 6 d))
          f :: Float <- fprSingle s r
          writeErr (printf "FP reg %d = %s (single)\n" loc (formatG 6 (float2Double f)))
          pure False
        TokInt
          | redo -> do
              loc :: Int32 <- (+ 4) <$> get (printLoc repl)
              set (printLoc repl) loc
              printMem s (fromIntegral loc)
              pure False
          | otherwise -> do
              intVal :: Int32 <- valInt s
              nxt :: Token <- readToken s
              if nxt == TokChar '('
                then do
                  regTok :: Token <- readToken s
                  reg :: Int <- if regTok == TokReg then fromIntegral <$> valInt s else pure (-1 :: Int)
                  close :: Token <- if regTok == TokReg then readToken s else pure TokEOF
                  if reg < 0 || close /= TokChar ')'
                    then cError "expected $reg) after '(' in print N($reg) form\n"
                    else do
                      base :: Int32 <- getGpr s reg
                      let loc :: Int32
                          loc = base + intVal
                      set (printLoc repl) loc
                      printMem s (fromIntegral loc)
                  pure (regTok == TokNL || close == TokNL)
                else do
                  set (printLoc repl) intVal
                  printMem s (fromIntegral intVal)
                  pure (nxt == TokNL)
        TokId -> do
          name :: String <- valStr s
          isReg :: Bool <- printRegFromString s name
          unless isReg $ do
            loc :: Int32 <- if redo then (+ 4) <$> get (printLoc repl) else fromIntegral <$> findSymbolAddress s name
            set (printLoc repl) loc
            if loc /= 0 then printMem s (fromIntegral loc) else cError ("Unknown label: " ++ name ++ "\n")
          pure False
        _ -> cError "Print what?\n" >> pure False
      unless (redo || consumed) $ flushToNewline s
      set (prevToken repl) t
      done PrintCmd
    PrintSymCmd -> do
      printSymbols s
      unless redo $ flushToNewline s
      done NopCmd
    PrintAllRegsCmd -> do
      t :: Token <- tokenOrPrev
      w :: String <- valStr s
      text :: String <- formatRegisters s (t == TokId && w == "hex")
      writeErr (text ++ "\n")
      -- With no operand the token read was the newline itself.
      unless (redo || t == TokNL) $ flushToNewline s
      done NopCmd
    ReinitializeCmd -> do
      flushToNewline s
      h :: ExceptionFiles <- handlerFor o
      quiet :: Bool <- get (optQuiet s)
      initializeWorld s h (not quiet)
      argv >>= initializeRunStack s
      writeStartupMessage
      done NopCmd
    AsmCmd -> parseFile s >> done AsmCmd
    RedoCmd -> pure True
    NopCmd -> done NopCmd
    ArgsCmd -> do
      old :: [String] <- argv
      let argv0 :: String
          argv0 = case old of (a : _) -> a; [] -> "<repl>"
          collect :: [String] -> IO [String]
          collect acc = do
            t :: Token <- readToken s
            case t of
              TokNL -> pure (reverse acc)
              TokStr -> valStr s >>= \v -> collect (v : acc)
              TokId -> valStr s >>= \v -> collect (v : acc)
              TokInt -> valInt s >>= \v -> collect (show v : acc)
              _ -> collect acc
      rest :: [String] <- collect []
      set (argvRef repl) (argv0 : rest)
      done ArgsCmd
    HelpCmd -> do
      unless redo $ flushToNewline s
      writeErr helpText
      done HelpCmd
    _
      | cmd == SetBkptCmd || cmd == DeleteBkptCmd -> do
          t :: Token <- tokenOrPrev
          unless (redo || t == TokNL) $ flushToNewline s
          old :: Word32 <- get (bkptAddr repl)
          addr :: Maybe Word32 <- case t of
            TokInt -> Just <$> if redo then pure (old + 4) else fromIntegral <$> valInt s
            TokId -> Just <$> if redo then pure (old + 4) else valStr s >>= findSymbolAddress s
            _ -> cError "Must supply an address for breakpoint\n" >> pure Nothing
          case addr of
            Nothing -> pure False
            Just a -> do
              set (bkptAddr repl) a
              if cmd == SetBkptCmd then addBreakpoint s a else deleteBreakpoint s a
              done cmd
      | cmd == ListBkptCmd -> do
          unless redo $ flushToNewline s
          listBreakpoints s
          done ListBkptCmd
      | cmd == DumpCmd || cmd == DumpNativeCmd -> do
          t :: Token <- tokenOrPrev
          fileName :: Maybe String <- case t of
            TokStr -> Just <$> valStr s
            TokNL -> pure (Just "spim.dump")
            _ -> pure Nothing
          case fileName of
            Nothing -> do
              writeErr (printf "usage: %s [ \"filename\" ]\n" (if cmd == DumpCmd then "dump" else "dumpnative" :: String))
              pure False
            Just f -> dumpCode cmd f >> done cmd
      | cmd == DisasmCmd -> do
          printTextListing s
          unless redo $ flushToNewline s
          done NopCmd
      | otherwise -> do
          let skip :: IO ()
              skip = readToken s >>= \t -> unless (t == TokNL) skip
          skip
          cError "Unknown spim command\n"
          pure False
  where
    dumpCode :: Command -> String -> IO ()
    dumpCode cmd f = do
      opened :: Either IOError Handle <- (Right <$> openFile f WriteMode) `catch` \e -> pure (Left (e :: IOError))
      case opened of
        Left _ -> do
          (Errno e :: Errno) <- getErrno
          msg :: String <- c_strerror e >>= peekCAString
          writeErr (f ++ ": " ++ msg ++ "\n")
        Right h -> do
          hSetBinaryMode h True
          userKernelTextSegment s False
          start :: Word32 <- findSymbolAddress s "__eoth"
          end :: Word32 <- currentTextPc s
          let addrs :: [Word32]
              addrs = takeWhile (< end) [start, start + 4 ..]
          forM_ addrs $ \a -> do
            code :: Int32 <- memReadInst s a >>= maybe (pure 0) (\r -> get r >>= instEncodeIO)
            let w :: Word32
                w = fromIntegral code :: Word32
                bytes :: [Word32]
                bytes = if cmd == DumpCmd then [w `shiftR` 24, w `shiftR` 16, w `shiftR` 8, w] else [w, w `shiftR` 8, w `shiftR` 16, w `shiftR` 24]
            B.hPut h (B.pack (map (chr . fromIntegral . (.&. 0xff)) bytes))
          hClose h
          writeErr (printf "Dumped %d words starting at 0x%08x to file %s\n" (length addrs) start f)

-- | @print $pc@ and the other special registers by name
-- (C: @print_reg_from_string@).  False if the name is none of them.
printRegFromString :: Spim -> String -> IO Bool
printRegFromString s name0 = do
  let name :: String
      name = map toLower (take 100 name0)
      bare :: String
      bare = if take 1 name == "$" then drop 1 name else name
      show' :: String -> Int32 -> IO Bool
      show' label v = writeErr (printf "%s = 0x%08x (%d)\n" (label :: String) (fromIntegral v :: Word32) (v :: Int32)) >> pure True
  case bare of
    "pc" -> get (regPC s) >>= show' "PC" . fromIntegral
    "hi" -> get (regHI s) >>= show' "HI"
    "lo" -> get (regLO s) >>= show' "LO"
    "fpcond" -> getFcr s 31 >>= show' "FCSR"
    "cause" -> getCp0 s cp0Cause >>= show' "Cause"
    "epc" -> getCp0 s cp0Epc >>= show' "EPC"
    "status" -> getCp0 s cp0Status >>= show' "Status"
    "badvaddr" -> getCp0 s cp0BadVAddr >>= show' "BadVAddr"
    _ -> pure False

helpText :: String
helpText =
  "\nSPIM is a MIPS32 simulator.\n\
  \Its top-level commands are:\n\
  \exit  -- Exit the simulator\n\
  \quit  -- Exit the simulator\n\
  \read \"FILE\" -- Read FILE containing assembly code into memory\n\
  \load \"FILE\" -- Same as read\n\
  \run <ADDR> -- Start the program at (optional) ADDRESS\n\
  \args <ARG ...> -- Set the args the next `run` will pass to the program;\n\
  \                  argv[0] stays as the loaded program path.  `args`\n\
  \                  with no operands clears the args.\n\
  \step <N> -- Step the program for N instructions (default 1)\n\
  \continue -- Continue program execution without stepping\n\
  \print $N -- Print register N\n\
  \print $fN -- Print floating point register N\n\
  \print ADDR -- Print contents of memory at ADDRESS\n\
  \print_symbols -- Print all global symbols\n\
  \print_all_regs -- Print all MIPS registers\n\
  \print_all_regs hex -- Print all MIPS registers in hex\n\
  \reinitialize -- Clear the memory and registers\n\
  \breakpoint <ADDR> -- Set a breakpoint at address ADDR\n\
  \delete <ADDR> -- Delete breakpoint at address ADDR\n\
  \list -- List all breakpoints\n\
  \dump [ \"FILE\" ] -- Dump binary code to spim.dump or FILE in network byte order\n\
  \dumpnative [ \"FILE\" ] -- Dump binary code to spim.dump or FILE in host byte order\n\
  \disasm -- Print a disassembly listing of the loaded program's text segment\n\
  \. -- Rest of line is assembly instruction to execute\n\
  \<cr> -- Newline reexecutes previous command\n\
  \? -- Print this message\n\
  \\nMost commands can be abbreviated to their unique prefix\n\
  \e.g., ex(it), re(ad), l(oad), ru(n), s(tep), p(rint)\n\n"
