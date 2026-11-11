-- | Assembling instructions into the text segment (C: the assembling half
-- of src/instruction.c).
--
-- The emitter calls these with an instruction's opcode and operands.  The
-- interesting one is 'iTypeInst': a MIPS immediate is only 16 bits, so an
-- immediate that does not fit (a big constant, or an address) becomes two
-- or three instructions using the assembler's register @$at@ ($1), and an
-- immediate naming a label not yet defined is recorded and patched later.
module Assemble
  ( textBeginsAtPoint
  , kTextBeginsAtPoint
  , alignText
  , setTextPc
  , userKernelTextSegment
  , iTypeInst
  , jTypeInst
  , rTypeInst
  , rCoTypeInst
  , rShTypeInst
  , rCondTypeInst
  , makeImmExpr
  , constImmExpr
  , incrExprOffset
  , makeAddrExpr
  , isZeroImm
  , immOpToOp
  , opToImmOp
  ) where

import AsmCommon
import Control.Monad (unless, when)
import Data.Bits (shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.IORef (IORef, newIORef)
import Data.Word (Word32)
import DataSegment (storeWord)
import Instruction
import Memory (memWriteInst)
import Opcodes
import Output
import State
import SymbolTable
import Text.Printf (printf)
import Types

-- | Start user text at an address (C: @text_begins_at_point@).
textBeginsAtPoint :: Spim -> Word32 -> IO ()
textBeginsAtPoint s addr = set (nextTextPc s) addr >> set (textInKernel s) False

kTextBeginsAtPoint :: Spim -> Word32 -> IO ()
kTextBeginsAtPoint s addr = set (nextKTextPc s) addr >> set (textInKernel s) False

-- | Send the following instructions to the kernel or user text segment.
userKernelTextSegment :: Spim -> Bool -> IO ()
userKernelTextSegment s = set (textInKernel s)

textPcRef :: Spim -> IO (IORef Word32)
textPcRef s = do
  k :: Bool <- get (textInKernel s)
  pure (if k then nextKTextPc s else nextTextPc s)

-- | @.align n@ in the text segment (C: @align_text@).
alignText :: Spim -> Int -> IO ()
alignText s n
  | n == 0 = pure ()
  | otherwise = do
      ref :: IORef Word32 <- textPcRef s
      pc :: Word32 <- get ref
      let pc' :: Word32
          pc' = (pc + (1 `shiftL` n) - 1) .&. (0xffffffff `shiftL` n)
      set ref pc'
      fixCurrentLabelAddress s pc'

setTextPc :: Spim -> Word32 -> IO ()
setTextPc s addr = textPcRef s >>= (`set` addr)

incrementTextPc :: Spim -> IO ()
incrementTextPc s = do
  k :: Bool <- get (textInKernel s)
  if k
    then do
      modify (nextKTextPc s) (+ 4)
      pc :: Word32 <- get (nextKTextPc s)
      top :: Word32 <- get (kTextTop s)
      when (top <= pc) $ runError "Can't expand kernel text segment\n"
    else do
      modify (nextTextPc s) (+ 4)
      pc :: Word32 <- get (nextTextPc s)
      top :: Word32 <- get (textTop s)
      when (top <= pc) $ runError "Can't expand text segment\n"

-- | Put an instruction at the next location (C: @store_instruction@).  In
-- the data segment it is stored as its encoding.
storeInstruction :: Spim -> InstRef -> IO ()
storeInstruction s ref = do
  inData :: Bool <- get (dataDir s)
  inText :: Bool <- get (textDir s)
  -- Never reached: the parser always sets dataDir and textDir as a pair, one true.
  unless (inData || inText) $
    fatalError "internal error: store_instruction: neither the data nor the text segment is active\n"
  if inData
    then get ref >>= instEncodeIO >>= storeWord s
    else when inText $ do
      at :: Word32 <- currentTextPc s
      set (exceptionOccurred s) False
      memWriteInst s at (Just ref)
      exc :: Bool <- get (exceptionOccurred s)
      if exc
        then cError (printf "Invalid address (0x%08x) for instruction\n" at)
        else incrementTextPc s
      override :: Maybe (Maybe String) <- get (emitSource s)
      src :: Maybe String <- maybe (sourceLine s) pure override
      modify ref (\i -> i {inSource = src})
      i :: Instruction <- get ref
      when (inEncoding i == 0) $ do
        enc :: Int32 <- instEncodeIO i
        set ref i {inEncoding = enc}
      enc :: Int32 <- inEncoding <$> get ref
      fire s (EvTextInst at (fromIntegral enc))

-- | An immediate expression (C: @make_imm_expr@).  Naming a label puts it
-- in the symbol table.
makeImmExpr :: Spim -> Int32 -> Maybe String -> Bool -> IO ImmExpr
makeImmExpr s offset sym pcRel = do
  l :: Maybe Label <- traverse (lookupLabel s) sym
  pure (ImmExpr offset l 0 pcRel)

-- | A constant (C: @const_imm_expr@).
constImmExpr :: Int32 -> ImmExpr
constImmExpr v = ImmExpr v Nothing 0 False

upperBits, lowerBits :: ImmExpr -> ImmExpr
upperBits e = e {ieBits = 1}
lowerBits e = e {ieBits = -1}

-- | C: @incr_expr_offset@.
incrExprOffset :: ImmExpr -> Int32 -> ImmExpr
incrExprOffset e v = e {ieOffset = ieOffset e + v}

-- | C: @is_zero_imm@.
isZeroImm :: ImmExpr -> Bool
isZeroImm e = ieOffset e == 0 && null (ieSymbol e)

-- | An address @symbol + offset(register)@ (C: @make_addr_expr@).  A small
-- global in the $gp area becomes an offset from $gp.
makeAddrExpr :: Spim -> Int32 -> Maybe String -> Int -> IO (Int, ImmExpr)
makeAddrExpr s offs sym regNo = do
  gpLabel :: Maybe Label <- case sym of
    Just name | regNo == 0 -> do
      l :: Label <- lookupLabel s name
      gp :: Bool <- get (lblGp l)
      pure (if gp then Just l else Nothing)
    _ -> pure Nothing
  case gpLabel of
    Just l -> do
      addr :: Word32 <- get (lblAddr l)
      mid :: Word32 <- get (gpMidpoint s)
      pure (28, constImmExpr (offs + fromIntegral addr - fromIntegral mid))
    Nothing -> do
      e :: ImmExpr <- makeImmExpr s offs sym False
      pure (regNo, e)

-- | Is this a signed-immediate instruction?
signedImmOp :: Int -> Bool
signedImmOp op =
  op `elem` map tok [ TOK_ADDI_OPCODE, TOK_ADDIU_OPCODE, TOK_SLTI_OPCODE, TOK_SLTIU_OPCODE
                    , TOK_TEQI_OPCODE, TOK_TGEI_OPCODE, TOK_TGEIU_OPCODE, TOK_TLTI_OPCODE
                    , TOK_TLTIU_OPCODE, TOK_TNEI_OPCODE ]

-- | An immediate instruction @op rt, rs, expr@ (C: @i_type_inst@).  It may
-- become several instructions when the value does not fit in 16 bits.
iTypeInst :: Spim -> Int -> Int -> Int -> ImmExpr -> IO ()
iTypeInst s op rt rs expr = do
  ref :: InstRef <- newIORef ((setRt rt (setRs rs (emptyInstruction op))) {inExpr = Just expr})
  known :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol expr)
  bare :: Bool <- get (optBare s)
  if known
    then do
      value :: Int32 <- evalImmExpr s expr
      let v :: Word32
          v = fromIntegral value :: Word32
          signedCheck :: Bool
          signedCheck = signedImmOp op || (opcodeIsLoadStore op && ieBits expr == 0)
          tooBig :: Bool
          tooBig =
            if signedCheck
              then (v .&. 0xffff8000) /= 0 && (v .&. 0xffff8000) /= 0xffff8000
              else (v .&. 0xffff0000) /= 0
      if not bare && tooBig
        then iTypeInstFullWord s op rt rs expr True value
        else resolveALabel s (ieSymbol expr) ref >> storeInstruction s ref
    else
      if bare || ieBits expr /= 0
        then case ieSymbol expr of
          Just l -> recordInstUsesSymbol s ref l >> storeInstruction s ref
          Nothing -> storeInstruction s ref
        else iTypeInstFullWord s op rt rs expr False 0

