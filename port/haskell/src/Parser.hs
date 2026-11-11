-- | The assembler's parser and emitter (C: src/parser.c and the expansion
-- helpers of src/pseudo-op.c).
--
-- Parsing a file happens in two passes over different things:
--
-- 1. /parse/: a recursive-descent parser reads tokens and builds a list of
--    'Node's, one per statement; a pseudo-instruction becomes a 'Pseudo'
--    node holding the real instructions it expands to.
-- 2. /emit/: 'emitAst' walks the nodes in order and calls the assembling
--    actions ('iTypeInst', 'storeWord', 'recordLabel', ...) that put each
--    statement into memory.
--
-- @-print-ast@, @-print-ast-json@ and @-show-expansion@ print the nodes
-- between the passes and skip the second.
--
-- The parser is a transcription of the C one, statement for statement,
-- including its error messages and its recovery after an error, because
-- the error text and the line it names are part of the output being
-- matched.
module Parser
  ( parserInit
  , parseFile
  ) where

import Assemble
import AsmCommon
import Ast
import Control.Monad (forM_, unless, when)
import Data.Bits (shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.Word (Word32, Word64)
import DataSegment
import GHC.Float (castDoubleToWord64, castFloatToWord32, double2Float)
import Instruction (opcodeIsNullifiedBranch, opcodeIsTrueBranch, tok)
import OpcodeTables (findOpType, opTokenName)
import OpcodeTypes (OpType (..))
import Opcodes
import Output
import Scanner
import State
import SymbolTable
import Text.Printf (printf)
import Types

-- | Start a file (C: @parser_init@).
parserInit :: Spim -> String -> String -> IO ()
parserInit s input fileName = do
  scannerInit s input
  set (inputFileName s) (Just fileName)
  set (parseErrorsSeen s) 0
  set (parseErrorOccurred s) False
  set (dataDir s) False
  set (textDir s) True
  set (astScope s) []

-- | A parse error, as the parser reports it (C: @parse_error_at@).
parseErrorAt :: Spim -> String -> IO ()
parseErrorAt s msg = do
  set (parseErrorOccurred s) True
  modify (parseErrorsSeen s) (+ 1)
  n :: Int <- lineNo s
  file :: Maybe String <- get (inputFileName s)
  cError (printf "spim: (parser) %s on line %d of file %s\n" msg n (maybe "(stdin)" id file))

peek, peek2, advance :: Spim -> IO Token
peek = scannerPeek
peek2 = scannerPeek2
advance = scannerAdvance

-- | The value of the last integer-like token taken.
valInt :: Spim -> IO Int32
valInt s = do
  v :: TokValue <- get (scanValue s)
  pure $ case v of
    VInt i -> i
    _ -> 0

-- | The text of the last identifier or string token taken.
valStr :: Spim -> IO String
valStr s = do
  v :: TokValue <- get (scanValue s)
  pure $ case v of
    VStr t -> t
    _ -> ""

valFp :: Spim -> IO Double
valFp s = do
  v :: TokValue <- get (scanValue s)
  pure $ case v of
    VFp d -> d
    _ -> 0

int :: Int32 -> Int
int = fromIntegral

-- | Skip to (but not past) the end of the line (C: @sync_to_nl@).
syncToNl :: Spim -> IO ()
syncToNl s = do
  t :: Token <- peek s
  unless (t == TokNL || t == TokEOF) $ advance s >> syncToNl s

-- | Take a token that must be there, or report an error and leave it.
expect :: Spim -> Token -> String -> IO ()
expect s t what = do
  p :: Token <- peek s
  if p == t then () <$ advance s else parseErrorAt s what

-- Building the tree.

-- | Add a statement to the list being built, stamped with the current line.
appendNode :: Spim -> NodeKind -> IO ()
appendNode s kind = makeNode s kind >>= appendBuilt s

makeNode :: Spim -> NodeKind -> IO Node
makeNode s kind = do
  n :: Int <- lineNo s
  src :: Maybe String <- sourceLine s
  pure (Node n src kind)

appendBuilt :: Spim -> Node -> IO ()
appendBuilt s node = modify (astScope s) (node :)

emitR, emitRShift, emitFpR, emitFpCompare :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
emitR s op rd rs rt = appendNode s (InstR (tok op) rd rs rt)
emitRShift s op rd rt sh = appendNode s (InstRShift (tok op) rd rt sh)
emitFpR s op fd fs ft = appendNode s (InstFpR (tok op) fd fs ft)
emitFpCompare s op fs ft cc = appendNode s (InstFpCompare (tok op) fs ft cc)

emitI :: Spim -> Opcode -> Int -> Int -> ImmExpr -> IO ()
emitI s op rt rs imm = appendNode s (InstI (tok op) rt rs imm)

emitJ :: Spim -> Opcode -> ImmExpr -> IO ()
emitJ s op target = appendNode s (InstJ (tok op) target)

-- Operands.

parseRegister :: Spim -> IO Int
parseRegister s = do
  t :: Token <- peek s
  if t == TokReg
    then advance s >> int <$> valInt s
    else parseErrorAt s "Expected register" >> pure 0

parseFpRegister :: Spim -> IO Int
parseFpRegister s = do
  t :: Token <- peek s
  if t == TokFpReg
    then advance s >> int <$> valInt s
    else parseErrorAt s "Expected FP register" >> pure 0

-- | An integer, or @int + int@, or @int -int@ (C: @parse_abs_addr@).
parseAbsAddr :: Spim -> IO Int32
parseAbsAddr s = do
  t :: Token <- peek s
  if t /= TokInt
    then parseErrorAt s "Expected integer" >> pure 0
    else do
      _ <- advance s
      v :: Int32 <- valInt s
      t2 :: Token <- peek s
      case t2 of
        TokChar '+' -> do
          _ <- advance s
          t3 :: Token <- peek s
          if t3 /= TokInt
            then parseErrorAt s "Expected integer after '+'" >> pure v
            else advance s >> (v +) <$> valInt s
        TokInt -> do
          _ <- advance s
          w :: Int32 <- valInt s
          when (w >= 0) $ parseErrorAt s "Syntax error"
          pure (v - negate w)
        _ -> pure v

-- | FACTOR: a number, a parenthesized expression, or a label's address
-- (C: @parse_factor@).  A label not yet defined counts as 0, and the first
-- such label is remembered so the datum can be patched later.
parseFactor :: Spim -> IO Int32
parseFactor s = do
  t :: Token <- peek s
  case t of
    TokInt -> advance s >> valInt s
    TokChar '(' -> do
      _ <- advance s
      v :: Int32 <- parseExpr s
      expect s (TokChar ')') "Expected ')'"
      pure v
    TokId -> do
      _ <- advance s
      name :: String <- valStr s
      l :: Label <- lookupLabel s name
      addr :: Word32 <- get (lblAddr l)
      if addr == 0
        then do
          pending :: Maybe Label <- get (exprUnresolvedLabel s)
          when (null pending) $ set (exprUnresolvedLabel s) (Just l)
          pure 0
        else pure (fromIntegral addr)
    _ -> parseErrorAt s "Expected expression" >> pure 0

-- | TERM: factors joined by @*@ and @/@ (division truncates, as in C;
-- dividing by zero gives 0).
parseTerm :: Spim -> IO Int32
parseTerm s = parseFactor s >>= loop
  where
    loop :: Int32 -> IO Int32
    loop v = do
      t :: Token <- peek s
      case t of
        TokChar '*' -> advance s >> parseFactor s >>= \r -> loop (v * r)
        TokChar '/' -> advance s >> parseFactor s >>= \r -> loop (cDiv v r)
        _ -> pure v
    cDiv :: Integral a => a -> a -> a
    cDiv v r
      | r == 0 = 0
      | r == -1 = negate v
      | otherwise = v `quot` r

-- | EXPR: terms joined by @+@ and @-@.
parseExpr :: Spim -> IO Int32
parseExpr s = parseTerm s >>= loop
  where
    loop :: Int32 -> IO Int32
    loop v = do
      t :: Token <- peek s
      case t of
        TokChar '+' -> advance s >> parseTerm s >>= \r -> loop (v + r)
        TokChar '-' -> advance s >> parseTerm s >>= \r -> loop (v - r)
        _ -> pure v

-- | An expression whose first word is read as an identifier
-- (C: @parse_expression@).
parseExpression :: Spim -> IO Int32
parseExpression s = do
  scannerForceIdentifier s
  set (exprUnresolvedLabel s) Nothing
  parseExpr s

-- | IMM32 (C: @parse_imm32@): @label@, @label+n@, @label-n@, a number, or
-- @(n) >> k@.
parseImm32 :: Spim -> IO ImmExpr
parseImm32 s = do
  scannerForceIdentifier s
  t :: Token <- peek s
  case t of
    TokId -> do
      _ <- advance s
      sym :: String <- valStr s
      t2 :: Token <- peek s
      case t2 of
        TokChar '+' -> advance s >> parseAbsAddr s >>= \off -> makeImmExpr s off (Just sym) False
        TokChar '-' -> advance s >> parseAbsAddr s >>= \off -> makeImmExpr s (negate off) (Just sym) False
        _ -> makeImmExpr s 0 (Just sym) False
    TokChar '(' -> do
      _ <- advance s
      v :: Int32 <- parseAbsAddr s
      expect s (TokChar ')') "Expected ')'"
      expect s (TokChar '>') "Expected '>'"
      expect s (TokChar '>') "Expected '>'"
      t2 :: Token <- peek s
      if t2 /= TokInt
        then parseErrorAt s "Expected integer after '>>'" >> pure (constImmExpr 0)
        else do
          _ <- advance s
          sh :: Int32 <- valInt s
          pure (constImmExpr (v `shiftR` (int sh .&. 31)))
    _ -> constImmExpr <$> parseAbsAddr s

-- | Warn when a known immediate is outside a range (C: @check_imm_range@).
checkImmRange :: Spim -> ImmExpr -> Int32 -> Int32 -> IO ()
checkImmRange s e lo hi = do
  known :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol e)
  when known $ do
    v :: Int32 <- evalImmExpr s e
    when (v < lo || hi < v) $ rangeError s v lo hi

