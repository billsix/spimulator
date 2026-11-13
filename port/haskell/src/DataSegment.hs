-- | Laying out the data segment: the data directives' actions
-- (C: src/data.c).
--
-- The first 64 KB of the user data segment are the "small data" area that
-- @$gp@ points into the middle of; ordinary data starts after it.  Each
-- datum is aligned to its natural boundary unless @.align 0@ turned that
-- off.  A datum that does not fit on its boundary is stored a piece at a
-- time.
module DataSegment
  ( userKernelDataSegment,
    endOfAssemblyFile,
    dataBeginsAtPoint,
    kDataBeginsAtPoint,
    alignData,
    setDataAlignment,
    enableDataAlignment,
    setDataPc,
    incrementDataPc,
    storeString,
    storeByte,
    storeHalf,
    storeWord,
    storeDouble,
    storeFloat,
  )
where

import AsmCommon
import Control.Monad (forM_, when)
import Data.Bits (complement, shiftL, shiftR, (.&.))
import Data.Char (ord)
import Data.IORef (IORef)
import Data.Int (Int32)
import Data.Word (Word32, Word64)
import GHC.Float (castDoubleToWord64, castFloatToWord32, double2Float)
import Memory
import Registers (setGpr)
import State

-- | Send the following data to the kernel or the user data segment.
userKernelDataSegment :: Spim -> Bool -> IO ()
userKernelDataSegment s = set (dataInKernel s)

-- | Reset the per-file data state (C: @end_of_assembly_file@).
endOfAssemblyFile :: Spim -> IO ()
endOfAssemblyFile s = set (dataInKernel s) False >> set (dataAutoAlign s) True

-- | Start the data segment at an address; reserve the $gp area unless on
-- the bare machine (C: @data_begins_at_point@).
dataBeginsAtPoint :: Spim -> Word32 -> IO ()
dataBeginsAtPoint s addr = do
  bare :: Bool <- get (optBare s)
  if bare
    then set (nextDataPc s) addr
    else do
      set (nextGpItemAddr s) addr
      let mid :: Word32
          mid = addr + fromIntegral (32 * kilo)
      set (gpMidpoint s) mid
      setGpr s 28 (fromIntegral mid)
      set (nextDataPc s) (addr + fromIntegral (64 * kilo))

kDataBeginsAtPoint :: Spim -> Word32 -> IO ()
kDataBeginsAtPoint s = set (nextKDataPc s)

-- | Round an address up to a 2^n boundary.
alignUp :: Int -> Word32 -> Word32
alignUp n a = (a + (1 `shiftL` n) - 1) .&. (0xffffffff `shiftL` n)

-- | @.align n@: move to a 2^n boundary; @.align 0@ turns automatic
-- alignment off (C: @align_data@).
alignData :: Spim -> Int -> IO ()
alignData s n
  | n == 0 = set (dataAutoAlign s) False
  | otherwise = do
      k :: Bool <- get (dataInKernel s)
      let ref :: IORef Word32
          ref = if k then nextKDataPc s else nextDataPc s
      pc :: Word32 <- get ref
      let pc' :: Word32
          pc' = alignUp n pc
      set ref pc'
      fixCurrentLabelAddress s pc'

-- | Align for a datum, if automatic alignment is on.
setDataAlignment :: Spim -> Int -> IO ()
setDataAlignment s n = do
  on :: Bool <- get (dataAutoAlign s)
  when on $ alignData s n

enableDataAlignment :: Spim -> IO ()
enableDataAlignment s = set (dataAutoAlign s) True

setDataPc :: Spim -> Word32 -> IO ()
setDataPc s addr = do
  k :: Bool <- get (dataInKernel s)
  set (if k then nextKDataPc s else nextDataPc s) addr

