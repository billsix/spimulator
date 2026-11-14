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
    kernelDataBeginsAtPoint,
    alignData,
    setDataAlignment,
    enableDataAlignment,
    setDataLocation,
    incrementDataLocation,
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
import Registers (setGeneralPurposeRegister)
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
dataBeginsAtPoint s address = do
  bare :: Bool <- get (optionBare s)
  if bare
    then set (nextDataLocation s) address
    else do
      set (nextGlobalPointerItemAddress s) address
      let mid :: Word32
          mid = address + fromIntegral (32 * kilo)
      set (globalPointerMidpoint s) mid
      setGeneralPurposeRegister s 28 (fromIntegral mid)
      set (nextDataLocation s) (address + fromIntegral (64 * kilo))

kernelDataBeginsAtPoint :: Spim -> Word32 -> IO ()
kernelDataBeginsAtPoint s = set (nextKernelDataLocation s)

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
          ref = if k then nextKernelDataLocation s else nextDataLocation s
      programCounter :: Word32 <- get ref
      let programCounter' :: Word32
          programCounter' = alignUp n programCounter
      set ref programCounter'
      fixCurrentLabelAddress s programCounter'

-- | Align for a datum, if automatic alignment is on.
setDataAlignment :: Spim -> Int -> IO ()
setDataAlignment s n = do
  on :: Bool <- get (dataAutoAlign s)
  when on $ alignData s n

enableDataAlignment :: Spim -> IO ()
enableDataAlignment s = set (dataAutoAlign s) True

setDataLocation :: Spim -> Word32 -> IO ()
setDataLocation s address = do
  k :: Bool <- get (dataInKernel s)
  set (if k then nextKernelDataLocation s else nextDataLocation s) address

-- | Move the data location forward, growing the segment in 64 KB steps
-- when it runs past the end (C: @increment_data_pc@).
incrementDataLocation :: Spim -> Int -> IO ()
incrementDataLocation s delta = do
  k :: Bool <- get (dataInKernel s)
  let roundUp64k :: Int -> Int
      roundUp64k v = (v + 64 * kilo - 1) .&. complement (64 * kilo - 1)
  if k
    then do
      modify (nextKernelDataLocation s) (+ fromIntegral delta)
      programCounter :: Word32 <- get (nextKernelDataLocation s)
      top :: Word32 <- get (kernelDataTop s)
      when (top <= programCounter) $ expandKernelData s (roundUp64k (fromIntegral (programCounter - top) + 1))
    else do
      modify (nextDataLocation s) (+ fromIntegral delta)
      programCounter :: Word32 <- get (nextDataLocation s)
      top :: Word32 <- get (dataTop s)
      when (top <= programCounter) $ expandData s (roundUp64k (fromIntegral (programCounter - top) + 1))

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
storeString s string nul = do
  at :: Word32 <- currentDataLocation s
  nested s $ do
    forM_ string $ \c -> storeByte s (fromIntegral (ord c))
    when nul $ storeByte s 0
  outer :: Bool <- isOuter s
  when outer $ fire s (EventDataString at (length string) nul)

-- | @.byte@ (C: @store_byte@).
storeByte :: Spim -> Int32 -> IO ()
storeByte s value = do
  at :: Word32 <- currentDataLocation s
  memoryWriteByte s at value
  incrementDataLocation s 1
  outer :: Bool <- isOuter s
  when outer $ fire s (EventDataByte at value)

-- | @.half@ (C: @store_half@).
storeHalf :: Spim -> Int32 -> IO ()
storeHalf s value = do
  at :: Word32 <- currentDataLocation s
  outer :: Bool <- isOuter s
  nested s $
    if at .&. 1 /= 0
      then do
        storeByte s (value .&. 0xff)
        storeByte s ((value `shiftR` 8) .&. 0xff)
      else do
        memoryWriteHalf s at value
        incrementDataLocation s 2
  when outer $ fire s (EventDataHalf at value)

-- | @.word@ (C: @store_word@).
storeWord :: Spim -> Int32 -> IO ()
storeWord s value = do
  at :: Word32 <- currentDataLocation s
  outer :: Bool <- isOuter s
  nested s $
    if at .&. 3 /= 0
      then do
        storeHalf s (value .&. 0xffff)
        storeHalf s ((value `shiftR` 16) .&. 0xffff)
      else do
        memoryWriteWord s at value
        incrementDataLocation s 4
  when outer $ fire s (EventDataWord at value)

-- | @.double@ (C: @store_double@): the two words of the double, low first.
storeDouble :: Spim -> Double -> IO ()
storeDouble s value = do
  at :: Word32 <- currentDataLocation s
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
        currentDataLocation s >>= \a -> memoryWriteWord s a w0
        incrementDataLocation s 4
        currentDataLocation s >>= \a -> memoryWriteWord s a w1
        incrementDataLocation s 4
  when outer $ fire s (EventDataDouble at value)

-- | @.float@ (C: @store_float@): the single's bit pattern.
storeFloat :: Spim -> Double -> IO ()
storeFloat s value = do
  let bits :: Int32
      bits = fromIntegral (castFloatToWord32 (double2Float value)) :: Int32
  at :: Word32 <- currentDataLocation s
  if at .&. 3 /= 0
    then do
      storeHalf s (bits .&. 0xffff)
      storeHalf s ((bits `shiftR` 16) .&. 0xffff)
    else do
      memoryWriteWord s at bits
      incrementDataLocation s 4