checkUimmRange :: Spim -> ImmExpr -> Word32 -> Word32 -> IO ()
checkUimmRange s e lo hi = do
  known :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol e)
  when known $ do
    v :: Word32 <- fromIntegral <$> evalImmExpr s e
    when (v < lo || hi < v) $ rangeError s (fromIntegral v) (fromIntegral lo) (fromIntegral hi)

rangeError :: Spim -> Int32 -> Int32 -> Int32 -> IO ()
rangeError s v lo hi = do
  n :: Int <- lineNo s
  cError (printf "spim: (parser) immediate value (%d) out of range (%d .. %d) on line %d\n" v lo hi n)

parseImm16, parseUimm16 :: Spim -> IO ImmExpr
parseImm16 s = do
  e :: ImmExpr <- parseImm32 s
  checkImmRange s e (-0x8000) 0x7fff
  pure e
parseUimm16 s = do
  e :: ImmExpr <- parseImm32 s
  checkUimmRange s e 0 0xffff
  pure e

-- | A branch target (C: @parse_label@): pc-relative, so its offset is
-- minus the current text address until the branch is assembled.
parseLabel :: Spim -> IO ImmExpr
parseLabel s = do
  t :: Token <- peek s
  if t /= TokId
    then parseErrorAt s "Expected label" >> pure (ImmExpr 0 Nothing 0 True)
    else do
      _ <- advance s
      sym :: String <- valStr s
      pc :: Word32 <- currentTextPc s
      makeImmExpr s (negate (fromIntegral pc)) (Just sym) True

-- | ADDRESS (C: @parse_address@): @(reg)@, @n@, @n(reg)@, @n+label@,
-- @label@, @label+n@, @label-n@, each with an optional @(reg)@.
parseAddress :: Spim -> IO (Int, ImmExpr)
parseAddress s = do
  scannerForceIdentifier s
  t :: Token <- peek s
  case t of
    TokChar '(' -> do
      _ <- advance s
      r :: Int <- parseRegister s
      expect s (TokChar ')') "Expected ')' after register"
      makeAddrExpr s 0 Nothing r
    TokInt -> do
      imm :: Int32 <- parseAbsAddr s
      t2 :: Token <- peek s
      t3 :: Token <- if t2 == TokChar '+' then peek2 s else pure TokEOF
      if t2 == TokChar '+' && t3 == TokId
        then do
          _ <- advance s
          scannerForceIdentifier s
          _ <- advance s
          sym :: String <- valStr s
          makeAddrExpr s imm (Just sym) 0
        else
          if t2 == TokChar '('
            then do
              _ <- advance s
              r :: Int <- parseRegister s
              expect s (TokChar ')') "Expected ')'"
              makeAddrExpr s imm Nothing r
            else makeAddrExpr s imm Nothing 0
    TokId -> do
      _ <- advance s
      sym :: String <- valStr s
      t2 :: Token <- peek s
      off :: Int32 <- case t2 of
        TokChar '+' -> advance s >> parseAbsAddr s
        TokChar '-' -> advance s >> negate <$> parseAbsAddr s
        _ -> pure 0
      t3 :: Token <- peek s
      r :: Int <-
        if t3 == TokChar '('
          then do
            _ <- advance s
            r :: Int <- parseRegister s
            expect s (TokChar ')') "Expected ')'"
            pure r
          else pure 0
      makeAddrExpr s off (Just sym) r
    _ -> parseErrorAt s "Expected address" >> makeAddrExpr s 0 Nothing 0

-- Instructions.

-- | Opcodes with both a register and an immediate form.
hasImmForm :: Opcode -> Bool
hasImmForm op = op `elem` [TOK_ADD_OPCODE, TOK_ADDU_OPCODE, TOK_AND_OPCODE, TOK_OR_OPCODE, TOK_XOR_OPCODE, TOK_SLT_OPCODE, TOK_SLTU_OPCODE]

-- | @op rd, rs, rt@ and its immediate forms (C: @parse_r3@).
parseR3 :: Spim -> Opcode -> IO ()
parseR3 s op = do
  rd :: Int <- parseRegister s
  case () of
    _
      | op == TOK_CLO_OPCODE || op == TOK_CLZ_OPCODE -> do
          rs :: Int <- parseRegister s
          emitR s op rd rs rd
      | op == TOK_MUL_OPCODE -> do
          rs :: Int <- parseRegister s
          t :: Token <- peek s
          if t == TokReg
            then parseRegister s >>= emitR s op rd rs
            else do
              imm :: ImmExpr <- parseImm32 s
              emitI s TOK_ORI_OPCODE 1 0 imm
              emitR s op rd rs 1
      | op == TOK_SUB_OPCODE || op == TOK_SUBU_OPCODE -> do
          let immOp :: Opcode
              immOp = if op == TOK_SUB_OPCODE then TOK_ADDI_OPCODE else TOK_ADDIU_OPCODE
          t :: Token <- peek s
          if t /= TokReg
            then do
              imm :: ImmExpr <- parseImm32 s
              v :: Int32 <- evalImmExpr s imm
              emitI s immOp rd rd (constImmExpr (negate v))
            else do
              rs :: Int <- parseRegister s
              t2 :: Token <- peek s
              if t2 == TokReg
                then parseRegister s >>= emitR s op rd rs
                else do
                  imm :: ImmExpr <- parseImm32 s
                  v :: Int32 <- evalImmExpr s imm
                  emitI s immOp rd rs (constImmExpr (negate v))
      | hasImmForm op -> do
          immOp :: Opcode <- opToImmOp op
          t :: Token <- peek s
          if t /= TokReg
            then parseImm32 s >>= emitI s immOp rd rd
            else do
              rs :: Int <- parseRegister s
              t2 :: Token <- peek s
              if t2 == TokReg
                then parseRegister s >>= emitR s op rd rs
                else parseImm32 s >>= emitI s immOp rd rs
      | otherwise -> do
          rs :: Int <- parseRegister s
          rt :: Int <- parseRegister s
          emitR s op rd rs rt

