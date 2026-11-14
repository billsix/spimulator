-- | Assembling instructions into the text segment (C: the assembling half
-- of src/instruction.c).
--
-- The emitter calls these with an instruction's opcode and operands.  The
-- interesting one is 'iType': a MIPS immediate is only 16 bits, so an
-- immediate that does not fit (a big constant, or an address) becomes two
-- or three instructions using the assembler's register @$at@ ($1), and an
-- immediate naming a label not yet defined is recorded and patched later.
module Assemble
  ( textBeginsAtPoint,
    kernelTextBeginsAtPoint,
    alignText,
    setTextLocation,
    userKernelTextSegment,
    iType,
    jType,
    rType,
    rCoprocessorType,
    rShiftAmountType,
    rConditionType,
    makeImmediateExpression,
    constantImmediateExpression,
    incrementExpressionOffset,
    makeAddressExpression,
    isZeroImmediate,
    immediateOpcodeToOpcode,
    opcodeToImmediateOpcode,
  )
where

import AsmCommon
import Control.Monad (unless, when)
import Data.Bits (shiftL, shiftR, (.&.), (.|.))
import Data.IORef (IORef, newIORef)
import Data.Int (Int32)
import Data.Word (Word32)
import DataSegment (storeWord)
import Instruction
import Memory (memoryWriteInstruction)
import Opcodes
import Output
import State
import SymbolTable
import Text.Printf (printf)
import Types

-- | Start user text at an address (C: @text_begins_at_point@).
textBeginsAtPoint :: Spim -> Word32 -> IO ()
textBeginsAtPoint s address = set (nextTextLocation s) address >> set (textInKernel s) False

kernelTextBeginsAtPoint :: Spim -> Word32 -> IO ()
kernelTextBeginsAtPoint s address = set (nextKernelTextLocation s) address >> set (textInKernel s) False

-- | Send the following instructions to the kernel or user text segment.
userKernelTextSegment :: Spim -> Bool -> IO ()
userKernelTextSegment s = set (textInKernel s)

textLocationReference :: Spim -> IO (IORef Word32)
textLocationReference s = do
  k :: Bool <- get (textInKernel s)
  pure (if k then nextKernelTextLocation s else nextTextLocation s)

-- | @.align n@ in the text segment (C: @align_text@).
alignText :: Spim -> Int -> IO ()
alignText s n
  | n == 0 = pure ()
  | otherwise = do
      ref :: IORef Word32 <- textLocationReference s
      programCounter :: Word32 <- get ref
      let programCounter' :: Word32
          programCounter' = (programCounter + (1 `shiftL` n) - 1) .&. (0xffffffff `shiftL` n)
      set ref programCounter'
      fixCurrentLabelAddress s programCounter'

setTextLocation :: Spim -> Word32 -> IO ()
setTextLocation s address = textLocationReference s >>= (`set` address)

incrementTextLocation :: Spim -> IO ()
incrementTextLocation s = do
  k :: Bool <- get (textInKernel s)
  if k
    then do
      modify (nextKernelTextLocation s) (+ 4)
      programCounter :: Word32 <- get (nextKernelTextLocation s)
      top :: Word32 <- get (kernelTextTop s)
      when (top <= programCounter) $ runError "Can't expand kernel text segment\n"
    else do
      modify (nextTextLocation s) (+ 4)
      programCounter :: Word32 <- get (nextTextLocation s)
      top :: Word32 <- get (textTop s)
      when (top <= programCounter) $ runError "Can't expand text segment\n"

-- | Put an instruction at the next location (C: @store_instruction@).  In
-- the data segment it is stored as its encoding.
storeInstruction :: Spim -> InstructionRef -> IO ()
storeInstruction s ref = do
  inData :: Bool <- get (dataDirective s)
  inText :: Bool <- get (textDirective s)
  -- Never reached: the parser always sets dataDirective and textDirective as a pair, one true.
  unless (inData || inText) $
    fatalError "internal error: store_instruction: neither the data nor the text segment is active\n"
  if inData
    then get ref >>= instructionEncodeIO >>= storeWord s
    else when inText $ do
      at :: Word32 <- currentTextLocation s
      set (exceptionOccurred s) False
      memoryWriteInstruction s at (Just ref)
      exception :: Bool <- get (exceptionOccurred s)
      if exception
        then cError (printf "Invalid address (0x%08x) for instruction\n" at)
        else incrementTextLocation s
      override :: Maybe (Maybe String) <- get (emitSource s)
      source :: Maybe String <- maybe (sourceLine s) pure override
      modify ref (\i -> i {instructionSource = source})
      i :: Instruction <- get ref
      when (instructionEncoding i == 0) $ do
        encoding :: Int32 <- instructionEncodeIO i
        set ref i {instructionEncoding = encoding}
      encoding :: Int32 <- instructionEncoding <$> get ref
      fire s (EventTextInstruction at (fromIntegral encoding))

