-- | System calls and exception reports (C: src/syscall.c, and the console
-- input routines of src/spim.c).
--
-- A MIPS program asks for a service by putting its number in @$v0@ (and
-- arguments in @$a0@..@$a2@, @$f12@) and executing @syscall@.  The C
-- engine's services print and read through the C library; the file
-- services (open, read, write, close) are passed straight to the host.
module Syscall
  ( doSyscall
  , handleException
  ) where

import CFormat (cStrtod, formatF, formatG)
import Control.Monad (forM_, when)
import Data.Array.IO (getBounds, readArray, writeArray)
import Data.Char (chr)
import Data.Int (Int32)
import Data.Word (Word32, Word8)
import Foreign.C.String (withCString)
import Foreign.Marshal.Alloc (allocaBytes)
import Foreign.Ptr (plusPtr)
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
    go n
      | n <= 1 = pure []
      | otherwise = do
          b <- readByte
          case b of
            Nothing -> pure []
            Just c
              | c == 10 -> pure [chr 10]
              | otherwise -> (chr (fromIntegral c) :) <$> go (n - 1)

-- | Read an integer the way @scanf("%d")@ would, for read_int: skip white
-- space, an optional sign, then digits.  The byte after the digits is
-- consumed (C: @read_int_input@).
readIntInput :: IO (Maybe Int)
readIntInput = do
  c0 <- skipSpace
  (sign, c1) <- case c0 of
    Just 45 -> (,) (-1) <$> readByte
    Just 43 -> (,) 1 <$> readByte
    _ -> pure (1, c0)
  digits sign False 0 c1
  where
    skipSpace = do
      b <- readByte
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
  v0 <- getGpr s 2
  a0 <- getGpr s 4
  a1 <- getGpr s 5
  a2 <- getGpr s 6
  case v0 of
    1 -> writeOut (show a0) >> continue
    2 -> do
      f <- fprSingle s 12
      writeOut (formatF 8 (float2Double f))
      continue
    3 -> do
      d <- doubleWords s 12
      writeOut (formatG 18 d)
      continue
    4 -> readCString s (fromIntegral a0) >>= writeOut >> continue
    5 -> do
      r <- readIntInput
      setGpr s 2 (maybe 0 fromIntegral r)
      setGpr s 7 (maybe 1 (const 0) r)
      continue
    6 -> do
      str <- readInput 256
      setFprSingle s 0 (double2Float (cStrtod str))
      continue
    7 -> do
      str <- readInput 256
      setFprDouble s 0 (cStrtod str)
      continue
    8 -> do
      ref <- memReference s (fromIntegral a0)
      str <- readInput (fromIntegral a1)
      case ref of
        Nothing -> pure ()
        Just (arr, off) -> do
          (_, hi) <- getBounds arr
          let bytes = zip [off ..] (map (fromIntegral . fromEnum) str ++ [0 | fromIntegral a1 - length str > 0])
          forM_ bytes $ \(i, b) -> when (i <= hi) (writeArray arr i b)
      setGpr s 7 (if null str then 1 else 0)
      continue
    9 -> do
      top <- get (dataTop s)
      expandData s (fromIntegral a0)
      setGpr s 2 (fromIntegral top)
      continue
    10 -> set (spimReturnValue s) 0 >> pure False
    11 -> writeOut [chr (fromIntegral (fromIntegral a0 :: Word8))] >> continue
    12 -> do
      str <- readInput 2
      setGpr s 2 (case str of (c : _) -> fromIntegral (fromEnum c); [] -> -1)
      continue
    13 -> do
      path <- readCString s (fromIntegral a0)
      fd <- withCString path $ \p -> cOpen p (fromIntegral a1) (fromIntegral a2)
      setGpr s 2 (fromIntegral fd)
      continue
    14 -> do
      _ <- memReference s (fromIntegral (a1 + a2 - 1))
      ref <- memReference s (fromIntegral a1)
      n <- transfer ref (fromIntegral a2) $ \p len -> cRead (fromIntegral a0) p len
      setGpr s 2 n
      continue
    15 -> do
      _ <- memReference s (fromIntegral (a1 + a2 - 1))
      ref <- memReference s (fromIntegral a1)
      n <- transferOut ref (fromIntegral a2) $ \p len -> cWrite (fromIntegral a0) p len
      setGpr s 2 n
      continue
    16 -> do
      r <- cClose (fromIntegral a0)
      setGpr s 2 (fromIntegral r)
      continue
    17 -> set (spimReturnValue s) a0 >> pure False
    _ -> runError (printf "Unknown system call: %d\n" v0)
  where
    continue = pure True
    -- read(2) into simulated memory: through a host buffer, then copied in.
    transfer ref len act = case ref of
      Nothing -> pure (-1)
      Just (arr, off) -> do
        (_, hi) <- getBounds arr
        let room = max 0 (min len (hi - off + 1))
        allocaBytes (max 1 len) $ \p -> do
          n <- act p (fromIntegral len)
          forM_ [0 .. min (fromIntegral n) room - 1] $ \i -> peekByteOff p i >>= writeArray arr (off + i)
          pure (fromIntegral n :: Int32)
    -- write(2) from simulated memory: copied into a host buffer first.
    transferOut ref len act = case ref of
      Nothing -> pure (-1)
      Just (arr, off) -> do
        (_, hi) <- getBounds arr
        let room = max 0 (min len (hi - off + 1))
        allocaBytes (max 1 len) $ \p -> do
          forM_ [0 .. room - 1] $ \i -> readArray arr (off + i) >>= (pokeByteOff p i :: Word8 -> IO ())
          n <- act (p `plusPtr` 0) (fromIntegral room)
          pure (fromIntegral n :: Int32)

-- | Report an exception and go to the handler at 0x80000180
-- (C: @handle_exception@).
handleException :: Spim -> IO ()
handleException s = do
  quiet <- get (optQuiet s)
  code <- exceptionCode s
  epc <- getCp0 s cp0Epc
  bad <- getCp0 s cp0BadVAddr
  let say msg = when (not quiet) (cError msg)
      hex32 :: Int32 -> Word32
      hex32 = fromIntegral
  when (not quiet && code /= excInt) $ cError (printf "Exception occurred at PC=0x%08x\n" (hex32 epc))
  set (exceptionOccurred s) False
  set (regPC s) exceptionAddr
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