-- | @op rd, rt, shamt@ (C: @parse_r2sh@); @ssnop@ is @sll $0, $0, 1@.
parseR2sh :: Spim -> Opcode -> IO ()
parseR2sh s op
  | op == TOK_SSNOP_OPCODE = emitRShift s TOK_SLL_OPCODE 0 0 1
  | otherwise = do
      rd :: Int <- parseRegister s
      rt :: Int <- parseRegister s
      t :: Token <- peek s
      if t /= TokInt
        then parseErrorAt s "Expected integer shift amount"
        else do
          _ <- advance s
          sh :: Int32 <- valInt s
          emitRShift s op rd rt (int sh)

-- | @op rt, rs, imm16@, or @op rt, imm16@ (C: @parse_i2@).
parseI2 :: Spim -> Opcode -> IO ()
parseI2 s op = do
  rt :: Int <- parseRegister s
  t :: Token <- peek s
  rs :: Int <- if t == TokReg then parseRegister s else pure rt
  imm :: ImmExpr <- if op `elem` [TOK_ANDI_OPCODE, TOK_ORI_OPCODE, TOK_XORI_OPCODE] then parseUimm16 s else parseImm16 s
  emitI s op rt rs imm

-- | Loads and stores: @op rt, address@.
parseI2a :: Spim -> Opcode -> IO ()
parseI2a s op = do
  rt :: Int <- parseRegister s
  (r, imm) :: (Int, ImmExpr) <- parseAddress s
  emitI s op rt r imm

-- | @beq rs, rt, label@, or with an immediate in place of rt (C: @parse_b2@).
parseB2 :: Spim -> Opcode -> IO ()
parseB2 s op = do
  src1 :: Int <- parseRegister s
  t :: Token <- peek s
  if t == TokReg
    then do
      src2 :: Int <- parseRegister s
      target :: ImmExpr <- parseLabel s
      emitI s op src2 src1 target
    else do
      imm :: ImmExpr <- parseImm32 s
      target :: ImmExpr <- parseLabel s
      if isZeroImm imm
        then emitI s op src1 0 target
        else do
          emitI s TOK_ORI_OPCODE 1 0 imm
          emitI s op src1 1 target

-- | @j label@ / @jal label@, or the register forms (C: @parse_j@). @jalr.hb@ keeps its own
-- opcode (@jalr@ with the hazard-barrier bit) and takes registers only; @cop2@ shares
-- @J_TYPE_INST@, but spim has no coprocessor 2, so it is an error.
parseJ :: Spim -> Opcode -> IO ()
parseJ s op
  | op == TOK_COP2_OPCODE = parseErrorAt s "cop2 is not supported: spim has no coprocessor 2"
  | otherwise = do
      t :: Token <- peek s
      if t == TokReg
        then do
          r1 :: Int <- parseRegister s
          t2 :: Token <- peek s
          if t2 == TokReg
            then do
              -- DEST SRC1 form
              r2 :: Int <- parseRegister s
              case op of
                TOK_J_OPCODE -> emitR s TOK_JR_OPCODE 0 r2 0
                TOK_JR_OPCODE -> emitR s TOK_JR_OPCODE 0 r2 0
                TOK_JAL_OPCODE -> emitR s TOK_JALR_OPCODE r1 r2 0
                TOK_JALR_OPCODE -> emitR s TOK_JALR_OPCODE r1 r2 0
                TOK_JALR_HB_OPCODE -> emitR s TOK_JALR_HB_OPCODE r1 r2 0
                -- Never reached: only J_TYPE_INST and RD_RS_INST opcodes come here.
                _ -> unexpected
            else case op of
              -- SRC1-only form
              TOK_J_OPCODE -> emitR s TOK_JR_OPCODE 0 r1 0
              TOK_JR_OPCODE -> emitR s TOK_JR_OPCODE 0 r1 0
              TOK_JAL_OPCODE -> emitR s TOK_JALR_OPCODE 31 r1 0
              TOK_JALR_OPCODE -> emitR s TOK_JALR_OPCODE 31 r1 0
              TOK_JALR_HB_OPCODE -> emitR s TOK_JALR_HB_OPCODE 31 r1 0
              -- Never reached: as in the DEST SRC1 form above.
              _ -> unexpected
        else
          if op == TOK_JALR_HB_OPCODE
            then parseErrorAt s "jalr.hb needs a register operand"
            else do
              target :: ImmExpr <- parseLabel s
              case op of
                TOK_J_OPCODE -> emitJ s TOK_J_OPCODE target
                TOK_JR_OPCODE -> emitJ s TOK_J_OPCODE target
                TOK_JAL_OPCODE -> emitJ s TOK_JAL_OPCODE target
                TOK_JALR_OPCODE -> emitJ s TOK_JAL_OPCODE target
                -- Never reached: jalr.hb and cop2 are handled above.
                _ -> unexpected
  where
    unexpected :: IO ()
    unexpected =
      fatalError ("internal error: parse_j: unexpected opcode " ++ show (opcodeToken op) ++ "\n")

-- | @syscall@, or @break n@ (C: @parse_noarg@).
parseNoarg :: Spim -> Opcode -> IO ()
parseNoarg s op = do
  t :: Token <- peek s
  if t == TokInt
    then do
      _ <- advance s
      n :: Int <- int <$> valInt s
      when (op == TOK_BREAK_OPCODE && n == 1) $ parseErrorAt s "Breakpoint 1 is reserved for debugger"
      emitR s op n 0 0
    else emitR s op 0 0 0

-- Pseudo-instruction expansions (C: src/pseudo-op.c).

-- | A branch over n instructions (C: @branch_offset@).
branchOffsetExpr :: Int32 -> ImmExpr
branchOffsetExpr n = constImmExpr (n `shiftL` 2)

nopInst, trapInst :: Spim -> IO ()
nopInst s = emitR s TOK_SLL_OPCODE 0 0 0
trapInst s = emitR s TOK_BREAK_OPCODE 0 0 0

-- | @div@/@rem@ with three operands: check for zero, divide, move the
-- quotient or the remainder (C: @div_inst@).
divInst :: Spim -> Opcode -> Int -> Int -> Int -> Bool -> IO ()
divInst s op rd rs rt constDivisor = do
  when (rd /= 0 && not constDivisor) $ do
    emitI s TOK_BNE_OPCODE 0 rt (branchOffsetExpr 3)
    nopInst s
    trapInst s
  if op == TOK_DIV_OPCODE || op == TOK_REM_PSEUDO_OP
    then emitR s TOK_DIV_OPCODE 0 rs rt
    else emitR s TOK_DIVU_OPCODE 0 rs rt
  when (rd /= 0) $
    if op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE
      then emitR s TOK_MFLO_OPCODE rd 0 0
      else emitR s TOK_MFHI_OPCODE rd 0 0

