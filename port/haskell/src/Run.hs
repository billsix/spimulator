-- | Executing instructions (C: src/run.c).
--
-- 'runSpim' is the fetch-execute loop: read the instruction at the PC, do
-- what it says, advance the PC, and handle any exception it raised.  The
-- arithmetic is plain Haskell on fixed-width types: 'Int32' for signed
-- registers, 'Word32' for unsigned views and addresses, 'Float' and
-- 'Double' for the FPU.  'quot' and 'rem' truncate toward zero like C's
-- @/@ and @%@ ('div' and 'mod' would floor, which is wrong here).
--
-- With @-delayed_branches@ a taken branch first runs the instruction after
-- it (its "delay slot"); with @-delayed_loads@ a loaded value arrives one
-- instruction late.  Both are modelled the way the C engine models them.
module Run
  ( runSpim,
  )
where

import Control.Monad (when)
import Data.Array.IO (readArray, writeArray)
import Data.Bits (complement, countLeadingZeros, shiftL, shiftR, testBit, xor, (.&.), (.|.))
import Data.Int (Int16, Int32, Int64)
import qualified Data.Set as Set
import Data.Word (Word32, Word64)
import GHC.Clock (getMonotonicTimeNSec)
import GHC.Float (castDoubleToWord64, castFloatToWord32, castWord64ToDouble, double2Float, float2Double)
import InstFormat (instructionToString, printInstruction)
import Memory
import OpcodeTables (opcodeOfToken)
import Opcodes
import Output
import Registers
import State
import Syscall
import Text.Printf (printf)
import Types

-- | What an instruction tells the loop to do next.
data Next
  = -- | advance the PC and carry on
    Continue
  | -- | leave 'runSpim' now with this answer
    Return Bool

-- | Run the program from an address for a number of steps; print each
-- instruction first if display is on.  True means the program can be
-- continued (C: @run_spim@).
runSpim :: Spim -> Word32 -> Int -> Bool -> IO Bool
runSpim s initialProgramCounter steps display = do
  set (registerProgramCounter s) initialProgramCounter
  bare :: Bool <- get (optionBare s)
  mio :: Bool <- get (optionMappedIO s)
  let chunk :: Int
      chunk = if not bare && mio then 100 else steps
  startTimer s
  outer chunk steps
  where
    outer :: Int -> Int -> IO Bool
    outer chunk left
      | left <= 0 = pure True
      | otherwise = do
          bare :: Bool <- get (optionBare s)
          mio :: Bool <- get (optionMappedIO s)
          when (not bare && mio) $ checkMemoryMappedIO s
          checkInterrupt
          set (forceBreak s) False
          r :: Maybe Bool <- inner (min chunk left)
          case r of
            Just b -> pure b
            Nothing -> outer chunk (left - min chunk left)
    checkInterrupt :: IO ()
    checkInterrupt = do
      status :: Word32 <- fromIntegral <$> getCoprocessor0 s coprocessor0Status :: IO Word32
      cause :: Word32 <- fromIntegral <$> getCoprocessor0 s coprocessor0Cause :: IO Word32
      when (status .&. statusInterruptEnable /= 0 && status .&. statusExceptionLevel == 0 && (cause .&. causeInterruptsPending) .&. (status .&. statusInterruptMask) /= 0) $ do
        raiseException s exceptionInterrupt
        handleException s
    inner :: Int -> IO (Maybe Bool)
    inner 0 = pure Nothing
    inner n = do
      fb :: Bool <- get (forceBreak s)
      if fb
        then pure (Just True)
        else do
          r :: Next <- step s display
          case r of
            Return b -> pure (Just b)
            Continue -> inner (n - 1)

-- | Fetch, check, execute and finish one instruction.
step :: Spim -> Bool -> IO Next
step s display = do
  setGeneralPurposeRegister s 0 0
  pollTimer s
  set (exceptionOccurred s) False
  programCounter :: Word32 <- get (registerProgramCounter s)
  minst :: Maybe InstructionRef <- memoryReadInstruction s programCounter
  exception :: Bool <- get (exceptionOccurred s)
  if exception
    then do
      set (exceptionOccurred s) False
      handleException s
      pure Continue
    else case minst of
      Nothing -> runError (printf "Attempt to execute non-instruction at 0x%08x\n" programCounter)
      Just ref -> do
        i :: Instruction <- get ref
        undefinedSymbol :: Bool <- case instructionExpression i >>= immediateExpressionSymbol of
          Just l -> (== 0) <$> get (labelAddress l)
          Nothing -> pure False
        when undefinedSymbol $ do
          text :: String <- instructionToString s programCounter
          runError (printf "Instruction references undefined symbol at 0x%08x\n  %s" programCounter text)
        doDelayedUpdate s
        r :: Next <- execute s display i
        case r of
          Return b -> pure (Return b)
          Continue -> do
            modify (registerProgramCounter s) (+ 4)
            when display $ get (registerProgramCounter s) >>= printInstruction s
            exception' :: Bool <- get (exceptionOccurred s)
            when exception' $ handleException s
            pure Continue

-- The CP0 timer: Count goes up every 10 ms of real time, and an interrupt
-- is raised when it reaches Compare.

startTimer :: Spim -> IO ()
startTimer s = do
  now :: Integer <- microseconds
  deadline :: Integer <- get (timerDeadline s)
  when (deadline <= now) $ set (timerDeadline s) (now + 10000)

