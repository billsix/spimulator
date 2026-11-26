{-# LANGUAGE ForeignFunctionInterface #-}

-- | The command line and the REPL (C: src/spim.c).
--
-- @spimulator-haskell@ takes the C engine's flags: @-f file@ (repeatable)
-- assembles files and runs them; with no file it starts the interactive
-- REPL.  The teaching mode (@-explain@) is not part of this port.
module Main (main) where

import AsmCommon (currentTextLocation)
import Assemble (userKernelTextSegment)
import CFormat (formatG)
import Control.Exception (catch, finally)
import Control.Monad (forM_, unless, when)
import Control.Monad.IO.Class (liftIO)
import Data.Bits (shiftR, (.&.))
import qualified Data.ByteString.Char8 as B
import Data.Char (chr, ord, toLower)
import Data.IORef (IORef, newIORef, readIORef, writeIORef)
import Data.Int (Int32)
import Data.List (intercalate, isPrefixOf)
import Data.Maybe (fromMaybe, listToMaybe)
import Data.Word (Word32, Word8)
import DataSegment (userKernelDataSegment)
import DisplayUtils
import Foreign.C.Error (Errno (..), getErrno)
import Foreign.C.String (CString, peekCAString)
import Foreign.C.Types (CInt (..))
import GHC.Float (float2Double)
import InstFormat
import Instruction (instructionEncodeIO)
import Memory
import Output
import Parser (parseFile)
import Registers
import Scanner
import SpimUtils
import State
import SymbolTable
import qualified System.Console.Haskeline as H
import System.Directory (doesFileExist)
import System.Environment (getArgs, getExecutablePath, lookupEnv)
import System.FilePath (takeDirectory, (</>))
import System.IO (Handle, IOMode (..), hClose, hFlush, hIsTerminalDevice, hSetBinaryMode, openFile, stderr, stdin)
import Text.Printf (printf)

foreign import ccall unsafe "strerror" c_strerror :: CInt -> IO CString

-- | Where the listing goes, if @-listing@ was given.
data Listing = NoListing | ListingTo Handle | ListingStderr

-- | What the command line asked for.
data Options = Options
  { -- | Nothing: the default handler
    exceptionFile :: Maybe String,
    loadHandler :: Bool,
    dumpUser :: Bool,
    dumpAll :: Bool,
    assembleOnly :: Bool
  }

main :: IO ()
main = do
  setupHandles
  s :: Spim <- newSpim
  arguments :: [String] <- getArgs
  listingRef :: IORef Listing <- newIORef NoListing
  let closeListing :: IO ()
      closeListing = do
        l :: Listing <- readIORef listingRef
        case l of
          ListingTo h -> hClose h
          _ -> pure ()
  (spimMain s arguments listingRef `catch` \RunErrorJump -> exitSpim 1) `finally` closeListing

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
    search directive n = do
      let candidate :: String
          candidate = directive </> "src" </> "exceptions.s"
      ok :: Bool <- doesFileExist candidate
      if ok
        then pure (Just candidate)
        else
          let up :: String
              up = takeDirectory directive
           in if up == directive then pure Nothing else search up (n - 1)

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
  "Usage: spimulator [option ...] [-file] file [-file file ...] [program arguments]\n\
  \\t-asm, -a\t\tExtended machine (pseudo-ops, no delayed branches and loads) (default)\n\
  \\t-bare, -b\t\tBare machine (no pseudo-ops, delayed branches and loads)\n\
  \\t-delayed_branches, -db\tExecute delayed branches\n\
  \\t-delayed_loads, -dl\tExecute delayed loads\n\
  \\t-pseudo, -p\t\tAccept pseudo-instructions (default)\n\
  \\t-nopseudo, -np\t\tDo not accept pseudo-instructions\n\
  \\t-exception, -e\t\tLoad the exception handler (default); also -trap, -t\n\
  \\t-noexception, -ne\tDo not load the exception handler; also -notrap, -nt\n\
  \\t-exception_file <file>, -ef <file>\n\
  \\t\t\t\tLoad <file> as the exception handler; also -trap_file, -tf\n\
  \\t-quiet, -q\t\tDo not print warnings\n\
  \\t-noquiet, -nq\t\tPrint warnings (default)\n\
  \\t-explain [N], -x [N]\tNarrate every instruction (teaching mode).\n\
  \\t\t\t\tLevel N selects detail: 1 = step header, 2 = + narration,\n\
  \\t\t\t\t3 = + bit-layout (default when N omitted),\n\
  \\t\t\t\t4 = decoder walkthrough in place of the bit-layout.\n\
  \\t\t\t\tAlso accepts -explain=N.\n\
  \\t-noexplain, -nx\t\tDisable teaching mode (default)\n\
  \\t-mapped_io, -mio\tEnable memory-mapped IO\n\
  \\t-nomapped_io, -nmio\tDo not enable memory-mapped IO (default)\n\
  \\t-stext, -st <size>\tInitial text segment size, in bytes\n\
  \\t-sdata, -sd <size>\tInitial data segment size, in bytes\n\
  \\t-sstack, -ss <size>\tInitial stack segment size, in bytes\n\
  \\t-sktext, -skt <size>\tInitial kernel text segment size, in bytes\n\
  \\t-skdata, -skd <size>\tInitial kernel data segment size, in bytes\n\
  \\t-ldata, -ld <size>\tData segment size limit, in bytes\n\
  \\t-lstack, -ls <size>\tStack segment size limit, in bytes\n\
  \\t-lkdata, -lkd <size>\tKernel data segment size limit, in bytes\n\
  \\t-file <file>, -f <file>\tAssembly code file; repeat to load several into one\n\
  \\t\t\t\tsymbol table (forward references resolve across files).\n\
  \\t\t\t\tA bare argument is loaded as a file too; the arguments\n\
  \\t\t\t\tafter the last file are the program's arguments.\n\
  \\t-assemble\t\tWrite assembled code to <file>.out\n\
  \\t-listing <file>\t\tWrite assemble-time event trace to <file> (use - for stderr)\n\
  \\t-print-ast\t\tParse to AST, print it to stderr, and exit without emitting any code\n\
  \\t-print-ast-json\t\tParse to AST, dump JSON to stdout (for tooling), and exit\n\
  \\t-show-expansion\t\tParse to AST, print just the pseudo-op wrappers + their expansion, and exit\n\
  \\t-dump\t\t\tWrite user data and text segments into files\n\
  \\t-full_dump\t\tWrite user and kernel data and text into files.\n"

-- | Loading state while the arguments are read.
data Loading = Loading
  { loaded :: Bool,
    attempted :: Bool,
    worldReady :: Bool,
    lastFile :: Int,
    programArguments :: [String],
    badUsage :: Bool
  }

-- | Read the arguments in order, loading files as they come, then act
-- (C: @main@).
spimMain :: Spim -> [String] -> IORef Listing -> IO ()
spimMain s argv listingRef = do
  environmentHandler :: Maybe String <- lookupEnv "SPIM_EXCEPTION_HANDLER"
  let opts0 :: Options
      opts0 = Options environmentHandler True False False False
      setB :: (Spim -> IORef Bool) -> Bool -> IO ()
      setB ref v = set (ref s) v
      sizes :: ((Int, Int, Int, Int, Int, Int, Int, Int) -> (Int, Int, Int, Int, Int, Int, Int, Int)) -> IO ()
      sizes f = modify (initialSizes s) f
      -- i is the index in argv of the first argument left, a.
      loop :: Int -> [String] -> Options -> Loading -> IO (Options, Loading)
      loop _ [] o ld = pure (o, ld)
      loop i (a : more) o ld = do
        let value :: Maybe String
            value = listToMaybe more
            int :: Int
            int = maybe 0 atoi value
            -- Past a flag and its value.
            skipValue :: Options -> Loading -> IO (Options, Loading)
            skipValue = loop (i + 2) (drop 1 more)
            next :: Options -> IO (Options, Loading)
            next o' = loop (i + 1) more o' ld
            -- A flag that takes a value, given last: say so, and show
            -- the usage (C: flag_value_missing).
            withValue :: IO (Options, Loading) -> IO (Options, Loading)
            withValue act
              | not (null more) = act
              | otherwise = do
                  cError ("spimulator: " ++ a ++ " requires a value\n")
                  loop (i + 1) more o ld {badUsage = True}
            -- Load file, argv's argument fileIndex; after is what follows
            -- it.  An explicit -file goes on reading arguments; a bare
            -- file name ends them, the rest being the program's.
            loadFile :: Bool -> Int -> String -> [String] -> IO (Options, Loading)
            loadFile explicit fileIndex file after = do
              unless (worldReady ld) $ do
                h :: ExceptionFiles <- handlerFor o
                quiet :: Bool <- get (optionQuiet s)
                initializeWorld s h (not quiet)
              let ld1 :: Loading
                  ld1 = ld {worldReady = True, attempted = True}
              ok :: Bool <- readAssemblyFile s file
              if not ok
                then pure (o, ld1)
                else do
                  let ld2 :: Loading
                      ld2 = ld1 {loaded = True, lastFile = fileIndex}
                  if explicit
                    then loop (fileIndex + 1) after o ld2
                    else pure (o, ld2 {programArguments = file : after})
            is :: [String] -> Bool
            is xs = a `elem` xs
        case () of
          _
            | is ["-asm", "-a"] -> setB optionBare False >> setB optionDelayedBranches False >> setB optionDelayedLoads False >> next o
            | is ["-bare", "-b"] -> setB optionBare True >> setB optionDelayedBranches True >> setB optionDelayedLoads True >> setB optionQuiet True >> next o
            | is ["-delayed_branches", "-db"] -> setB optionDelayedBranches True >> next o
            | is ["-delayed_loads", "-dl"] -> setB optionDelayedLoads True >> next o
            | is ["-exception", "-e", "-trap", "-t"] -> next o {loadHandler = True}
            | is ["-noexception", "-ne", "-notrap", "-nt"] -> next o {loadHandler = False}
            | is ["-exception_file", "-ef", "-trap_file", "-tf"] ->
                withValue $ skipValue o {exceptionFile = Just (fromMaybe "" value), loadHandler = True} ld
            | is ["-mapped_io", "-mio"] -> setB optionMappedIO True >> next o
            | is ["-nomapped_io", "-nmio"] -> setB optionMappedIO False >> next o
            | is ["-pseudo", "-p"] -> setB optionAcceptPseudo True >> next o
            | is ["-nopseudo", "-np"] -> setB optionAcceptPseudo False >> next o
            | is ["-quiet", "-q"] -> setB optionQuiet True >> next o
            | is ["-noquiet", "-nq"] -> setB optionQuiet False >> next o
            | is ["-noexplain", "-nx"] -> next o
            | is ["-explain", "-x"] || "-explain=" `isPrefixOf` a || "-x=" `isPrefixOf` a -> do
                writeError "spimulator-haskell: the teaching mode (-explain) is not supported by this port\n"
                exitSpim 2
            | is ["-stext", "-st"] -> withValue $ sizes (\(_, b, c, d, e, f, g, h) -> (int, b, c, d, e, f, g, h)) >> skipValue o ld
            | is ["-sdata", "-sd"] -> withValue $ sizes (\(t, _, c, d, e, f, g, h) -> (t, int, c, d, e, f, g, h)) >> skipValue o ld
            | is ["-ldata", "-ld"] -> withValue $ sizes (\(t, b, _, d, e, f, g, h) -> (t, b, int, d, e, f, g, h)) >> skipValue o ld
            | is ["-sstack", "-ss"] -> withValue $ sizes (\(t, b, c, _, e, f, g, h) -> (t, b, c, int, e, f, g, h)) >> skipValue o ld
            | is ["-lstack", "-ls"] -> withValue $ sizes (\(t, b, c, d, _, f, g, h) -> (t, b, c, d, int, f, g, h)) >> skipValue o ld
            | is ["-sktext", "-skt"] -> withValue $ sizes (\(t, b, c, d, e, _, g, h) -> (t, b, c, d, e, int, g, h)) >> skipValue o ld
            | is ["-skdata", "-skd"] -> withValue $ sizes (\(t, b, c, d, e, f, _, h) -> (t, b, c, d, e, f, int, h)) >> skipValue o ld
            | is ["-lkdata", "-lkd"] -> withValue $ sizes (\(t, b, c, d, e, f, g, _) -> (t, b, c, d, e, f, g, int)) >> skipValue o ld
            | is ["-file", "-f"], file : after <- more -> loadFile True (i + 1) file after
            | take 1 a /= "-" -> if loaded ld then pure (o, ld) else loadFile False i a more
            | a == "-assemble" -> next o {assembleOnly = True}
            | a == "-print-ast" -> setB printAstFlag True >> setB printAstOnly True >> next o
            | a == "-show-expansion" -> setB showExpansionFlag True >> setB printAstOnly True >> next o
            | a == "-print-ast-json" -> setB printAstJsonFlag True >> setB printAstOnly True >> next o
            | a == "-listing" ->
                case value of
                  Nothing -> do
                    cError "\n-listing requires a filename argument\n"
                    loop (i + 1) more o ld {badUsage = True}
                  Just path -> do
                    opened :: Maybe Listing <-
                      if path == "-"
                        then pure (Just ListingStderr)
                        else (Just . ListingTo <$> openListing path) `catch` \e -> const (pure Nothing) (e :: IOError)
                    case opened of
                      Nothing -> do
                        cError ("\nCannot open listing file '" ++ path ++ "'\n")
                        skipValue o ld {badUsage = True}
                      Just l -> do
                        writeIORef listingRef l
                        set (assemblerObserver s) (Just (listingObserver s l))
                        skipValue o ld
            | a == "-dump" -> next o {dumpUser = True}
            | a == "-full_dump" -> next o {dumpAll = True}
            | otherwise -> do
                cError ("\nUnknown argument: " ++ a ++ " (ignored)\n")
                loop (i + 1) more o ld {badUsage = True}
  (o, ld0) :: (Options, Loading) <- loop 0 argv opts0 (Loading False False False (-1) [] False)
  let ld :: Loading
      ld = if loaded ld0 && null (programArguments ld0) then ld0 {programArguments = drop (lastFile ld0) argv} else ld0
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
          quiet :: Bool <- get (optionQuiet s)
          initializeWorld s h (not quiet)
          initializeRunStack s (programArguments ld)
          topLevel s o (programArguments ld)
    else do
      errs :: Int <- get (parseErrorsSeen s)
      when (errs > 0) $ orStatus 2
      only :: Bool <- get (printAstOnly s)
      when only $ exitSpim 0
      if assembleOnly o
        then do
          bad :: Bool <- writeAssembledCode s (head' (programArguments ld))
          exitSpim (if bad then 1 else 0)
        else
          if dumpUser o || dumpAll o
            then do
              dumpDataSegment s (not (dumpUser o))
              dumpTextSegment s (not (dumpUser o))
              get (spimReturnValue s) >>= exitSpim . fromIntegral
            else do
              initializeRunStack s (programArguments ld)
              undefinedLabels :: Maybe String <- undefinedSymbolString s
              case undefinedLabels of
                Just names -> do
                  writeError "The following symbols are undefined:\n"
                  writeError names
                  writeError "\n"
                  orStatus 1
                Nothing -> do
                  start :: Word32 <- findSymbolAddress s "__start"
                  _ <-
                    runProgram s start maxBound False False `catch` \RunErrorJump -> do
                      -- run_error printed why the run stopped; exit 1 unless the
                      -- program already set its own status.
                      rv :: Int32 <- get (spimReturnValue s)
                      when (rv == 0) $ set (spimReturnValue s) 1
                      pure (False, False)
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
atoi string =
  let t :: String
      t = dropWhile (`elem` " \t\n\v\f\r") string
      sign :: Int
      digits :: String
      (sign, digits) = case t of
        ('-' : r) -> (-1, r)
        ('+' : r) -> (1, r)
        _ -> (1, t)
   in fromIntegral (fromIntegral (sign * foldl (\acc c -> acc * 10 + (ord c - 48)) 0 (takeWhile (`elem` ['0' .. '9']) digits)) :: Int32)

-- | Write each assembler event to the listing (C: @listing_observer@).
listingObserver :: Spim -> Listing -> AssemblerEvent -> Int -> IO ()
listingObserver s listing ev line = do
  text :: String <- case ev of
    EventLabelDefinition address name global ->
      pure (printf "line %4d  label  %-24s defined at 0x%08x%s\n" line name address (if global then "  (global)" else "" :: String))
    EventTextInstruction address encoding -> do
      disassembly :: String <- instructionToString s address
      pure (printf "line %4d  text   0x%08x  %-32s  0x%08x\n" line address disassembly encoding)
    EventDataByte address v -> pure (printf "line %4d  data   0x%08x  byte    0x%02x\n" line address (v .&. 0xff))
    EventDataHalf address v -> pure (printf "line %4d  data   0x%08x  half    0x%04x\n" line address (v .&. 0xffff))
    EventDataWord address v -> pure (printf "line %4d  data   0x%08x  word    0x%08x\n" line address (fromIntegral v :: Word32))
    EventDataDouble address v -> pure (printf "line %4d  data   0x%08x  double  %s\n" line address (formatG 18 v))
    EventDataString address byteLength nul ->
      pure (printf "line %4d  data   0x%08x  string  len=%d%s\n" line address byteLength (if nul then " (+\\0)" else "" :: String))
    EventForwardReference from name -> pure (printf "line %4d  fref   0x%08x  uses unresolved symbol '%s'\n" line from name)
    EventForwardResolved at name v ->
      pure (printf "line %4d  fres   0x%08x  patched '%s' = 0x%08x\n" line at name (fromIntegral v :: Word32))
  case listing of
    ListingTo h -> B.hPut h (B.pack text)
    ListingStderr -> writeError text
    NoListing -> pure ()

-- | @-dump@: the user data segment, or with @-full_dump@ all data segments,
-- into data.asm (C: @dump_data_seg@).
dumpDataSegment :: Spim -> Bool -> IO ()
dumpDataSegment s kernelAlso = do
  text :: String <-
    if kernelAlso
      then formatDataSegments s
      else do
        top :: Word32 <- get (dataTop s)
        ("\tDATA\n" ++) <$> formatMemory s dataBottom top
  B.writeFile "data.asm" (B.pack text)

-- | The text segments into text.asm (C: @dump_text_seg@).
dumpTextSegment :: Spim -> Bool -> IO ()
dumpTextSegment s kernelAlso = do
  ttop :: Word32 <- get (textTop s)
  user :: String <- formatInstructions s textBottom ttop
  text :: String <-
    if kernelAlso
      then do
        kttop :: Word32 <- get (kernelTextTop s)
        kernel :: String <- formatInstructions s kernelTextBottom kttop
        pure (user ++ "\n\tKERNEL\n" ++ kernel)
      else pure ("\n\tUSER TEXT SEGMENT\n" ++ user)
  B.writeFile "text.asm" (B.pack text)

-- | @-assemble@: the user text and data as @.word@ lists in FILE.out
-- (C: @write_assembled_code@).  True means it failed.
writeAssembledCode :: Spim -> String -> IO Bool
writeAssembledCode s name = do
  errorOccurred :: Bool <- get (parseErrorOccurred s)
  if errorOccurred
    then pure True
    else do
      userKernelTextSegment s False
      start :: Word32 <- findSymbolAddress s "__eoth"
      end :: Word32 <- currentTextLocation s
      let addressesTo :: Word32 -> Word32 -> [Word32]
          addressesTo a b = takeWhile (< b) [a, a + 4 ..]
      codes :: [Int32] <- mapM (\a -> memoryReadInstruction s a >>= maybe (pure 0) (\r -> get r >>= instructionEncodeIO)) (addressesTo start end)
      -- The words, separated by ", ".
      let list :: [Int32] -> String
          list xs = intercalate ", " (map (\c -> printf "0x%x" (fromIntegral c :: Word32)) xs)
          textPart :: String
          textPart = printf ".text # 0x%x .. 0x%x\n.word " start end ++ list codes ++ "\n"
      userKernelDataSegment s False
      bare :: Bool <- get (optionBare s)
      let dstart :: Word32
          dstart = if bare then 0 else dataBottom
      dend :: Word32 <- get (nextDataLocation s)
      dataPart :: String <-
        if dend > dstart
          then do
            ws :: [Int32] <- mapM (memoryReadWord s) (addressesTo dstart dend)
            pure (printf ".data # 0x%x .. 0x%x\n.word " dstart dend ++ list ws ++ "\n")
          else pure ""
      B.writeFile (name ++ ".out") (B.pack (textPart ++ dataPart))
      pure False

-- The REPL.

-- | The REPL's memory between commands (C: the statics of
-- @parse_spim_command@).
data Repl = Repl
  { previousCommand :: IORef Command,
    previousToken :: IORef Token,
    runAddress :: IORef Word32,
    stepCount :: IORef Int,
    printLocation :: IORef Int32,
    breakpointAddress :: IORef Word32,
    argvRef :: IORef [String]
  }

data Command
  = UnknownCommand
  | ExitCommand
  | ReadCommand
  | RunCommand
  | StepCommand
  | PrintCommand
  | PrintSymbolCommand
  | PrintAllRegistersCommand
  | ReinitializeCommand
  | AssembleCommand
  | RedoCommand
  | NopCommand
  | HelpCommand
  | ContinueCommand
  | SetBreakpointCommand
  | DeleteBreakpointCommand
  | ListBreakpointCommand
  | DumpNativeCommand
  | DumpCommand
  | DisassemblyCommand
  | ArgumentsCommand
  deriving (Eq)

-- | The read-eval-print loop (C: @top_level@).  On a terminal, lines are
-- read with haskeline (editing and history); otherwise straight from
-- standard input, byte by byte, so that a program run from the REPL reads
-- the input that follows its @run@ command, as in C.
topLevel :: Spim -> Options -> [String] -> IO ()
topLevel s o argv = do
  scannerInitialize s ""
  set (inputFileName s) (Just "<standard input>")
  repl :: Repl <- Repl <$> newIORef NopCommand <*> newIORef TokenNewline <*> newIORef 0 <*> newIORef 0 <*> newIORef 0 <*> newIORef 0 <*> newIORef argv
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
        Nothing -> writeError "\n" >> exitSpim 0
        Just text -> do
          redo' :: Bool <- oneCommand repl redo text
          plainLoop repl redo'
    ttyLoop :: Repl -> Bool -> H.InputT IO b
    ttyLoop repl redo = do
      line :: Maybe String <- if redo then pure (Just "") else H.getInputLine "(spim) "
      case line of
        Nothing -> liftIO (writeError "\n" >> exitSpim 0)
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
  if t == TokenEOF then exitSpim 0 else pure t

flushToNewline :: Spim -> IO ()
flushToNewline s = do
  t :: Token <- readToken s
  unless (t == TokenNewline) $ flushToNewline s

valueInteger :: Spim -> IO Int32
valueInteger s = do
  v :: TokenValue <- get (scanValue s)
  pure (case v of ValueInteger i -> i; _ -> 0)

valueString :: Spim -> IO String
valueString s = do
  v :: TokenValue <- get (scanValue s)
  pure (case v of ValueString t -> t; _ -> "")

-- | An optional integer argument, 0 if none (C: @get_opt_int@).
getOptionalInteger :: Spim -> IO Int32
getOptionalInteger s = do
  t :: Token <- readToken s
  case t of
    TokenInteger -> flushToNewline s >> valueInteger s
    TokenNewline -> pure 0
    _ -> flushToNewline s >> pure 0

-- | Is the typed word a prefix of the command, at least n characters long?
stringPrefix :: String -> String -> Int -> Bool
stringPrefix typed command n = typed `isPrefixOf` command && length typed >= n

-- | Which command a line starts with (C: @read_assembly_command@).
readAssemblyCommand :: Spim -> IO Command
readAssemblyCommand s = do
  t :: Token <- readToken s
  case t of
    TokenNewline -> pure RedoCommand
    TokenIdentifier -> do
      w :: String <- valueString s
      pure $ case () of
        _
          | stringPrefix w "exit" 2 -> ExitCommand
          | stringPrefix w "quit" 2 -> ExitCommand
          | stringPrefix w "print" 1 -> PrintCommand
          | stringPrefix w "print_symbols" 7 -> PrintSymbolCommand
          | stringPrefix w "print_all_regs" 7 -> PrintAllRegistersCommand
          | stringPrefix w "run" 2 -> RunCommand
          | stringPrefix w "args" 2 -> ArgumentsCommand
          | stringPrefix w "read" 2 -> ReadCommand
          | stringPrefix w "load" 2 -> ReadCommand
          | stringPrefix w "reinitialize" 6 -> ReinitializeCommand
          | stringPrefix w "step" 1 -> StepCommand
          | stringPrefix w "help" 1 -> HelpCommand
          | stringPrefix w "continue" 1 -> ContinueCommand
          | stringPrefix w "breakpoint" 2 -> SetBreakpointCommand
          | stringPrefix w "delete" 1 -> DeleteBreakpointCommand
          | stringPrefix w "list" 2 -> ListBreakpointCommand
          | stringPrefix w "dumpnative" 5 -> DumpNativeCommand
          | stringPrefix w "dump" 4 -> DumpCommand
          | stringPrefix w "disasm" 3 -> DisassemblyCommand
          | take 1 w == "?" -> HelpCommand
          | take 1 w == "." -> AssembleCommand
          | otherwise -> UnknownCommand
    _ -> pure UnknownCommand

-- | Run one command; True means "do the previous command again"
-- (C: @parse_spim_command@).
parseSpimCommand :: Spim -> Options -> Repl -> Bool -> IO Bool
parseSpimCommand s o repl redo = do
  command :: Command <- if redo then get (previousCommand repl) else readAssemblyCommand s
  let done :: Command -> IO Bool
      done c = set (previousCommand repl) c >> pure False
      tokenOrPrevious :: IO Token
      tokenOrPrevious = if redo then get (previousToken repl) else readToken s
      argv :: IO [String]
      argv = get (argvRef repl)
      breakMessage :: Bool -> IO ()
      breakMessage hit = when hit $ get (registerProgramCounter s) >>= \programCounter -> writeError (printf "Breakpoint encountered at 0x%08x\n" programCounter)
  case command of
    ExitCommand -> exitSpim 0
    ReadCommand -> do
      t :: Token <- tokenOrPrevious
      unless redo $ flushToNewline s
      if t == TokenString
        then do
          name :: String <- valueString s
          _ <- readAssemblyFile s name
          scannerPopSource s
        else cError "Must supply a filename to read\n"
      done ReadCommand
    RunCommand -> do
      a0 :: Word32 <- if redo then get (runAddress repl) else fromIntegral <$> getOptionalInteger s
      address :: Word32 <- if a0 == 0 then findSymbolAddress s "__start" else pure a0
      set (runAddress repl) address
      argv >>= initializeRunStack s
      when (address /= 0) $ do
        undefinedLabels :: Maybe String <- undefinedSymbolString s
        forM_ undefinedLabels $ \names -> do
          writeError "The following symbols are undefined:\n"
          writeError names
          writeError "\n"
        (hit, _) :: (Bool, Bool) <- runProgram s address maxBound False False
        breakMessage hit
      done RunCommand
    ContinueCommand -> do
      programCounter :: Word32 <- get (registerProgramCounter s)
      when (programCounter /= 0) $ runProgram s programCounter maxBound False True >>= breakMessage . fst
      done ContinueCommand
    StepCommand -> do
      n0 :: Int <- if redo then get (stepCount repl) else fromIntegral <$> getOptionalInteger s
      let n :: Int
          n = if n0 == 0 then 1 else n0
      set (stepCount repl) n
      programCounter :: Word32 <- get (registerProgramCounter s)
      address :: Word32 <- if programCounter == 0 then findSymbolAddress s "__start" else pure programCounter
      when (address /= 0) $ runProgram s address n True True >>= breakMessage . fst
      done StepCommand
    PrintCommand -> do
      t :: Token <- tokenOrPrevious
      consumed :: Bool <- case t of
        TokenRegister -> do
          location :: Int32 <- if redo then (+ 1) <$> get (printLocation repl) else valueInteger s
          set (printLocation repl) location
          v :: Int32 <- getGeneralPurposeRegister s (fromIntegral location `mod` 32)
          writeError (printf "Reg %d = 0x%08x (%d)\n" location (fromIntegral v :: Word32) v)
          pure False
        TokenFloatingPointRegister -> do
          location :: Int32 <- if redo then (+ 2) <$> get (printLocation repl) else valueInteger s
          set (printLocation repl) location
          let r :: Int
              r = fromIntegral location `mod` 32
          when (even location) $ doubleWords s (r - r `mod` 2) >>= \d -> writeError (printf "FP reg %d = %s (double)\n" location (formatG 6 d))
          f :: Float <- floatingPointRegisterSingle s r
          writeError (printf "FP reg %d = %s (single)\n" location (formatG 6 (float2Double f)))
          pure False
        TokenInteger
          | redo -> do
              location :: Int32 <- (+ 4) <$> get (printLocation repl)
              set (printLocation repl) location
              printMemory s (fromIntegral location)
              pure False
          | otherwise -> do
              integerValue :: Int32 <- valueInteger s
              nxt :: Token <- readToken s
              if nxt == TokenChar '('
                then do
                  registerToken :: Token <- readToken s
                  registerNumber :: Int <- if registerToken == TokenRegister then fromIntegral <$> valueInteger s else pure (-1 :: Int)
                  close :: Token <- if registerToken == TokenRegister then readToken s else pure TokenEOF
                  if registerNumber < 0 || close /= TokenChar ')'
                    then cError "expected $reg) after '(' in print N($reg) form\n"
                    else do
                      base :: Int32 <- getGeneralPurposeRegister s registerNumber
                      let location :: Int32
                          location = base + integerValue
                      set (printLocation repl) location
                      printMemory s (fromIntegral location)
                  pure (registerToken == TokenNewline || close == TokenNewline)
                else do
                  set (printLocation repl) integerValue
                  printMemory s (fromIntegral integerValue)
                  pure (nxt == TokenNewline)
        TokenIdentifier -> do
          name :: String <- valueString s
          isRegister :: Bool <- printRegisterFromString s name
          unless isRegister $ do
            location :: Int32 <- if redo then (+ 4) <$> get (printLocation repl) else fromIntegral <$> findSymbolAddress s name
            set (printLocation repl) location
            if location /= 0 then printMemory s (fromIntegral location) else cError ("Unknown label: " ++ name ++ "\n")
          pure False
        _ -> cError "Print what?\n" >> pure False
      unless (redo || consumed) $ flushToNewline s
      set (previousToken repl) t
      done PrintCommand
    PrintSymbolCommand -> do
      printSymbols s
      unless redo $ flushToNewline s
      done NopCommand
    PrintAllRegistersCommand -> do
      t :: Token <- tokenOrPrevious
      w :: String <- valueString s
      text :: String <- formatRegisters s (t == TokenIdentifier && w == "hex")
      writeError (text ++ "\n")
      -- With no operand the token read was the newline itself.
      unless (redo || t == TokenNewline) $ flushToNewline s
      done NopCommand
    ReinitializeCommand -> do
      flushToNewline s
      h :: ExceptionFiles <- handlerFor o
      quiet :: Bool <- get (optionQuiet s)
      initializeWorld s h (not quiet)
      argv >>= initializeRunStack s
      writeStartupMessage
      done NopCommand
    AssembleCommand -> parseFile s >> done AssembleCommand
    RedoCommand -> pure True
    NopCommand -> done NopCommand
    ArgumentsCommand -> do
      old :: [String] <- argv
      let argv0 :: String
          argv0 = case old of (a : _) -> a; [] -> "<repl>"
          collect :: [String] -> IO [String]
          collect acc = do
            t :: Token <- readToken s
            case t of
              TokenNewline -> pure (reverse acc)
              TokenString -> valueString s >>= \v -> collect (v : acc)
              TokenIdentifier -> valueString s >>= \v -> collect (v : acc)
              TokenInteger -> valueInteger s >>= \v -> collect (show v : acc)
              _ -> collect acc
      rest :: [String] <- collect []
      set (argvRef repl) (argv0 : rest)
      done ArgumentsCommand
    HelpCommand -> do
      unless redo $ flushToNewline s
      writeError helpText
      done HelpCommand
    _
      | command == SetBreakpointCommand || command == DeleteBreakpointCommand -> do
          t :: Token <- tokenOrPrevious
          unless (redo || t == TokenNewline) $ flushToNewline s
          old :: Word32 <- get (breakpointAddress repl)
          address :: Maybe Word32 <- case t of
            TokenInteger -> Just <$> if redo then pure (old + 4) else fromIntegral <$> valueInteger s
            TokenIdentifier -> Just <$> if redo then pure (old + 4) else valueString s >>= findSymbolAddress s
            _ -> cError "Must supply an address for breakpoint\n" >> pure Nothing
          case address of
            Nothing -> pure False
            Just a -> do
              set (breakpointAddress repl) a
              if command == SetBreakpointCommand then addBreakpoint s a else deleteBreakpoint s a
              done command
      | command == ListBreakpointCommand -> do
          unless redo $ flushToNewline s
          listBreakpoints s
          done ListBreakpointCommand
      | command == DumpCommand || command == DumpNativeCommand -> do
          t :: Token <- tokenOrPrevious
          fileName :: Maybe String <- case t of
            TokenString -> Just <$> valueString s
            TokenNewline -> pure (Just "spim.dump")
            _ -> pure Nothing
          case fileName of
            Nothing -> do
              writeError (printf "usage: %s [ \"filename\" ]\n" (if command == DumpCommand then "dump" else "dumpnative" :: String))
              pure False
            Just f -> dumpCode command f >> done command
      | command == DisassemblyCommand -> do
          printTextListing s
          unless redo $ flushToNewline s
          done NopCommand
      | otherwise -> do
          let skip :: IO ()
              skip = readToken s >>= \t -> unless (t == TokenNewline) skip
          skip
          cError "Unknown spim command\n"
          pure False
  where
    dumpCode :: Command -> String -> IO ()
    dumpCode command f = do
      opened :: Either IOError Handle <- (Right <$> openFile f WriteMode) `catch` \e -> pure (Left (e :: IOError))
      case opened of
        Left _ -> do
          (Errno e :: Errno) <- getErrno
          message :: String <- c_strerror e >>= peekCAString
          writeError (f ++ ": " ++ message ++ "\n")
        Right h -> do
          hSetBinaryMode h True
          userKernelTextSegment s False
          start :: Word32 <- findSymbolAddress s "__eoth"
          end :: Word32 <- currentTextLocation s
          let addresses :: [Word32]
              addresses = takeWhile (< end) [start, start + 4 ..]
          forM_ addresses $ \a -> do
            code :: Int32 <- memoryReadInstruction s a >>= maybe (pure 0) (\r -> get r >>= instructionEncodeIO)
            let w :: Word32
                w = fromIntegral code :: Word32
                bytes :: [Word32]
                bytes = if command == DumpCommand then [w `shiftR` 24, w `shiftR` 16, w `shiftR` 8, w] else [w, w `shiftR` 8, w `shiftR` 16, w `shiftR` 24]
            B.hPut h (B.pack (map (chr . fromIntegral . (.&. 0xff)) bytes))
          hClose h
          writeError (printf "Dumped %d words starting at 0x%08x to file %s\n" (length addresses) start f)

-- | @print $pc@ and the other special registers by name
-- (C: @print_reg_from_string@).  False if the name is none of them.
printRegisterFromString :: Spim -> String -> IO Bool
printRegisterFromString s name0 = do
  let name :: String
      name = map toLower (take 100 name0)
      show' :: String -> Int32 -> IO Bool
      show' label v = writeError (printf "%s = 0x%08x (%d)\n" (label :: String) (fromIntegral v :: Word32) (v :: Int32)) >> pure True
  -- The name without a leading `$`.
  case if take 1 name == "$" then drop 1 name else name of
    "pc" -> get (registerProgramCounter s) >>= show' "PC" . fromIntegral
    "hi" -> get (registerMultiplyDivideHigh s) >>= show' "HI"
    "lo" -> get (registerMultiplyDivideLow s) >>= show' "LO"
    "fpcond" -> getFloatingPointControlRegister s 31 >>= show' "FCSR"
    "cause" -> getCoprocessor0 s coprocessor0Cause >>= show' "Cause"
    "epc" -> getCoprocessor0 s coprocessor0ExceptionProgramCounter >>= show' "EPC"
    "status" -> getCoprocessor0 s coprocessor0Status >>= show' "Status"
    "badvaddr" -> getCoprocessor0 s coprocessor0BadVirtualAddress >>= show' "BadVAddr"
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