-- | @mulo@/@mulou@: multiply and trap on overflow (C: @mult_inst@).
multInst :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
multInst s op rd rs rt = do
  emitR s (if op == TOK_MULOU_PSEUDO_OP then TOK_MULTU_OPCODE else TOK_MULT_OPCODE) 0 rs rt
  if op == TOK_MULOU_PSEUDO_OP && rd /= 0
    then do
      emitR s TOK_MFHI_OPCODE 1 0 0
      emitI s TOK_BEQ_OPCODE 0 1 (branchOffsetExpr 3)
      nopInst s
      trapInst s
    else when (op == TOK_MULO_PSEUDO_OP && rd /= 0) $ do
      emitR s TOK_MFHI_OPCODE 1 0 0
      emitR s TOK_MFLO_OPCODE rd 0 0
      emitRShift s TOK_SRA_OPCODE rd rd 31
      emitI s TOK_BEQ_OPCODE rd 1 (branchOffsetExpr 3)
      nopInst s
      trapInst s
  when (rd /= 0) $ emitR s TOK_MFLO_OPCODE rd 0 0

-- | @sle@/@sge@ family: a branch-around sequence (C: @set_le_inst@,
-- @set_ge_inst@).
setCompareInst :: Spim -> Opcode -> Int -> Int -> Int -> Int -> IO ()
setCompareInst s slt rd rs rt finalRs = do
  emitI s TOK_BNE_OPCODE rs rt (branchOffsetExpr 3)
  emitI s TOK_ORI_OPCODE rd 0 (constImmExpr 1)
  emitI s TOK_BEQ_OPCODE 0 0 (branchOffsetExpr 3)
  nopInst s
  emitR s slt rd finalRs (if finalRs == rs then rt else rs)

-- | @seq@/@sne@ (C: @set_eq_inst@).
setEqInst :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
setEqInst s op rd rs rt = do
  let ifEq :: Int32
      ifNeq :: Int32
      (ifEq, ifNeq) = if op == TOK_SEQ_PSEUDO_OP then (1, 0) else (0, 1)
  emitI s TOK_BEQ_OPCODE rs rt (branchOffsetExpr 3)
  emitI s TOK_ORI_OPCODE rd 0 (constImmExpr ifNeq)
  emitI s TOK_BEQ_OPCODE 0 0 (branchOffsetExpr 3)
  nopInst s
  emitI s TOK_ORI_OPCODE rd 0 (constImmExpr ifEq)

-- | A pseudo-instruction: parse it into a 'Pseudo' node that holds its
-- expansion (C: @parse_pseudo@).
parsePseudo :: Spim -> Opcode -> IO ()
parsePseudo s op = do
  start :: Node <- makeNode s (Pseudo "" [])
  outer :: [Node] <- get (astScope s)
  set (astScope s) []
  doParsePseudo s op
  kids :: [Node] <- get (astScope s)
  set (astScope s) outer
  appendBuilt s start {nodeKind = Pseudo (opTokenName (tok op)) (reverse kids)}