-- | The immediate does not (or may not) fit in 16 bits: build the value in
-- @$at@ with @lui@ / @ori@ first (C: @i_type_inst_full_word@).
iTypeInstFullWord :: Spim -> Int -> Int -> Int -> ImmExpr -> Bool -> Int32 -> IO ()
iTypeInstFullWord s op rt rs expr known value = do
  gpOffset :: Maybe Int32 <- smallGpOffset
  if opcodeIsLoadStore op
    then case gpOffset of
      Just offset -> iTypeInst s op rt 28 (constImmExpr offset)
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
                iTypeInst s (tok TOK_LUI_OPCODE) 1 0 (constImmExpr high)
                when (rs /= 0) $ rTypeInst s (tok TOK_ADDU_OPCODE) 1 1 rs
                iTypeInst s op rt 1 (lowerBits (constImmExpr low))
              else iTypeInst s op rt rs (constImmExpr low)
        | otherwise -> do
            iTypeInst s (tok TOK_LUI_OPCODE) 1 0 (upperBits expr)
            when (rs /= 0) $ rTypeInst s (tok TOK_ADDU_OPCODE) 1 1 rs
            iTypeInst s op rt 1 (lowerBits expr)
    else
      if opcodeIsBranch op
        then iTypeInst s op rt rs (lowerBits expr)
        else case gpOffset of
          Just offset ->
            iTypeInst s (if isTok TOK_LUI_OPCODE op then tok TOK_ADDIU_OPCODE else op) rt 28 (constImmExpr offset)
          Nothing
            | op `elem` map tok [TOK_ORI_OPCODE, TOK_ADDI_OPCODE, TOK_ADDIU_OPCODE, TOK_LUI_OPCODE] && rs == 0 ->
                produceImmediate s expr rt known value
            | otherwise -> do
                produceImmediate s expr 1 known value
                op' :: Int <- immOpToOp op
                rTypeInst s op' rt rs 1
  where
    -- A label in the $gp area, used without a base register, can be
    -- reached with a 16-bit offset from $gp.
    smallGpOffset :: IO (Maybe Int32)
    smallGpOffset = case ieSymbol expr of
      Just l | rs == 0 -> do
        gp :: Bool <- get (lblGp l)
        addr :: Word32 <- get (lblAddr l)
        let offset :: Int32
            offset = fromIntegral addr + ieOffset expr :: Int32
        pure (if gp && offset >= -0x8000 && offset <= 0x7fff then Just offset else Nothing)
      _ -> pure Nothing

