-- | The assembler's parser and emitter (C: src/parser.c and the expansion
-- helpers of src/pseudo-op.c).
--
-- Parsing a file happens in two passes over different things:
--
-- 1. /parse/: a recursive-descent parser reads tokens and builds a list of
--    'Node's, one per statement; a pseudo-instruction becomes a 'Pseudo'
--    node holding the real instructions it expands to.
-- 2. /emit/: 'emitAst' walks the nodes in order and calls the assembling
--    actions ('iType', 'storeWord', 'recordLabel', ...) that put each
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
  ( parserInitialize,
    parseFile,
  )
where

import AsmCommon
import Assemble
import Ast
import Control.Monad (forM_, unless, when)
import Data.Bits (shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.Word (Word32, Word64)
import DataSegment
import GHC.Float (castDoubleToWord64, castFloatToWord32, double2Float)
import Instruction (opcodeIsNullifiedBranch, opcodeIsTrueBranch, token)
import OpcodeTables (findOpcodeType, opcodeTokenName)
import OpcodeTypes (OpType (..))
import Opcodes
import Output
import Scanner
import State
import SymbolTable
import Text.Printf (printf)
import Types

-- | Start a file (C: @parser_init@).
parserInitialize :: Spim -> String -> String -> IO ()
parserInitialize s input fileName = do
  scannerInitialize s input
  set (inputFileName s) (Just fileName)
  set (parseErrorsSeen s) 0
  set (parseErrorOccurred s) False
  set (dataDirective s) False
  set (textDirective s) True
  set (astScope s) []

-- | A parse error, as the parser reports it (C: @parse_error_at@).
parseErrorAt :: Spim -> String -> IO ()
parseErrorAt s message = do
  n :: Int <- lineNo s
  parseErrorOnLine s n message

-- | A parse error reported on a given line (C: @parse_error_on_line@).
parseErrorOnLine :: Spim -> Int -> String -> IO ()
parseErrorOnLine s n message = do
  set (parseErrorOccurred s) True
  modify (parseErrorsSeen s) (+ 1)
  file :: Maybe String <- get (inputFileName s)
  cError (printf "spim: (parser) %s on line %d of file %s\n" message n (maybe "(stdin)" id file))

peek, peek2, advance :: Spim -> IO Token
peek = scannerPeek
peek2 = scannerPeek2
advance = scannerAdvance

-- | The value of the last integer-like token taken.
valueInteger :: Spim -> IO Int32
valueInteger s = do
  v :: TokenValue <- get (scanValue s)
  pure $ case v of
    ValueInteger i -> i
    _ -> 0

-- | The text of the last identifier or string token taken.
valueString :: Spim -> IO String
valueString s = do
  v :: TokenValue <- get (scanValue s)
  pure $ case v of
    ValueString t -> t
    _ -> ""

valueFloatingPoint :: Spim -> IO Double
valueFloatingPoint s = do
  v :: TokenValue <- get (scanValue s)
  pure $ case v of
    ValueFloatingPoint d -> d
    _ -> 0

int :: Int32 -> Int
int = fromIntegral

-- | Skip to (but not past) the end of the line (C: @sync_to_nl@).
syncToNewline :: Spim -> IO ()
syncToNewline s = do
  t :: Token <- peek s
  unless (t == TokenNewline || t == TokenEOF) $ advance s >> syncToNewline s

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
  source :: Maybe String <- sourceLine s
  pure (Node n source kind)

appendBuilt :: Spim -> Node -> IO ()
appendBuilt s node = modify (astScope s) (node :)

emitR, emitRShift, emitFloatingPointR, emitFloatingPointCompare :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
emitR s op destinationRegister sourceRegister targetRegister = appendNode s (InstructionR (token op) destinationRegister sourceRegister targetRegister)
emitRShift s op destinationRegister targetRegister sh = appendNode s (InstructionRShift (token op) destinationRegister targetRegister sh)
emitFloatingPointR s op floatingPointDestination floatingPointSource floatingPointTarget = appendNode s (InstructionFloatingPointR (token op) floatingPointDestination floatingPointSource floatingPointTarget)
emitFloatingPointCompare s op floatingPointSource floatingPointTarget conditionCode = appendNode s (InstructionFloatingPointCompare (token op) floatingPointSource floatingPointTarget conditionCode)

emitI :: Spim -> Opcode -> Int -> Int -> ImmediateExpression -> IO ()
emitI s op targetRegister sourceRegister immediate = appendNode s (InstructionI (token op) targetRegister sourceRegister immediate)

emitJ :: Spim -> Opcode -> ImmediateExpression -> IO ()
emitJ s op target = appendNode s (InstructionJ (token op) target)

-- Operands.

parseRegister :: Spim -> IO Int
parseRegister s = do
  t :: Token <- peek s
  if t == TokenRegister
    then advance s >> int <$> valueInteger s
    else parseErrorAt s "Expected register" >> pure 0

parseFloatingPointRegister :: Spim -> IO Int
parseFloatingPointRegister s = do
  t :: Token <- peek s
  if t == TokenFloatingPointRegister
    then advance s >> int <$> valueInteger s
    else parseErrorAt s "Expected FP register" >> pure 0

-- | An integer, or @int + int@, or @int -int@ (C: @parse_abs_addr@).
parseAbsoluteAddress :: Spim -> IO Int32
parseAbsoluteAddress s = do
  t :: Token <- peek s
  if t /= TokenInteger
    then parseErrorAt s "Expected integer" >> pure 0
    else do
      _ <- advance s
      v :: Int32 <- valueInteger s
      t2 :: Token <- peek s
      case t2 of
        TokenChar '+' -> do
          _ <- advance s
          t3 :: Token <- peek s
          if t3 /= TokenInteger
            then parseErrorAt s "Expected integer after '+'" >> pure v
            else advance s >> (v +) <$> valueInteger s
        TokenInteger -> do
          _ <- advance s
          w :: Int32 <- valueInteger s
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
    TokenInteger -> advance s >> valueInteger s
    TokenChar '(' -> do
      _ <- advance s
      v :: Int32 <- parseSumOfTerms s
      expect s (TokenChar ')') "Expected ')'"
      pure v
    TokenIdentifier -> do
      _ <- advance s
      name :: String <- valueString s
      l :: Label <- lookupLabel s name
      address :: Word32 <- get (labelAddress l)
      if address == 0
        then do
          pending :: Maybe Label <- get (expressionUnresolvedLabel s)
          when (null pending) $ set (expressionUnresolvedLabel s) (Just l)
          pure 0
        else pure (fromIntegral address)
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
        TokenChar '*' -> advance s >> parseFactor s >>= \r -> loop (v * r)
        TokenChar '/' -> advance s >> parseFactor s >>= \r -> loop (cDivide v r)
        _ -> pure v
    cDivide :: (Integral a) => a -> a -> a
    cDivide v r
      | r == 0 = 0
      | r == -1 = negate v
      | otherwise = v `quot` r

-- | EXPR: terms joined by @+@ and @-@.
parseSumOfTerms :: Spim -> IO Int32
parseSumOfTerms s = parseTerm s >>= loop
  where
    loop :: Int32 -> IO Int32
    loop v = do
      t :: Token <- peek s
      case t of
        TokenChar '+' -> advance s >> parseTerm s >>= \r -> loop (v + r)
        TokenChar '-' -> advance s >> parseTerm s >>= \r -> loop (v - r)
        _ -> pure v

-- | An expression whose first word is read as an identifier
-- (C: @parse_expression@).
parseExpression :: Spim -> IO Int32
parseExpression s = do
  scannerForceIdentifier s
  set (expressionUnresolvedLabel s) Nothing
  parseSumOfTerms s

-- | IMM32 (C: @parse_imm32@): @label@, @label+n@, @label-n@, a number, or
-- @(n) >> k@.
parseImmediate32 :: Spim -> IO ImmediateExpression
parseImmediate32 s = do
  scannerForceIdentifier s
  t :: Token <- peek s
  case t of
    TokenIdentifier -> do
      _ <- advance s
      symbol :: String <- valueString s
      t2 :: Token <- peek s
      case t2 of
        TokenChar '+' -> advance s >> parseAbsoluteAddress s >>= \offset -> makeImmediateExpression s offset (Just symbol) False
        TokenChar '-' -> advance s >> parseAbsoluteAddress s >>= \offset -> makeImmediateExpression s (negate offset) (Just symbol) False
        _ -> makeImmediateExpression s 0 (Just symbol) False
    TokenChar '(' -> do
      _ <- advance s
      v :: Int32 <- parseAbsoluteAddress s
      expect s (TokenChar ')') "Expected ')'"
      expect s (TokenChar '>') "Expected '>'"
      expect s (TokenChar '>') "Expected '>'"
      t2 :: Token <- peek s
      if t2 /= TokenInteger
        then parseErrorAt s "Expected integer after '>>'" >> pure (constantImmediateExpression 0)
        else do
          _ <- advance s
          sh :: Int32 <- valueInteger s
          pure (constantImmediateExpression (v `shiftR` (int sh .&. 31)))
    _ -> constantImmediateExpression <$> parseAbsoluteAddress s

-- | Warn when a known immediate is outside a range (C: @check_imm_range@), on
-- the instruction's line: the scanner's line can already be the next one, as
-- reading past the immediate scans the newline.
checkImmediateRange :: Spim -> ImmediateExpression -> Int32 -> Int32 -> Int -> IO ()
checkImmediateRange s e lo hi line = do
  known :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol e)
  when known $ do
    v :: Int32 <- evaluateImmediateExpression s e
    when (v < lo || hi < v) $ rangeError v lo hi line