-- | The expansion of each pseudo-instruction (C: @do_parse_pseudo@).
doParsePseudo :: Spim -> Opcode -> IO ()
doParsePseudo s op = case op of
  TOK_LI_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    parseImm32 s >>= emitI s TOK_ORI_OPCODE rt 0
  TOK_MOVE_PSEUDO_OP -> twoRegs (\rd rs -> emitR s TOK_ADDU_OPCODE rd 0 rs)
  TOK_NEG_PSEUDO_OP -> twoRegs (\rd rs -> emitR s TOK_SUB_OPCODE rd 0 rs)
  TOK_NEGU_PSEUDO_OP -> twoRegs (\rd rs -> emitR s TOK_SUBU_OPCODE rd 0 rs)
  TOK_NOT_PSEUDO_OP -> twoRegs (\rd rs -> emitR s TOK_NOR_OPCODE rd rs 0)
  _ | op == TOK_ROR_PSEUDO_OP || op == TOK_ROL_PSEUDO_OP -> do
    rd :: Int <- parseRegister s
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokReg
      then do
        rt :: Int <- parseRegister s
        emitR s TOK_SUBU_OPCODE 1 0 rt
        if op == TOK_ROR_PSEUDO_OP
          then emitR s TOK_SLLV_OPCODE 1 1 rs >> emitR s TOK_SRLV_OPCODE rd rt rs
          else emitR s TOK_SRLV_OPCODE 1 1 rs >> emitR s TOK_SLLV_OPCODE rd rt rs
        emitR s TOK_OR_OPCODE rd rd 1
      else do
        imm :: ImmExpr <- parseImm32 s
        dist :: Int <- int <$> evalImmExpr s imm
        checkImmRange s imm 0 31
        if op == TOK_ROR_PSEUDO_OP
          then emitRShift s TOK_SLL_OPCODE 1 rs (negate dist) >> emitRShift s TOK_SRL_OPCODE rd rs dist
          else emitRShift s TOK_SRL_OPCODE 1 rs (negate dist) >> emitRShift s TOK_SLL_OPCODE rd rs dist
        emitR s TOK_OR_OPCODE rd rd 1
  _ | op `elem` [TOK_SLE_PSEUDO_OP, TOK_SLEU_PSEUDO_OP, TOK_SGT_PSEUDO_OP, TOK_SGTU_PSEUDO_OP, TOK_SGE_PSEUDO_OP, TOK_SGEU_PSEUDO_OP, TOK_SEQ_PSEUDO_OP, TOK_SNE_PSEUDO_OP] -> do
    rd :: Int <- parseRegister s
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    rt :: Int <-
      if t == TokReg
        then parseRegister s
        else do
          imm :: ImmExpr <- parseImm32 s
          if not (isZeroImm imm)
            then emitI s TOK_ORI_OPCODE 1 0 imm >> pure 1
            else pure 0
    case op of
      TOK_SLE_PSEUDO_OP -> setCompareInst s TOK_SLT_OPCODE rd rs rt rs
      TOK_SLEU_PSEUDO_OP -> setCompareInst s TOK_SLTU_OPCODE rd rs rt rs
      TOK_SGT_PSEUDO_OP -> emitR s TOK_SLT_OPCODE rd rt rs
      TOK_SGTU_PSEUDO_OP -> emitR s TOK_SLTU_OPCODE rd rt rs
      TOK_SGE_PSEUDO_OP -> setCompareInst s TOK_SLT_OPCODE rd rs rt rt
      TOK_SGEU_PSEUDO_OP -> setCompareInst s TOK_SLTU_OPCODE rd rs rt rt
      _ -> setEqInst s op rd rs rt
  TOK_ABS_PSEUDO_OP -> do
    rd :: Int <- parseRegister s
    rs :: Int <- parseRegister s
    when (rd /= rs) $ emitR s TOK_ADDU_OPCODE rd 0 rs
    emitI s TOK_BGEZ_OPCODE 0 rs (branchOffsetExpr 3)
    nopInst s
    emitR s TOK_SUB_OPCODE rd 0 rs
  TOK_NOP_PSEUDO_OP -> nopInst s
  _ | op == TOK_REM_PSEUDO_OP || op == TOK_REMU_PSEUDO_OP -> do
    rd :: Int <- parseRegister s
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokReg
      then parseRegister s >>= \rt -> divInst s op rd rs rt False
      else do
        imm :: ImmExpr <- parseImm32 s
        if isZeroImm imm
          then parseErrorAt s "Divide by zero"
          else emitI s TOK_ORI_OPCODE 1 0 imm >> divInst s op rd rs 1 True
  _ | op == TOK_MULO_PSEUDO_OP || op == TOK_MULOU_PSEUDO_OP -> do
    rd :: Int <- parseRegister s
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokReg
      then parseRegister s >>= multInst s op rd rs
      else do
        imm :: ImmExpr <- parseImm32 s
        if isZeroImm imm
          then emitI s TOK_ORI_OPCODE rd 0 imm
          else emitI s TOK_ORI_OPCODE 1 0 imm >> multInst s op rd rs 1
  _ | op == TOK_MFC1_D_PSEUDO_OP || op == TOK_MTC1_D_PSEUDO_OP -> do
    let real :: Opcode
        real = if op == TOK_MFC1_D_PSEUDO_OP then TOK_MFC1_OPCODE else TOK_MTC1_OPCODE
    r :: Int <- parseRegister s
    copreg :: Int <- coprocessorRegister s
    emitFpR s real 0 copreg r
    emitFpR s real 0 (copreg + 1) (r + 1)
  TOK_LI_D_PSEUDO_OP -> do
    fd :: Int <- parseFpRegister s
    t :: Token <- peek s
    if t /= TokFp
      then parseErrorAt s "Expected FP literal"
      else do
        _ <- advance s
        bits :: Word64 <- castDoubleToWord64 <$> valFp s
        emitI s TOK_ORI_OPCODE 1 0 (constImmExpr (fromIntegral bits))
        emitFpR s TOK_MTC1_OPCODE 0 fd 1
        emitI s TOK_ORI_OPCODE 1 0 (constImmExpr (fromIntegral (bits `shiftR` 32)))
        emitFpR s TOK_MTC1_OPCODE 0 (fd + 1) 1
  TOK_LI_S_PSEUDO_OP -> do
    fd :: Int <- parseFpRegister s
    t :: Token <- peek s
    if t /= TokFp
      then parseErrorAt s "Expected FP literal"
      else do
        _ <- advance s
        bits :: Word32 <- castFloatToWord32 . double2Float <$> valFp s
        emitI s TOK_ORI_OPCODE 1 0 (constImmExpr (fromIntegral bits))
        emitFpR s TOK_MTC1_OPCODE 0 fd 1
  TOK_L_D_PSEUDO_OP -> fpAddress TOK_LDC1_OPCODE
  TOK_L_S_PSEUDO_OP -> fpAddress TOK_LWC1_OPCODE
  TOK_S_D_PSEUDO_OP -> fpAddress TOK_SDC1_OPCODE
  TOK_S_S_PSEUDO_OP -> fpAddress TOK_SWC1_OPCODE
  _ | op == TOK_LD_PSEUDO_OP || op == TOK_SD_PSEUDO_OP -> do
    let real :: Opcode
        real = if op == TOK_LD_PSEUDO_OP then TOK_LW_OPCODE else TOK_SW_OPCODE
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    emitI s real rt r imm
    emitI s real (rt + 1) r (incrExprOffset imm 4)
  TOK_ULW_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    emitI s TOK_LWL_OPCODE rt r (incrExprOffset imm 3)
    emitI s TOK_LWR_OPCODE rt r imm
  _ | op == TOK_ULH_PSEUDO_OP || op == TOK_ULHU_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    emitI s (if op == TOK_ULH_PSEUDO_OP then TOK_LB_OPCODE else TOK_LBU_OPCODE) rt r (incrExprOffset imm 1)
    emitI s TOK_LBU_OPCODE 1 r imm
    emitRShift s TOK_SLL_OPCODE rt rt 8
    emitR s TOK_OR_OPCODE rt rt 1
  TOK_USW_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    emitI s TOK_SWL_OPCODE rt r (incrExprOffset imm 3)
    emitI s TOK_SWR_OPCODE rt r imm
  TOK_USH_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    emitI s TOK_SB_OPCODE rt r imm
    emitRShift s TOK_SLL_OPCODE 1 rt 24
    emitRShift s TOK_SRL_OPCODE rt rt 8
    emitR s TOK_OR_OPCODE rt rt 1
    emitI s TOK_SB_OPCODE rt r (incrExprOffset imm 1)
    emitRShift s TOK_SRL_OPCODE 1 rt 24
    emitRShift s TOK_SLL_OPCODE rt rt 8
    emitR s TOK_OR_OPCODE rt rt 1
  TOK_LA_PSEUDO_OP -> do
    rt :: Int <- parseRegister s
    (r, imm) :: (Int, ImmExpr) <- parseAddress s
    if r /= 0 then emitI s TOK_ADDI_OPCODE rt r imm else emitI s TOK_ORI_OPCODE rt 0 imm
  _ | op == TOK_BEQZ_PSEUDO_OP || op == TOK_BNEZ_PSEUDO_OP -> do
    rs :: Int <- parseRegister s
    target :: ImmExpr <- parseLabel s
    emitI s (if op == TOK_BEQZ_PSEUDO_OP then TOK_BEQ_OPCODE else TOK_BNE_OPCODE) 0 rs target
  _ | op == TOK_B_PSEUDO_OP || op == TOK_BAL_PSEUDO_OP -> do
    target :: ImmExpr <- parseLabel s
    emitI s (if op == TOK_BAL_PSEUDO_OP then TOK_BGEZAL_OPCODE else TOK_BGEZ_OPCODE) 0 0 target
  _ | op == TOK_BGT_PSEUDO_OP || op == TOK_BGTU_PSEUDO_OP -> do
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokReg
      then do
        rt :: Int <- parseRegister s
        target :: ImmExpr <- parseLabel s
        emitR s (if op == TOK_BGT_PSEUDO_OP then TOK_SLT_OPCODE else TOK_SLTU_OPCODE) 1 rt rs
        emitI s TOK_BNE_OPCODE 0 1 target
      else do
        imm :: ImmExpr <- parseImm32 s
        target :: ImmExpr <- parseLabel s
        if op == TOK_BGT_PSEUDO_OP
          then do
            emitI s TOK_SLTI_OPCODE 1 rs (incrExprOffset imm 1)
            emitI s TOK_BEQ_OPCODE 0 1 target
          else do
            emitI s TOK_ORI_OPCODE 1 0 imm
            emitI s TOK_BEQ_OPCODE rs 1 (branchOffsetExpr 3)
            emitR s TOK_SLTU_OPCODE 1 rs 1
            emitI s TOK_BEQ_OPCODE 0 1 target
  _ | op == TOK_BGE_PSEUDO_OP || op == TOK_BGEU_PSEUDO_OP ->
    branchCompare (if op == TOK_BGE_PSEUDO_OP then TOK_SLT_OPCODE else TOK_SLTU_OPCODE)
      (if op == TOK_BGE_PSEUDO_OP then TOK_SLTI_OPCODE else TOK_SLTIU_OPCODE) TOK_BEQ_OPCODE
  _ | op == TOK_BLT_PSEUDO_OP || op == TOK_BLTU_PSEUDO_OP ->
    branchCompare (if op == TOK_BLT_PSEUDO_OP then TOK_SLT_OPCODE else TOK_SLTU_OPCODE)
      (if op == TOK_BLT_PSEUDO_OP then TOK_SLTI_OPCODE else TOK_SLTIU_OPCODE) TOK_BNE_OPCODE
  _ | op == TOK_BLE_PSEUDO_OP || op == TOK_BLEU_PSEUDO_OP -> do
    rs :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokReg
      then do
        rt :: Int <- parseRegister s
        target :: ImmExpr <- parseLabel s
        emitR s (if op == TOK_BLE_PSEUDO_OP then TOK_SLT_OPCODE else TOK_SLTU_OPCODE) 1 rt rs
        emitI s TOK_BEQ_OPCODE 0 1 target
      else do
        imm :: ImmExpr <- parseImm32 s
        target :: ImmExpr <- parseLabel s
        if op == TOK_BLE_PSEUDO_OP
          then do
            emitI s TOK_SLTI_OPCODE 1 rs (incrExprOffset imm 1)
            emitI s TOK_BNE_OPCODE 0 1 target
          else do
            emitI s TOK_ORI_OPCODE 1 0 imm
            emitI s TOK_BEQ_OPCODE rs 1 target
            emitR s TOK_SLTU_OPCODE 1 rs 1
            emitI s TOK_BNE_OPCODE 0 1 target
  _ -> parseErrorAt s "Pseudo-op not yet supported" >> syncToNl s
  where
    twoRegs :: (Int -> Int -> IO b) -> IO b
    twoRegs f = do
      rd :: Int <- parseRegister s
      rs :: Int <- parseRegister s
      f rd rs
    fpAddress :: Opcode -> IO ()
    fpAddress real = do
      fr :: Int <- parseFpRegister s
      (r, imm) :: (Int, ImmExpr) <- parseAddress s
      emitI s real fr r imm
    -- bge/bgeu/blt/bltu: set $at by a comparison, then branch on it.
    branchCompare :: Opcode -> Opcode -> Opcode -> IO ()
    branchCompare slt slti br = do
      rs :: Int <- parseRegister s
      t :: Token <- peek s
      if t == TokReg
        then do
          rt :: Int <- parseRegister s
          target :: ImmExpr <- parseLabel s
          emitR s slt 1 rs rt
          emitI s br 0 1 target
        else do
          imm :: ImmExpr <- parseImm32 s
          target :: ImmExpr <- parseLabel s
          emitI s slti 1 rs imm
          emitI s br 0 1 target

