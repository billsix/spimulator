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
  ( runSpim
  ) where

import Control.Monad (when)
import Data.Array.IO (readArray, writeArray)
import Data.Bits (complement, countLeadingZeros, shiftL, shiftR, testBit, (.&.), (.|.), xor)
import Data.Int (Int32, Int64)
import Data.Word (Word32, Word64)
import GHC.Clock (getMonotonicTimeNSec)
import GHC.Float (castDoubleToWord64, castFloatToWord32, castWord64ToDouble, double2Float, float2Double)
import InstFormat (instToString, printInst)
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
  = Continue -- ^ advance the PC and carry on
  | Return Bool -- ^ leave 'runSpim' now with this answer

-- | Run the program from an address for a number of steps; print each
-- instruction first if display is on.  True means the program can be
-- continued (C: @run_spim@).
runSpim :: Spim -> Word32 -> Int -> Bool -> IO Bool
runSpim s initialPc steps display = do
  set (regPC s) initialPc
  bare <- get (optBare s)
  mio <- get (optMappedIo s)
  let chunk = if not bare && mio then 100 else steps
  startTimer s
  outer chunk steps
  where
    outer chunk left
      | left <= 0 = pure True
      | otherwise = do
          bare <- get (optBare s)
          mio <- get (optMappedIo s)
          when (not bare && mio) $ checkMemoryMappedIO s
          checkInterrupt
          set (forceBreak s) False
          r <- inner (min chunk left)
          case r of
            Just b -> pure b
            Nothing -> outer chunk (left - min chunk left)
    checkInterrupt = do
      status <- fromIntegral <$> getCp0 s cp0Status :: IO Word32
      cause <- fromIntegral <$> getCp0 s cp0Cause :: IO Word32
      when (status .&. statusIE /= 0 && status .&. statusEXL == 0 && (cause .&. causeIP) .&. (status .&. statusIM) /= 0) $ do
        raiseException s excInt
        handleException s
    inner :: Int -> IO (Maybe Bool)
    inner 0 = pure Nothing
    inner n = do
      fb <- get (forceBreak s)
      if fb
        then pure (Just True)
        else do
          r <- step s display
          case r of
            Return b -> pure (Just b)
            Continue -> inner (n - 1)

-- | Fetch, check, execute and finish one instruction.
step :: Spim -> Bool -> IO Next
step s display = do
  setGpr s 0 0
  pollTimer s
  set (exceptionOccurred s) False
  pc <- get (regPC s)
  minst <- memReadInst s pc
  exc <- get (exceptionOccurred s)
  if exc
    then do
      set (exceptionOccurred s) False
      handleException s
      pure Continue
    else case minst of
      Nothing -> runError (printf "Attempt to execute non-instruction at 0x%08x\n" pc)
      Just ref -> do
        i <- get ref
        undefinedSymbol <- case inExpr i >>= ieSymbol of
          Just l -> (== 0) <$> get (lblAddr l)
          Nothing -> pure False
        when undefinedSymbol $ do
          text <- instToString s pc
          runError (printf "Instruction references undefined symbol at 0x%08x\n  %s" pc text)
        doDelayedUpdate s
        r <- execute s display i
        case r of
          Return b -> pure (Return b)
          Continue -> do
            modify (regPC s) (+ 4)
            when display $ get (regPC s) >>= printInst s
            exc' <- get (exceptionOccurred s)
            when exc' $ handleException s
            pure Continue

-- The CP0 timer: Count goes up every 10 ms of real time, and an interrupt
-- is raised when it reaches Compare.

startTimer :: Spim -> IO ()
startTimer s = do
  now <- microseconds
  deadline <- get (timerDeadline s)
  when (deadline <= now) $ set (timerDeadline s) (now + 10000)