checkUnsignedImmediateRange :: Spim -> ImmediateExpression -> Word32 -> Word32 -> Int -> IO ()
checkUnsignedImmediateRange s e lo hi line = do
  known :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol e)
  when known $ do
    v :: Word32 <- fromIntegral <$> evaluateImmediateExpression s e
    when (v < lo || hi < v) $ rangeError (fromIntegral v) (fromIntegral lo) (fromIntegral hi) line

rangeError :: Int32 -> Int32 -> Int32 -> Int -> IO ()
rangeError v lo hi line =
  cError (printf "spim: (parser) immediate value (%d) out of range (%d .. %d) on line %d\n" v lo hi line)

parseImmediate16, parseUnsignedImmediate16 :: Spim -> IO ImmediateExpression
parseImmediate16 s = do
  line :: Int <- lineNo s
  e :: ImmediateExpression <- parseImmediate32 s
  checkImmediateRange s e (-0x8000) 0x7fff line
  pure e
parseUnsignedImmediate16 s = do
  line :: Int <- lineNo s
  e :: ImmediateExpression <- parseImmediate32 s
  checkUnsignedImmediateRange s e 0 0xffff line
  pure e

-- | A branch target (C: @parse_label@): pc-relative, so its offset is
-- minus the current text address until the branch is assembled.
parseLabel :: Spim -> IO ImmediateExpression
parseLabel s = do
  t :: Token <- peek s
  if t /= TokenIdentifier
    then parseErrorAt s "Expected label" >> pure (ImmediateExpression 0 Nothing 0 True)
    else do
      _ <- advance s
      symbol :: String <- valueString s
      programCounter :: Word32 <- currentTextLocation s
      makeImmediateExpression s (negate (fromIntegral programCounter)) (Just symbol) True

-- | ADDRESS (C: @parse_address@): @(reg)@, @n@, @n(reg)@, @n+label@,
-- @label@, @label+n@, @label-n@, each with an optional @(reg)@.
parseAddress :: Spim -> IO (Int, ImmediateExpression)
parseAddress s = do
  scannerForceIdentifier s
  t :: Token <- peek s
  case t of
    TokenChar '(' -> do
      _ <- advance s
      r :: Int <- parseRegister s
      expect s (TokenChar ')') "Expected ')' after register"
      makeAddressExpression s 0 Nothing r
    TokenInteger -> do
      immediate :: Int32 <- parseAbsoluteAddress s
      t2 :: Token <- peek s
      t3 :: Token <- if t2 == TokenChar '+' then peek2 s else pure TokenEOF
      if t2 == TokenChar '+' && t3 == TokenIdentifier
        then do
          _ <- advance s
          scannerForceIdentifier s
          _ <- advance s
          symbol :: String <- valueString s
          makeAddressExpression s immediate (Just symbol) 0
        else
          if t2 == TokenChar '('
            then do
              _ <- advance s
              r :: Int <- parseRegister s
              expect s (TokenChar ')') "Expected ')'"
              makeAddressExpression s immediate Nothing r
            else makeAddressExpression s immediate Nothing 0
    TokenIdentifier -> do
      _ <- advance s
      symbol :: String <- valueString s
      t2 :: Token <- peek s
      offset :: Int32 <- case t2 of
        TokenChar '+' -> advance s >> parseAbsoluteAddress s
        TokenChar '-' -> advance s >> negate <$> parseAbsoluteAddress s
        _ -> pure 0
      t3 :: Token <- peek s
      r :: Int <-
        if t3 == TokenChar '('
          then do
            _ <- advance s
            r :: Int <- parseRegister s
            expect s (TokenChar ')') "Expected ')'"
            pure r
          else pure 0
      makeAddressExpression s offset (Just symbol) r
    _ -> parseErrorAt s "Expected address" >> makeAddressExpression s 0 Nothing 0

-- Instructions.

-- | Opcodes with both a register and an immediate form.
hasImmediateForm :: Opcode -> Bool
hasImmediateForm op = op `elem` [TOKEN_ADD_OPCODE, TOKEN_ADDU_OPCODE, TOKEN_AND_OPCODE, TOKEN_OR_OPCODE, TOKEN_XOR_OPCODE, TOKEN_SLT_OPCODE, TOKEN_SLTU_OPCODE]

-- | @op rd, rs, rt@ and its immediate forms (C: @parse_r3@).
parseR3 :: Spim -> Opcode -> IO ()
parseR3 s op = do
  destinationRegister :: Int <- parseRegister s
  case () of
    _
      | op == TOKEN_CLO_OPCODE || op == TOKEN_CLZ_OPCODE -> do
          sourceRegister :: Int <- parseRegister s
          emitR s op destinationRegister sourceRegister destinationRegister
      | op == TOKEN_MUL_OPCODE -> do
          sourceRegister :: Int <- parseRegister s
          t :: Token <- peek s
          if t == TokenRegister
            then parseRegister s >>= emitR s op destinationRegister sourceRegister
            else do
              immediate :: ImmediateExpression <- parseImmediate32 s
              emitI s TOKEN_ORI_OPCODE 1 0 immediate
              emitR s op destinationRegister sourceRegister 1
      | op == TOKEN_SUB_OPCODE || op == TOKEN_SUBU_OPCODE -> do
          let immediateOpcode :: Opcode
              immediateOpcode = if op == TOKEN_SUB_OPCODE then TOKEN_ADDI_OPCODE else TOKEN_ADDIU_OPCODE
          t :: Token <- peek s
          if t /= TokenRegister
            then do
              immediate :: ImmediateExpression <- parseImmediate32 s
              v :: Int32 <- evaluateImmediateExpression s immediate
              emitI s immediateOpcode destinationRegister destinationRegister (constantImmediateExpression (negate v))
            else do
              sourceRegister :: Int <- parseRegister s
              t2 :: Token <- peek s
              if t2 == TokenRegister
                then parseRegister s >>= emitR s op destinationRegister sourceRegister
                else do
                  immediate :: ImmediateExpression <- parseImmediate32 s
                  v :: Int32 <- evaluateImmediateExpression s immediate
                  emitI s immediateOpcode destinationRegister sourceRegister (constantImmediateExpression (negate v))
      | hasImmediateForm op -> do
          immediateOpcode :: Opcode <- opcodeToImmediateOpcode op
          t :: Token <- peek s
          if t /= TokenRegister
            then parseImmediate32 s >>= emitI s immediateOpcode destinationRegister destinationRegister
            else do
              sourceRegister :: Int <- parseRegister s
              t2 :: Token <- peek s
              if t2 == TokenRegister
                then parseRegister s >>= emitR s op destinationRegister sourceRegister
                else parseImmediate32 s >>= emitI s immediateOpcode destinationRegister sourceRegister
      | otherwise -> do
          sourceRegister :: Int <- parseRegister s
          targetRegister :: Int <- parseRegister s
          emitR s op destinationRegister sourceRegister targetRegister