-- | A coprocessor register, written as @$n@ or @$fn@.
coprocessorRegister :: Spim -> IO Int
coprocessorRegister s = do
  t :: Token <- peek s
  if t == TokFpReg then parseFpRegister s else parseRegister s

-- | One instruction (C: @parse_asm_code@), dispatched on its operand shape.
parseAsmCode :: Spim -> IO ()
parseAsmCode s = do
  t :: Token <- advance s
  case t of
    TokOp op -> parseOp op (findOpType op)
    _ -> unsupported
  where
    unsupported :: IO ()
    unsupported = parseErrorAt s "Instruction not yet supported" >> syncToNl s
    parseOp :: Opcode -> OpType -> IO ()
    parseOp op ty = case ty of
      NOARG_TYPE_INST -> parseNoarg s op
      RD_RS_RT_INST -> parseR3 s op
      RD_RT_SHAMT_INST -> parseR2sh s op
      RS_INST -> parseRegister s >>= \rs -> emitR s op 0 rs 0
      RT_RS_IMM_INST -> parseI2 s op
      RT_IMM_INST -> do
        rt :: Int <- parseRegister s
        parseUimm16 s >>= emitI s op rt 0
      RT_ADDR_INST -> parseI2a s op
      BRANCH_RS_RT_LABEL_INST -> parseB2 s op
      BRANCH_RS_LABEL_INST -> do
        rs :: Int <- parseRegister s
        parseLabel s >>= emitI s op 0 rs
      J_TYPE_INST -> parseJ s op
      PSEUDO_OP -> parsePseudo s op
      RT_COPREG_INST -> copreg op
      RD_RS_INST -> parseJ s op
      RS_RT_INST -> do
        r1 :: Int <- parseRegister s
        t :: Token <- peek s
        if t /= TokReg && t /= TokInt && t /= TokId
          then parseErrorAt s "Expected operand for div/mult"
          else
            if op `elem` [TOK_TEQ_OPCODE, TOK_TGE_OPCODE, TOK_TGEU_OPCODE, TOK_TLT_OPCODE, TOK_TLTU_OPCODE, TOK_TNE_OPCODE]
              then parseRegister s >>= emitR s op 0 r1
              else
                if op == TOK_DIV_OPCODE || op == TOK_DIVU_OPCODE
                  then do
                    r2 :: Int <- parseRegister s
                    t2 :: Token <- peek s
                    if t2 == TokNL || t2 == TokEOF
                      then emitR s op 0 r1 r2
                      else
                        if t2 == TokReg
                          then parseRegister s >>= \r3 -> divInst s op r1 r2 r3 False
                          else do
                            imm :: ImmExpr <- parseImm32 s
                            if isZeroImm imm
                              then parseErrorAt s "Divide by zero"
                              else emitI s TOK_ORI_OPCODE 1 0 imm >> divInst s op r1 r2 1 True
                  else parseRegister s >>= emitR s op 0 r1
      RD_INST -> parseRegister s >>= \rd -> emitR s op rd 0 0
      RD_RT_RS_INST -> do
        rd :: Int <- parseRegister s
        rs :: Int <- parseRegister s
        t :: Token <- peek s
        if t == TokReg
          then parseRegister s >>= \rt -> emitR s op rd rt rs
          else
            if t /= TokInt
              then parseErrorAt s "Expected register or integer shift"
              else do
                _ <- advance s
                sh :: Int <- int <$> valInt s
                immOp :: Opcode <- opToImmOp op
                emitRShift s immOp rd rs sh
      RS_IMM_INST -> do
        rs :: Int <- parseRegister s
        parseImm16 s >>= emitI s op 0 rs
      BRANCH_COPROC_COND_INST -> do
        let nd :: Int
            nd = if opcodeIsNullifiedBranch op then 1 else 0
            tf :: Int
            tf = if opcodeIsTrueBranch op then 1 else 0
        t :: Token <- peek s
        cc :: Int <- if t == TokInt then advance s >> int <$> valInt s else pure 0
        target :: ImmExpr <- parseLabel s
        emitI s op ((cc `shiftL` 2) .|. (nd `shiftL` 1) .|. tf) 0 target
      MOVC_TYPE_INST -> do
        rd :: Int <- parseRegister s
        rs :: Int <- parseRegister s
        t :: Token <- peek s
        cc :: Int <- if t == TokInt then advance s >> int <$> valInt s else pure 0
        emitR s op rd rs ((cc .&. 7) `shiftL` 2)
      FP_FD_FS_INST -> do
        fd :: Int <- parseFpRegister s
        fs :: Int <- parseFpRegister s
        emitFpR s op fd fs 0
      FP_FD_FS_FT_INST -> do
        fd :: Int <- parseFpRegister s
        fs :: Int <- parseFpRegister s
        ft :: Int <- parseFpRegister s
        emitFpR s op fd fs ft
      FP_CMP_FS_FT_INST -> do
        t :: Token <- peek s
        cc :: Int <- if t == TokInt then advance s >> int <$> valInt s else pure 0
        fs :: Int <- parseFpRegister s
        ft :: Int <- parseFpRegister s
        emitFpCompare s op fs ft cc
      FP_MOVC_TYPE_INST -> do
        fd :: Int <- parseFpRegister s
        fs :: Int <- parseFpRegister s
        t :: Token <- peek s
        case t of
          TokReg -> parseRegister s >>= emitFpR s op fd fs
          TokInt -> do
            _ <- advance s
            cc :: Int <- int <$> valInt s
            emitFpR s op fd fs ((cc .&. 7) `shiftL` 2)
          _ -> emitFpR s op fd fs 0
      FP_FT_ADDR_INST -> do
        fr :: Int <- parseFpRegister s
        (r, imm) :: (Int, ImmExpr) <- parseAddress s
        emitI s op fr r imm
      FP_RT_COPREG_INST -> copreg op
      _ -> unsupported
    copreg :: Opcode -> IO ()
    copreg op = do
      r :: Int <- parseRegister s
      c :: Int <- coprocessorRegister s
      emitFpR s op 0 c r

-- Directives.

