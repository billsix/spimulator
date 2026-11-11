-- | The register files and exceptions (C: include/registers.h, and
-- @raise_exception@ from src/run.c).
--
-- The general registers are 32 signed words.  The FPU has 32 single
-- registers, which the C engine overlays with 16 doubles: double register
-- 2k is the 64 bits of singles 2k (low half) and 2k+1 (high half).  This
-- port stores the 32 raw words and reinterprets their bits on each access,
-- which is what the C union does.
module Registers
  ( getGpr
  , setGpr
  , getCp0
  , setCp0
  , getFcr
  , setFcr
  , fprWord
  , setFprWord
  , fprSingle
  , setFprSingle
  , fprDouble
  , setFprDouble
  , doubleWords
  , fcc
  , setFcc
  , raiseException
  , exceptionCode
    -- * CP0 register numbers and bits
  , cp0BadVAddr
  , cp0Count
  , cp0Compare
  , cp0Status
  , cp0Cause
  , cp0Epc
  , cp0Config
  , statusEXL
  , statusIE
  , statusIM
  , statusCU
  , statusUM
  , statusMask
  , causeIP
  , causeBD
  , causeMask
  , configMask
  , exceptionAddr
    -- * Exception codes
  , excInt
  , excAdEL
  , excAdES
  , excIBE
  , excDBE
  , excSys
  , excBp
  , excRI
  , excCpU
  , excOv
  , excTr
  , excFPE
  , regNames
  ) where

import Data.Array.IO (readArray, writeArray)
import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.Word (Word32, Word64)
import GHC.Float (castDoubleToWord64, castFloatToWord32, castWord32ToFloat, castWord64ToDouble)
import Output (runError)
import State

-- | Register names, by number, for disassembly (C: @int_reg_names@).
regNames :: [String]
regNames =
  [ "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3"
  , "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7"
  , "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"
  , "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
  ]

getGpr :: Spim -> Int -> IO Int32
getGpr s = readArray (gpr s)

setGpr :: Spim -> Int -> Int32 -> IO ()
setGpr s = writeArray (gpr s)

-- | Coprocessor 0's register n.
getCp0 :: Spim -> Int -> IO Int32
getCp0 s n = readArray (cpRegs s) n

setCp0 :: Spim -> Int -> Int32 -> IO ()
setCp0 s n = writeArray (cpRegs s) n