-- | An immediate expression (C: @make_imm_expr@).  Naming a label puts it
-- in the symbol table.
makeImmediateExpression :: Spim -> Int32 -> Maybe String -> Bool -> IO ImmediateExpression
makeImmediateExpression s offset symbol programCounterRelative = do
  l :: Maybe Label <- traverse (lookupLabel s) symbol
  pure (ImmediateExpression offset l 0 programCounterRelative)

-- | A constant (C: @const_imm_expr@).
constantImmediateExpression :: Int32 -> ImmediateExpression
constantImmediateExpression v = ImmediateExpression v Nothing 0 False

upperBits, lowerBits :: ImmediateExpression -> ImmediateExpression
upperBits e = e {immediateExpressionBits = 1}
lowerBits e = e {immediateExpressionBits = -1}

-- | C: @incr_expr_offset@.
incrementExpressionOffset :: ImmediateExpression -> Int32 -> ImmediateExpression
incrementExpressionOffset e v = e {immediateExpressionOffset = immediateExpressionOffset e + v}

-- | C: @is_zero_imm@.
isZeroImmediate :: ImmediateExpression -> Bool
isZeroImmediate e = immediateExpressionOffset e == 0 && null (immediateExpressionSymbol e)

-- | An address @symbol + offset(register)@ (C: @make_addr_expr@).  A small
-- global in the $gp area becomes an offset from $gp.
makeAddressExpression :: Spim -> Int32 -> Maybe String -> Int -> IO (Int, ImmediateExpression)
makeAddressExpression s offset symbol registerNo = do
  globalPointerLabel :: Maybe Label <- case symbol of
    Just name | registerNo == 0 -> do
      l :: Label <- lookupLabel s name
      gp :: Bool <- get (labelGlobalPointer l)
      pure (if gp then Just l else Nothing)
    _ -> pure Nothing
  case globalPointerLabel of
    Just l -> do
      address :: Word32 <- get (labelAddress l)
      mid :: Word32 <- get (globalPointerMidpoint s)
      pure (28, constantImmediateExpression (offset + fromIntegral address - fromIntegral mid))
    Nothing -> do
      e :: ImmediateExpression <- makeImmediateExpression s offset symbol False
      pure (registerNo, e)

-- | Is this a signed-immediate instruction?
signedImmediateOpcode :: Int -> Bool
signedImmediateOpcode op =
  op
    `elem` map
      token
      [ TOKEN_ADDI_OPCODE,
        TOKEN_ADDIU_OPCODE,
        TOKEN_SLTI_OPCODE,
        TOKEN_SLTIU_OPCODE,
        TOKEN_TEQI_OPCODE,
        TOKEN_TGEI_OPCODE,
        TOKEN_TGEIU_OPCODE,
        TOKEN_TLTI_OPCODE,
        TOKEN_TLTIU_OPCODE,
        TOKEN_TNEI_OPCODE
      ]