-- | @.data@/@.kdata@/@.text@/@.ktext@ with an optional address
-- (C: @parse_dir_data@, @parse_dir_text@, @emit_dir_seg@).
parseDirSeg :: Spim -> (Maybe Word32 -> NodeKind) -> Bool -> IO ()
parseDirSeg s kind isText = do
  t :: Token <- peek s
  addr :: Maybe Word32 <- if t == TokInt then advance s >> Just . fromIntegral <$> valInt s else pure Nothing
  if isText
    then set (dataDir s) False >> set (textDir s) True
    else set (dataDir s) True >> set (textDir s) False >> set (autoAlign s) True
  appendNode s (kind addr)

-- | An identifier after a directive, or an error.
directiveName :: Spim -> String -> IO (Maybe String)
directiveName s msg = do
  scannerForceIdentifier s
  t :: Token <- peek s
  if t /= TokId
    then parseErrorAt s msg >> pure Nothing
    else advance s >> Just <$> valStr s

-- | Labels on this line move to where the datum will start
-- (C: @align_labels_to@).
alignLabelsTo :: Spim -> Int -> IO ()
alignLabelsTo s n = do
  inData :: Bool <- get (dataDir s)
  auto :: Bool <- get (autoAlign s)
  ls :: [Label] <- get (thisLineLabels s)
  when (inData && auto && not (null ls)) $ do
    cur :: Word32 <- currentDataPc s
    fixCurrentLabelAddress s ((cur + (1 `shiftL` n) - 1) .&. (0xffffffff `shiftL` n))

-- | Align for a datum if in the data segment.
alignForData :: Spim -> Int -> IO ()
alignForData s n = do
  alignLabelsTo s n
  inData :: Bool <- get (dataDir s)
  when inData $ setDataAlignment s n

-- | A list of integer expressions (C: @parse_expr_list@); stops at an
-- error after skipping the rest of the line.
parseExprList :: Spim -> ([ImmExpr] -> NodeKind) -> IO ()
parseExprList s kind = do
  line :: Int <- lineNo s
  values :: [ImmExpr] <- loop []
  unless (null values) $ do
    node :: Node <- makeNode s (kind (reverse values))
    appendBuilt s node {nodeLine = line}
  where
    loop :: [ImmExpr] -> IO [ImmExpr]
    loop acc = do
      before :: Int <- get (parseErrorsSeen s)
      v :: Int32 <- parseExpression s
      after :: Int <- get (parseErrorsSeen s)
      if after /= before
        then syncToNl s >> pure acc
        else do
          pending :: Maybe Label <- get (exprUnresolvedLabel s)
          e :: ImmExpr <- makeImmExpr s v (lblName <$> pending) False
          set (exprUnresolvedLabel s) Nothing
          t :: Token <- peek s
          if t == TokNL || t == TokEOF then pure (e : acc) else loop (e : acc)

-- | A list of floating-point literals (C: @parse_fp_expr_list@).
parseFpExprList :: Spim -> ([Double] -> NodeKind) -> IO ()
parseFpExprList s kind = do
  line :: Int <- lineNo s
  r :: Maybe [Double] <- loop []
  case r of
    Just values -> do
      node :: Node <- makeNode s (kind (reverse values))
      appendBuilt s node {nodeLine = line}
    Nothing -> pure ()
  where
    loop :: [Double] -> IO (Maybe [Double])
    loop acc = do
      t :: Token <- peek s
      if t /= TokFp
        then parseErrorAt s "Expected floating-point literal" >> pure Nothing
        else do
          _ <- advance s
          v :: Double <- valFp s
          t2 :: Token <- peek s
          if t2 == TokNL || t2 == TokEOF then pure (Just (v : acc)) else loop (v : acc)

-- | Strings for @.ascii@/@.asciiz@ (C: @parse_string_list@).  The string
-- ends at its first NUL, as C's @strlen@ does.
parseStringList :: Spim -> IO ()
parseStringList s = do
  t :: Token <- peek s
  if t /= TokStr
    then parseErrorAt s "Expected string literal"
    else do
      _ <- advance s
      str :: String <- takeWhile (/= '\0') <$> valStr s
      inText :: Bool <- get (textDir s)
      nul :: Bool <- get (nullTerm s)
      if inText
        then parseErrorAt s "Can't put data in text segment"
        else appendNode s (DataString str nul)
      t2 :: Token <- peek s
      unless (t2 == TokNL || t2 == TokEOF) $ parseStringList s

-- | One directive (C: @parse_directive@).
parseDirective :: Spim -> Opcode -> IO ()
parseDirective s d = case d of
  TOK_DATA_DIRECTIVE -> parseDirSeg s DirData False
  TOK_K_DATA_DIRECTIVE -> parseDirSeg s DirKData False
  TOK_TEXT_DIRECTIVE -> parseDirSeg s DirText True
  TOK_K_TEXT_DIRECTIVE -> parseDirSeg s DirKText True
  TOK_GLOBAL_DIRECTIVE -> directiveName s "Expected identifier after .globl" >>= mapM_ (appendNode s . DirGlobl)
  TOK_WORD_DIRECTIVE -> alignForData s 2 >> parseExprList s DataWord
  TOK_HALF_DIRECTIVE -> alignForData s 1 >> parseExprList s DataHalf
  TOK_BYTE_DIRECTIVE -> parseExprList s DataByte
  TOK_ASCII_DIRECTIVE -> set (nullTerm s) False >> parseStringList s
  TOK_ASCIIZ_DIRECTIVE -> set (nullTerm s) True >> parseStringList s
  TOK_FLOAT_DIRECTIVE -> alignForData s 2 >> parseFpExprList s DataFloat
  TOK_DOUBLE_DIRECTIVE -> alignForData s 3 >> parseFpExprList s DataDouble
  TOK_SPACE_DIRECTIVE -> do
    v :: Int32 <- parseExpression s
    inText :: Bool <- get (textDir s)
    if inText then parseErrorAt s "Can't put data in text segment" else appendNode s (DirSpace (int v))
  TOK_ALIGN_DIRECTIVE -> do
    v :: Int32 <- parseExpression s
    when (v == 0) $ set (autoAlign s) False
    appendNode s (DirAlign (int v))
  TOK_COMM_DIRECTIVE -> namedSize DirComm
  TOK_EXTERN_DIRECTIVE -> namedSize DirExtern
  TOK_ERR_DIRECTIVE -> parseErrorAt s ".err directive"
  _
    | d `elem` ignored -> syncToNl s
    | otherwise -> parseErrorAt s "Directive not yet supported" >> syncToNl s
  where
    namedSize :: (String -> Int -> NodeKind) -> IO ()
    namedSize kind = do
      name :: Maybe String <- directiveName s "Expected ID"
      case name of
        Nothing -> pure ()
        Just n -> do
          size :: Int32 <- parseExpression s
          appendNode s (kind n (int size))
    ignored :: [Opcode]
    ignored =
      [ TOK_FILE_DIRECTIVE, TOK_LOC_DIRECTIVE, TOK_FRAME_DIRECTIVE, TOK_MASK_DIRECTIVE
      , TOK_FMASK_DIRECTIVE, TOK_ENT_DIRECTIVE, TOK_END_DIRECTIVE, TOK_LABEL_DIRECTIVE
      , TOK_LIVEREG_DIRECTIVE, TOK_OPTIONS_DIRECTIVE, TOK_BGNB_DIRECTIVE, TOK_ENDB_DIRECTIVE
      , TOK_ENDR_DIRECTIVE, TOK_ASM0_DIRECTIVE, TOK_ALIAS_DIRECTIVE, TOK_SET_DIRECTIVE
      ]

-- | @name:@ or @name = expression@ (C: @parse_opt_label@).
parseOptLabel :: Spim -> IO ()
parseOptLabel s = do
  _ <- advance s
  name :: String <- valStr s
  sep :: Token <- advance s
  if sep == TokChar ':'
    then appendNode s (LabelDef name LabelNormal)
    else do
      v :: Int32 <- parseExpression s
      appendNode s (LabelDef name (LabelConst v))