pollTimer :: Spim -> IO ()
pollTimer s = do
  now <- microseconds
  deadline <- get (timerDeadline s)
  when (deadline <= now) $ do
    count <- (+ 1) <$> getCp0 s cp0Count
    setCp0 s cp0Count count
    compare' <- getCp0 s cp0Compare
    when (count == compare') $ do
      c <- getCp0 s cp0Cause
      setCp0 s cp0Cause (c .|. (1 `shiftL` 15))
    set (timerDeadline s) (now + 10000)

microseconds :: IO Integer
microseconds = (`quot` 1000) . toInteger <$> getMonotonicTimeNSec

-- Delayed loads: a two-stage pipeline of (destination, value).

writeDest :: Spim -> LoadDest -> Int32 -> IO ()
writeDest s (DestGpr r) v = setGpr s r v
writeDest s (DestFpr r) v = setFprWord s r (fromIntegral v)

doDelayedUpdate :: Spim -> IO ()
doDelayedUpdate s = do
  delayed <- get (optDelayedLoads s)
  when delayed $ do
    d2 <- get (delayedLoad2 s)
    mapM_ (uncurry (writeDest s)) d2
    get (delayedLoad1 s) >>= set (delayedLoad2 s)
    set (delayedLoad1 s) Nothing

-- | Write a loaded value now, or one instruction later (C: @LOAD_INST@).
loadInto :: Spim -> LoadDest -> Int32 -> IO ()
loadInto s dest v = do
  delayed <- get (optDelayedLoads s)
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
execute s display i = case opcodeOfToken (inOpcode i) of
  Nothing -> unknown
  Just op -> exec op
  where
    unknown = fatalError (printf "Unknown instruction type: %d\n" (inOpcode i))
    ok = pure Continue
    rs = instRs i
    rt = instRt i
    rd = instRd i
    fs = instFs i
    ft = instFt i
    fd = instFd i
    imm = instImm i
    immS = fromIntegral imm :: Int32
    immU = fromIntegral immS :: Word32
    r = getGpr s
    ru n = (fromIntegral :: Int32 -> Word32) <$> getGpr s n
    w = setGpr s
    raise code = raiseException s code
    raiseOk code = raise code >> ok
    address = (\b -> b + immU) <$> ru (instBase i)
    -- JUMP_INST: with delayed branches, run the delay slot first.
    jump target = do
      delayed <- get (optDelayedBranches s)
      when delayed $ do
        set (runningInDelaySlot s) True
        pc <- get (regPC s)
        _ <- runSpim s (pc + 4) 1 display
        set (runningInDelaySlot s) False
      set (regPC s) (target - 4)
      ok
    -- BRANCH_INST: jump if the test holds; a branch-likely that is not
    -- taken skips its delay slot.
    branch test nullify = do
      pc <- get (regPC s)
      if test
        then do
          delayed <- get (optDelayedBranches s)
          jump (pc + fromIntegral (branchOffset i) + (if delayed then 4 else 0))
        else do
          when nullify $ modify (regPC s) (+ 4)
          ok
    link reg = do
      pc <- get (regPC s)
      delayed <- get (optDelayedBranches s)
      w reg (fromIntegral (pc + (if delayed then 8 else 4)))
    signBit v = v .&. 0x80000000 /= (0 :: Word32)
    setHiLo :: Word64 -> IO ()
    setHiLo p = set (regLO s) (fromIntegral p) >> set (regHI s) (fromIntegral (p `shiftR` 32))
    hiLo = do
      hi <- get (regHI s)
      lo <- get (regLO s)
      pure ((fromIntegral (fromIntegral hi :: Word32) `shiftL` 32) .|. fromIntegral (fromIntegral lo :: Word32) :: Word64)
    signedProduct a b = fromIntegral (fromIntegral a * fromIntegral b :: Int64) :: Word64
    unsignedProduct a b = (fromIntegral (fromIntegral a :: Word32) * fromIntegral (fromIntegral b :: Word32)) :: Word64
    trapIf c = when c (raise excTr) >> ok
    single = fprSingle s
    double = fprDouble s
    setS n v = setFprSingle s n v
    setD n v = setFprDouble s n v
    setW n v = setFprWord s n (fromIntegral (v :: Int32))
    unaryS f = single fs >>= setS fd . f
    unaryD f = double fs >>= setD fd . f
    binaryS f = do a <- single fs; b <- single ft; setS fd (f a b)
    binaryD f = do a <- double fs; b <- double ft; setD fd (f a b)
    -- c.cond.fmt: set condition code CCFP from less/equal/unordered.
    compareFp :: Bool -> Bool -> Bool -> IO Next
    compareFp nan less equal = do
      let cond = instCond i
          cc = fd `shiftR` 2
          result l e u = (if cond .&. 4 /= 0 then l else 0) .|. (if cond .&. 2 /= 0 then e else 0) .|. (if cond .&. 1 /= 0 then u else 0)
          b x = if x then 1 else 0 :: Int
      if nan
        then
          if cond .&. 8 /= 0
            then raiseOk excFPE
            else setFcc s cc (result 0 0 1) >> ok
        else setFcc s cc (result (b less) (b equal) 0) >> ok
    exec op = case op of
      TOK_ADD_OPCODE -> do
        a <- r rs
        b <- r rt
        let sum' = a + b
        if (a >= 0) == (b >= 0) && (sum' >= 0) /= (a >= 0) then raiseOk excOv else w rd sum' >> ok
      TOK_ADDI_OPCODE -> do
        a <- r rs
        let sum' = a + immS
        if (a >= 0) == (immS >= 0) && (sum' >= 0) /= (a >= 0) then raiseOk excOv else w rt sum' >> ok
      TOK_ADDIU_OPCODE -> r rs >>= \a -> w rt (a + immS) >> ok
      TOK_ADDU_OPCODE -> do a <- r rs; b <- r rt; w rd (a + b); ok
      TOK_AND_OPCODE -> do a <- r rs; b <- r rt; w rd (a .&. b); ok
      TOK_ANDI_OPCODE -> r rs >>= \a -> w rt (a .&. (0xffff .&. immS)) >> ok
      TOK_BC2F_OPCODE -> raiseOk excCpU
      TOK_BC2FL_OPCODE -> raiseOk excCpU
      TOK_BC2T_OPCODE -> raiseOk excCpU
      TOK_BC2TL_OPCODE -> raiseOk excCpU
      TOK_BEQ_OPCODE -> do a <- r rs; b <- r rt; branch (a == b) False
      TOK_BEQL_OPCODE -> do a <- r rs; b <- r rt; branch (a == b) True
      TOK_BGEZ_OPCODE -> ru rs >>= \a -> branch (not (signBit a)) False
      TOK_BGEZL_OPCODE -> ru rs >>= \a -> branch (not (signBit a)) True
      TOK_BGEZAL_OPCODE -> link 31 >> ru rs >>= \a -> branch (not (signBit a)) False
      TOK_BGEZALL_OPCODE -> link 31 >> ru rs >>= \a -> branch (not (signBit a)) True
      TOK_BGTZ_OPCODE -> ru rs >>= \a -> branch (a /= 0 && not (signBit a)) False
      TOK_BGTZL_OPCODE -> ru rs >>= \a -> branch (a /= 0 && not (signBit a)) True
      TOK_BLEZ_OPCODE -> ru rs >>= \a -> branch (a == 0 || signBit a) False
      TOK_BLEZL_OPCODE -> ru rs >>= \a -> branch (a == 0 || signBit a) True
      TOK_BLTZ_OPCODE -> ru rs >>= \a -> branch (signBit a) False
      TOK_BLTZL_OPCODE -> ru rs >>= \a -> branch (signBit a) True
      TOK_BLTZAL_OPCODE -> link 31 >> ru rs >>= \a -> branch (signBit a) False
      TOK_BLTZALL_OPCODE -> link 31 >> ru rs >>= \a -> branch (signBit a) True
      TOK_BNE_OPCODE -> do a <- r rs; b <- r rt; branch (a /= b) False
      TOK_BNEL_OPCODE -> do a <- r rs; b <- r rt; branch (a /= b) True
      TOK_BREAK_OPCODE
        | rd == 1 -> raise excBp >> pure (Return True) -- a debugger breakpoint
        | otherwise -> raiseOk excBp
      TOK_CACHE_OPCODE -> ok
      TOK_CFC0_OPCODE -> readCtl rd >>= w rt >> ok
      TOK_CFC2_OPCODE -> raiseOk excCpU
      TOK_CLO_OPCODE -> ru rs >>= \a -> w rd (fromIntegral (countLeadingZeros (complement a))) >> ok
      TOK_CLZ_OPCODE -> ru rs >>= \a -> w rd (fromIntegral (countLeadingZeros a)) >> ok
      TOK_COP2_OPCODE -> raiseOk excCpU
      TOK_CTC0_OPCODE -> r rt >>= writeCtl rd >> ok
      TOK_CTC2_OPCODE -> raiseOk excCpU
      TOK_DIV_OPCODE -> do
        a <- r rs
        b <- r rt
        when (b /= 0 && not (a == minBound && b == -1)) $ do
          set (regLO s) (a `quot` b)
          set (regHI s) (a `rem` b)
        ok
      TOK_DIVU_OPCODE -> do
        a <- ru rs
        b <- ru rt
        when (b /= 0) $ do
          set (regLO s) (fromIntegral (a `quot` b))
          set (regHI s) (fromIntegral (a `rem` b))
        ok
      TOK_ERET_OPCODE -> do
        st <- getCp0 s cp0Status
        setCp0 s cp0Status (st .&. complement (fromIntegral statusEXL))
        getCp0 s cp0Epc >>= jump . fromIntegral
      TOK_J_OPCODE -> do pc <- get (regPC s); jump ((pc .&. 0xf0000000) .|. (instTarget i `shiftL` 2))
      TOK_JAL_OPCODE -> do
        link 31
        pc <- get (regPC s)
        jump ((pc .&. 0xf0000000) .|. (instTarget i `shiftL` 2))
      TOK_JALR_OPCODE -> do
        target <- ru rs
        link rd
        jump target
      TOK_JR_OPCODE -> ru rs >>= jump
      TOK_LB_OPCODE -> address >>= memReadByte s >>= loadInto s (DestGpr rt) >> ok
      TOK_LBU_OPCODE -> address >>= memReadByte s >>= loadInto s (DestGpr rt) . (.&. 0xff) >> ok
      TOK_LH_OPCODE -> address >>= memReadHalf s >>= loadInto s (DestGpr rt) >> ok
      TOK_LHU_OPCODE -> address >>= memReadHalf s >>= loadInto s (DestGpr rt) . (.&. 0xffff) >> ok
      TOK_LL_OPCODE -> address >>= memReadWord s >>= loadInto s (DestGpr rt) >> ok
      TOK_LUI_OPCODE -> w rt (fromIntegral ((immU `shiftL` 16) .&. 0xffff0000)) >> ok
      TOK_LW_OPCODE -> address >>= memReadWord s >>= loadInto s (DestGpr rt) >> ok
      TOK_LDC2_OPCODE -> raiseOk excCpU
      TOK_LWC2_OPCODE -> raiseOk excCpU
      TOK_LWL_OPCODE -> do
        addr <- address
        regVal <- ru rt
        word <- fromIntegral <$> memReadWord s (addr .&. 0xfffffffc) :: IO Word32
        exc <- get (exceptionOccurred s)
        let merged = case addr .&. 3 of
              0 -> ((word .&. 0xff) `shiftL` 24) .|. (regVal .&. 0xffffff)
              1 -> ((word .&. 0xffff) `shiftL` 16) .|. (regVal .&. 0xffff)
              2 -> ((word .&. 0xffffff) `shiftL` 8) .|. (regVal .&. 0xff)
              _ -> word
        loadInto s (DestGpr rt) (fromIntegral (if exc then word else merged))
        ok
      TOK_LWR_OPCODE -> do
        addr <- address
        regVal <- ru rt
        word <- fromIntegral <$> memReadWord s (addr .&. 0xfffffffc) :: IO Word32
        exc <- get (exceptionOccurred s)
        let merged = case addr .&. 3 of
              0 -> word
              1 -> (regVal .&. 0xff000000) .|. ((word .&. 0xffffff00) `shiftR` 8)
              2 -> (regVal .&. 0xffff0000) .|. ((word .&. 0xffff0000) `shiftR` 16)
              _ -> (regVal .&. 0xffffff00) .|. ((word .&. 0xff000000) `shiftR` 24)
        loadInto s (DestGpr rt) (fromIntegral (if exc then word else merged))
        ok
      TOK_MADD_OPCODE -> multiplyAdd signedProduct (+)
      TOK_MADDU_OPCODE -> multiplyAdd unsignedProduct (+)
      TOK_MFC0_OPCODE -> getCp0 s fs >>= w rt >> ok
      TOK_MFC2_OPCODE -> raiseOk excCpU
      TOK_MFHI_OPCODE -> get (regHI s) >>= w rd >> ok
      TOK_MFLO_OPCODE -> get (regLO s) >>= w rd >> ok
      TOK_MOVN_OPCODE -> do c <- r rt; when (c /= 0) (r rs >>= w rd); ok
      TOK_MOVZ_OPCODE -> do c <- r rt; when (c == 0) (r rs >>= w rd); ok
      TOK_MSUB_OPCODE -> multiplyAdd signedProduct (-)
      TOK_MSUBU_OPCODE -> multiplyAdd unsignedProduct (-)
      TOK_MTC0_OPCODE -> do
        r rt >>= setCp0 s fs
        let masked mask = getCp0 s fs >>= \v -> setCp0 s fs (v .&. fromIntegral (mask :: Word32))
        if fs == cp0Compare
          then getCp0 s cp0Cause >>= \c -> setCp0 s cp0Cause (c .&. complement 0x8000)
          else
            if fs == cp0Status
              then do
                st <- getCp0 s cp0Status
                let st' = (fromIntegral st .&. statusMask) .|. ((statusCU .&. 0x30000000) .|. statusUM)
                setCp0 s cp0Status (fromIntegral st')
              else
                if fs == cp0Cause
                  then masked causeMask
                  else when (fs == cp0Config) (masked configMask)
        ok
      TOK_MTC2_OPCODE -> raiseOk excCpU
      TOK_MTHI_OPCODE -> r rs >>= set (regHI s) >> ok
      TOK_MTLO_OPCODE -> r rs >>= set (regLO s) >> ok
      TOK_MUL_OPCODE -> do
        a <- r rs
        b <- r rt
        setHiLo (signedProduct a b)
        get (regLO s) >>= w rd
        ok
      TOK_MULT_OPCODE -> do a <- r rs; b <- r rt; setHiLo (signedProduct a b); ok
      TOK_MULTU_OPCODE -> do a <- r rs; b <- r rt; setHiLo (unsignedProduct a b); ok
      TOK_NOR_OPCODE -> do a <- r rs; b <- r rt; w rd (complement (a .|. b)); ok
      TOK_OR_OPCODE -> do a <- r rs; b <- r rt; w rd (a .|. b); ok
      TOK_ORI_OPCODE -> r rs >>= \a -> w rt (a .|. (0xffff .&. immS)) >> ok
      TOK_PREF_OPCODE -> ok
      TOK_RFE_OPCODE -> raiseOk excRI
      TOK_SB_OPCODE -> do a <- address; r rt >>= memWriteByte s a; ok
      TOK_SC_OPCODE -> do a <- address; r rt >>= memWriteWord s a; ok
      TOK_SDC2_OPCODE -> raiseOk excCpU
      TOK_SH_OPCODE -> do a <- address; r rt >>= memWriteHalf s a; ok
      TOK_SLL_OPCODE -> shiftBy (pure (instShamt i)) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftL` n))
      TOK_SLLV_OPCODE -> shiftBy (shiftReg) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftL` n))
      TOK_SLT_OPCODE -> do a <- r rs; b <- r rt; w rd (if a < b then 1 else 0); ok
      TOK_SLTI_OPCODE -> r rs >>= \a -> w rt (if a < immS then 1 else 0) >> ok
      TOK_SLTIU_OPCODE -> ru rs >>= \a -> w rt (if a < immU then 1 else 0) >> ok
      TOK_SLTU_OPCODE -> do a <- ru rs; b <- ru rt; w rd (if a < b then 1 else 0); ok
      TOK_SRA_OPCODE -> shiftBy (pure (instShamt i)) shiftR
      TOK_SRAV_OPCODE -> shiftBy shiftReg shiftR
      TOK_SRL_OPCODE -> shiftBy (pure (instShamt i)) (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftR` n))
      TOK_SRLV_OPCODE -> shiftBy shiftReg (\v n -> fromIntegral ((fromIntegral v :: Word32) `shiftR` n))
      TOK_SUB_OPCODE -> do
        a <- r rs
        b <- r rt
        let diff = a - b
        if (a >= 0) /= (b >= 0) && (diff >= 0) /= (a >= 0) then raiseOk excOv else w rd diff >> ok
      TOK_SUBU_OPCODE -> do a <- r rs; b <- r rt; w rd (a - b); ok
      TOK_SW_OPCODE -> do a <- address; r rt >>= memWriteWord s a; ok
      TOK_SWC2_OPCODE -> raiseOk excCpU
      TOK_SWL_OPCODE -> do
        addr <- address
        regVal <- ru rt
        let aligned = addr .&. 0xfffffffc
        dat <- fromIntegral <$> memReadWord s aligned :: IO Word32
        let new = case addr .&. 3 of
              0 -> (dat .&. 0xffffff00) .|. ((regVal `shiftR` 24) .&. 0xff)
              1 -> (dat .&. 0xffff0000) .|. ((regVal `shiftR` 16) .&. 0xffff)
              2 -> (dat .&. 0xff000000) .|. ((regVal `shiftR` 8) .&. 0xffffff)
              _ -> regVal
        memWriteWord s aligned (fromIntegral new)
        ok
      TOK_SWR_OPCODE -> do
        addr <- address
        regVal <- ru rt
        let aligned = addr .&. 0xfffffffc
        dat <- fromIntegral <$> memReadWord s aligned :: IO Word32
        let new = case addr .&. 3 of
              0 -> regVal
              1 -> ((regVal `shiftL` 8) .&. 0xffffff00) .|. (dat .&. 0xff)
              2 -> ((regVal `shiftL` 16) .&. 0xffff0000) .|. (dat .&. 0xffff)
              _ -> ((regVal `shiftL` 24) .&. 0xff000000) .|. (dat .&. 0xffffff)
        memWriteWord s aligned (fromIntegral new)
        ok
      TOK_SYNC_OPCODE -> ok
      TOK_SYSCALL_OPCODE -> do
        more <- doSyscall s
        pure (if more then Continue else Return False)
      TOK_TEQ_OPCODE -> do a <- r rs; b <- r rt; trapIf (a == b)
      TOK_TEQI_OPCODE -> r rs >>= \a -> trapIf (a == immS)
      TOK_TGE_OPCODE -> do a <- r rs; b <- r rt; trapIf (a >= b)
      TOK_TGEI_OPCODE -> r rs >>= \a -> trapIf (a >= immS)
      TOK_TGEIU_OPCODE -> ru rs >>= \a -> trapIf (a >= immU)
      TOK_TGEU_OPCODE -> do a <- ru rs; b <- ru rt; trapIf (a >= b)
      TOK_TLBP_OPCODE -> raiseOk excRI
      TOK_TLBR_OPCODE -> raiseOk excRI
      TOK_TLBWI_OPCODE -> raiseOk excRI
      TOK_TLBWR_OPCODE -> raiseOk excRI
      TOK_TLT_OPCODE -> do a <- r rs; b <- r rt; trapIf (a < b)
      TOK_TLTI_OPCODE -> r rs >>= \a -> trapIf (a < immS)
      TOK_TLTIU_OPCODE -> ru rs >>= \a -> trapIf (a < immU)
      TOK_TLTU_OPCODE -> do a <- ru rs; b <- ru rt; trapIf (a < b)
      TOK_TNE_OPCODE -> do a <- r rs; b <- r rt; trapIf (a /= b)
      TOK_TNEI_OPCODE -> r rs >>= \a -> trapIf (a /= immS)
      TOK_XOR_OPCODE -> do a <- r rs; b <- r rt; w rd (a `xor` b); ok
      TOK_XORI_OPCODE -> r rs >>= \a -> w rt (a `xor` (0xffff .&. immS)) >> ok
      -- The floating-point unit.
      TOK_ABS_S_OPCODE -> unaryS (\x -> double2Float (fabsD (float2Double x))) >> ok
      TOK_ABS_D_OPCODE -> unaryD fabsD >> ok
      TOK_ADD_S_OPCODE -> binaryS (+) >> ok
      TOK_ADD_D_OPCODE -> binaryD (+) >> ok
      TOK_BC1F_OPCODE -> fpBranch
      TOK_BC1FL_OPCODE -> fpBranch
      TOK_BC1T_OPCODE -> fpBranch
      TOK_BC1TL_OPCODE -> fpBranch
      _ | op `elem` singleCompares -> do
            a <- single fs
            b <- single ft
            compareFp (isNaN a || isNaN b) (a < b) (a == b)
        | op `elem` doubleCompares -> do
            a <- double fs
            b <- double ft
            compareFp (isNaN a || isNaN b) (a < b) (a == b)
      TOK_CFC1_OPCODE -> getFcr s fs >>= w rt >> ok
      TOK_CTC1_OPCODE -> do
        v <- r rt
        setFcr s fs v
        if fs == 31 && fromIntegral v .&. complement (0xfe800000 :: Word32) /= 0 then raiseOk excFPE else ok
      TOK_CEIL_W_D_OPCODE -> double fs >>= setW fd . toInt32 . cCeil >> ok
      TOK_CEIL_W_S_OPCODE -> single fs >>= setW fd . toInt32 . cCeil . float2Double >> ok
      TOK_CVT_D_S_OPCODE -> single fs >>= setD fd . float2Double >> ok
      TOK_CVT_D_W_OPCODE -> fprWord s fs >>= setD fd . fromIntegral . (fromIntegral :: Word32 -> Int32) >> ok
      TOK_CVT_S_D_OPCODE -> double fs >>= setS fd . double2Float >> ok
      TOK_CVT_S_W_OPCODE -> fprWord s fs >>= setS fd . double2Float . fromIntegral . (fromIntegral :: Word32 -> Int32) >> ok
      TOK_CVT_W_D_OPCODE -> double fs >>= setW fd . toInt32 >> ok
      TOK_CVT_W_S_OPCODE -> single fs >>= setW fd . toInt32 . float2Double >> ok
      TOK_DIV_S_OPCODE -> binaryS (/) >> ok
      TOK_DIV_D_OPCODE -> binaryD (/) >> ok
      TOK_FLOOR_W_D_OPCODE -> double fs >>= setW fd . toInt32 . cFloor >> ok
      TOK_FLOOR_W_S_OPCODE -> single fs >>= setW fd . toInt32 . cFloor . float2Double >> ok
      TOK_LDC1_OPCODE -> do
        addr <- address
        when (addr .&. 3 /= 0) $ raise excAdEL >> setCp0 s cp0BadVAddr (fromIntegral addr)
        memReadWord s addr >>= loadInto s (DestFpr ft)
        memReadWord s (addr + 4) >>= loadInto s (DestFpr (ft + 1))
        ok
      TOK_LWC1_OPCODE -> address >>= memReadWord s >>= loadInto s (DestFpr ft) >> ok
      TOK_MFC1_OPCODE -> fprWord s fs >>= w rt . fromIntegral >> ok
      TOK_MOV_S_OPCODE -> fprWord s fs >>= setFprWord s fd >> ok
      TOK_MOV_D_OPCODE -> unaryD id >> ok
      TOK_MOVF_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c == 0) (r rs >>= w rd); ok
      TOK_MOVF_D_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c == 0) (unaryD id); ok
      TOK_MOVF_S_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c == 0) (fprWord s fs >>= setFprWord s fd); ok
      TOK_MOVN_D_OPCODE -> do c <- r rt; when (c /= 0) (unaryD id); ok
      TOK_MOVN_S_OPCODE -> do c <- r rt; when (c /= 0) (fprWord s fs >>= setFprWord s fd); ok
      TOK_MOVT_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c /= 0) (r rs >>= w rd); ok
      TOK_MOVT_D_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c /= 0) (unaryD id); ok
      TOK_MOVT_S_OPCODE -> do c <- fcc s (rt `shiftR` 2); when (c /= 0) (fprWord s fs >>= setFprWord s fd); ok
      TOK_MOVZ_D_OPCODE -> do c <- r rt; when (c == 0) (unaryD id); ok
      TOK_MOVZ_S_OPCODE -> do c <- r rt; when (c == 0) (fprWord s fs >>= setFprWord s fd); ok
      TOK_MTC1_OPCODE -> r rt >>= setFprWord s fs . fromIntegral >> ok
      TOK_MUL_S_OPCODE -> binaryS (*) >> ok
      TOK_MUL_D_OPCODE -> binaryD (*) >> ok
      TOK_NEG_S_OPCODE -> fprWord s fs >>= setFprWord s fd . xor 0x80000000 >> ok
      TOK_NEG_D_OPCODE -> unaryD (\x -> castWord64ToDouble (castDoubleToWord64 x `xor` (1 `shiftL` 63))) >> ok
      TOK_ROUND_W_D_OPCODE -> double fs >>= setW fd . toInt32 . (+ 0.5) >> ok
      TOK_ROUND_W_S_OPCODE -> single fs >>= setW fd . toInt32 . (+ 0.5) . float2Double >> ok
      TOK_SDC1_OPCODE -> do
        v <- double rt
        addr <- address
        when (addr .&. 3 /= 0) $ raise excAdEL >> setCp0 s cp0BadVAddr (fromIntegral addr)
        let bits = castDoubleToWord64 v
        memWriteWord s addr (fromIntegral bits)
        memWriteWord s (addr + 4) (fromIntegral (bits `shiftR` 32))
        ok
      TOK_SQRT_D_OPCODE -> unaryD sqrt >> ok
      TOK_SQRT_S_OPCODE -> unaryS (\x -> double2Float (sqrt (float2Double x))) >> ok
      TOK_SUB_S_OPCODE -> binaryS (-) >> ok
      TOK_SUB_D_OPCODE -> binaryD (-) >> ok
      TOK_SWC1_OPCODE -> do
        bits <- castFloatToWord32 <$> single rt
        a <- address
        memWriteWord s a (fromIntegral bits)
        ok
      TOK_TRUNC_W_D_OPCODE -> double fs >>= setW fd . toInt32 >> ok
      TOK_TRUNC_W_S_OPCODE -> single fs >>= setW fd . toInt32 . float2Double >> ok
      _ -> unknown
    shiftReg = (\v -> fromIntegral (v .&. 0x1f)) <$> r rs
    shiftBy :: IO Int -> (Int32 -> Int -> Int32) -> IO Next
    shiftBy getN f = do
      n <- getN
      v <- r rt
      w rd (if n >= 0 && n < 32 then f v n else v)
      ok
    multiplyAdd product' combine = do
      old <- hiLo
      a <- r rs
      b <- r rt
      setHiLo (old `combine` product' a b)
      ok
    fpBranch = do
      c <- fcc s (rt `shiftR` 2)
      branch (c == (rt .&. 1)) (testBit rt 1)
    readCtl n = readCtlReg s n
    writeCtl n v = writeCtlReg s n v

-- | C's @ceil@ and @floor@ on a double (NaN and infinities stay as they are).
cCeil, cFloor :: Double -> Double
cCeil x
  | isNaN x || isInfinite x || abs x >= 4503599627370496 = x
  | otherwise = let t = fromIntegral (truncate x :: Int64) in if t < x then t + 1 else t
cFloor x
  | isNaN x || isInfinite x || abs x >= 4503599627370496 = x
  | otherwise = let t = fromIntegral (truncate x :: Int64) in if t > x then t - 1 else t

readCtlReg :: Spim -> Int -> IO Int32
readCtlReg s n = readArray (cpControl s) n

writeCtlReg :: Spim -> Int -> Int32 -> IO ()
writeCtlReg s n v = writeArray (cpControl s) n v

singleCompares, doubleCompares :: [Opcode]
singleCompares =
  [ TOK_C_F_S_OPCODE, TOK_C_UN_S_OPCODE, TOK_C_EQ_S_OPCODE, TOK_C_UEQ_S_OPCODE, TOK_C_OLT_S_OPCODE
  , TOK_C_OLE_S_OPCODE, TOK_C_ULT_S_OPCODE, TOK_C_ULE_S_OPCODE, TOK_C_SF_S_OPCODE, TOK_C_NGLE_S_OPCODE
  , TOK_C_SEQ_S_OPCODE, TOK_C_NGL_S_OPCODE, TOK_C_LT_S_OPCODE, TOK_C_NGE_S_OPCODE, TOK_C_LE_S_OPCODE
  , TOK_C_NGT_S_OPCODE ]
doubleCompares =
  [ TOK_C_F_D_OPCODE, TOK_C_UN_D_OPCODE, TOK_C_EQ_D_OPCODE, TOK_C_UEQ_D_OPCODE, TOK_C_OLT_D_OPCODE
  , TOK_C_OLE_D_OPCODE, TOK_C_ULT_D_OPCODE, TOK_C_ULE_D_OPCODE, TOK_C_SF_D_OPCODE, TOK_C_NGLE_D_OPCODE
  , TOK_C_SEQ_D_OPCODE, TOK_C_NGL_D_OPCODE, TOK_C_LT_D_OPCODE, TOK_C_NGE_D_OPCODE, TOK_C_LE_D_OPCODE
  , TOK_C_NGT_D_OPCODE ]