-- | An immediate instruction @op rt, rs, expr@ (C: @i_type@).  It may
-- become several instructions when the value does not fit in 16 bits.
iType :: Spim -> Int -> Int -> Int -> ImmediateExpression -> IO ()
iType s op targetRegister sourceRegister expression = do
  ref :: InstructionRef <- newIORef ((setTargetRegister targetRegister (setSourceRegister sourceRegister (emptyInstruction op))) {instructionExpression = Just expression})
  known :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol expression)
  bare :: Bool <- get (optionBare s)
  if known
    then do
      value :: Int32 <- evaluateImmediateExpression s expression
      let v :: Word32
          v = fromIntegral value :: Word32
          signedCheck :: Bool
          signedCheck = signedImmediateOpcode op || (opcodeIsLoadStore op && immediateExpressionBits expression == 0)
          tooBig :: Bool
          tooBig =
            if signedCheck
              then (v .&. 0xffff8000) /= 0 && (v .&. 0xffff8000) /= 0xffff8000
              else (v .&. 0xffff0000) /= 0
      if not bare && tooBig
        then iTypeFullWord s op targetRegister sourceRegister expression True value
        else resolveALabel s (immediateExpressionSymbol expression) ref >> storeInstruction s ref
    else
      if bare || immediateExpressionBits expression /= 0
        then case immediateExpressionSymbol expression of
          Just l -> recordInstructionUsesSymbol s ref l >> storeInstruction s ref
          Nothing -> storeInstruction s ref
        else iTypeFullWord s op targetRegister sourceRegister expression False 0

-- | The immediate does not (or may not) fit in 16 bits: build the value in
-- @$at@ with @lui@ / @ori@ first (C: @i_type_full_word@).
iTypeFullWord :: Spim -> Int -> Int -> Int -> ImmediateExpression -> Bool -> Int32 -> IO ()
iTypeFullWord s op targetRegister sourceRegister expression known value = do
  globalPointerOffset :: Maybe Int32 <- smallGlobalPointerOffset
  if opcodeIsLoadStore op
    then case globalPointerOffset of
      Just offset -> iType s op targetRegister 28 (constantImmediateExpression offset)
      Nothing
        | known -> do
            let high0 :: Int32
                high0 = (value `shiftR` 16) .&. 0xffff
                low :: Int32
                low = value .&. 0xffff
            if not (high0 == 0 && low .&. 0x8000 == 0) && not (high0 == 0xffff && low .&. 0x8000 /= 0)
              then do
                let high :: Int32
                    high = if low .&. 0x8000 /= 0 then high0 + 1 else high0
                iType s (token TOKEN_LUI_OPCODE) 1 0 (constantImmediateExpression high)
                when (sourceRegister /= 0) $ rType s (token TOKEN_ADDU_OPCODE) 1 1 sourceRegister
                iType s op targetRegister 1 (lowerBits (constantImmediateExpression low))
              else iType s op targetRegister sourceRegister (constantImmediateExpression low)
        | otherwise -> do
            iType s (token TOKEN_LUI_OPCODE) 1 0 (upperBits expression)
            when (sourceRegister /= 0) $ rType s (token TOKEN_ADDU_OPCODE) 1 1 sourceRegister
            iType s op targetRegister 1 (lowerBits expression)
    else
      if opcodeIsBranch op
        then iType s op targetRegister sourceRegister (lowerBits expression)
        else case globalPointerOffset of
          Just offset ->
            iType s (if isToken TOKEN_LUI_OPCODE op then token TOKEN_ADDIU_OPCODE else op) targetRegister 28 (constantImmediateExpression offset)
          Nothing
            | op `elem` map token [TOKEN_ORI_OPCODE, TOKEN_ADDI_OPCODE, TOKEN_ADDIU_OPCODE, TOKEN_LUI_OPCODE] && sourceRegister == 0 ->
                produceImmediate s expression targetRegister known value
            | otherwise -> do
                produceImmediate s expression 1 known value
                op' :: Int <- immediateOpcodeToOpcode op
                rType s op' targetRegister sourceRegister 1
  where
    -- A label in the $gp area, used without a base register, can be
    -- reached with a 16-bit offset from $gp.
    smallGlobalPointerOffset :: IO (Maybe Int32)
    smallGlobalPointerOffset = case immediateExpressionSymbol expression of
      Just l | sourceRegister == 0 -> do
        gp :: Bool <- get (labelGlobalPointer l)
        address :: Word32 <- get (labelAddress l)
        let offset :: Int32
            offset = fromIntegral address + immediateExpressionOffset expression :: Int32
        pure (if gp && offset >= -0x8000 && offset <= 0x7fff then Just offset else Nothing)
      _ -> pure Nothing