-- | Load a 32-bit value into a register (C: @produce_immediate@).
produceImmediate :: Spim -> ImmExpr -> Int -> Bool -> Int32 -> IO ()
produceImmediate s expr rt known value
  | known && value .&. 0xffff == 0 = iTypeInst s (tok TOK_LUI_OPCODE) rt 0 (upperBits expr)
  | known && (fromIntegral value .&. (0xffff0000 :: Word32)) == 0 = iTypeInst s (tok TOK_ORI_OPCODE) rt 0 (lowerBits expr)
  | otherwise = do
      iTypeInst s (tok TOK_LUI_OPCODE) 1 0 (upperBits expr)
      iTypeInst s (tok TOK_ORI_OPCODE) rt 1 (lowerBits expr)

-- | A jump to a label (C: @j_type_inst@).
jTypeInst :: Spim -> Int -> ImmExpr -> IO ()
jTypeInst s op target0 = do
  let target :: ImmExpr
      target = target0 {ieOffset = 0, iePcRelative = False}
  ref :: InstRef <- newIORef ((emptyInstruction op) {inExpr = Just target})
  known :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol target)
  case ieSymbol target of
    Just l | not known -> recordInstUsesSymbol s ref l
    _ -> resolveALabel s (ieSymbol target) ref
  storeInstruction s ref

makeR :: Int -> Int -> Int -> Int -> Instruction
makeR op rd rs rt = setRd rd (setRt rt (setRs rs (emptyInstruction op)))

-- | C: @r_type_inst@.
rTypeInst :: Spim -> Int -> Int -> Int -> Int -> IO ()
rTypeInst s op rd rs rt = newIORef (makeR op rd rs rt) >>= storeInstruction s

-- | An FP instruction; FD goes in the shamt byte (C: @r_co_type_inst@).
rCoTypeInst :: Spim -> Int -> Int -> Int -> Int -> IO ()
rCoTypeInst s op fd fs ft = newIORef (setShamt fd (makeR op fs 0 ft)) >>= storeInstruction s

-- | A shift by a constant (C: @r_sh_type_inst@).
rShTypeInst :: Spim -> Int -> Int -> Int -> Int -> IO ()
rShTypeInst s op rd rt shamt = newIORef (setShamt (shamt .&. 0x1f) (makeR op rd 0 rt)) >>= storeInstruction s

