-- | System calls and exception reports (C: src/syscall.c, and the console
-- input routines of src/spim.c).
--
-- A MIPS program asks for a service by putting its number in @$v0@ (and
-- arguments in @$a0@..@$a2@, @$f12@) and executing @syscall@.  The C
-- engine's services print and read through the C library; the file
-- services (open, read, write, close) are passed straight to the host.
module Syscall
  ( doSyscall,
    handleException,
  )
where

import CFormat (cStrtod, formatF, formatG)
import Control.Monad (forM_, when)
import Data.Array.IO (readArray, writeArray)
import Data.Char (chr)
import Data.Int (Int32)
import Data.Word (Word32, Word8)
import Foreign.C.String (withCString)
import Foreign.C.Types (CInt, CSize)
import Foreign.Marshal.Alloc (allocaBytes)
import Foreign.Ptr (Ptr, plusPtr)
import Foreign.Storable (peekByteOff, pokeByteOff)
import GHC.Float (double2Float, float2Double)
import Memory
import Output
import Registers
import State
import Text.Printf (printf)

-- | C's @read_input@: read up to n-1 bytes, through the first newline;
-- returns the bytes read.
readInput :: Int -> IO String
readInput size = go size
  where
    go :: Int -> IO String
    go n
      | n <= 1 = pure []
      | otherwise = do
          b :: Maybe Word8 <- readByte
          case b of
            Nothing -> pure []
            Just c
              | c == 10 -> pure [chr 10]
              | otherwise -> (chr (fromIntegral c) :) <$> go (n - 1)

-- | Read an integer the way @scanf("%d")@ would, for read_int: skip white
-- space, an optional sign, then digits.  The byte after the digits is
-- consumed (C: @read_int_input@).
readIntegerInput :: IO (Maybe Int)
readIntegerInput = do
  c0 :: Maybe Word8 <- skipSpace
  (sign, c1) :: (Int, Maybe Word8) <- case c0 of
    Just 45 -> (,) (-1) <$> readByte
    Just 43 -> (,) 1 <$> readByte
    _ -> pure (1, c0)
  digits sign False 0 c1
  where
    skipSpace :: IO (Maybe Word8)
    skipSpace = do
      b :: Maybe Word8 <- readByte
      case b of
        Just c | c `elem` [32, 9, 10, 13] -> skipSpace
        _ -> pure b
    digits :: Int -> Bool -> Int -> Maybe Word8 -> IO (Maybe Int)
    digits sign got v b = case b of
      Just c | c >= 48 && c <= 57 -> readByte >>= digits sign True (v * 10 + fromIntegral (c - 48))
      _ -> pure (if got then Just (sign * v) else Nothing)