-- | Move the data location forward, growing the segment in 64 KB steps
-- when it runs past the end (C: @increment_data_pc@).
incrementDataPc :: Spim -> Int -> IO ()
incrementDataPc s delta = do
  k :: Bool <- get (dataInKernel s)
  let roundUp64k :: Int -> Int
      roundUp64k v = (v + 64 * kilo - 1) .&. complement (64 * kilo - 1)
  if k
    then do
      modify (nextKDataPc s) (+ fromIntegral delta)
      pc :: Word32 <- get (nextKDataPc s)
      top :: Word32 <- get (kDataTop s)
      when (top <= pc) $ expandKData s (roundUp64k (fromIntegral (pc - top) + 1))
    else do
      modify (nextDataPc s) (+ fromIntegral delta)
      pc :: Word32 <- get (nextDataPc s)
      top :: Word32 <- get (dataTop s)
      when (top <= pc) $ expandData s (roundUp64k (fromIntegral (pc - top) + 1))

-- | Run a store one level deeper, so that its inner stores report no
-- events of their own.
nested :: Spim -> IO a -> IO a
nested s act = do
  modify (dataStoreDepth s) (+ 1)
  r :: a <- act
  modify (dataStoreDepth s) (subtract 1)
  pure r

isOuter :: Spim -> IO Bool
isOuter s = (== 0) <$> get (dataStoreDepth s)

-- | @.ascii@ / @.asciiz@ (C: @store_string@).
storeString :: Spim -> String -> Bool -> IO ()
storeString s str nul = do
  at :: Word32 <- currentDataPc s
  nested s $ do
    forM_ str $ \c -> storeByte s (fromIntegral (ord c))
    when nul $ storeByte s 0
  outer :: Bool <- isOuter s
  when outer $ fire s (EvDataString at (length str) nul)

-- | @.byte@ (C: @store_byte@).
storeByte :: Spim -> Int32 -> IO ()
storeByte s value = do
  at :: Word32 <- currentDataPc s
  memWriteByte s at value
  incrementDataPc s 1
  outer :: Bool <- isOuter s
  when outer $ fire s (EvDataByte at value)

-- | @.half@ (C: @store_half@).
storeHalf :: Spim -> Int32 -> IO ()
storeHalf s value = do
  at :: Word32 <- currentDataPc s
  outer :: Bool <- isOuter s
  nested s $
    if at .&. 1 /= 0
      then do
        storeByte s (value .&. 0xff)
        storeByte s ((value `shiftR` 8) .&. 0xff)
      else do
        memWriteHalf s at value
        incrementDataPc s 2
  when outer $ fire s (EvDataHalf at value)

-- | @.word@ (C: @store_word@).
storeWord :: Spim -> Int32 -> IO ()
storeWord s value = do
  at :: Word32 <- currentDataPc s
  outer :: Bool <- isOuter s
  nested s $
    if at .&. 3 /= 0
      then do
        storeHalf s (value .&. 0xffff)
        storeHalf s ((value `shiftR` 16) .&. 0xffff)
      else do
        memWriteWord s at value
        incrementDataPc s 4
  when outer $ fire s (EvDataWord at value)

-- | @.double@ (C: @store_double@): the two words of the double, low first.
storeDouble :: Spim -> Double -> IO ()
storeDouble s value = do
  at :: Word32 <- currentDataPc s
  outer :: Bool <- isOuter s
  let bits :: Word64
      bits = castDoubleToWord64 value
      w0 :: Int32
      w0 = fromIntegral bits :: Int32
      w1 :: Int32
      w1 = fromIntegral (bits `shiftR` 32) :: Int32
  nested s $
    if at .&. 7 /= 0
      then storeWord s w0 >> storeWord s w1
      else do
        currentDataPc s >>= \a -> memWriteWord s a w0
        incrementDataPc s 4
        currentDataPc s >>= \a -> memWriteWord s a w1
        incrementDataPc s 4
  when outer $ fire s (EvDataDouble at value)

-- | @.float@ (C: @store_float@): the single's bit pattern.
storeFloat :: Spim -> Double -> IO ()
storeFloat s value = do
  let bits :: Int32
      bits = fromIntegral (castFloatToWord32 (double2Float value)) :: Int32
  at :: Word32 <- currentDataPc s
  if at .&. 3 /= 0
    then do
      storeHalf s (bits .&. 0xffff)
      storeHalf s ((bits `shiftR` 16) .&. 0xffff)
    else do
      memWriteWord s at bits
      incrementDataPc s 4
