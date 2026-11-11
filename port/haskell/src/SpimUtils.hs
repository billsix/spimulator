{-# LANGUAGE ForeignFunctionInterface #-}

-- | Setting up the machine, loading files, and running the program
-- (C: src/spim-utils.c).
module SpimUtils
  ( ExceptionFiles (..)
  , initializeWorld
  , readAssemblyFile
  , initializeRunStack
  , runProgram
  , writeStartupMessage
  ) where

import Assemble (kTextBeginsAtPoint, textBeginsAtPoint)
import Control.Exception (IOException, try)
import Control.Monad (forM, forM_, unless, when)
import qualified Data.ByteString.Char8 as B
import Data.Bits (complement, (.&.), (.|.))
import Data.Char (ord)
import Data.Int (Int32)
import Data.Word (Word32)
import DataSegment
import Foreign.C.String (CString, peekCAString)
import Foreign.Marshal.Array (peekArray0)
import Foreign.Ptr (Ptr, nullPtr)
import Foreign.Storable (peek)
import InstFormat
import Memory
import Output
import Parser
import Registers
import Run (runSpim)
import Scanner (scannerInit)
import State
import SymbolTable
import System.Directory (doesDirectoryExist)

foreign import ccall "&environ" c_environ :: Ptr (Ptr CString)

-- | Which exception handler to load: none, the default one (its text and
-- the name to show for it), or files named by the user (separated by @;@).
data ExceptionFiles = NoHandler | DefaultHandler String String | HandlerFiles String

-- | Make a fresh machine: memory, registers, symbol table, and the
-- exception handler (C: @initialize_world@).
initializeWorld :: Spim -> ExceptionFiles -> Bool -> IO ()
initializeWorld s handler printMessage = do
  makeMemory s
  initializeRegisters s
  initializeSymbolTable s
  kTextBeginsAtPoint s kTextBot
  kDataBeginsAtPoint s kDataBot
  dataBeginsAtPoint s dataBot
  textBeginsAtPoint s textBot
  case handler of
    NoHandler -> pure ()
    _ -> do
      oldBare :: Bool <- get (optBare s)
      oldPseudo :: Bool <- get (optAcceptPseudo s)
      set (optBare s) False
      set (optAcceptPseudo s) True
      case handler of
        DefaultHandler text name -> do
          assembleText s text name
          when printMessage $ writeErr ("Loaded: " ++ name ++ "\n")
        HandlerFiles names ->
          forM_ (splitOn ';' names) $ \file -> do
            okRead :: Bool <- readAssemblyFile s file
            unless okRead $ fatalError ("Cannot read exception handler: " ++ file ++ "\n")
            when printMessage $ writeErr ("Loaded: " ++ file ++ "\n")
      set (optBare s) oldBare
      set (optAcceptPseudo s) oldPseudo
      unless oldBare $ do
        _ <- makeLabelGlobal s "main"
        _ <- recordLabel s "main" 0 False
        pure ()
  scannerInit s ""
  deleteAllBreakpoints s
  where
    -- strtok: the pieces between separators, skipping empty ones.
    splitOn :: Char -> String -> [String]
    splitOn c str = case break (== c) (dropWhile (== c) str) of
      ("", _) -> []
      (piece, rest) -> piece : splitOn c rest

-- | C: @write_startup_message@.
writeStartupMessage :: IO ()
writeStartupMessage =
  writeErr
    ( "SPIM Version 9.1.24 of August 1, 2023 (final)\n"
        ++ "Copyright 1990-2023 by James Larus.\n"
        ++ "All Rights Reserved.\n"
        ++ "SPIM is distributed under a BSD license.\n"
        ++ "See the file README for a full copyright notice.\n"
    )

-- | Zero the registers and set the few with starting values
-- (C: @initialize_registers@).
initializeRegisters :: Spim -> IO ()
initializeRegisters s = do
  forM_ [0 .. 31] $ \n -> setFprWord s n 0 >> setGpr s n 0
  setGpr s 29 (fromIntegral (stackTop - 4 - 4096))
  set (regHI s) 0
  set (regLO s) 0
  set (regPC s) 0
  forM_ [cp0BadVAddr, cp0Count, cp0Compare, cp0Cause, cp0Epc, cp0Config] $ \r -> setCp0 s r 0
  setCp0 s cp0Status (fromIntegral ((statusCU .&. 0x30000000) .|. statusIM .|. statusUM))
  setFcr s 0 (0x8000 + 0x1000 + 0x800)
  setFcr s 31 0

-- | Assemble one file's text (the body of C's @read_assembly_file@).
assembleText :: Spim -> String -> String -> IO ()
assembleText s text name = do
  parserInit s text name
  _ <- parseFile s
  err :: Bool <- get (parseErrorOccurred s)
  flushLocalLabels s (not err)
  endOfAssemblyFile s