pollTimer :: Spim -> IO ()
pollTimer s = do
  now :: Integer <- microseconds
  deadline :: Integer <- get (timerDeadline s)
  when (deadline <= now) $ do
    count :: Int32 <- (+ 1) <$> getCoprocessor0 s coprocessor0Count
    setCoprocessor0 s coprocessor0Count count
    compare' :: Int32 <- getCoprocessor0 s coprocessor0Compare
    when (count == compare') $ do
      c :: Int32 <- getCoprocessor0 s coprocessor0Cause
      setCoprocessor0 s coprocessor0Cause (c .|. (1 `shiftL` 15))
    set (timerDeadline s) (now + 10000)

microseconds :: IO Integer
microseconds = (`quot` 1000) . toInteger <$> getMonotonicTimeNSec

-- Delayed loads: a two-stage pipeline of (destination, value).

writeDest :: Spim -> LoadDest -> Int32 -> IO ()
writeDest s (DestinationGeneralPurposeRegister r) v = setGeneralPurposeRegister s r v
writeDest s (DestinationFloatingPointRegister r) v = setFloatingPointRegisterWord s r (fromIntegral v)

doDelayedUpdate :: Spim -> IO ()
doDelayedUpdate s = do
  delayed :: Bool <- get (optionDelayedLoads s)
  when delayed $ do
    d2 :: Maybe (LoadDest, Int32) <- get (delayedLoad2 s)
    mapM_ (uncurry (writeDest s)) d2
    get (delayedLoad1 s) >>= set (delayedLoad2 s)
    set (delayedLoad1 s) Nothing

-- | Write a loaded value now, or one instruction later (C: @LOAD_INST@).
loadInto :: Spim -> LoadDest -> Int32 -> IO ()
loadInto s dest v = do
  delayed :: Bool <- get (optionDelayedLoads s)
  if delayed then set (delayedLoad1 s) (Just (dest, v)) else writeDest s dest v

-- | C's conversion of a double to int32_t, as x86 does it: truncation, and
-- 0x80000000 for NaN or a value out of range.
toInt32 :: Double -> Int32
toInt32 x
  | isNaN x || x <= -2147483649 || x >= 2147483648 = minBound
  | otherwise = truncate x

fabsD :: Double -> Double
fabsD d = castWord64ToDouble (castDoubleToWord64 d .&. complement (1 `shiftL` 63))

-- | Execute one instruction (the big switch of C's @run_spim@).
execute :: Spim -> Bool -> Instruction -> IO Next
execute s display i = case opcodeOfToken (instructionOpcode i) of
  Nothing -> unknown
  Just op -> exec op
  where
    unknown :: IO a
    unknown = fatalError (printf "Unknown instruction type: %d\n" (instructionOpcode i))
    ok :: IO Next
    ok = pure Continue
    sourceRegister :: Int
    sourceRegister = instructionSourceRegister i
    targetRegister :: Int
    targetRegister = instructionTargetRegister i
    destinationRegister :: Int
    destinationRegister = instructionDestinationRegister i
    floatingPointSource :: Int
    floatingPointSource = instructionFloatingPointSource i
    floatingPointTarget :: Int
    floatingPointTarget = instructionFloatingPointTarget i
    floatingPointDestination :: Int
    floatingPointDestination = instructionFloatingPointDestination i
    immediate :: Int16
    immediate = instructionImmediate i
    immediateSigned :: Int32
    immediateSigned = fromIntegral immediate :: Int32
    immediateUnsigned :: Word32
    immediateUnsigned = fromIntegral immediateSigned :: Word32
    r :: Int -> IO Int32
    r = getGeneralPurposeRegister s
    ru :: Int -> IO Word32
    ru n = (fromIntegral :: Int32 -> Word32) <$> getGeneralPurposeRegister s n
    w :: Int -> Int32 -> IO ()
    w = setGeneralPurposeRegister s
    raise :: Int -> IO ()
    raise code = raiseException s code
    raiseOk :: Int -> IO Next
    raiseOk code = raise code >> ok
    effectiveAddress :: IO Word32
    effectiveAddress = (\b -> b + immediateUnsigned) <$> ru (instructionBase i)
    -- JUMP_INST: with delayed branches, run the delay slot first.
    jump :: Word32 -> IO Next
    jump target = do
      delayed :: Bool <- get (optionDelayedBranches s)
      when delayed $ do
        set (runningInDelaySlot s) True
        programCounter :: Word32 <- get (registerProgramCounter s)
        _ <- runSpim s (programCounter + 4) 1 display
        set (runningInDelaySlot s) False
      set (registerProgramCounter s) (target - 4)
      ok
    -- BRANCH_INST: jump if the test holds; a branch-likely that is not
    -- taken skips its delay slot.
    branch :: Bool -> Bool -> IO Next
    branch test nullify = do
      programCounter :: Word32 <- get (registerProgramCounter s)
      if test
        then do
          delayed :: Bool <- get (optionDelayedBranches s)
          jump (programCounter + fromIntegral (branchOffset i) + (if delayed then 4 else 0))
        else do
          when nullify $ modify (registerProgramCounter s) (+ 4)
          ok
    link :: Int -> IO ()
    link registerNumber = do
      programCounter :: Word32 <- get (registerProgramCounter s)
      delayed :: Bool <- get (optionDelayedBranches s)
      w registerNumber (fromIntegral (programCounter + (if delayed then 8 else 4)))
    -- jalr and jalr.hb: link in rd, then jump to rs (read before rd is written).
    jalr :: IO Next
    jalr = do
      target :: Word32 <- ru sourceRegister
      link destinationRegister
      jump target
    signBit :: Word32 -> Bool
    signBit v = v .&. 0x80000000 /= (0 :: Word32)
    setMultiplyDivide :: Word64 -> IO ()
    setMultiplyDivide p = set (registerMultiplyDivideLow s) (fromIntegral p) >> set (registerMultiplyDivideHigh s) (fromIntegral (p `shiftR` 32))
    multiplyDivide :: IO Word64
    multiplyDivide = do
      hi :: Int32 <- get (registerMultiplyDivideHigh s)
      lo :: Int32 <- get (registerMultiplyDivideLow s)
      pure ((fromIntegral (fromIntegral hi :: Word32) `shiftL` 32) .|. fromIntegral (fromIntegral lo :: Word32) :: Word64)
    signedProduct :: Int32 -> Int32 -> Word64
    signedProduct a b = fromIntegral (fromIntegral a * fromIntegral b :: Int64) :: Word64
    unsignedProduct :: Int32 -> Int32 -> Word64
    unsignedProduct a b = (fromIntegral (fromIntegral a :: Word32) * fromIntegral (fromIntegral b :: Word32)) :: Word64
    trapIf :: Bool -> IO Next
    trapIf c = when c (raise exceptionTrap) >> ok
    single :: Int -> IO Float
    single = floatingPointRegisterSingle s
    double :: Int -> IO Double
    double = floatingPointRegisterDouble s
    setS :: Int -> Float -> IO ()
    setS n v = setFloatingPointRegisterSingle s n v
    setD :: Int -> Double -> IO ()
    setD n v = setFloatingPointRegisterDouble s n v
    setW :: Int -> Int32 -> IO ()
    setW n v = setFloatingPointRegisterWord s n (fromIntegral (v :: Int32))
    unaryS :: (Float -> Float) -> IO ()
    unaryS f = single floatingPointSource >>= setS floatingPointDestination . f
    unaryD :: (Double -> Double) -> IO ()
    unaryD f = double floatingPointSource >>= setD floatingPointDestination . f
    binaryS :: (Float -> Float -> Float) -> IO ()
    binaryS f = do a :: Float <- single floatingPointSource; b :: Float <- single floatingPointTarget; setS floatingPointDestination (f a b)
    binaryD :: (Double -> Double -> Double) -> IO ()
    binaryD f = do a :: Double <- double floatingPointSource; b :: Double <- double floatingPointTarget; setD floatingPointDestination (f a b)
    -- c.cond.fmt: set condition code CCFP from less/equal/unordered.
    compareFloatingPoint :: Bool -> Bool -> Bool -> IO Next
    compareFloatingPoint nan less equal = do
      let condition :: Int
          condition = instructionCondition i
          conditionCode :: Int
          conditionCode = floatingPointDestination `shiftR` 2
          result :: Int -> Int -> Int -> Int
          result l e u = (if condition .&. 4 /= 0 then l else 0) .|. (if condition .&. 2 /= 0 then e else 0) .|. (if condition .&. 1 /= 0 then u else 0)
          b :: Bool -> Int
          b x = if x then 1 else 0 :: Int
      if nan
        then
          if condition .&. 8 /= 0
            then raiseOk exceptionFloatingPointException
            else setFloatingPointConditionCode s conditionCode (result 0 0 1) >> ok
        else setFloatingPointConditionCode s conditionCode (result (b less) (b equal) 0) >> ok
    exec :: Opcode -> IO Next
    exec op = case op of
      TOKEN_ADD_OPCODE -> do
        a :: Int32 <- r sourceRegister
        b :: Int32 <- r targetRegister
        let sum' :: Int32
            sum' = a + b
        if (a >= 0) == (b >= 0) && (sum' >= 0) /= (a >= 0) then raiseOk exceptionArithmeticOverflow else w destinationRegister sum' >> ok
      TOKEN_ADDI_OPCODE -> do
        a :: Int32 <- r sourceRegister
        let sum' :: Int32
            sum' = a + immediateSigned
        if (a >= 0) == (immediateSigned >= 0) && (sum' >= 0) /= (a >= 0) then raiseOk exceptionArithmeticOverflow else w targetRegister sum' >> ok
      TOKEN_ADDIU_OPCODE -> r sourceRegister >>= \a -> w targetRegister (a + immediateSigned) >> ok
      TOKEN_ADDU_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (a + b); ok
      TOKEN_AND_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (a .&. b); ok
      TOKEN_ANDI_OPCODE -> r sourceRegister >>= \a -> w targetRegister (a .&. (0xffff .&. immediateSigned)) >> ok
      TOKEN_BC2F_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_BC2FL_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_BC2T_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_BC2TL_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_BEQ_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; branch (a == b) False
      TOKEN_BEQL_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; branch (a == b) True
      TOKEN_BGEZ_OPCODE -> ru sourceRegister >>= \a -> branch (not (signBit a)) False
      TOKEN_BGEZL_OPCODE -> ru sourceRegister >>= \a -> branch (not (signBit a)) True
      TOKEN_BGEZAL_OPCODE -> link 31 >> ru sourceRegister >>= \a -> branch (not (signBit a)) False
      TOKEN_BGEZALL_OPCODE -> link 31 >> ru sourceRegister >>= \a -> branch (not (signBit a)) True
      TOKEN_BGTZ_OPCODE -> ru sourceRegister >>= \a -> branch (a /= 0 && not (signBit a)) False
      TOKEN_BGTZL_OPCODE -> ru sourceRegister >>= \a -> branch (a /= 0 && not (signBit a)) True
      TOKEN_BLEZ_OPCODE -> ru sourceRegister >>= \a -> branch (a == 0 || signBit a) False
      TOKEN_BLEZL_OPCODE -> ru sourceRegister >>= \a -> branch (a == 0 || signBit a) True
      TOKEN_BLTZ_OPCODE -> ru sourceRegister >>= \a -> branch (signBit a) False
      TOKEN_BLTZL_OPCODE -> ru sourceRegister >>= \a -> branch (signBit a) True
      TOKEN_BLTZAL_OPCODE -> link 31 >> ru sourceRegister >>= \a -> branch (signBit a) False
      TOKEN_BLTZALL_OPCODE -> link 31 >> ru sourceRegister >>= \a -> branch (signBit a) True
      TOKEN_BNE_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; branch (a /= b) False
      TOKEN_BNEL_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; branch (a /= b) True
      TOKEN_BREAK_OPCODE
        | destinationRegister == 1 -> raise exceptionBreakpoint >> pure (Return True) -- a debugger breakpoint
        | otherwise -> raiseOk exceptionBreakpoint
      TOKEN_CACHE_OPCODE -> ok
      TOKEN_CFC0_OPCODE -> readCtl destinationRegister >>= w targetRegister >> ok
      TOKEN_CFC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_CLO_OPCODE -> ru sourceRegister >>= \a -> w destinationRegister (fromIntegral (countLeadingZeros (complement a))) >> ok
      TOKEN_CLZ_OPCODE -> ru sourceRegister >>= \a -> w destinationRegister (fromIntegral (countLeadingZeros a)) >> ok
      TOKEN_COP2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_CTC0_OPCODE -> r targetRegister >>= writeCtl destinationRegister >> ok
      TOKEN_CTC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_DIV_OPCODE -> do
        a :: Int32 <- r sourceRegister
        b :: Int32 <- r targetRegister
        when (b /= 0 && not (a == minBound && b == -1)) $ do
          set (registerMultiplyDivideLow s) (a `quot` b)
          set (registerMultiplyDivideHigh s) (a `rem` b)
        ok
      TOKEN_DIVU_OPCODE -> do
        a :: Word32 <- ru sourceRegister
        b :: Word32 <- ru targetRegister
        when (b /= 0) $ do
          set (registerMultiplyDivideLow s) (fromIntegral (a `quot` b))
          set (registerMultiplyDivideHigh s) (fromIntegral (a `rem` b))
        ok
      TOKEN_ERET_OPCODE -> do
        st :: Int32 <- getCoprocessor0 s coprocessor0Status
        setCoprocessor0 s coprocessor0Status (st .&. complement (fromIntegral statusExceptionLevel))
        getCoprocessor0 s coprocessor0ExceptionProgramCounter >>= jump . fromIntegral
      TOKEN_J_OPCODE -> do programCounter :: Word32 <- get (registerProgramCounter s); jump ((programCounter .&. 0xf0000000) .|. (instructionTarget i `shiftL` 2))
      TOKEN_JAL_OPCODE -> do
        link 31
        programCounter :: Word32 <- get (registerProgramCounter s)
        jump ((programCounter .&. 0xf0000000) .|. (instructionTarget i `shiftL` 2))
      -- The hazard-barrier forms (.hb) run as the plain ones: the simulator has no pipeline
      -- hazards for a barrier to clear.
      TOKEN_JALR_OPCODE -> jalr
      TOKEN_JALR_HB_OPCODE -> jalr
      TOKEN_JR_OPCODE -> ru sourceRegister >>= jump
      TOKEN_JR_HB_OPCODE -> ru sourceRegister >>= jump
      TOKEN_LB_OPCODE -> effectiveAddress >>= memoryReadByte s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) >> ok
      TOKEN_LBU_OPCODE -> effectiveAddress >>= memoryReadByte s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) . (.&. 0xff) >> ok
      TOKEN_LH_OPCODE -> effectiveAddress >>= memoryReadHalf s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) >> ok
      TOKEN_LHU_OPCODE -> effectiveAddress >>= memoryReadHalf s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) . (.&. 0xffff) >> ok
      TOKEN_LL_OPCODE -> effectiveAddress >>= memoryReadWord s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) >> ok
      TOKEN_LUI_OPCODE -> w targetRegister (fromIntegral ((immediateUnsigned `shiftL` 16) .&. 0xffff0000)) >> ok
      TOKEN_LW_OPCODE -> effectiveAddress >>= memoryReadWord s >>= loadInto s (DestinationGeneralPurposeRegister targetRegister) >> ok
      TOKEN_LDC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_LWC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_LWL_OPCODE -> do
        address :: Word32 <- effectiveAddress
        registerValue :: Word32 <- ru targetRegister
        word :: Word32 <- fromIntegral <$> memoryReadWord s (address .&. 0xfffffffc) :: IO Word32
        exception :: Bool <- get (exceptionOccurred s)
        let merged :: Word32
            merged = case address .&. 3 of
              0 -> ((word .&. 0xff) `shiftL` 24) .|. (registerValue .&. 0xffffff)
              1 -> ((word .&. 0xffff) `shiftL` 16) .|. (registerValue .&. 0xffff)
              2 -> ((word .&. 0xffffff) `shiftL` 8) .|. (registerValue .&. 0xff)
              _ -> word
        loadInto s (DestinationGeneralPurposeRegister targetRegister) (fromIntegral (if exception then word else merged))
        ok
      TOKEN_LWR_OPCODE -> do
        address :: Word32 <- effectiveAddress
        registerValue :: Word32 <- ru targetRegister
        word :: Word32 <- fromIntegral <$> memoryReadWord s (address .&. 0xfffffffc) :: IO Word32
        exception :: Bool <- get (exceptionOccurred s)
        let merged :: Word32
            merged = case address .&. 3 of
              0 -> word
              1 -> (registerValue .&. 0xff000000) .|. ((word .&. 0xffffff00) `shiftR` 8)
              2 -> (registerValue .&. 0xffff0000) .|. ((word .&. 0xffff0000) `shiftR` 16)
              _ -> (registerValue .&. 0xffffff00) .|. ((word .&. 0xff000000) `shiftR` 24)
        loadInto s (DestinationGeneralPurposeRegister targetRegister) (fromIntegral (if exception then word else merged))
        ok
      TOKEN_MADD_OPCODE -> multiplyAdd signedProduct (+)
      TOKEN_MADDU_OPCODE -> multiplyAdd unsignedProduct (+)
      TOKEN_MFC0_OPCODE -> getCoprocessor0 s floatingPointSource >>= w targetRegister >> ok
      TOKEN_MFC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_MFHI_OPCODE -> get (registerMultiplyDivideHigh s) >>= w destinationRegister >> ok
      TOKEN_MFLO_OPCODE -> get (registerMultiplyDivideLow s) >>= w destinationRegister >> ok
      TOKEN_MOVN_OPCODE -> do c :: Int32 <- r targetRegister; when (c /= 0) (r sourceRegister >>= w destinationRegister); ok
      TOKEN_MOVZ_OPCODE -> do c :: Int32 <- r targetRegister; when (c == 0) (r sourceRegister >>= w destinationRegister); ok
      TOKEN_MSUB_OPCODE -> multiplyAdd signedProduct (-)
      TOKEN_MSUBU_OPCODE -> multiplyAdd unsignedProduct (-)
      TOKEN_MTC0_OPCODE -> do
        r targetRegister >>= setCoprocessor0 s floatingPointSource
        let masked :: Word32 -> IO ()
            masked mask = getCoprocessor0 s floatingPointSource >>= \v -> setCoprocessor0 s floatingPointSource (v .&. fromIntegral (mask :: Word32))
        if floatingPointSource == coprocessor0Compare
          then getCoprocessor0 s coprocessor0Cause >>= \c -> setCoprocessor0 s coprocessor0Cause (c .&. complement 0x8000)
          else
            if floatingPointSource == coprocessor0Status
              then do
                st :: Int32 <- getCoprocessor0 s coprocessor0Status
                let st' :: Word32
                    st' = (fromIntegral st .&. statusMask) .|. ((statusCoprocessorUsable .&. 0x30000000) .|. statusUserMode)
                setCoprocessor0 s coprocessor0Status (fromIntegral st')
              else
                if floatingPointSource == coprocessor0Cause
                  then masked causeMask
                  else when (floatingPointSource == coprocessor0Config) (masked configMask)
        ok
      TOKEN_MTC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_MTHI_OPCODE -> r sourceRegister >>= set (registerMultiplyDivideHigh s) >> ok
      TOKEN_MTLO_OPCODE -> r sourceRegister >>= set (registerMultiplyDivideLow s) >> ok
      TOKEN_MUL_OPCODE -> do
        a :: Int32 <- r sourceRegister
        b :: Int32 <- r targetRegister
        setMultiplyDivide (signedProduct a b)
        get (registerMultiplyDivideLow s) >>= w destinationRegister
        ok
      TOKEN_MULT_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; setMultiplyDivide (signedProduct a b); ok
      TOKEN_MULTU_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; setMultiplyDivide (unsignedProduct a b); ok
      TOKEN_NOR_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (complement (a .|. b)); ok
      TOKEN_OR_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (a .|. b); ok
      TOKEN_ORI_OPCODE -> r sourceRegister >>= \a -> w targetRegister (a .|. (0xffff .&. immediateSigned)) >> ok
      TOKEN_PREF_OPCODE -> ok
      TOKEN_RFE_OPCODE -> raiseOk exceptionReservedInstruction
      TOKEN_SB_OPCODE -> do a :: Word32 <- effectiveAddress; r targetRegister >>= memoryWriteByte s a; ok
      TOKEN_SC_OPCODE -> do a :: Word32 <- effectiveAddress; r targetRegister >>= memoryWriteWord s a; ok
      TOKEN_SDC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_SH_OPCODE -> do a :: Word32 <- effectiveAddress; r targetRegister >>= memoryWriteHalf s a; ok
      TOKEN_SLL_OPCODE -> shiftBy (pure (instructionShiftAmount i)) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftL` n))
      TOKEN_SLLV_OPCODE -> shiftBy (shiftRegister) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftL` n))
      TOKEN_SLT_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (if a < b then 1 else 0); ok
      TOKEN_SLTI_OPCODE -> r sourceRegister >>= \a -> w targetRegister (if a < immediateSigned then 1 else 0) >> ok
      TOKEN_SLTIU_OPCODE -> ru sourceRegister >>= \a -> w targetRegister (if a < immediateUnsigned then 1 else 0) >> ok
      TOKEN_SLTU_OPCODE -> do a :: Word32 <- ru sourceRegister; b :: Word32 <- ru targetRegister; w destinationRegister (if a < b then 1 else 0); ok
      TOKEN_SRA_OPCODE -> shiftBy (pure (instructionShiftAmount i)) shiftR
      TOKEN_SRAV_OPCODE -> shiftBy shiftRegister shiftR
      TOKEN_SRL_OPCODE -> shiftBy (pure (instructionShiftAmount i)) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftR` n))
      TOKEN_SRLV_OPCODE -> shiftBy shiftRegister (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftR` n))
      TOKEN_SUB_OPCODE -> do
        a :: Int32 <- r sourceRegister
        b :: Int32 <- r targetRegister
        let diff :: Int32
            diff = a - b
        if (a >= 0) /= (b >= 0) && (diff >= 0) /= (a >= 0) then raiseOk exceptionArithmeticOverflow else w destinationRegister diff >> ok
      TOKEN_SUBU_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (a - b); ok
      TOKEN_SW_OPCODE -> do a :: Word32 <- effectiveAddress; r targetRegister >>= memoryWriteWord s a; ok
      TOKEN_SWC2_OPCODE -> raiseOk exceptionCoprocessorUnusable
      TOKEN_SWL_OPCODE -> do
        address :: Word32 <- effectiveAddress
        registerValue :: Word32 <- ru targetRegister
        let aligned :: Word32
            aligned = address .&. 0xfffffffc
        dat :: Word32 <- fromIntegral <$> memoryReadWord s aligned :: IO Word32
        let new :: Word32
            new = case address .&. 3 of
              0 -> (dat .&. 0xffffff00) .|. ((registerValue `shiftR` 24) .&. 0xff)
              1 -> (dat .&. 0xffff0000) .|. ((registerValue `shiftR` 16) .&. 0xffff)
              2 -> (dat .&. 0xff000000) .|. ((registerValue `shiftR` 8) .&. 0xffffff)
              _ -> registerValue
        memoryWriteWord s aligned (fromIntegral new)
        ok
      TOKEN_SWR_OPCODE -> do
        address :: Word32 <- effectiveAddress
        registerValue :: Word32 <- ru targetRegister
        let aligned :: Word32
            aligned = address .&. 0xfffffffc
        dat :: Word32 <- fromIntegral <$> memoryReadWord s aligned :: IO Word32
        let new :: Word32
            new = case address .&. 3 of
              0 -> registerValue
              1 -> ((registerValue `shiftL` 8) .&. 0xffffff00) .|. (dat .&. 0xff)
              2 -> ((registerValue `shiftL` 16) .&. 0xffff0000) .|. (dat .&. 0xffff)
              _ -> ((registerValue `shiftL` 24) .&. 0xff000000) .|. (dat .&. 0xffffff)
        memoryWriteWord s aligned (fromIntegral new)
        ok
      TOKEN_SYNC_OPCODE -> ok
      TOKEN_SYSCALL_OPCODE -> do
        more :: Bool <- doSyscall s
        pure (if more then Continue else Return False)
      TOKEN_TEQ_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; trapIf (a == b)
      TOKEN_TEQI_OPCODE -> r sourceRegister >>= \a -> trapIf (a == immediateSigned)
      TOKEN_TGE_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; trapIf (a >= b)
      TOKEN_TGEI_OPCODE -> r sourceRegister >>= \a -> trapIf (a >= immediateSigned)
      TOKEN_TGEIU_OPCODE -> ru sourceRegister >>= \a -> trapIf (a >= immediateUnsigned)
      TOKEN_TGEU_OPCODE -> do a :: Word32 <- ru sourceRegister; b :: Word32 <- ru targetRegister; trapIf (a >= b)
      TOKEN_TLBP_OPCODE -> raiseOk exceptionReservedInstruction
      TOKEN_TLBR_OPCODE -> raiseOk exceptionReservedInstruction
      TOKEN_TLBWI_OPCODE -> raiseOk exceptionReservedInstruction
      TOKEN_TLBWR_OPCODE -> raiseOk exceptionReservedInstruction
      TOKEN_TLT_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; trapIf (a < b)
      TOKEN_TLTI_OPCODE -> r sourceRegister >>= \a -> trapIf (a < immediateSigned)
      TOKEN_TLTIU_OPCODE -> ru sourceRegister >>= \a -> trapIf (a < immediateUnsigned)
      TOKEN_TLTU_OPCODE -> do a :: Word32 <- ru sourceRegister; b :: Word32 <- ru targetRegister; trapIf (a < b)
      TOKEN_TNE_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; trapIf (a /= b)
      TOKEN_TNEI_OPCODE -> r sourceRegister >>= \a -> trapIf (a /= immediateSigned)
      TOKEN_XOR_OPCODE -> do a :: Int32 <- r sourceRegister; b :: Int32 <- r targetRegister; w destinationRegister (a `xor` b); ok
      TOKEN_XORI_OPCODE -> r sourceRegister >>= \a -> w targetRegister (a `xor` (0xffff .&. immediateSigned)) >> ok
      -- The floating-point unit.
      TOKEN_ABS_S_OPCODE -> unaryS (\x -> double2Float (fabsD (float2Double x))) >> ok
      TOKEN_ABS_D_OPCODE -> unaryD fabsD >> ok
      TOKEN_ADD_S_OPCODE -> binaryS (+) >> ok
      TOKEN_ADD_D_OPCODE -> binaryD (+) >> ok
      TOKEN_BC1F_OPCODE -> floatingPointBranch
      TOKEN_BC1FL_OPCODE -> floatingPointBranch
      TOKEN_BC1T_OPCODE -> floatingPointBranch
      TOKEN_BC1TL_OPCODE -> floatingPointBranch
      TOKEN_CFC1_OPCODE -> getFloatingPointControlRegister s floatingPointSource >>= w targetRegister >> ok
      TOKEN_CTC1_OPCODE -> do
        v :: Int32 <- r targetRegister
        setFloatingPointControlRegister s floatingPointSource v
        if floatingPointSource == 31 && fromIntegral v .&. complement floatingPointControlStatusMask /= 0 then raiseOk exceptionFloatingPointException else ok
      TOKEN_CEIL_W_D_OPCODE -> double floatingPointSource >>= setW floatingPointDestination . toInt32 . cCeil >> ok
      TOKEN_CEIL_W_S_OPCODE -> single floatingPointSource >>= setW floatingPointDestination . toInt32 . cCeil . float2Double >> ok
      TOKEN_CVT_D_S_OPCODE -> single floatingPointSource >>= setD floatingPointDestination . float2Double >> ok
      TOKEN_CVT_D_W_OPCODE -> floatingPointRegisterWord s floatingPointSource >>= setD floatingPointDestination . fromIntegral . (fromIntegral :: Word32 -> Int32) >> ok
      TOKEN_CVT_S_D_OPCODE -> double floatingPointSource >>= setS floatingPointDestination . double2Float >> ok
      TOKEN_CVT_S_W_OPCODE -> floatingPointRegisterWord s floatingPointSource >>= setS floatingPointDestination . double2Float . fromIntegral . (fromIntegral :: Word32 -> Int32) >> ok
      TOKEN_CVT_W_D_OPCODE -> double floatingPointSource >>= setW floatingPointDestination . toInt32 >> ok
      TOKEN_CVT_W_S_OPCODE -> single floatingPointSource >>= setW floatingPointDestination . toInt32 . float2Double >> ok
      TOKEN_DIV_S_OPCODE -> binaryS (/) >> ok
      TOKEN_DIV_D_OPCODE -> binaryD (/) >> ok
      TOKEN_FLOOR_W_D_OPCODE -> double floatingPointSource >>= setW floatingPointDestination . toInt32 . cFloor >> ok
      TOKEN_FLOOR_W_S_OPCODE -> single floatingPointSource >>= setW floatingPointDestination . toInt32 . cFloor . float2Double >> ok
      TOKEN_LDC1_OPCODE -> do
        address :: Word32 <- effectiveAddress
        when (address .&. 3 /= 0) $ raise exceptionAddressErrorLoad >> setCoprocessor0 s coprocessor0BadVirtualAddress (fromIntegral address)
        memoryReadWord s address >>= loadInto s (DestinationFloatingPointRegister floatingPointTarget)
        memoryReadWord s (address + 4) >>= loadInto s (DestinationFloatingPointRegister (floatingPointTarget + 1))
        ok
      TOKEN_LWC1_OPCODE -> effectiveAddress >>= memoryReadWord s >>= loadInto s (DestinationFloatingPointRegister floatingPointTarget) >> ok
      TOKEN_MFC1_OPCODE -> floatingPointRegisterWord s floatingPointSource >>= w targetRegister . fromIntegral >> ok
      TOKEN_MOV_S_OPCODE -> floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination >> ok
      TOKEN_MOV_D_OPCODE -> unaryD id >> ok
      TOKEN_MOVF_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c == 0) (r sourceRegister >>= w destinationRegister); ok
      TOKEN_MOVF_D_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c == 0) (unaryD id); ok
      TOKEN_MOVF_S_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c == 0) (floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination); ok
      TOKEN_MOVN_D_OPCODE -> do c :: Int32 <- r targetRegister; when (c /= 0) (unaryD id); ok
      TOKEN_MOVN_S_OPCODE -> do c :: Int32 <- r targetRegister; when (c /= 0) (floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination); ok
      TOKEN_MOVT_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c /= 0) (r sourceRegister >>= w destinationRegister); ok
      TOKEN_MOVT_D_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c /= 0) (unaryD id); ok
      TOKEN_MOVT_S_OPCODE -> do c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2); when (c /= 0) (floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination); ok
      TOKEN_MOVZ_D_OPCODE -> do c :: Int32 <- r targetRegister; when (c == 0) (unaryD id); ok
      TOKEN_MOVZ_S_OPCODE -> do c :: Int32 <- r targetRegister; when (c == 0) (floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination); ok
      TOKEN_MTC1_OPCODE -> r targetRegister >>= setFloatingPointRegisterWord s floatingPointSource . fromIntegral >> ok
      TOKEN_MUL_S_OPCODE -> binaryS (*) >> ok
      TOKEN_MUL_D_OPCODE -> binaryD (*) >> ok
      TOKEN_NEG_S_OPCODE -> floatingPointRegisterWord s floatingPointSource >>= setFloatingPointRegisterWord s floatingPointDestination . xor 0x80000000 >> ok
      TOKEN_NEG_D_OPCODE -> unaryD (\x -> castWord64ToDouble (castDoubleToWord64 x `xor` (1 `shiftL` 63))) >> ok
      TOKEN_ROUND_W_D_OPCODE -> double floatingPointSource >>= setW floatingPointDestination . toInt32 . (+ 0.5) >> ok
      TOKEN_ROUND_W_S_OPCODE -> single floatingPointSource >>= setW floatingPointDestination . toInt32 . (+ 0.5) . float2Double >> ok
      TOKEN_SDC1_OPCODE -> do
        v :: Double <- double targetRegister
        address :: Word32 <- effectiveAddress
        when (address .&. 3 /= 0) $ raise exceptionAddressErrorLoad >> setCoprocessor0 s coprocessor0BadVirtualAddress (fromIntegral address)
        let bits :: Word64
            bits = castDoubleToWord64 v
        memoryWriteWord s address (fromIntegral bits)
        memoryWriteWord s (address + 4) (fromIntegral (bits `shiftR` 32))
        ok
      TOKEN_SQRT_D_OPCODE -> unaryD sqrt >> ok
      TOKEN_SQRT_S_OPCODE -> unaryS (\x -> double2Float (sqrt (float2Double x))) >> ok
      TOKEN_SUB_S_OPCODE -> binaryS (-) >> ok
      TOKEN_SUB_D_OPCODE -> binaryD (-) >> ok
      TOKEN_SWC1_OPCODE -> do
        bits :: Word32 <- castFloatToWord32 <$> single targetRegister
        a :: Word32 <- effectiveAddress
        memoryWriteWord s a (fromIntegral bits)
        ok
      TOKEN_TRUNC_W_D_OPCODE -> double floatingPointSource >>= setW floatingPointDestination . toInt32 >> ok
      TOKEN_TRUNC_W_S_OPCODE -> single floatingPointSource >>= setW floatingPointDestination . toInt32 . float2Double >> ok
      -- The c.cond.fmt compares, here at the end so that no other opcode
      -- pays for the set lookups.
      _
        | op `Set.member` singleCompares -> do
            a :: Float <- single floatingPointSource
            b :: Float <- single floatingPointTarget
            compareFloatingPoint (isNaN a || isNaN b) (a < b) (a == b)
        | op `Set.member` doubleCompares -> do
            a :: Double <- double floatingPointSource
            b :: Double <- double floatingPointTarget
            compareFloatingPoint (isNaN a || isNaN b) (a < b) (a == b)
        | otherwise -> unknown
    shiftRegister :: IO Int
    shiftRegister = (\v -> fromIntegral (v .&. 0x1f)) <$> r sourceRegister
    shiftBy :: IO Int -> (Int32 -> Int -> Int32) -> IO Next
    shiftBy getN f = do
      n :: Int <- getN
      v :: Int32 <- r targetRegister
      w destinationRegister (if n >= 0 && n < 32 then f v n else v)
      ok
    multiplyAdd :: (Int32 -> Int32 -> Word64) -> (Word64 -> Word64 -> Word64) -> IO Next
    multiplyAdd product' combine = do
      old :: Word64 <- multiplyDivide
      a :: Int32 <- r sourceRegister
      b :: Int32 <- r targetRegister
      setMultiplyDivide (old `combine` product' a b)
      ok
    floatingPointBranch :: IO Next
    floatingPointBranch = do
      c :: Int <- floatingPointConditionCode s (targetRegister `shiftR` 2)
      branch (c == (targetRegister .&. 1)) (testBit targetRegister 1)
    readCtl :: Int -> IO Int32
    readCtl n = readControlRegister s n
    writeCtl :: Int -> Int32 -> IO ()
    writeCtl n v = writeControlRegister s n v

-- | C's @ceil@ and @floor@ on a double (NaN and infinities stay as they are).
cCeil, cFloor :: Double -> Double
cCeil x
  | isNaN x || isInfinite x || abs x >= 4503599627370496 = x
  | otherwise =
      let t :: Double
          t = fromIntegral (truncate x :: Int64)
       in if t < x then t + 1 else t
cFloor x
  | isNaN x || isInfinite x || abs x >= 4503599627370496 = x
  | otherwise =
      let t :: Double
          t = fromIntegral (truncate x :: Int64)
       in if t > x then t - 1 else t

readControlRegister :: Spim -> Int -> IO Int32
readControlRegister s n = readArray (coprocessorControl s) n

writeControlRegister :: Spim -> Int -> Int32 -> IO ()
writeControlRegister s n v = writeArray (coprocessorControl s) n v

-- | The FCSR bits a program may set; ctc1 of any other bit to FCSR raises
-- a floating-point exception.
floatingPointControlStatusMask :: Word32
floatingPointControlStatusMask = 0xfe800000

-- | The single- and double-precision c.cond.fmt compares.
singleCompares, doubleCompares :: Set.Set Opcode
singleCompares =
  Set.fromList
    [ TOKEN_C_F_S_OPCODE,
      TOKEN_C_UN_S_OPCODE,
      TOKEN_C_EQ_S_OPCODE,
      TOKEN_C_UEQ_S_OPCODE,
      TOKEN_C_OLT_S_OPCODE,
      TOKEN_C_OLE_S_OPCODE,
      TOKEN_C_ULT_S_OPCODE,
      TOKEN_C_ULE_S_OPCODE,
      TOKEN_C_SF_S_OPCODE,
      TOKEN_C_NGLE_S_OPCODE,
      TOKEN_C_SEQ_S_OPCODE,
      TOKEN_C_NGL_S_OPCODE,
      TOKEN_C_LT_S_OPCODE,
      TOKEN_C_NGE_S_OPCODE,
      TOKEN_C_LE_S_OPCODE,
      TOKEN_C_NGT_S_OPCODE
    ]
doubleCompares =
  Set.fromList
    [ TOKEN_C_F_D_OPCODE,
      TOKEN_C_UN_D_OPCODE,
      TOKEN_C_EQ_D_OPCODE,
      TOKEN_C_UEQ_D_OPCODE,
      TOKEN_C_OLT_D_OPCODE,
      TOKEN_C_OLE_D_OPCODE,
      TOKEN_C_ULT_D_OPCODE,
      TOKEN_C_ULE_D_OPCODE,
      TOKEN_C_SF_D_OPCODE,
      TOKEN_C_NGLE_D_OPCODE,
      TOKEN_C_SEQ_D_OPCODE,
      TOKEN_C_NGL_D_OPCODE,
      TOKEN_C_LT_D_OPCODE,
      TOKEN_C_NGE_D_OPCODE,
      TOKEN_C_LE_D_OPCODE,
      TOKEN_C_NGT_D_OPCODE
    ]
