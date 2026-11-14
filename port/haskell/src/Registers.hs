-- | The register files and exceptions (C: include/registers.h, and
-- @raise_exception@ from src/run.c).
--
-- The general registers are 32 signed words.  The FPU has 32 single
-- registers, which the C engine overlays with 16 doubles: double register
-- 2k is the 64 bits of singles 2k (low half) and 2k+1 (high half).  This
-- port stores the 32 raw words and reinterprets their bits on each access,
-- which is what the C union does.
module Registers
  ( getGeneralPurposeRegister,
    setGeneralPurposeRegister,
    getCoprocessor0,
    setCoprocessor0,
    getFloatingPointControlRegister,
    setFloatingPointControlRegister,
    floatingPointRegisterWord,
    setFloatingPointRegisterWord,
    floatingPointRegisterSingle,
    setFloatingPointRegisterSingle,
    floatingPointRegisterDouble,
    setFloatingPointRegisterDouble,
    doubleWords,
    floatingPointConditionCode,
    setFloatingPointConditionCode,
    raiseException,
    exceptionCode,

    -- * CP0 register numbers and bits
    coprocessor0BadVirtualAddress,
    coprocessor0Count,
    coprocessor0Compare,
    coprocessor0Status,
    coprocessor0Cause,
    coprocessor0ExceptionProgramCounter,
    coprocessor0Config,
    statusExceptionLevel,
    statusInterruptEnable,
    statusInterruptMask,
    statusCoprocessorUsable,
    statusUserMode,
    statusMask,
    causeInterruptsPending,
    causeBD,
    causeMask,
    configMask,
    exceptionAddress,

    -- * Exception codes
    exceptionInterrupt,
    exceptionAddressErrorLoad,
    exceptionAddressErrorStore,
    exceptionInstructionBusError,
    exceptionDataBusError,
    exceptionSystemCall,
    exceptionBreakpoint,
    exceptionReservedInstruction,
    exceptionCoprocessorUnusable,
    exceptionArithmeticOverflow,
    exceptionTrap,
    exceptionFloatingPointException,
    registerNames,
  )
where

import Data.Array.IO (readArray, writeArray)
import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.Word (Word32, Word64)
import GHC.Float (castDoubleToWord64, castFloatToWord32, castWord32ToFloat, castWord64ToDouble)
import Output (runError)
import State

-- | Register names, by number, for disassembly (C: @int_reg_names@).
registerNames :: [String]
registerNames =
  [ "r0",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra"
  ]

getGeneralPurposeRegister :: Spim -> Int -> IO Int32
getGeneralPurposeRegister s = readArray (generalPurposeRegisters s)

setGeneralPurposeRegister :: Spim -> Int -> Int32 -> IO ()
setGeneralPurposeRegister s = writeArray (generalPurposeRegisters s)

-- | Coprocessor 0's register n.
getCoprocessor0 :: Spim -> Int -> IO Int32
getCoprocessor0 s n = readArray (coprocessorRegisters s) n

setCoprocessor0 :: Spim -> Int -> Int32 -> IO ()
setCoprocessor0 s n = writeArray (coprocessorRegisters s) n