-- | An FP compare: the condition bits go in the rs byte, the condition
-- code (times four) in shamt (C: @r_cond_type_inst@).
rCondTypeInst :: Spim -> Int -> Int -> Int -> Int -> IO ()
rCondTypeInst s op fs ft cc = do
  let un :: Int
      un = 1
      eq :: Int
      eq = 2
      lt :: Int
      lt = 4
      inv :: Int
      inv = 8
      both :: Int -> Opcode -> Bool
      both a b = a == tok b
      -- Nothing for an opcode that is not one of the 32 compares (16 conditions, .s and .d).
      condBits :: Maybe Int
      condBits
        | any (both op) [TOK_C_EQ_D_OPCODE, TOK_C_EQ_S_OPCODE] = Just eq
        | any (both op) [TOK_C_LE_D_OPCODE, TOK_C_LE_S_OPCODE] = Just (inv .|. lt .|. eq)
        | any (both op) [TOK_C_LT_D_OPCODE, TOK_C_LT_S_OPCODE] = Just (inv .|. lt)
        | any (both op) [TOK_C_NGE_D_OPCODE, TOK_C_NGE_S_OPCODE] = Just (inv .|. lt .|. un)
        | any (both op) [TOK_C_NGLE_D_OPCODE, TOK_C_NGLE_S_OPCODE] = Just (inv .|. un)
        | any (both op) [TOK_C_NGL_D_OPCODE, TOK_C_NGL_S_OPCODE] = Just (inv .|. eq .|. un)
        | any (both op) [TOK_C_NGT_D_OPCODE, TOK_C_NGT_S_OPCODE] = Just (inv .|. lt .|. eq .|. un)
        | any (both op) [TOK_C_OLT_D_OPCODE, TOK_C_OLT_S_OPCODE] = Just lt
        | any (both op) [TOK_C_OLE_D_OPCODE, TOK_C_OLE_S_OPCODE] = Just (lt .|. eq)
        | any (both op) [TOK_C_SEQ_D_OPCODE, TOK_C_SEQ_S_OPCODE] = Just (inv .|. eq)
        | any (both op) [TOK_C_SF_D_OPCODE, TOK_C_SF_S_OPCODE] = Just inv
        | any (both op) [TOK_C_F_D_OPCODE, TOK_C_F_S_OPCODE] = Just 0
        | any (both op) [TOK_C_UEQ_D_OPCODE, TOK_C_UEQ_S_OPCODE] = Just (eq .|. un)
        | any (both op) [TOK_C_ULT_D_OPCODE, TOK_C_ULT_S_OPCODE] = Just (lt .|. un)
        | any (both op) [TOK_C_ULE_D_OPCODE, TOK_C_ULE_S_OPCODE] = Just (lt .|. eq .|. un)
        | any (both op) [TOK_C_UN_D_OPCODE, TOK_C_UN_S_OPCODE] = Just un
        | otherwise = Nothing
  -- Never reached: only FP_CMP_FS_FT_INST rows reach here, and the guards above are all 32.
  cond :: Int <-
    maybe
      (fatalError ("internal error: r_cond_type_inst: not an FP compare: " ++ show op ++ "\n"))
      pure
      condBits
  newIORef (setRs cond (setShamt (cc `shiftL` 2) (makeR op fs 0 ft))) >>= storeInstruction s

-- | The register form of an immediate instruction (C: @imm_op_to_op@).
immOpToOp :: Int -> IO Int
immOpToOp op = maybe (fatalError "Can't convert immediate op to op\n") (pure . tok) (lookup op table)
  where
    table :: [(Int, Opcode)]
    table =
      [ (tok TOK_ADDI_OPCODE, TOK_ADD_OPCODE), (tok TOK_ADDIU_OPCODE, TOK_ADDU_OPCODE)
      , (tok TOK_ANDI_OPCODE, TOK_AND_OPCODE), (tok TOK_ORI_OPCODE, TOK_OR_OPCODE)
      , (tok TOK_XORI_OPCODE, TOK_XOR_OPCODE), (tok TOK_SLTI_OPCODE, TOK_SLT_OPCODE)
      , (tok TOK_SLTIU_OPCODE, TOK_SLTU_OPCODE), (tok TOK_J_OPCODE, TOK_JR_OPCODE)
      , (tok TOK_LUI_OPCODE, TOK_ADDU_OPCODE), (tok TOK_SLL_OPCODE, TOK_SLLV_OPCODE)
      , (tok TOK_SRA_OPCODE, TOK_SRAV_OPCODE), (tok TOK_SRL_OPCODE, TOK_SRLV_OPCODE)
      ]

-- | The immediate form of a register instruction (C: @op_to_imm_op@).
opToImmOp :: Opcode -> IO Opcode
opToImmOp op = maybe (fatalError "Can't convert op to immediate op\n") pure (lookup op table)
  where
    table :: [(Opcode, Opcode)]
    table =
      [ (TOK_ADD_OPCODE, TOK_ADDI_OPCODE), (TOK_ADDU_OPCODE, TOK_ADDIU_OPCODE)
      , (TOK_AND_OPCODE, TOK_ANDI_OPCODE), (TOK_OR_OPCODE, TOK_ORI_OPCODE)
      , (TOK_XOR_OPCODE, TOK_XORI_OPCODE), (TOK_SLT_OPCODE, TOK_SLTI_OPCODE)
      , (TOK_SLTU_OPCODE, TOK_SLTIU_OPCODE), (TOK_SLLV_OPCODE, TOK_SLL_OPCODE)
      , (TOK_SRAV_OPCODE, TOK_SRA_OPCODE), (TOK_SRLV_OPCODE, TOK_SRL_OPCODE)
      ]