-- | @op rd, rt, shamt@ (C: @parse_r2sh@); @ssnop@ is @sll $0, $0, 1@.
parseTwoRegistersShiftAmount :: Spim -> Opcode -> IO ()
parseTwoRegistersShiftAmount s op
  | op == TOKEN_SSNOP_OPCODE = emitRShift s TOKEN_SLL_OPCODE 0 0 1
  | otherwise = do
      destinationRegister :: Int <- parseRegister s
      targetRegister :: Int <- parseRegister s
      t :: Token <- peek s
      if t /= TokenInteger
        then parseErrorAt s "Expected integer shift amount"
        else do
          _ <- advance s
          sh :: Int32 <- valueInteger s
          emitRShift s op destinationRegister targetRegister (int sh)

-- | @op rt, rs, imm16@, or @op rt, imm16@ (C: @parse_i2@).
parseI2 :: Spim -> Opcode -> IO ()
parseI2 s op = do
  targetRegister :: Int <- parseRegister s
  t :: Token <- peek s
  sourceRegister :: Int <- if t == TokenRegister then parseRegister s else pure targetRegister
  immediate :: ImmediateExpression <- if op `elem` [TOKEN_ANDI_OPCODE, TOKEN_ORI_OPCODE, TOKEN_XORI_OPCODE] then parseUnsignedImmediate16 s else parseImmediate16 s
  emitI s op targetRegister sourceRegister immediate

