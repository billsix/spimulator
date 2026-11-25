{-# LANGUAGE ForeignFunctionInterface #-}

-- | Setting up the machine, loading files, and running the program
-- (C: src/spim-utils.c).
module SpimUtils
  ( ExceptionFiles (..),
    initializeWorld,
    readAssemblyFile,
    initializeRunStack,
    runProgram,
    writeStartupMessage,
  )
where

import Assemble (kernelTextBeginsAtPoint, textBeginsAtPoint)
import Control.Exception (IOException, try)
import Control.Monad (forM, forM_, unless, when)
import Data.Bits (complement, (.&.), (.|.))
import qualified Data.ByteString.Char8 as B
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
import Scanner (scannerInitialize)
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
  kernelTextBeginsAtPoint s kernelTextBottom
  kernelDataBeginsAtPoint s kernelDataBottom
  dataBeginsAtPoint s dataBottom
  textBeginsAtPoint s textBottom
  case handler of
    NoHandler -> pure ()
    _ -> do
      oldBare :: Bool <- get (optionBare s)
      oldPseudo :: Bool <- get (optionAcceptPseudo s)
      set (optionBare s) False
      set (optionAcceptPseudo s) True
      case handler of
        DefaultHandler text name -> do
          assembleText s text name
          when printMessage $ writeError ("Loaded: " ++ name ++ "\n")
        HandlerFiles names ->
          forM_ (splitOn ';' names) $ \file -> do
            okRead :: Bool <- readAssemblyFile s file
            unless okRead $ fatalError ("Cannot read exception handler: " ++ file ++ "\n")
            when printMessage $ writeError ("Loaded: " ++ file ++ "\n")
      set (optionBare s) oldBare
      set (optionAcceptPseudo s) oldPseudo
      unless oldBare $ do
        _ <- makeLabelGlobal s "main"
        _ <- recordLabel s "main" 0 False
        pure ()
  scannerInitialize s ""
  deleteAllBreakpoints s
  where
    -- strtok: the pieces between separators, skipping empty ones.
    splitOn :: Char -> String -> [String]
    splitOn c string = case break (== c) (dropWhile (== c) string) of
      ("", _) -> []
      (piece, rest) -> piece : splitOn c rest

-- | C: @write_startup_message@.
writeStartupMessage :: IO ()
writeStartupMessage =
  writeError
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
  forM_ [0 .. 31] $ \n -> setFloatingPointRegisterWord s n 0 >> setGeneralPurposeRegister s n 0
  setGeneralPurposeRegister s 29 (fromIntegral (stackTop - 4 - 4096))
  set (registerMultiplyDivideHigh s) 0
  set (registerMultiplyDivideLow s) 0
  set (registerProgramCounter s) 0
  forM_ [coprocessor0BadVirtualAddress, coprocessor0Count, coprocessor0Compare, coprocessor0Cause, coprocessor0ExceptionProgramCounter, coprocessor0Config] $ \r -> setCoprocessor0 s r 0
  setCoprocessor0 s coprocessor0Status (fromIntegral ((statusCoprocessorUsable .&. 0x30000000) .|. statusInterruptMask .|. statusUserMode))
  setFloatingPointControlRegister s 0 (0x8000 + 0x1000 + 0x800)
  setFloatingPointControlRegister s 31 0

-- | Assemble one file's text (the body of C's @read_assembly_file@).
assembleText :: Spim -> String -> String -> IO ()
assembleText s text name = do
  parserInitialize s text name
  _ <- parseFile s
  errorOccurred :: Bool <- get (parseErrorOccurred s)
  flushLocalLabels s (not errorOccurred)
  endOfAssemblyFile s