-- | The FPU control register n (C: @FCR[n]@, coprocessor 1's registers).
getFcr :: Spim -> Int -> IO Int32
getFcr s n = readArray (cpRegs s) (32 + n)

setFcr :: Spim -> Int -> Int32 -> IO ()
setFcr s n = writeArray (cpRegs s) (32 + n)

cp0BadVAddr, cp0Count, cp0Compare, cp0Status, cp0Cause, cp0Epc, cp0Config :: Int
cp0BadVAddr = 8
cp0Count = 9
cp0Compare = 11
cp0Status = 12
cp0Cause = 13
cp0Epc = 14
cp0Config = 16

statusEXL, statusIE, statusIM, statusCU, statusUM, statusMask :: Word32
statusEXL = 0x2
statusIE = 0x1
statusIM = 0x0000ff00
statusCU = 0xf0000000
statusUM = 0x10
statusMask = statusCU .|. statusUM .|. statusIM .|. statusEXL .|. statusIE

causeIP, causeBD, causeExcCode, causeMask, configMask :: Word32
causeIP = 0x0000ff00
causeBD = 0x80000000
causeExcCode = 0x7c
causeMask = causeBD .|. causeIP .|. 0x8000 .|. 0x4000 .|. 0x2000 .|. 0x1000 .|. 0x800 .|. 0x400 .|. causeExcCode
configMask = 0x80000 .|. 0x60000 .|. 0x1c000 .|. 0x380

-- | Where the processor jumps on an exception (MIPS32).
exceptionAddr :: Word32
exceptionAddr = 0x80000180

excInt, excAdEL, excAdES, excIBE, excDBE, excSys, excBp, excRI, excCpU, excOv, excTr, excFPE :: Int
excInt = 0
excAdEL = 4
excAdES = 5
excIBE = 6
excDBE = 7
excSys = 8
excBp = 9
excRI = 10
excCpU = 11
excOv = 12
excTr = 13
excFPE = 15

-- | The ExcCode field of the Cause register (C: @CP0_ExCode@).
exceptionCode :: Spim -> IO Int
exceptionCode s = do
  cause :: Int32 <- getCp0 s cp0Cause
  pure (fromIntegral ((fromIntegral cause .&. causeExcCode) `shiftR` 2 :: Word32))

-- | The raw 32 bits of FP register n (C: @FPR_W@).
fprWord :: Spim -> Int -> IO Word32
fprWord s = readArray (fpRegs s)

setFprWord :: Spim -> Int -> Word32 -> IO ()
setFprWord s = writeArray (fpRegs s)

-- | FP register n as a single (C: @FPR_S@).
fprSingle :: Spim -> Int -> IO Float
fprSingle s n = castWord32ToFloat <$> fprWord s n

setFprSingle :: Spim -> Int -> Float -> IO ()
setFprSingle s n v = setFprWord s n (castFloatToWord32 v)

-- | The double in register pair n, n+1, read without the even check
-- (C: @fp_double_view[n / 2]@).
doubleWords :: Spim -> Int -> IO Double
doubleWords s n = do
  lo :: Word32 <- fprWord s n
  hi :: Word32 <- fprWord s (n + 1)
  pure (castWord64ToDouble ((fromIntegral hi `shiftL` 32) .|. (fromIntegral lo :: Word64)))

-- | Double register n (C: @FPR_D@): an odd number is a run-time error.
fprDouble :: Spim -> Int -> IO Double
fprDouble s n
  | odd n = runError "Odd FP double register number\n"
  | otherwise = doubleWords s n

setFprDouble :: Spim -> Int -> Double -> IO ()
setFprDouble s n v
  | odd n = runError "Odd FP double register number\n"
  | otherwise = do
      let w :: Word64
          w = castDoubleToWord64 v
      setFprWord s n (fromIntegral w)
      setFprWord s (n + 1) (fromIntegral (w `shiftR` 32))

-- | The FCSR bit for condition code n (cc 0 is bit 23, 1..7 are 25..31).
ccMask :: Int -> Word32
ccMask n
  | n == 0 || n > 7 = 1 `shiftL` 23
  | otherwise = 1 `shiftL` (25 + n - 1)

-- | Condition code n, 0 or 1.
fcc :: Spim -> Int -> IO Int
fcc s n = do
  fcsr :: Int32 <- getFcr s 31
  pure (if fromIntegral fcsr .&. ccMask n == 0 then 0 else 1)

setFcc :: Spim -> Int -> Int -> IO ()
setFcc s n v = do
  fcsr :: Word32 <- fromIntegral <$> getFcr s 31
  setFcr s 31 (fromIntegral (if v == 0 then fcsr .&. complement (ccMask n) else fcsr .|. ccMask n :: Word32))

-- | Record an exception in the CP0 registers (C: @raise_exception@).  An
-- interrupt is ignored while interrupts are disabled.
raiseException :: Spim -> Int -> IO ()
raiseException s code = do
  status :: Word32 <- fromIntegral <$> getCp0 s cp0Status :: IO Word32
  let enabled :: Bool
      enabled = status .&. statusIE /= 0 && status .&. statusEXL == 0
  if code /= excInt || enabled
    then do
      set (exceptionOccurred s) True
      first :: Int <- get (firstBadException s)
      if first == -1 && code /= excInt && code /= excSys && code /= excBp
        then set (firstBadException s) code
        else pure ()
      pc :: Word32 <- get (regPC s)
      inSlot :: Bool <- get (runningInDelaySlot s)
      if status .&. statusEXL == 0
        then
          if inSlot
            then do
              setCp0 s cp0Epc (fromIntegral ((pc - 4) .&. complement 3))
              cause :: Int32 <- getCp0 s cp0Cause
              setCp0 s cp0Cause (fromIntegral (fromIntegral cause .|. causeBD :: Word32))
            else setCp0 s cp0Epc (fromIntegral (pc .&. complement 3))
        else pure ()
      cause :: Word32 <- fromIntegral <$> getCp0 s cp0Cause :: IO Word32
      setCp0 s cp0Cause (fromIntegral ((cause .&. complement causeExcCode) .|. (fromIntegral code `shiftL` 2)))
      st :: Word32 <- fromIntegral <$> getCp0 s cp0Status :: IO Word32
      setCp0 s cp0Status (fromIntegral (st .|. statusEXL))
    else pure ()