-- | Load a 32-bit value into a register (C: @produce_immediate@).
produceImmediate :: Spim -> ImmediateExpression -> Int -> Bool -> Int32 -> IO ()
produceImmediate s expression targetRegister known value
  | known && value .&. 0xffff == 0 = iType s (token TOKEN_LUI_OPCODE) targetRegister 0 (upperBits expression)
  | known && (fromIntegral value .&. (0xffff0000 :: Word32)) == 0 = iType s (token TOKEN_ORI_OPCODE) targetRegister 0 (lowerBits expression)
  | otherwise = do
      iType s (token TOKEN_LUI_OPCODE) 1 0 (upperBits expression)
      iType s (token TOKEN_ORI_OPCODE) targetRegister 1 (lowerBits expression)

-- | A jump to a label (C: @j_type@).
jType :: Spim -> Int -> ImmediateExpression -> IO ()
jType s op target0 = do
  let target :: ImmediateExpression
      target = target0 {immediateExpressionOffset = 0, immediateExpressionProgramCounterRelative = False}
  ref :: InstructionRef <- newIORef ((emptyInstruction op) {instructionExpression = Just target})
  known :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol target)
  case immediateExpressionSymbol target of
    Just l | not known -> recordInstructionUsesSymbol s ref l
    _ -> resolveALabel s (immediateExpressionSymbol target) ref
  storeInstruction s ref

makeR :: Int -> Int -> Int -> Int -> Instruction
makeR op destinationRegister sourceRegister targetRegister = setDestinationRegister destinationRegister (setTargetRegister targetRegister (setSourceRegister sourceRegister (emptyInstruction op)))

-- | C: @r_type@.
rType :: Spim -> Int -> Int -> Int -> Int -> IO ()
rType s op destinationRegister sourceRegister targetRegister = newIORef (makeR op destinationRegister sourceRegister targetRegister) >>= storeInstruction s

-- | An FP instruction; FD goes in the shamt byte (C: @r_co_type@).
rCoprocessorType :: Spim -> Int -> Int -> Int -> Int -> IO ()
rCoprocessorType s op floatingPointDestination floatingPointSource floatingPointTarget = newIORef (setShiftAmount floatingPointDestination (makeR op floatingPointSource 0 floatingPointTarget)) >>= storeInstruction s

-- | A shift by a constant (C: @r_sh_type@).
rShiftAmountType :: Spim -> Int -> Int -> Int -> Int -> IO ()
rShiftAmountType s op destinationRegister targetRegister shiftAmount = newIORef (setShiftAmount (shiftAmount .&. 0x1f) (makeR op destinationRegister 0 targetRegister)) >>= storeInstruction s

-- | An FP compare: the condition bits go in the rs byte, the condition
-- code (times four) in shamt (C: @r_cond_type@).
rConditionType :: Spim -> Int -> Int -> Int -> Int -> IO ()
rConditionType s op floatingPointSource floatingPointTarget conditionCode = do
  let un :: Int
      un = 1
      eq :: Int
      eq = 2
      lt :: Int
      lt = 4
      inv :: Int
      inv = 8
      both :: Int -> Opcode -> Bool
      both a b = a == token b
      -- Nothing for an opcode that is not one of the 32 compares (16 conditions, .s and .d).
      conditionBits :: Maybe Int
      conditionBits
        | any (both op) [TOKEN_C_EQ_D_OPCODE, TOKEN_C_EQ_S_OPCODE] = Just eq
        | any (both op) [TOKEN_C_LE_D_OPCODE, TOKEN_C_LE_S_OPCODE] = Just (inv .|. lt .|. eq)
        | any (both op) [TOKEN_C_LT_D_OPCODE, TOKEN_C_LT_S_OPCODE] = Just (inv .|. lt)
        | any (both op) [TOKEN_C_NGE_D_OPCODE, TOKEN_C_NGE_S_OPCODE] = Just (inv .|. lt .|. un)
        | any (both op) [TOKEN_C_NGLE_D_OPCODE, TOKEN_C_NGLE_S_OPCODE] = Just (inv .|. un)
        | any (both op) [TOKEN_C_NGL_D_OPCODE, TOKEN_C_NGL_S_OPCODE] = Just (inv .|. eq .|. un)
        | any (both op) [TOKEN_C_NGT_D_OPCODE, TOKEN_C_NGT_S_OPCODE] = Just (inv .|. lt .|. eq .|. un)
        | any (both op) [TOKEN_C_OLT_D_OPCODE, TOKEN_C_OLT_S_OPCODE] = Just lt
        | any (both op) [TOKEN_C_OLE_D_OPCODE, TOKEN_C_OLE_S_OPCODE] = Just (lt .|. eq)
        | any (both op) [TOKEN_C_SEQ_D_OPCODE, TOKEN_C_SEQ_S_OPCODE] = Just (inv .|. eq)
        | any (both op) [TOKEN_C_SF_D_OPCODE, TOKEN_C_SF_S_OPCODE] = Just inv
        | any (both op) [TOKEN_C_F_D_OPCODE, TOKEN_C_F_S_OPCODE] = Just 0
        | any (both op) [TOKEN_C_UEQ_D_OPCODE, TOKEN_C_UEQ_S_OPCODE] = Just (eq .|. un)
        | any (both op) [TOKEN_C_ULT_D_OPCODE, TOKEN_C_ULT_S_OPCODE] = Just (lt .|. un)
        | any (both op) [TOKEN_C_ULE_D_OPCODE, TOKEN_C_ULE_S_OPCODE] = Just (lt .|. eq .|. un)
        | any (both op) [TOKEN_C_UN_D_OPCODE, TOKEN_C_UN_S_OPCODE] = Just un
        | otherwise = Nothing
  -- Never reached: only FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION rows reach here, and the guards above are all 32.
  condition :: Int <-
    maybe
      (fatalError ("internal error: r_cond_type: not an FP compare: " ++ show op ++ "\n"))
      pure
      conditionBits
  newIORef (setSourceRegister condition (setShiftAmount (conditionCode `shiftL` 2) (makeR op floatingPointSource 0 floatingPointTarget))) >>= storeInstruction s