-- | The FPU control register n (C: @FCR[n]@, coprocessor 1's registers).
getFloatingPointControlRegister :: Spim -> Int -> IO Int32
getFloatingPointControlRegister s n = readArray (coprocessorRegisters s) (32 + n)

setFloatingPointControlRegister :: Spim -> Int -> Int32 -> IO ()
setFloatingPointControlRegister s n = writeArray (coprocessorRegisters s) (32 + n)

coprocessor0BadVirtualAddress, coprocessor0Count, coprocessor0Compare, coprocessor0Status, coprocessor0Cause, coprocessor0ExceptionProgramCounter, coprocessor0Config :: Int
coprocessor0BadVirtualAddress = 8
coprocessor0Count = 9
coprocessor0Compare = 11
coprocessor0Status = 12
coprocessor0Cause = 13
coprocessor0ExceptionProgramCounter = 14
coprocessor0Config = 16

statusExceptionLevel, statusInterruptEnable, statusInterruptMask, statusCoprocessorUsable, statusUserMode, statusMask :: Word32
statusExceptionLevel = 0x2
statusInterruptEnable = 0x1
statusInterruptMask = 0x0000ff00
statusCoprocessorUsable = 0xf0000000
statusUserMode = 0x10
statusMask = statusCoprocessorUsable .|. statusUserMode .|. statusInterruptMask .|. statusExceptionLevel .|. statusInterruptEnable

causeInterruptsPending, causeBD, causeExceptionCode, causeMask, configMask :: Word32
causeInterruptsPending = 0x0000ff00
causeBD = 0x80000000
causeExceptionCode = 0x7c
causeMask = causeBD .|. causeInterruptsPending .|. 0x8000 .|. 0x4000 .|. 0x2000 .|. 0x1000 .|. 0x800 .|. 0x400 .|. causeExceptionCode
configMask = 0x80000 .|. 0x60000 .|. 0x1c000 .|. 0x380

-- | Where the processor jumps on an exception (MIPS32).
exceptionAddress :: Word32
exceptionAddress = 0x80000180

exceptionInterrupt, exceptionAddressErrorLoad, exceptionAddressErrorStore, exceptionInstructionBusError, exceptionDataBusError, exceptionSystemCall, exceptionBreakpoint, exceptionReservedInstruction, exceptionCoprocessorUnusable, exceptionArithmeticOverflow, exceptionTrap, exceptionFloatingPointException :: Int
exceptionInterrupt = 0
exceptionAddressErrorLoad = 4
exceptionAddressErrorStore = 5
exceptionInstructionBusError = 6
exceptionDataBusError = 7
exceptionSystemCall = 8
exceptionBreakpoint = 9
exceptionReservedInstruction = 10
exceptionCoprocessorUnusable = 11
exceptionArithmeticOverflow = 12
exceptionTrap = 13
exceptionFloatingPointException = 15

-- | The ExcCode field of the Cause register (C: @CP0_ExCode@).
exceptionCode :: Spim -> IO Int
exceptionCode s = do
  cause :: Int32 <- getCoprocessor0 s coprocessor0Cause
  pure (fromIntegral ((fromIntegral cause .&. causeExceptionCode) `shiftR` 2 :: Word32))

-- | The raw 32 bits of FP register n (C: @FPR_W@).
floatingPointRegisterWord :: Spim -> Int -> IO Word32
floatingPointRegisterWord s = readArray (floatingPointRegisters s)

setFloatingPointRegisterWord :: Spim -> Int -> Word32 -> IO ()
setFloatingPointRegisterWord s = writeArray (floatingPointRegisters s)

-- | FP register n as a single (C: @FPR_S@).
floatingPointRegisterSingle :: Spim -> Int -> IO Float
floatingPointRegisterSingle s n = castWord32ToFloat <$> floatingPointRegisterWord s n

setFloatingPointRegisterSingle :: Spim -> Int -> Float -> IO ()
setFloatingPointRegisterSingle s n v = setFloatingPointRegisterWord s n (castFloatToWord32 v)

-- | The double in register pair n, n+1, read without the even check
-- (C: @fp_double_view[n / 2]@).
doubleWords :: Spim -> Int -> IO Double
doubleWords s n = do
  lo :: Word32 <- floatingPointRegisterWord s n
  hi :: Word32 <- floatingPointRegisterWord s (n + 1)
  pure (castWord64ToDouble ((fromIntegral hi `shiftL` 32) .|. (fromIntegral lo :: Word64)))

-- | Double register n (C: @FPR_D@): an odd number is a run-time error.
floatingPointRegisterDouble :: Spim -> Int -> IO Double
floatingPointRegisterDouble s n
  | odd n = runError "Odd FP double register number\n"
  | otherwise = doubleWords s n

setFloatingPointRegisterDouble :: Spim -> Int -> Double -> IO ()
setFloatingPointRegisterDouble s n v
  | odd n = runError "Odd FP double register number\n"
  | otherwise = do
      let w :: Word64
          w = castDoubleToWord64 v
      setFloatingPointRegisterWord s n (fromIntegral w)
      setFloatingPointRegisterWord s (n + 1) (fromIntegral (w `shiftR` 32))

-- | The FCSR bit for condition code n (cc 0 is bit 23, 1..7 are 25..31).
conditionCodeMask :: Int -> Word32
conditionCodeMask n
  | n == 0 || n > 7 = 1 `shiftL` 23
  | otherwise = 1 `shiftL` (25 + n - 1)

-- | Condition code n, 0 or 1.
floatingPointConditionCode :: Spim -> Int -> IO Int
floatingPointConditionCode s n = do
  floatingPointControlStatusRegister :: Int32 <- getFloatingPointControlRegister s 31
  pure (if fromIntegral floatingPointControlStatusRegister .&. conditionCodeMask n == 0 then 0 else 1)

setFloatingPointConditionCode :: Spim -> Int -> Int -> IO ()
setFloatingPointConditionCode s n v = do
  floatingPointControlStatusRegister :: Word32 <- fromIntegral <$> getFloatingPointControlRegister s 31
  setFloatingPointControlRegister s 31 (fromIntegral (if v == 0 then floatingPointControlStatusRegister .&. complement (conditionCodeMask n) else floatingPointControlStatusRegister .|. conditionCodeMask n :: Word32))

-- | Record an exception in the CP0 registers (C: @raise_exception@).  An
-- interrupt is ignored while interrupts are disabled.
raiseException :: Spim -> Int -> IO ()
raiseException s code = do
  status :: Word32 <- fromIntegral <$> getCoprocessor0 s coprocessor0Status :: IO Word32
  let enabled :: Bool
      enabled = status .&. statusInterruptEnable /= 0 && status .&. statusExceptionLevel == 0
  if code /= exceptionInterrupt || enabled
    then do
      set (exceptionOccurred s) True
      first :: Int <- get (firstBadException s)
      if first == -1 && code /= exceptionInterrupt && code /= exceptionSystemCall && code /= exceptionBreakpoint
        then set (firstBadException s) code
        else pure ()
      programCounter :: Word32 <- get (registerProgramCounter s)
      inSlot :: Bool <- get (runningInDelaySlot s)
      if status .&. statusExceptionLevel == 0
        then
          if inSlot
            then do
              setCoprocessor0 s coprocessor0ExceptionProgramCounter (fromIntegral ((programCounter - 4) .&. complement 3))
              cause :: Int32 <- getCoprocessor0 s coprocessor0Cause
              setCoprocessor0 s coprocessor0Cause (fromIntegral (fromIntegral cause .|. causeBD :: Word32))
            else setCoprocessor0 s coprocessor0ExceptionProgramCounter (fromIntegral (programCounter .&. complement 3))
        else pure ()
      cause :: Word32 <- fromIntegral <$> getCoprocessor0 s coprocessor0Cause :: IO Word32
      setCoprocessor0 s coprocessor0Cause (fromIntegral ((cause .&. complement causeExceptionCode) .|. (fromIntegral code `shiftL` 2)))
      st :: Word32 <- fromIntegral <$> getCoprocessor0 s coprocessor0Status :: IO Word32
      setCoprocessor0 s coprocessor0Status (fromIntegral (st .|. statusExceptionLevel))
    else pure ()