-- | Do the system call in @$v0@.  False means the program exited
-- (C: @do_syscall@).
doSyscall :: Spim -> IO Bool
doSyscall s = do
  v0 :: Int32 <- getGeneralPurposeRegister s 2
  a0 :: Int32 <- getGeneralPurposeRegister s 4
  a1 :: Int32 <- getGeneralPurposeRegister s 5
  a2 :: Int32 <- getGeneralPurposeRegister s 6
  case v0 of
    1 -> writeOut (show a0) >> continue
    2 -> do
      f :: Float <- floatingPointRegisterSingle s 12
      writeOut (formatF 8 (float2Double f))
      continue
    3 -> do
      d :: Double <- doubleWords s 12
      writeOut (formatG 18 d)
      continue
    4 -> readCString s (fromIntegral a0) >>= writeOut >> continue
    5 -> do
      r :: Maybe Int <- readIntegerInput
      setGeneralPurposeRegister s 2 (maybe 0 fromIntegral r)
      setGeneralPurposeRegister s 7 (maybe 1 (const 0) r)
      continue
    6 -> do
      string :: String <- readInput 256
      setFloatingPointRegisterSingle s 0 (double2Float (cStrtod string))
      continue
    7 -> do
      string :: String <- readInput 256
      setFloatingPointRegisterDouble s 0 (cStrtod string)
      continue
    8 -> do
      ref :: Maybe SegmentRef <- memoryReference s (fromIntegral a0)
      string :: String <- readInput (fromIntegral a1)
      case ref of
        Nothing -> pure ()
        Just (array, offset, hi) -> do
          let bytes :: [(Int, Word8)]
              bytes = zip [offset ..] (map (fromIntegral . fromEnum) string ++ [0 | fromIntegral a1 - length string > 0])
          forM_ bytes $ \(i, b) -> when (i <= hi) (writeArray array i b)
      setGeneralPurposeRegister s 7 (if null string then 1 else 0)
      continue
    9 -> do
      top :: Word32 <- get (dataTop s)
      expandData s (fromIntegral a0)
      setGeneralPurposeRegister s 2 (fromIntegral top)
      continue
    10 -> set (spimReturnValue s) 0 >> pure False
    11 -> writeOut [chr (fromIntegral (fromIntegral a0 :: Word8))] >> continue
    12 -> do
      string :: String <- readInput 2
      setGeneralPurposeRegister s 2 (case string of (c : _) -> fromIntegral (fromEnum c); [] -> -1)
      continue
    13 -> do
      path :: String <- readCString s (fromIntegral a0)
      floatingPointDestination :: CInt <- withCString path $ \p -> cOpen p (fromIntegral a1) (fromIntegral a2)
      setGeneralPurposeRegister s 2 (fromIntegral floatingPointDestination)
      continue
    14 -> do
      _ <- memoryReference s (fromIntegral (a1 + a2 - 1))
      ref :: Maybe SegmentRef <- memoryReference s (fromIntegral a1)
      n :: Int32 <- transfer ref (fromIntegral a2) $ \p byteLength -> cRead (fromIntegral a0) p byteLength
      setGeneralPurposeRegister s 2 n
      continue
    15 -> do
      _ <- memoryReference s (fromIntegral (a1 + a2 - 1))
      ref :: Maybe SegmentRef <- memoryReference s (fromIntegral a1)
      n :: Int32 <- transferOut ref (fromIntegral a2) $ \p byteLength -> cWrite (fromIntegral a0) p byteLength
      setGeneralPurposeRegister s 2 n
      continue
    16 -> do
      r :: CInt <- cClose (fromIntegral a0)
      setGeneralPurposeRegister s 2 (fromIntegral r)
      continue
    17 -> set (spimReturnValue s) a0 >> pure False
    _ -> runError (printf "Unknown system call: %d\n" v0)
  where
    continue :: IO Bool
    continue = pure True
    -- read(2) into simulated memory: through a host buffer, then copied in.
    transfer :: Maybe SegmentRef -> Int -> (Ptr Word8 -> CSize -> IO CInt) -> IO Int32
    transfer ref byteLength act = case ref of
      Nothing -> pure (-1)
      Just (array, offset, hi) -> do
        let room :: Int
            room = max 0 (min byteLength (hi - offset + 1))
        allocaBytes (max 1 byteLength) $ \p -> do
          n :: CInt <- act p (fromIntegral byteLength)
          forM_ [0 .. min (fromIntegral n) room - 1] $ \i -> peekByteOff p i >>= writeArray array (offset + i)
          pure (fromIntegral n :: Int32)
    -- write(2) from simulated memory: copied into a host buffer first.
    transferOut :: Maybe SegmentRef -> Int -> (Ptr Word8 -> CSize -> IO CInt) -> IO Int32
    transferOut ref byteLength act = case ref of
      Nothing -> pure (-1)
      Just (array, offset, hi) -> do
        let room :: Int
            room = max 0 (min byteLength (hi - offset + 1))
        allocaBytes (max 1 byteLength) $ \p -> do
          forM_ [0 .. room - 1] $ \i -> readArray array (offset + i) >>= (pokeByteOff p i :: Word8 -> IO ())
          n :: CInt <- act (p `plusPtr` 0) (fromIntegral room)
          pure (fromIntegral n :: Int32)

-- | Report an exception and go to the handler at 0x80000180
-- (C: @handle_exception@).
handleException :: Spim -> IO ()
handleException s = do
  quiet :: Bool <- get (optionQuiet s)
  code :: Int <- exceptionCode s
  exceptionProgramCounter :: Int32 <- getCoprocessor0 s coprocessor0ExceptionProgramCounter
  bad :: Int32 <- getCoprocessor0 s coprocessor0BadVirtualAddress
  let say :: String -> IO ()
      say message = when (not quiet) (cError message)
      hex32 :: Int32 -> Word32
      hex32 = fromIntegral
  when (not quiet && code /= exceptionInterrupt) $ cError (printf "Exception occurred at PC=0x%08x\n" (hex32 exceptionProgramCounter))
  set (exceptionOccurred s) False
  set (registerProgramCounter s) exceptionAddress
  case code of
    0 -> pure ()
    4 -> say (printf "  Unaligned address in instruction/data fetch: 0x%08x\n" (hex32 bad))
    5 -> say (printf "  Unaligned address in store: 0x%08x\n" (hex32 bad))
    6 -> say (printf "  Bad address in text read: 0x%08x\n" (hex32 bad))
    7 -> say (printf "  Bad address in data/stack read: 0x%08x\n" (hex32 bad))
    8 -> say "  Error in syscall\n"
    9 -> set (exceptionOccurred s) False
    10 -> say "  Reserved instruction execution\n"
    11 -> say "  Coprocessor unuable\n"
    12 -> say "  Arithmetic overflow\n"
    13 -> say "  Trap\n"
    15 -> say "  Floating point\n"
    _ -> say (printf "Unknown exception: %d\n" code)