-- | The register form of an immediate instruction (C: @imm_op_to_op@).
immediateOpcodeToOpcode :: Int -> IO Int
immediateOpcodeToOpcode op = maybe (fatalError "Can't convert immediate op to op\n") (pure . token) (lookup op table)
  where
    table :: [(Int, Opcode)]
    table =
      [ (token TOKEN_ADDI_OPCODE, TOKEN_ADD_OPCODE),
        (token TOKEN_ADDIU_OPCODE, TOKEN_ADDU_OPCODE),
        (token TOKEN_ANDI_OPCODE, TOKEN_AND_OPCODE),
        (token TOKEN_ORI_OPCODE, TOKEN_OR_OPCODE),
        (token TOKEN_XORI_OPCODE, TOKEN_XOR_OPCODE),
        (token TOKEN_SLTI_OPCODE, TOKEN_SLT_OPCODE),
        (token TOKEN_SLTIU_OPCODE, TOKEN_SLTU_OPCODE),
        (token TOKEN_J_OPCODE, TOKEN_JR_OPCODE),
        (token TOKEN_LUI_OPCODE, TOKEN_ADDU_OPCODE),
        (token TOKEN_SLL_OPCODE, TOKEN_SLLV_OPCODE),
        (token TOKEN_SRA_OPCODE, TOKEN_SRAV_OPCODE),
        (token TOKEN_SRL_OPCODE, TOKEN_SRLV_OPCODE)
      ]

-- | The immediate form of a register instruction (C: @op_to_imm_op@).
opcodeToImmediateOpcode :: Opcode -> IO Opcode
opcodeToImmediateOpcode op = maybe (fatalError "Can't convert op to immediate op\n") pure (lookup op table)
  where
    table :: [(Opcode, Opcode)]
    table =
      [ (TOKEN_ADD_OPCODE, TOKEN_ADDI_OPCODE),
        (TOKEN_ADDU_OPCODE, TOKEN_ADDIU_OPCODE),
        (TOKEN_AND_OPCODE, TOKEN_ANDI_OPCODE),
        (TOKEN_OR_OPCODE, TOKEN_ORI_OPCODE),
        (TOKEN_XOR_OPCODE, TOKEN_XORI_OPCODE),
        (TOKEN_SLT_OPCODE, TOKEN_SLTI_OPCODE),
        (TOKEN_SLTU_OPCODE, TOKEN_SLTIU_OPCODE),
        (TOKEN_SLLV_OPCODE, TOKEN_SLL_OPCODE),
        (TOKEN_SRAV_OPCODE, TOKEN_SRA_OPCODE),
        (TOKEN_SRLV_OPCODE, TOKEN_SRL_OPCODE)
      ]