isDirective :: Token -> Bool
isDirective (TokOp op) = findOpType op == ASM_DIR
isDirective _ = False

-- | One line (C: @parse_line@).
parseLine :: Spim -> IO ()
parseLine s = do
  t0 :: Token <- peek s
  case t0 of
    TokNL -> () <$ advance s
    TokEOF -> pure ()
    _ -> do
      t1 :: Token <- if t0 == TokId then peek2 s else pure TokEOF
      when (t0 == TokId && (t1 == TokChar ':' || t1 == TokChar '=')) $ parseOptLabel s
      t :: Token <- peek s
      case t of
        TokNL -> () <$ advance s
        TokEOF -> pure ()
        TokOp d | isDirective t -> do
          _ <- advance s
          parseDirective s d
          clearLabels s
          finish
        _ -> parseAsmCode s >> clearLabels s >> finish
  where
    finish :: IO ()
    finish = do
      err :: Bool <- get (parseErrorOccurred s)
      t :: Token <- peek s
      if err
        then syncToNl s
        else
          if t == TokNL
            then () <$ advance s
            else unless (t == TokEOF) $ parseErrorAt s "Extra tokens after instruction" >> syncToNl s

-- | Patch the uses of the labels defined on this line, and forget them
-- (C: @clear_labels@).
clearLabels :: Spim -> IO ()
clearLabels s = do
  ls :: [Label] <- get (thisLineLabels s)
  case ls of
    [] -> pure ()
    (l : rest) -> do
      resolveLabelUses s l
      set (thisLineLabels s) rest
      clearLabels s

-- | Parse the whole file, print the tree if asked, then emit it; returns
-- the number of parse errors (C: @parse_file@).
parseFile :: Spim -> IO Int
parseFile s = do
  -- Emit only what this call parses, starting in the segment that was
  -- current when it began: the REPL's `.` command parses one line onto the
  -- tree of the file loaded last, whose statements were emitted already,
  -- and the parse itself moves dataDir/textDir as it meets .data and .text.
  emitted :: Int <- length <$> get (astScope s)
  startData :: Bool <- get (dataDir s)
  startText :: Bool <- get (textDir s)
  let loop :: IO ()
      loop = do
        t :: Token <- peek s
        unless (t == TokEOF) $ do
          set (parseErrorOccurred s) False
          scannerStartLine s
          parseLine s
          loop
  loop
  clearLabels s
  kids :: [Node] <- reverse <$> get (astScope s)
  file :: Maybe String <- get (inputFileName s)
  let root :: Node
      root = Node 1 Nothing (File file kids)
  pAst :: Bool <- get (printAstFlag s)
  when pAst $ writeErr (printAst root)
  pExp :: Bool <- get (showExpansionFlag s)
  when pExp $ forM_ kids $ \k -> case nodeKind k of
    Pseudo _ _ -> writeErr (printAst k)
    _ -> pure ()
  pJson :: Bool <- get (printAstJsonFlag s)
  when pJson $ writeOut (printAstJson root)
  only :: Bool <- get (printAstOnly s)
  unless only $ do
    set (dataDir s) startData
    set (textDir s) startText
    emitAst s (drop emitted kids)
  get (parseErrorsSeen s)

-- | Commit the statements to memory, in order (C: @emit_ast@).
emitAst :: Spim -> [Node] -> IO ()
emitAst s kids = do
  emitList s kids
  clearLabels s
  set (emitSource s) Nothing

-- | Emit nodes, clearing the line's labels after each statement that is not
-- itself a label or @.globl@.
emitList :: Spim -> [Node] -> IO ()
emitList s = mapM_ $ \n -> do
  emitOne s n
  case nodeKind n of
    LabelDef _ _ -> pure ()
    DirGlobl _ -> pure ()
    _ -> clearLabels s

-- | Commit one statement (C: @emit_one@).
emitOne :: Spim -> Node -> IO ()
emitOne s node = do
  setLineNo s (nodeLine node)
  set (emitSource s) (Just (nodeSrc node))
  case nodeKind node of
    File _ _ -> pure ()
    DirText a -> segText False a
    DirKText a -> segText True a
    DirData a -> segData False a
    DirKData a -> segData True a
    DirAlign v -> do
      when (v == 0) $ set (autoAlign s) False
      inText :: Bool <- get (textDir s)
      if inText then alignText s v else alignData s v
    DirSpace n -> incrementDataPc s n
    DirGlobl name -> () <$ makeLabelGlobal s name
    DirExtern name size -> do
      _ <- makeLabelGlobal s name
      defineIfNew name
      incrementDataPc s size
    DirComm name size -> do
      alignData s 2
      defineIfNew name
      incrementDataPc s size
    DataByte es -> emitData es (storeByte s) 1
    DataHalf es -> alignLabelsTo s 1 >> alignIfData 1 >> emitData es (storeHalf s) 2
    DataWord es -> alignLabelsTo s 2 >> alignIfData 2 >> emitData es (storeWord s) 4
    DataFloat vs -> alignLabelsTo s 2 >> alignIfData 2 >> mapM_ (storeFloat s) vs
    DataDouble vs -> alignLabelsTo s 3 >> alignIfData 3 >> mapM_ (storeDouble s) vs
    DataString bytes nul -> do
      inText :: Bool <- get (textDir s)
      unless inText $ storeString s bytes nul
    LabelDef name LabelNormal -> do
      inText :: Bool <- get (textDir s)
      addr :: Word32 <- if inText then currentTextPc s else currentDataPc s
      l :: Label <- recordLabel s name addr False
      modify (thisLineLabels s) (l :)
    LabelDef name (LabelConst v) -> do
      l :: Label <- recordLabel s name (fromIntegral v) True
      set (lblConst l) True
    InstR op rd rs rt -> rTypeInst s op rd rs rt
    InstRShift op rd rt sh -> rShTypeInst s op rd rt sh
    InstI op rt rs imm -> iTypeInst s op rt rs imm
    InstJ op target -> jTypeInst s op target
    InstFpR op fd fs ft -> rCoTypeInst s op fd fs ft
    InstFpCompare op fs ft cc -> rCondTypeInst s op fs ft cc
    Pseudo _ kids -> emitList s kids
  where
    segText :: Foldable t => Bool -> t Word32 -> IO ()
    segText kernel addr = do
      userKernelTextSegment s kernel
      set (dataDir s) False
      set (textDir s) True
      mapM_ (setTextPc s) addr
    segData :: Foldable t => Bool -> t Word32 -> IO ()
    segData kernel addr = do
      userKernelDataSegment s kernel
      set (dataDir s) True
      set (textDir s) False
      enableDataAlignment s
      set (autoAlign s) True
      mapM_ (setDataPc s) addr
    alignIfData :: Int -> IO ()
    alignIfData n = do
      inData :: Bool <- get (dataDir s)
      when inData $ setDataAlignment s n
    defineIfNew :: String -> IO ()
    defineIfNew name = do
      l :: Label <- lookupLabel s name
      addr :: Word32 <- get (lblAddr l)
      when (addr == 0) $ currentDataPc s >>= \pc -> () <$ recordLabel s name pc True
    -- A value naming a label not yet defined is stored as its offset and
    -- patched when the label is defined.
    emitData :: Foldable t => t ImmExpr -> (Int32 -> IO b) -> Int -> IO ()
    emitData es store width = forM_ es $ \e -> do
      defined :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol e)
      case ieSymbol e of
        Just l | not defined -> do
          pc :: Word32 <- currentDataPc s
          recordDataUsesSymbol s pc l (ieOffset e) width
          store (ieOffset e)
        _ -> evalImmExpr s e >>= store