-- | Read and assemble a file; False if it cannot be opened
-- (C: @read_assembly_file@).
readAssemblyFile :: Spim -> String -> IO Bool
readAssemblyFile s name = do
  isDirective :: Bool <- doesDirectoryExist name
  contents :: Either IOException B.ByteString <-
    if isDirective
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
  environment :: [String] <- readEnvironment
  setGeneralPurposeRegister s 29 (fromIntegral (stackTop - 1))
  environmentAddresses :: [Word32] <- forM environment (copyStringToStack s)
  argumentAddresses :: [Word32] <- forM argv (copyStringToStack s)
  sp :: Word32 <- fromIntegral <$> getGeneralPurposeRegister s 29 :: IO Word32
  -- One word below the word-aligned stack pointer, then down to a multiple of 8.
  setGeneralPurposeRegister s 29 (fromIntegral (((sp .&. complement 3) - 4) .&. complement 7))
  _ <- copyIntegerToStack s 0
  forM_ (reverse environmentAddresses) $ \a -> copyIntegerToStack s (fromIntegral a) >>= setGeneralPurposeRegister s 6 . fromIntegral
  _ <- copyIntegerToStack s 0
  forM_ (reverse argumentAddresses) $ \a -> copyIntegerToStack s (fromIntegral a) >>= setGeneralPurposeRegister s 5 . fromIntegral
  setGeneralPurposeRegister s 4 (fromIntegral (length argv))
  sp' :: Word32 <- fromIntegral <$> getGeneralPurposeRegister s 29
  memoryWriteWord s sp' (fromIntegral (length argv))

-- | The process's environment strings, in order, as bytes.
readEnvironment :: IO [String]
readEnvironment = do
  envp :: Ptr CString <- peek c_environ
  if envp == nullPtr then pure [] else peekArray0 nullPtr envp >>= mapM peekCAString

-- | Push a string (with its NUL) byte by byte; returns its address.
copyStringToStack :: Spim -> String -> IO Word32
copyStringToStack s string = do
  forM_ (reverse (map ord string ++ [0])) $ \b -> do
    sp :: Int32 <- getGeneralPurposeRegister s 29
    memoryWriteByte s (fromIntegral sp) (fromIntegral b)
    setGeneralPurposeRegister s 29 (sp - 1)
  (+ 1) . fromIntegral <$> getGeneralPurposeRegister s 29

-- | Push a word; returns its address.
copyIntegerToStack :: Spim -> Int32 -> IO Word32
copyIntegerToStack s n = do
  sp :: Int32 <- getGeneralPurposeRegister s 29
  memoryWriteWord s (fromIntegral sp) n
  setGeneralPurposeRegister s 29 (sp - 4)
  pure (fromIntegral sp)

-- | Run from an address for some steps; step over a breakpoint first if
-- asked.  Returns (stopped at a breakpoint, can continue)
-- (C: @run_program@).
runProgram :: Spim -> Word32 -> Int -> Bool -> Bool -> IO (Bool, Bool)
runProgram s programCounter0 steps0 display continueBreakpoint = do
  atBreak :: Bool <- if continueBreakpoint then instructionIsBreakpoint s programCounter0 else pure False
  (programCounter, steps) :: (Word32, Int) <-
    if atBreak
      then do
        current :: Word32 <- get (registerProgramCounter s)
        let address :: Word32
            address = if current == 0 then programCounter0 else current
        deleteBreakpoint s address
        set (exceptionOccurred s) False
        _ <- runSpim s address 1 display
        addBreakpoint s address
        current' :: Word32 <- get (registerProgramCounter s)
        pure (current', steps0 - 1)
      else pure (programCounter0, steps0)
  set (exceptionOccurred s) False
  continuable :: Bool <- runSpim s programCounter steps display
  exception :: Bool <- get (exceptionOccurred s)
  code :: Int <- exceptionCode s
  if exception && code == exceptionBreakpoint
    then do
      st :: Int32 <- getCoprocessor0 s coprocessor0Status
      setCoprocessor0 s coprocessor0Status (st .&. complement (fromIntegral statusExceptionLevel))
      pure (True, continuable)
    else pure (False, continuable)