-- | Read and assemble a file; False if it cannot be opened
-- (C: @read_assembly_file@).
readAssemblyFile :: Spim -> String -> IO Bool
readAssemblyFile s name = do
  isDir :: Bool <- doesDirectoryExist name
  contents :: Either IOException B.ByteString <-
    if isDir
      then pure (Right B.empty)
      else try (B.readFile name) :: IO (Either IOException B.ByteString)
  case contents of
    Left _ -> do
      cError ("Cannot open file: `" ++ name ++ "'\n")
      pure False
    Right bytes -> do
      assembleText s (B.unpack bytes) name
      pure True

-- | Put the environment and the program's arguments on the stack, as a
-- Unix @main(argc, argv, envp)@ would receive them (C: @initialize_run_stack@).
initializeRunStack :: Spim -> [String] -> IO ()
initializeRunStack s argv = do
  env :: [String] <- readEnvironment
  setGpr s 29 (fromIntegral (stackTop - 1))
  envAddrs :: [Word32] <- forM env (copyStrToStack s)
  argAddrs :: [Word32] <- forM argv (copyStrToStack s)
  sp :: Word32 <- fromIntegral <$> getGpr s 29 :: IO Word32
  let aligned :: Word32
      aligned = ((sp .&. complement 3) - 4) .&. complement 7
  setGpr s 29 (fromIntegral aligned)
  _ <- copyIntToStack s 0
  forM_ (reverse envAddrs) $ \a -> copyIntToStack s (fromIntegral a) >>= setGpr s 6 . fromIntegral
  _ <- copyIntToStack s 0
  forM_ (reverse argAddrs) $ \a -> copyIntToStack s (fromIntegral a) >>= setGpr s 5 . fromIntegral
  setGpr s 4 (fromIntegral (length argv))
  sp' :: Word32 <- fromIntegral <$> getGpr s 29
  memWriteWord s sp' (fromIntegral (length argv))

-- | The process's environment strings, in order, as bytes.
readEnvironment :: IO [String]
readEnvironment = do
  envp :: Ptr CString <- peek c_environ
  if envp == nullPtr then pure [] else peekArray0 nullPtr envp >>= mapM peekCAString

-- | Push a string (with its NUL) byte by byte; returns its address.
copyStrToStack :: Spim -> String -> IO Word32
copyStrToStack s str = do
  forM_ (reverse (map ord str ++ [0])) $ \b -> do
    sp :: Int32 <- getGpr s 29
    memWriteByte s (fromIntegral sp) (fromIntegral b)
    setGpr s 29 (sp - 1)
  (+ 1) . fromIntegral <$> getGpr s 29

-- | Push a word; returns its address.
copyIntToStack :: Spim -> Int32 -> IO Word32
copyIntToStack s n = do
  sp :: Int32 <- getGpr s 29
  memWriteWord s (fromIntegral sp) n
  setGpr s 29 (sp - 4)
  pure (fromIntegral sp)

-- | Run from an address for some steps; step over a breakpoint first if
-- asked.  Returns (stopped at a breakpoint, can continue)
-- (C: @run_program@).
runProgram :: Spim -> Word32 -> Int -> Bool -> Bool -> IO (Bool, Bool)
runProgram s pc0 steps0 display contBkpt = do
  atBreak :: Bool <- if contBkpt then instIsBreakpoint s pc0 else pure False
  (pc, steps) :: (Word32, Int) <-
    if atBreak
      then do
        cur :: Word32 <- get (regPC s)
        let addr :: Word32
            addr = if cur == 0 then pc0 else cur
        deleteBreakpoint s addr
        set (exceptionOccurred s) False
        _ <- runSpim s addr 1 display
        addBreakpoint s addr
        cur' :: Word32 <- get (regPC s)
        pure (cur', steps0 - 1)
      else pure (pc0, steps0)
  set (exceptionOccurred s) False
  continuable :: Bool <- runSpim s pc steps display
  exc :: Bool <- get (exceptionOccurred s)
  code :: Int <- exceptionCode s
  if exc && code == excBp
    then do
      st :: Int32 <- getCp0 s cp0Status
      setCp0 s cp0Status (st .&. complement (fromIntegral statusEXL))
      pure (True, continuable)
    else pure (False, continuable)