-- | Loads and stores: @op rt, address@.
parseI2a :: Spim -> Opcode -> IO ()
parseI2a s op = do
  targetRegister :: Int <- parseRegister s
  (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
  emitI s op targetRegister r immediate

-- | @beq rs, rt, label@, or with an immediate in place of rt (C: @parse_b2@).
parseB2 :: Spim -> Opcode -> IO ()
parseB2 s op = do
  source1 :: Int <- parseRegister s
  t :: Token <- peek s
  if t == TokenRegister
    then do
      source2 :: Int <- parseRegister s
      target :: ImmediateExpression <- parseLabel s
      emitI s op source2 source1 target
    else do
      immediate :: ImmediateExpression <- parseImmediate32 s
      target :: ImmediateExpression <- parseLabel s
      if isZeroImmediate immediate
        then emitI s op source1 0 target
        else do
          emitI s TOKEN_ORI_OPCODE 1 0 immediate
          emitI s op source1 1 target

-- | @j label@ / @jal label@, or the register forms (C: @parse_j@). @jalr.hb@ keeps its own
-- opcode (@jalr@ with the hazard-barrier bit) and takes registers only; @cop2@ shares
-- @J_TYPE_INSTRUCTION@, but spim has no coprocessor 2, so it is an error.
parseJ :: Spim -> Opcode -> IO ()
parseJ s op
  | op == TOKEN_COP2_OPCODE = parseErrorAt s "cop2 is not supported: spim has no coprocessor 2"
  | otherwise = do
      t :: Token <- peek s
      if t == TokenRegister
        then do
          r1 :: Int <- parseRegister s
          t2 :: Token <- peek s
          if t2 == TokenRegister
            then do
              -- DEST SRC1 form
              r2 :: Int <- parseRegister s
              case op of
                TOKEN_J_OPCODE -> emitR s TOKEN_JR_OPCODE 0 r2 0
                TOKEN_JR_OPCODE -> emitR s TOKEN_JR_OPCODE 0 r2 0
                TOKEN_JAL_OPCODE -> emitR s TOKEN_JALR_OPCODE r1 r2 0
                TOKEN_JALR_OPCODE -> emitR s TOKEN_JALR_OPCODE r1 r2 0
                TOKEN_JALR_HB_OPCODE -> emitR s TOKEN_JALR_HB_OPCODE r1 r2 0
                -- Never reached: only J_TYPE_INSTRUCTION and DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION opcodes come here.
                _ -> unexpected
            else case op of
              -- SRC1-only form
              TOKEN_J_OPCODE -> emitR s TOKEN_JR_OPCODE 0 r1 0
              TOKEN_JR_OPCODE -> emitR s TOKEN_JR_OPCODE 0 r1 0
              TOKEN_JAL_OPCODE -> emitR s TOKEN_JALR_OPCODE 31 r1 0
              TOKEN_JALR_OPCODE -> emitR s TOKEN_JALR_OPCODE 31 r1 0
              TOKEN_JALR_HB_OPCODE -> emitR s TOKEN_JALR_HB_OPCODE 31 r1 0
              -- Never reached: as in the DEST SRC1 form above.
              _ -> unexpected
        else
          if op == TOKEN_JALR_HB_OPCODE
            then parseErrorAt s "jalr.hb needs a register operand"
            else do
              target :: ImmediateExpression <- parseLabel s
              case op of
                TOKEN_J_OPCODE -> emitJ s TOKEN_J_OPCODE target
                TOKEN_JR_OPCODE -> emitJ s TOKEN_J_OPCODE target
                TOKEN_JAL_OPCODE -> emitJ s TOKEN_JAL_OPCODE target
                TOKEN_JALR_OPCODE -> emitJ s TOKEN_JAL_OPCODE target
                -- Never reached: jalr.hb and cop2 are handled above.
                _ -> unexpected
  where
    unexpected :: IO ()
    unexpected =
      fatalError ("internal error: parse_j: unexpected opcode " ++ show (opcodeToken op) ++ "\n")

-- | @syscall@, or @break n@ (C: @parse_noarg@).
parseNoArgument :: Spim -> Opcode -> IO ()
parseNoArgument s op = do
  t :: Token <- peek s
  if t == TokenInteger
    then do
      _ <- advance s
      n :: Int <- int <$> valueInteger s
      when (op == TOKEN_BREAK_OPCODE && n == 1) $ parseErrorAt s "Breakpoint 1 is reserved for debugger"
      emitR s op n 0 0
    else emitR s op 0 0 0

-- Pseudo-instruction expansions (C: src/pseudo-op.c).

-- | A branch over n instructions (C: @branch_offset@).
branchOffsetExpression :: Int32 -> ImmediateExpression
branchOffsetExpression n = constantImmediateExpression (n `shiftL` 2)

nopInstruction, trapInstruction :: Spim -> IO ()
nopInstruction s = emitR s TOKEN_SLL_OPCODE 0 0 0
trapInstruction s = emitR s TOKEN_BREAK_OPCODE 0 0 0

-- | @div@/@rem@ with three operands: check for zero, divide, move the
-- quotient or the remainder (C: @div_inst@).
divideInstruction :: Spim -> Opcode -> Int -> Int -> Int -> Bool -> IO ()
divideInstruction s op destinationRegister sourceRegister targetRegister constantDivisor = do
  when (destinationRegister /= 0 && not constantDivisor) $ do
    emitI s TOKEN_BNE_OPCODE 0 targetRegister (branchOffsetExpression 3)
    nopInstruction s
    trapInstruction s
  if op == TOKEN_DIV_OPCODE || op == TOKEN_REM_PSEUDO_OP
    then emitR s TOKEN_DIV_OPCODE 0 sourceRegister targetRegister
    else emitR s TOKEN_DIVU_OPCODE 0 sourceRegister targetRegister
  when (destinationRegister /= 0) $
    if op == TOKEN_DIV_OPCODE || op == TOKEN_DIVU_OPCODE
      then emitR s TOKEN_MFLO_OPCODE destinationRegister 0 0
      else emitR s TOKEN_MFHI_OPCODE destinationRegister 0 0

-- | @mulo@/@mulou@: multiply and trap on overflow (C: @mult_inst@).
multiplyInstruction :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
multiplyInstruction s op destinationRegister sourceRegister targetRegister = do
  emitR s (if op == TOKEN_MULOU_PSEUDO_OP then TOKEN_MULTU_OPCODE else TOKEN_MULT_OPCODE) 0 sourceRegister targetRegister
  if op == TOKEN_MULOU_PSEUDO_OP && destinationRegister /= 0
    then do
      emitR s TOKEN_MFHI_OPCODE 1 0 0
      emitI s TOKEN_BEQ_OPCODE 0 1 (branchOffsetExpression 3)
      nopInstruction s
      trapInstruction s
    else when (op == TOKEN_MULO_PSEUDO_OP && destinationRegister /= 0) $ do
      emitR s TOKEN_MFHI_OPCODE 1 0 0
      emitR s TOKEN_MFLO_OPCODE destinationRegister 0 0
      emitRShift s TOKEN_SRA_OPCODE destinationRegister destinationRegister 31
      emitI s TOKEN_BEQ_OPCODE destinationRegister 1 (branchOffsetExpression 3)
      nopInstruction s
      trapInstruction s
  when (destinationRegister /= 0) $ emitR s TOKEN_MFLO_OPCODE destinationRegister 0 0

-- | @sle@/@sge@ family: a branch-around sequence (C: @set_le_inst@,
-- @set_ge_inst@).
setCompareInstruction :: Spim -> Opcode -> Int -> Int -> Int -> Int -> IO ()
setCompareInstruction s slt destinationRegister sourceRegister targetRegister finalSourceRegister = do
  emitI s TOKEN_BNE_OPCODE sourceRegister targetRegister (branchOffsetExpression 3)
  emitI s TOKEN_ORI_OPCODE destinationRegister 0 (constantImmediateExpression 1)
  emitI s TOKEN_BEQ_OPCODE 0 0 (branchOffsetExpression 3)
  nopInstruction s
  emitR s slt destinationRegister finalSourceRegister (if finalSourceRegister == sourceRegister then targetRegister else sourceRegister)

-- | @seq@/@sne@ (C: @set_eq_inst@).
setEqualInstruction :: Spim -> Opcode -> Int -> Int -> Int -> IO ()
setEqualInstruction s op destinationRegister sourceRegister targetRegister = do
  let ifEqual :: Int32
      ifNotEqual :: Int32
      (ifEqual, ifNotEqual) = if op == TOKEN_SEQ_PSEUDO_OP then (1, 0) else (0, 1)
  emitI s TOKEN_BEQ_OPCODE sourceRegister targetRegister (branchOffsetExpression 3)
  emitI s TOKEN_ORI_OPCODE destinationRegister 0 (constantImmediateExpression ifNotEqual)
  emitI s TOKEN_BEQ_OPCODE 0 0 (branchOffsetExpression 3)
  nopInstruction s
  emitI s TOKEN_ORI_OPCODE destinationRegister 0 (constantImmediateExpression ifEqual)

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
  -- The expansion is the pseudo-op's line, even when its last operand
  -- read on to the newline before a child was built.
  let atLine :: Node -> Node
      atLine kid = kid {nodeLine = nodeLine start}
  appendBuilt s start {nodeKind = Pseudo (opcodeTokenName (token op)) (reverse (map atLine kids))}

-- | The expansion of each pseudo-instruction (C: @do_parse_pseudo@).
doParsePseudo :: Spim -> Opcode -> IO ()
doParsePseudo s op = case op of
  TOKEN_LI_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    parseImmediate32 s >>= emitI s TOKEN_ORI_OPCODE targetRegister 0
  TOKEN_MOVE_PSEUDO_OP -> twoRegisters (\destinationRegister sourceRegister -> emitR s TOKEN_ADDU_OPCODE destinationRegister 0 sourceRegister)
  TOKEN_NEG_PSEUDO_OP -> twoRegisters (\destinationRegister sourceRegister -> emitR s TOKEN_SUB_OPCODE destinationRegister 0 sourceRegister)
  TOKEN_NEGU_PSEUDO_OP -> twoRegisters (\destinationRegister sourceRegister -> emitR s TOKEN_SUBU_OPCODE destinationRegister 0 sourceRegister)
  TOKEN_NOT_PSEUDO_OP -> twoRegisters (\destinationRegister sourceRegister -> emitR s TOKEN_NOR_OPCODE destinationRegister sourceRegister 0)
  _ | op == TOKEN_ROR_PSEUDO_OP || op == TOKEN_ROL_PSEUDO_OP -> do
    destinationRegister :: Int <- parseRegister s
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokenRegister
      then do
        targetRegister :: Int <- parseRegister s
        emitR s TOKEN_SUBU_OPCODE 1 0 targetRegister
        if op == TOKEN_ROR_PSEUDO_OP
          then emitR s TOKEN_SLLV_OPCODE 1 1 sourceRegister >> emitR s TOKEN_SRLV_OPCODE destinationRegister targetRegister sourceRegister
          else emitR s TOKEN_SRLV_OPCODE 1 1 sourceRegister >> emitR s TOKEN_SLLV_OPCODE destinationRegister targetRegister sourceRegister
        emitR s TOKEN_OR_OPCODE destinationRegister destinationRegister 1
      else do
        line :: Int <- lineNo s
        immediate :: ImmediateExpression <- parseImmediate32 s
        distance :: Int <- int <$> evaluateImmediateExpression s immediate
        checkImmediateRange s immediate 0 31 line
        if op == TOKEN_ROR_PSEUDO_OP
          then emitRShift s TOKEN_SLL_OPCODE 1 sourceRegister (negate distance) >> emitRShift s TOKEN_SRL_OPCODE destinationRegister sourceRegister distance
          else emitRShift s TOKEN_SRL_OPCODE 1 sourceRegister (negate distance) >> emitRShift s TOKEN_SLL_OPCODE destinationRegister sourceRegister distance
        emitR s TOKEN_OR_OPCODE destinationRegister destinationRegister 1
  _ | op `elem` [TOKEN_SLE_PSEUDO_OP, TOKEN_SLEU_PSEUDO_OP, TOKEN_SGT_PSEUDO_OP, TOKEN_SGTU_PSEUDO_OP, TOKEN_SGE_PSEUDO_OP, TOKEN_SGEU_PSEUDO_OP, TOKEN_SEQ_PSEUDO_OP, TOKEN_SNE_PSEUDO_OP] -> do
    destinationRegister :: Int <- parseRegister s
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    targetRegister :: Int <-
      if t == TokenRegister
        then parseRegister s
        else do
          immediate :: ImmediateExpression <- parseImmediate32 s
          if not (isZeroImmediate immediate)
            then emitI s TOKEN_ORI_OPCODE 1 0 immediate >> pure 1
            else pure 0
    case op of
      TOKEN_SLE_PSEUDO_OP -> setCompareInstruction s TOKEN_SLT_OPCODE destinationRegister sourceRegister targetRegister sourceRegister
      TOKEN_SLEU_PSEUDO_OP -> setCompareInstruction s TOKEN_SLTU_OPCODE destinationRegister sourceRegister targetRegister sourceRegister
      TOKEN_SGT_PSEUDO_OP -> emitR s TOKEN_SLT_OPCODE destinationRegister targetRegister sourceRegister
      TOKEN_SGTU_PSEUDO_OP -> emitR s TOKEN_SLTU_OPCODE destinationRegister targetRegister sourceRegister
      TOKEN_SGE_PSEUDO_OP -> setCompareInstruction s TOKEN_SLT_OPCODE destinationRegister sourceRegister targetRegister targetRegister
      TOKEN_SGEU_PSEUDO_OP -> setCompareInstruction s TOKEN_SLTU_OPCODE destinationRegister sourceRegister targetRegister targetRegister
      _ -> setEqualInstruction s op destinationRegister sourceRegister targetRegister
  TOKEN_ABS_PSEUDO_OP -> do
    destinationRegister :: Int <- parseRegister s
    sourceRegister :: Int <- parseRegister s
    when (destinationRegister /= sourceRegister) $ emitR s TOKEN_ADDU_OPCODE destinationRegister 0 sourceRegister
    emitI s TOKEN_BGEZ_OPCODE 0 sourceRegister (branchOffsetExpression 3)
    nopInstruction s
    emitR s TOKEN_SUB_OPCODE destinationRegister 0 sourceRegister
  TOKEN_NOP_PSEUDO_OP -> nopInstruction s
  _ | op == TOKEN_REM_PSEUDO_OP || op == TOKEN_REMU_PSEUDO_OP -> do
    destinationRegister :: Int <- parseRegister s
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokenRegister
      then parseRegister s >>= \targetRegister -> divideInstruction s op destinationRegister sourceRegister targetRegister False
      else do
        immediate :: ImmediateExpression <- parseImmediate32 s
        if isZeroImmediate immediate
          then parseErrorAt s "Divide by zero"
          else emitI s TOKEN_ORI_OPCODE 1 0 immediate >> divideInstruction s op destinationRegister sourceRegister 1 True
  _ | op == TOKEN_MULO_PSEUDO_OP || op == TOKEN_MULOU_PSEUDO_OP -> do
    destinationRegister :: Int <- parseRegister s
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokenRegister
      then parseRegister s >>= multiplyInstruction s op destinationRegister sourceRegister
      else do
        immediate :: ImmediateExpression <- parseImmediate32 s
        if isZeroImmediate immediate
          then emitI s TOKEN_ORI_OPCODE destinationRegister 0 immediate
          else emitI s TOKEN_ORI_OPCODE 1 0 immediate >> multiplyInstruction s op destinationRegister sourceRegister 1
  _ | op == TOKEN_MFC1_D_PSEUDO_OP || op == TOKEN_MTC1_D_PSEUDO_OP -> do
    let real :: Opcode
        real = if op == TOKEN_MFC1_D_PSEUDO_OP then TOKEN_MFC1_OPCODE else TOKEN_MTC1_OPCODE
    line :: Int <- lineNo s -- the last operand can consume the newline
    r :: Int <- parseRegister s
    coprocessorOperand :: Int <- coprocessorRegister s
    if r == 31 || coprocessorOperand == 31 -- the pair's second register would be 32
      then parseErrorOnLine s line "Register pair starts at the last register (31)"
      else do
        emitFloatingPointR s real 0 coprocessorOperand r
        emitFloatingPointR s real 0 (coprocessorOperand + 1) (r + 1)
  TOKEN_LI_D_PSEUDO_OP -> do
    floatingPointDestination :: Int <- parseFloatingPointRegister s
    t :: Token <- peek s
    if t /= TokenFloatingPoint
      then parseErrorAt s "Expected FP literal"
      else do
        _ <- advance s
        bits :: Word64 <- castDoubleToWord64 <$> valueFloatingPoint s
        emitI s TOKEN_ORI_OPCODE 1 0 (constantImmediateExpression (fromIntegral bits))
        emitFloatingPointR s TOKEN_MTC1_OPCODE 0 floatingPointDestination 1
        emitI s TOKEN_ORI_OPCODE 1 0 (constantImmediateExpression (fromIntegral (bits `shiftR` 32)))
        emitFloatingPointR s TOKEN_MTC1_OPCODE 0 (floatingPointDestination + 1) 1
  TOKEN_LI_S_PSEUDO_OP -> do
    floatingPointDestination :: Int <- parseFloatingPointRegister s
    t :: Token <- peek s
    if t /= TokenFloatingPoint
      then parseErrorAt s "Expected FP literal"
      else do
        _ <- advance s
        bits :: Word32 <- castFloatToWord32 . double2Float <$> valueFloatingPoint s
        emitI s TOKEN_ORI_OPCODE 1 0 (constantImmediateExpression (fromIntegral bits))
        emitFloatingPointR s TOKEN_MTC1_OPCODE 0 floatingPointDestination 1
  TOKEN_L_D_PSEUDO_OP -> floatingPointAddress TOKEN_LDC1_OPCODE
  TOKEN_L_S_PSEUDO_OP -> floatingPointAddress TOKEN_LWC1_OPCODE
  TOKEN_S_D_PSEUDO_OP -> floatingPointAddress TOKEN_SDC1_OPCODE
  TOKEN_S_S_PSEUDO_OP -> floatingPointAddress TOKEN_SWC1_OPCODE
  _ | op == TOKEN_LD_PSEUDO_OP || op == TOKEN_SD_PSEUDO_OP -> do
    let real :: Opcode
        real = if op == TOKEN_LD_PSEUDO_OP then TOKEN_LW_OPCODE else TOKEN_SW_OPCODE
    line :: Int <- lineNo s -- the address can consume the newline
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    if targetRegister == 31 -- the pair's second register would be 32
      then parseErrorOnLine s line "Register pair starts at the last register (31)"
      else do
        emitI s real targetRegister r immediate
        emitI s real (targetRegister + 1) r (incrementExpressionOffset immediate 4)
  TOKEN_ULW_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    emitI s TOKEN_LWL_OPCODE targetRegister r (incrementExpressionOffset immediate 3)
    emitI s TOKEN_LWR_OPCODE targetRegister r immediate
  _ | op == TOKEN_ULH_PSEUDO_OP || op == TOKEN_ULHU_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    emitI s (if op == TOKEN_ULH_PSEUDO_OP then TOKEN_LB_OPCODE else TOKEN_LBU_OPCODE) targetRegister r (incrementExpressionOffset immediate 1)
    emitI s TOKEN_LBU_OPCODE 1 r immediate
    emitRShift s TOKEN_SLL_OPCODE targetRegister targetRegister 8
    emitR s TOKEN_OR_OPCODE targetRegister targetRegister 1
  TOKEN_USW_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    emitI s TOKEN_SWL_OPCODE targetRegister r (incrementExpressionOffset immediate 3)
    emitI s TOKEN_SWR_OPCODE targetRegister r immediate
  TOKEN_USH_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    emitI s TOKEN_SB_OPCODE targetRegister r immediate
    emitRShift s TOKEN_SLL_OPCODE 1 targetRegister 24
    emitRShift s TOKEN_SRL_OPCODE targetRegister targetRegister 8
    emitR s TOKEN_OR_OPCODE targetRegister targetRegister 1
    emitI s TOKEN_SB_OPCODE targetRegister r (incrementExpressionOffset immediate 1)
    emitRShift s TOKEN_SRL_OPCODE 1 targetRegister 24
    emitRShift s TOKEN_SLL_OPCODE targetRegister targetRegister 8
    emitR s TOKEN_OR_OPCODE targetRegister targetRegister 1
  TOKEN_LA_PSEUDO_OP -> do
    targetRegister :: Int <- parseRegister s
    (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
    if r /= 0 then emitI s TOKEN_ADDI_OPCODE targetRegister r immediate else emitI s TOKEN_ORI_OPCODE targetRegister 0 immediate
  _ | op == TOKEN_BEQZ_PSEUDO_OP || op == TOKEN_BNEZ_PSEUDO_OP -> do
    sourceRegister :: Int <- parseRegister s
    target :: ImmediateExpression <- parseLabel s
    emitI s (if op == TOKEN_BEQZ_PSEUDO_OP then TOKEN_BEQ_OPCODE else TOKEN_BNE_OPCODE) 0 sourceRegister target
  _ | op == TOKEN_B_PSEUDO_OP || op == TOKEN_BAL_PSEUDO_OP -> do
    target :: ImmediateExpression <- parseLabel s
    emitI s (if op == TOKEN_BAL_PSEUDO_OP then TOKEN_BGEZAL_OPCODE else TOKEN_BGEZ_OPCODE) 0 0 target
  _ | op == TOKEN_BGT_PSEUDO_OP || op == TOKEN_BGTU_PSEUDO_OP -> do
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokenRegister
      then do
        targetRegister :: Int <- parseRegister s
        target :: ImmediateExpression <- parseLabel s
        emitR s (if op == TOKEN_BGT_PSEUDO_OP then TOKEN_SLT_OPCODE else TOKEN_SLTU_OPCODE) 1 targetRegister sourceRegister
        emitI s TOKEN_BNE_OPCODE 0 1 target
      else do
        immediate :: ImmediateExpression <- parseImmediate32 s
        target :: ImmediateExpression <- parseLabel s
        if op == TOKEN_BGT_PSEUDO_OP
          then do
            emitI s TOKEN_SLTI_OPCODE 1 sourceRegister (incrementExpressionOffset immediate 1)
            emitI s TOKEN_BEQ_OPCODE 0 1 target
          else do
            emitI s TOKEN_ORI_OPCODE 1 0 immediate
            emitI s TOKEN_BEQ_OPCODE sourceRegister 1 (branchOffsetExpression 3)
            emitR s TOKEN_SLTU_OPCODE 1 sourceRegister 1
            emitI s TOKEN_BEQ_OPCODE 0 1 target
  _
    | op == TOKEN_BGE_PSEUDO_OP || op == TOKEN_BGEU_PSEUDO_OP ->
        branchCompare
          (if op == TOKEN_BGE_PSEUDO_OP then TOKEN_SLT_OPCODE else TOKEN_SLTU_OPCODE)
          (if op == TOKEN_BGE_PSEUDO_OP then TOKEN_SLTI_OPCODE else TOKEN_SLTIU_OPCODE)
          TOKEN_BEQ_OPCODE
  _
    | op == TOKEN_BLT_PSEUDO_OP || op == TOKEN_BLTU_PSEUDO_OP ->
        branchCompare
          (if op == TOKEN_BLT_PSEUDO_OP then TOKEN_SLT_OPCODE else TOKEN_SLTU_OPCODE)
          (if op == TOKEN_BLT_PSEUDO_OP then TOKEN_SLTI_OPCODE else TOKEN_SLTIU_OPCODE)
          TOKEN_BNE_OPCODE
  _ | op == TOKEN_BLE_PSEUDO_OP || op == TOKEN_BLEU_PSEUDO_OP -> do
    sourceRegister :: Int <- parseRegister s
    t :: Token <- peek s
    if t == TokenRegister
      then do
        targetRegister :: Int <- parseRegister s
        target :: ImmediateExpression <- parseLabel s
        emitR s (if op == TOKEN_BLE_PSEUDO_OP then TOKEN_SLT_OPCODE else TOKEN_SLTU_OPCODE) 1 targetRegister sourceRegister
        emitI s TOKEN_BEQ_OPCODE 0 1 target
      else do
        immediate :: ImmediateExpression <- parseImmediate32 s
        target :: ImmediateExpression <- parseLabel s
        if op == TOKEN_BLE_PSEUDO_OP
          then do
            emitI s TOKEN_SLTI_OPCODE 1 sourceRegister (incrementExpressionOffset immediate 1)
            emitI s TOKEN_BNE_OPCODE 0 1 target
          else do
            emitI s TOKEN_ORI_OPCODE 1 0 immediate
            emitI s TOKEN_BEQ_OPCODE sourceRegister 1 target
            emitR s TOKEN_SLTU_OPCODE 1 sourceRegister 1
            emitI s TOKEN_BNE_OPCODE 0 1 target
  _ -> parseErrorAt s "Pseudo-op not yet supported" >> syncToNewline s
  where
    twoRegisters :: (Int -> Int -> IO b) -> IO b
    twoRegisters f = do
      destinationRegister :: Int <- parseRegister s
      sourceRegister :: Int <- parseRegister s
      f destinationRegister sourceRegister
    floatingPointAddress :: Opcode -> IO ()
    floatingPointAddress real = do
      floatingPointAddend :: Int <- parseFloatingPointRegister s
      (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
      emitI s real floatingPointAddend r immediate
    -- bge/bgeu/blt/bltu: set $at by a comparison, then branch on it.
    branchCompare :: Opcode -> Opcode -> Opcode -> IO ()
    branchCompare slt slti br = do
      sourceRegister :: Int <- parseRegister s
      t :: Token <- peek s
      if t == TokenRegister
        then do
          targetRegister :: Int <- parseRegister s
          target :: ImmediateExpression <- parseLabel s
          emitR s slt 1 sourceRegister targetRegister
          emitI s br 0 1 target
        else do
          immediate :: ImmediateExpression <- parseImmediate32 s
          target :: ImmediateExpression <- parseLabel s
          emitI s slti 1 sourceRegister immediate
          emitI s br 0 1 target

-- | A coprocessor register, written as @$n@ or @$fn@.
coprocessorRegister :: Spim -> IO Int
coprocessorRegister s = do
  t :: Token <- peek s
  if t == TokenFloatingPointRegister then parseFloatingPointRegister s else parseRegister s

-- | One instruction (C: @parse_asm_code@), dispatched on its operand shape.
parseAssemblyCode :: Spim -> IO ()
parseAssemblyCode s = do
  t :: Token <- advance s
  case t of
    TokenOpcode op -> parseOpcode op (findOpcodeType op)
    _ -> unsupported
  where
    unsupported :: IO ()
    unsupported = parseErrorAt s "Instruction not yet supported" >> syncToNewline s
    parseOpcode :: Opcode -> OpType -> IO ()
    parseOpcode op ty = case ty of
      NO_ARGUMENT_TYPE_INSTRUCTION -> parseNoArgument s op
      DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> parseR3 s op
      DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION -> parseTwoRegistersShiftAmount s op
      SOURCE_REGISTER_INSTRUCTION -> parseRegister s >>= \sourceRegister -> emitR s op 0 sourceRegister 0
      TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> parseI2 s op
      TARGET_REGISTER_IMMEDIATE_INSTRUCTION -> do
        targetRegister :: Int <- parseRegister s
        parseUnsignedImmediate16 s >>= emitI s op targetRegister 0
      TARGET_REGISTER_ADDRESS_INSTRUCTION -> parseI2a s op
      BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION -> parseB2 s op
      BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION -> do
        sourceRegister :: Int <- parseRegister s
        parseLabel s >>= emitI s op 0 sourceRegister
      J_TYPE_INSTRUCTION -> parseJ s op
      PSEUDO_OP -> parsePseudo s op
      TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> coprocessorOperand op
      DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION -> parseJ s op
      SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> do
        r1 :: Int <- parseRegister s
        t :: Token <- peek s
        if t /= TokenRegister && t /= TokenInteger && t /= TokenIdentifier
          then parseErrorAt s "Expected operand for div/mult"
          else
            if op `elem` [TOKEN_TEQ_OPCODE, TOKEN_TGE_OPCODE, TOKEN_TGEU_OPCODE, TOKEN_TLT_OPCODE, TOKEN_TLTU_OPCODE, TOKEN_TNE_OPCODE]
              then parseRegister s >>= emitR s op 0 r1
              else
                if op == TOKEN_DIV_OPCODE || op == TOKEN_DIVU_OPCODE
                  then do
                    r2 :: Int <- parseRegister s
                    t2 :: Token <- peek s
                    if t2 == TokenNewline || t2 == TokenEOF
                      then emitR s op 0 r1 r2
                      else
                        if t2 == TokenRegister
                          then parseRegister s >>= \r3 -> divideInstruction s op r1 r2 r3 False
                          else do
                            immediate :: ImmediateExpression <- parseImmediate32 s
                            if isZeroImmediate immediate
                              then parseErrorAt s "Divide by zero"
                              else emitI s TOKEN_ORI_OPCODE 1 0 immediate >> divideInstruction s op r1 r2 1 True
                  else parseRegister s >>= emitR s op 0 r1
      DESTINATION_REGISTER_INSTRUCTION -> parseRegister s >>= \destinationRegister -> emitR s op destinationRegister 0 0
      DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION -> do
        destinationRegister :: Int <- parseRegister s
        sourceRegister :: Int <- parseRegister s
        t :: Token <- peek s
        if t == TokenRegister
          then parseRegister s >>= \targetRegister -> emitR s op destinationRegister targetRegister sourceRegister
          else
            if t /= TokenInteger
              then parseErrorAt s "Expected register or integer shift"
              else do
                _ <- advance s
                sh :: Int <- int <$> valueInteger s
                immediateOpcode :: Opcode <- opcodeToImmediateOpcode op
                emitRShift s immediateOpcode destinationRegister sourceRegister sh
      SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> do
        sourceRegister :: Int <- parseRegister s
        parseImmediate16 s >>= emitI s op 0 sourceRegister
      BRANCH_COPROCESSOR_CONDITION_INSTRUCTION -> do
        let nd :: Int
            nd = if opcodeIsNullifiedBranch op then 1 else 0
            tf :: Int
            tf = if opcodeIsTrueBranch op then 1 else 0
        t :: Token <- peek s
        conditionCode :: Int <- if t == TokenInteger then advance s >> int <$> valueInteger s else pure 0
        target :: ImmediateExpression <- parseLabel s
        emitI s op ((conditionCode `shiftL` 2) .|. (nd `shiftL` 1) .|. tf) 0 target
      MOVE_CONDITIONAL_TYPE_INSTRUCTION -> do
        destinationRegister :: Int <- parseRegister s
        sourceRegister :: Int <- parseRegister s
        t :: Token <- peek s
        conditionCode :: Int <- if t == TokenInteger then advance s >> int <$> valueInteger s else pure 0
        emitR s op destinationRegister sourceRegister ((conditionCode .&. 7) `shiftL` 2)
      FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION -> do
        floatingPointDestination :: Int <- parseFloatingPointRegister s
        floatingPointSource :: Int <- parseFloatingPointRegister s
        emitFloatingPointR s op floatingPointDestination floatingPointSource 0
      FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION -> do
        floatingPointDestination :: Int <- parseFloatingPointRegister s
        floatingPointSource :: Int <- parseFloatingPointRegister s
        floatingPointTarget :: Int <- parseFloatingPointRegister s
        emitFloatingPointR s op floatingPointDestination floatingPointSource floatingPointTarget
      FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION -> do
        t :: Token <- peek s
        conditionCode :: Int <- if t == TokenInteger then advance s >> int <$> valueInteger s else pure 0
        floatingPointSource :: Int <- parseFloatingPointRegister s
        floatingPointTarget :: Int <- parseFloatingPointRegister s
        emitFloatingPointCompare s op floatingPointSource floatingPointTarget conditionCode
      FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION -> do
        floatingPointDestination :: Int <- parseFloatingPointRegister s
        floatingPointSource :: Int <- parseFloatingPointRegister s
        t :: Token <- peek s
        case t of
          TokenRegister -> parseRegister s >>= emitFloatingPointR s op floatingPointDestination floatingPointSource
          TokenInteger -> do
            _ <- advance s
            conditionCode :: Int <- int <$> valueInteger s
            emitFloatingPointR s op floatingPointDestination floatingPointSource ((conditionCode .&. 7) `shiftL` 2)
          _ -> emitFloatingPointR s op floatingPointDestination floatingPointSource 0
      FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION -> do
        floatingPointAddend :: Int <- parseFloatingPointRegister s
        (r, immediate) :: (Int, ImmediateExpression) <- parseAddress s
        emitI s op floatingPointAddend r immediate
      FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> coprocessorOperand op
      _ -> unsupported
    coprocessorOperand :: Opcode -> IO ()
    coprocessorOperand op = do
      r :: Int <- parseRegister s
      c :: Int <- coprocessorRegister s
      emitFloatingPointR s op 0 c r

-- Directives.

-- | @.data@/@.kdata@/@.text@/@.ktext@ with an optional address
-- (C: @parse_dir_data@, @parse_dir_text@, @emit_dir_seg@).
parseDirectiveSegment :: Spim -> (Maybe Word32 -> NodeKind) -> Bool -> IO ()
parseDirectiveSegment s kind isText = do
  t :: Token <- peek s
  address :: Maybe Word32 <- if t == TokenInteger then advance s >> Just . fromIntegral <$> valueInteger s else pure Nothing
  if isText
    then set (dataDirective s) False >> set (textDirective s) True
    else set (dataDirective s) True >> set (textDirective s) False >> set (autoAlign s) True
  appendNode s (kind address)

-- | An identifier after a directive, or an error.
directiveName :: Spim -> String -> IO (Maybe String)
directiveName s message = do
  scannerForceIdentifier s
  t :: Token <- peek s
  if t /= TokenIdentifier
    then parseErrorAt s message >> pure Nothing
    else advance s >> Just <$> valueString s

-- | Labels on this line move to where the datum will start
-- (C: @align_labels_to@).
alignLabelsTo :: Spim -> Int -> IO ()
alignLabelsTo s n = do
  inData :: Bool <- get (dataDirective s)
  auto :: Bool <- get (autoAlign s)
  ls :: [Label] <- get (thisLineLabels s)
  when (inData && auto && not (null ls)) $ do
    current :: Word32 <- currentDataLocation s
    fixCurrentLabelAddress s ((current + (1 `shiftL` n) - 1) .&. (0xffffffff `shiftL` n))

-- | Align for a datum if in the data segment.
alignForData :: Spim -> Int -> IO ()
alignForData s n = do
  alignLabelsTo s n
  inData :: Bool <- get (dataDirective s)
  when inData $ setDataAlignment s n

-- | A list of integer expressions (C: @parse_expr_list@); stops at an
-- error after skipping the rest of the line.
parseExpressionList :: Spim -> ([ImmediateExpression] -> NodeKind) -> IO ()
parseExpressionList s kind = do
  line :: Int <- lineNo s
  values :: [ImmediateExpression] <- loop []
  unless (null values) $ do
    node :: Node <- makeNode s (kind (reverse values))
    appendBuilt s node {nodeLine = line}
  where
    loop :: [ImmediateExpression] -> IO [ImmediateExpression]
    loop acc = do
      before :: Int <- get (parseErrorsSeen s)
      v :: Int32 <- parseExpression s
      after :: Int <- get (parseErrorsSeen s)
      if after /= before
        then syncToNewline s >> pure acc
        else do
          pending :: Maybe Label <- get (expressionUnresolvedLabel s)
          e :: ImmediateExpression <- makeImmediateExpression s v (labelName <$> pending) False
          set (expressionUnresolvedLabel s) Nothing
          t :: Token <- peek s
          if t == TokenNewline || t == TokenEOF then pure (e : acc) else loop (e : acc)

-- | A list of floating-point literals (C: @parse_fp_expr_list@).
parseFloatingPointExpressionList :: Spim -> ([Double] -> NodeKind) -> IO ()
parseFloatingPointExpressionList s kind = do
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
      if t /= TokenFloatingPoint
        then parseErrorAt s "Expected floating-point literal" >> pure Nothing
        else do
          _ <- advance s
          v :: Double <- valueFloatingPoint s
          t2 :: Token <- peek s
          if t2 == TokenNewline || t2 == TokenEOF then pure (Just (v : acc)) else loop (v : acc)

-- | Strings for @.ascii@/@.asciiz@ (C: @parse_string_list@).  The string
-- ends at its first NUL, as C's @strlen@ does.
parseStringList :: Spim -> IO ()
parseStringList s = do
  t :: Token <- peek s
  if t /= TokenString
    then parseErrorAt s "Expected string literal"
    else do
      _ <- advance s
      string :: String <- takeWhile (/= '\0') <$> valueString s
      inText :: Bool <- get (textDirective s)
      nul :: Bool <- get (nullTerm s)
      if inText
        then parseErrorAt s "Can't put data in text segment"
        else appendNode s (DataString string nul)
      t2 :: Token <- peek s
      unless (t2 == TokenNewline || t2 == TokenEOF) $ parseStringList s

-- | One directive (C: @parse_directive@).
parseDirective :: Spim -> Opcode -> IO ()
parseDirective s d = case d of
  TOKEN_DATA_DIRECTIVE -> parseDirectiveSegment s DirectiveData False
  TOKEN_K_DATA_DIRECTIVE -> parseDirectiveSegment s DirectiveKdata False
  TOKEN_TEXT_DIRECTIVE -> parseDirectiveSegment s DirectiveText True
  TOKEN_K_TEXT_DIRECTIVE -> parseDirectiveSegment s DirectiveKtext True
  TOKEN_GLOBAL_DIRECTIVE -> directiveName s "Expected identifier after .globl" >>= mapM_ (appendNode s . DirectiveGlobl)
  TOKEN_WORD_DIRECTIVE -> alignForData s 2 >> parseExpressionList s DataWord
  TOKEN_HALF_DIRECTIVE -> alignForData s 1 >> parseExpressionList s DataHalf
  TOKEN_BYTE_DIRECTIVE -> parseExpressionList s DataByte
  TOKEN_ASCII_DIRECTIVE -> set (nullTerm s) False >> parseStringList s
  TOKEN_ASCIIZ_DIRECTIVE -> set (nullTerm s) True >> parseStringList s
  TOKEN_FLOAT_DIRECTIVE -> alignForData s 2 >> parseFloatingPointExpressionList s DataFloat
  TOKEN_DOUBLE_DIRECTIVE -> alignForData s 3 >> parseFloatingPointExpressionList s DataDouble
  TOKEN_SPACE_DIRECTIVE -> do
    v :: Int32 <- parseExpression s
    inText :: Bool <- get (textDirective s)
    if inText then parseErrorAt s "Can't put data in text segment" else appendNode s (DirectiveSpace (int v))
  TOKEN_ALIGN_DIRECTIVE -> do
    line :: Int <- lineNo s -- the expression can consume the newline
    v :: Int32 <- parseExpression s
    if v < 0 || v > 31 -- C shifts a 32-bit mask by it
      then parseErrorOnLine s line "Alignment out of range (0 .. 31)"
      else do
        when (v == 0) $ set (autoAlign s) False
        appendNode s (DirectiveAlign (int v))
  TOKEN_COMM_DIRECTIVE -> namedSize DirectiveComm
  TOKEN_EXTERN_DIRECTIVE -> namedSize DirectiveExtern
  TOKEN_ERR_DIRECTIVE -> parseErrorAt s ".err directive"
  _
    | d `elem` ignored -> syncToNewline s
    | otherwise -> parseErrorAt s "Directive not yet supported" >> syncToNewline s
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
      [ TOKEN_FILE_DIRECTIVE,
        TOKEN_LOC_DIRECTIVE,
        TOKEN_FRAME_DIRECTIVE,
        TOKEN_MASK_DIRECTIVE,
        TOKEN_FMASK_DIRECTIVE,
        TOKEN_ENT_DIRECTIVE,
        TOKEN_END_DIRECTIVE,
        TOKEN_LABEL_DIRECTIVE,
        TOKEN_LIVEREG_DIRECTIVE,
        TOKEN_OPTIONS_DIRECTIVE,
        TOKEN_BGNB_DIRECTIVE,
        TOKEN_ENDB_DIRECTIVE,
        TOKEN_ENDR_DIRECTIVE,
        TOKEN_ASM0_DIRECTIVE,
        TOKEN_ALIAS_DIRECTIVE,
        TOKEN_SET_DIRECTIVE
      ]

-- | @name:@ or @name = expression@ (C: @parse_opt_label@).
parseOptionalLabel :: Spim -> IO ()
parseOptionalLabel s = do
  _ <- advance s
  name :: String <- valueString s
  separator :: Token <- advance s
  if separator == TokenChar ':'
    then appendNode s (LabelDefinition name LabelNormal)
    else do
      v :: Int32 <- parseExpression s
      appendNode s (LabelDefinition name (LabelConstant v))

isDirective :: Token -> Bool
isDirective (TokenOpcode op) = findOpcodeType op == ASSEMBLER_DIRECTIVE
isDirective _ = False

-- | One line (C: @parse_line@).
parseLine :: Spim -> IO ()
parseLine s = do
  t0 :: Token <- peek s
  case t0 of
    TokenNewline -> () <$ advance s
    TokenEOF -> pure ()
    _ -> do
      t1 :: Token <- if t0 == TokenIdentifier then peek2 s else pure TokenEOF
      when (t0 == TokenIdentifier && (t1 == TokenChar ':' || t1 == TokenChar '=')) $ parseOptionalLabel s
      t :: Token <- peek s
      case t of
        TokenNewline -> () <$ advance s
        TokenEOF -> pure ()
        TokenOpcode d | isDirective t -> do
          _ <- advance s
          parseDirective s d
          clearLabels s
          finish
        _ -> parseAssemblyCode s >> clearLabels s >> finish
  where
    finish :: IO ()
    finish = do
      errorOccurred :: Bool <- get (parseErrorOccurred s)
      t :: Token <- peek s
      if errorOccurred
        then syncToNewline s
        else
          if t == TokenNewline
            then () <$ advance s
            else unless (t == TokenEOF) $ parseErrorAt s "Extra tokens after instruction" >> syncToNewline s

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
  -- and the parse itself moves dataDirective/textDirective as it meets .data and .text.
  emitted :: Int <- length <$> get (astScope s)
  startData :: Bool <- get (dataDirective s)
  startText :: Bool <- get (textDirective s)
  let loop :: IO ()
      loop = do
        t :: Token <- peek s
        unless (t == TokenEOF) $ do
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
  when pAst $ writeError (printAst root)
  pExp :: Bool <- get (showExpansionFlag s)
  when pExp $ forM_ kids $ \k -> case nodeKind k of
    Pseudo _ _ -> writeError (printAst k)
    _ -> pure ()
  pJson :: Bool <- get (printAstJsonFlag s)
  when pJson $ writeOut (printAstJson root)
  only :: Bool <- get (printAstOnly s)
  unless only $ do
    set (dataDirective s) startData
    set (textDirective s) startText
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
    LabelDefinition _ _ -> pure ()
    DirectiveGlobl _ -> pure ()
    _ -> clearLabels s

-- | Commit one statement (C: @emit_one@).
emitOne :: Spim -> Node -> IO ()
emitOne s node = do
  setLineNo s (nodeLine node)
  set (emitSource s) (Just (nodeSource node))
  case nodeKind node of
    File _ _ -> pure ()
    DirectiveText a -> segmentText False a
    DirectiveKtext a -> segmentText True a
    DirectiveData a -> segmentData False a
    DirectiveKdata a -> segmentData True a
    DirectiveAlign v -> do
      when (v == 0) $ set (autoAlign s) False
      inText :: Bool <- get (textDirective s)
      if inText then alignText s v else alignData s v
    DirectiveSpace n -> incrementDataLocation s n
    DirectiveGlobl name -> () <$ makeLabelGlobal s name
    DirectiveExtern name size -> do
      _ <- makeLabelGlobal s name
      defineIfNew name
      incrementDataLocation s size
    DirectiveComm name size -> do
      alignData s 2
      defineIfNew name
      incrementDataLocation s size
    DataByte es -> emitData es (storeByte s) 1
    DataHalf es -> alignLabelsTo s 1 >> alignIfData 1 >> emitData es (storeHalf s) 2
    DataWord es -> alignLabelsTo s 2 >> alignIfData 2 >> emitData es (storeWord s) 4
    DataFloat vs -> alignLabelsTo s 2 >> alignIfData 2 >> mapM_ (storeFloat s) vs
    DataDouble vs -> alignLabelsTo s 3 >> alignIfData 3 >> mapM_ (storeDouble s) vs
    DataString bytes nul -> do
      inText :: Bool <- get (textDirective s)
      unless inText $ storeString s bytes nul
    LabelDefinition name LabelNormal -> do
      inText :: Bool <- get (textDirective s)
      address :: Word32 <- if inText then currentTextLocation s else currentDataLocation s
      l :: Label <- recordLabel s name address False
      modify (thisLineLabels s) (l :)
    LabelDefinition name (LabelConstant v) -> do
      l :: Label <- recordLabel s name (fromIntegral v) True
      set (labelConstant l) True
    InstructionR op destinationRegister sourceRegister targetRegister -> rType s op destinationRegister sourceRegister targetRegister
    InstructionRShift op destinationRegister targetRegister sh -> rShiftAmountType s op destinationRegister targetRegister sh
    InstructionI op targetRegister sourceRegister immediate -> iType s op targetRegister sourceRegister immediate
    InstructionJ op target -> jType s op target
    InstructionFloatingPointR op floatingPointDestination floatingPointSource floatingPointTarget -> rCoprocessorType s op floatingPointDestination floatingPointSource floatingPointTarget
    InstructionFloatingPointCompare op floatingPointSource floatingPointTarget conditionCode -> rConditionType s op floatingPointSource floatingPointTarget conditionCode
    Pseudo _ kids -> emitList s kids
  where
    segmentText :: (Foldable t) => Bool -> t Word32 -> IO ()
    segmentText kernel address = do
      userKernelTextSegment s kernel
      set (dataDirective s) False
      set (textDirective s) True
      mapM_ (setTextLocation s) address
    segmentData :: (Foldable t) => Bool -> t Word32 -> IO ()
    segmentData kernel address = do
      userKernelDataSegment s kernel
      set (dataDirective s) True
      set (textDirective s) False
      enableDataAlignment s
      set (autoAlign s) True
      mapM_ (setDataLocation s) address
    alignIfData :: Int -> IO ()
    alignIfData n = do
      inData :: Bool <- get (dataDirective s)
      when inData $ setDataAlignment s n
    defineIfNew :: String -> IO ()
    defineIfNew name = do
      l :: Label <- lookupLabel s name
      address :: Word32 <- get (labelAddress l)
      when (address == 0) $ currentDataLocation s >>= \programCounter -> () <$ recordLabel s name programCounter True
    -- A value naming a label not yet defined is stored as its offset and
    -- patched when the label is defined.
    emitData :: (Foldable t) => t ImmediateExpression -> (Int32 -> IO b) -> Int -> IO ()
    emitData es store width = forM_ es $ \e -> do
      defined :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol e)
      case immediateExpressionSymbol e of
        Just l | not defined -> do
          programCounter :: Word32 <- currentDataLocation s
          recordDataUsesSymbol s programCounter l (immediateExpressionOffset e) width
          store (immediateExpressionOffset e)
        _ -> evaluateImmediateExpression s e >>= store
