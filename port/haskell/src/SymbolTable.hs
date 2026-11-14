-- | The symbol table (C: src/symbol-table.c, and @eval_imm_expr@ from
-- src/instruction.c).
--
-- Labels live in a hash table of 8191 buckets, using the C engine's hash
-- function, because @print_symbols@ and the undefined-symbol report list
-- labels in bucket order.  A label used before it is defined collects
-- "uses"; when the label is defined, each use is patched.
module SymbolTable
  ( initializeSymbolTable,
    labelIsDefined,
    lookupLabel,
    recordLabel,
    makeLabelGlobal,
    recordInstructionUsesSymbol,
    recordDataUsesSymbol,
    resolveLabelUses,
    resolveALabel,
    flushLocalLabels,
    findSymbolAddress,
    printSymbols,
    undefinedSymbolString,
    allLabels,
    evaluateImmediateExpression,
    symbolIsDefined,
  )
where

import AsmCommon
import Control.Monad (forM, forM_, unless, when)
import Data.Array.IO (readArray, writeArray)
import Data.Bits (complement, shiftR, (.&.))
import Data.Char (ord)
import Data.IORef (newIORef)
import Data.Int (Int32)
import Data.Word (Word32)
import InstFormat (printInstruction)
import Instruction
import Memory
import Opcodes
import Output (cError, writeError)
import State
import Text.Printf (printf)
import Types

tableSize :: Int
tableSize = 8191

-- | The bucket for a name (C: @get_hash@).  Characters are C's signed
-- @char@, so a byte of 128 or more counts as negative.
hashOf :: String -> Int
hashOf name = fromIntegral ((h .&. (2 ^ (30 :: Int) - 1)) `rem` fromIntegral tableSize)
  where
    h :: Word32
    h = foldl step (fromIntegral (length name)) name :: Word32
    step :: (Num a) => a -> Char -> a
    step acc c = acc * 613 + signedChar c
    signedChar :: (Num b) => Char -> b
    signedChar c =
      let b :: Int
          b = ord c
       in if b >= 128 then fromIntegral (b - 256 :: Int) else fromIntegral b

-- | Empty the table (C: @initialize_symbol_table@).
initializeSymbolTable :: Spim -> IO ()
initializeSymbolTable s = do
  forM_ [0 .. tableSize - 1] $ \i -> writeArray (labelTable s) i []
  set (localLabels s) []

-- | The label with this name, if it is in the table (C: @label_is_defined@,
-- which despite its name does not check that the label has an address).
labelIsDefined :: Spim -> String -> IO (Maybe Label)
labelIsDefined s name = do
  bucket :: [Label] <- readArray (labelTable s) (hashOf name)
  pure
    ( case filter ((== name) . labelName) bucket of
        (l : _) -> Just l
        [] -> Nothing
    )

-- | The label with this name, created (undefined) if it is new
-- (C: @lookup_label@).
lookupLabel :: Spim -> String -> IO Label
lookupLabel s name = do
  found :: Maybe Label <- labelIsDefined s name
  case found of
    Just l -> pure l
    Nothing -> do
      n :: Int <- get (labelCounter s)
      set (labelCounter s) (n + 1)
      l :: Label <- Label n name <$> newIORef 0 <*> newIORef False <*> newIORef False <*> newIORef False <*> newIORef []
      let h :: Int
          h = hashOf name
      bucket :: [Label] <- readArray (labelTable s) h
      writeArray (labelTable s) h (l : bucket)
      pure l

symbolIsDefined :: Label -> IO Bool
symbolIsDefined l = (/= 0) <$> get (labelAddress l)

-- | Define a label at an address (C: @record_label@).
recordLabel :: Spim -> String -> Word32 -> Bool -> IO Label
recordLabel s name effectiveAddress resolve = do
  l :: Label <- lookupLabel s name
  gp :: Bool <- get (labelGlobalPointer l)
  old :: Word32 <- get (labelAddress l)
  if not gp && old /= 0
    then parseError s "Label is defined for the second time" >> pure l
    else do
      unless gp $ set (labelAddress l) effectiveAddress
      global :: Bool <- get (labelGlobal l)
      fire s (EventLabelDefinition effectiveAddress (labelName l) global)
      when resolve $ resolveLabelUses s l
      unless global $ modify (localLabels s) (l :)
      pure l

-- | C: @make_label_global@.
makeLabelGlobal :: Spim -> String -> IO Label
makeLabelGlobal s name = do
  l :: Label <- lookupLabel s name
  set (labelGlobal l) True
  pure l

-- | Remember that an instruction uses a label not yet defined
-- (C: @record_inst_uses_symbol@).  An instruction assembled into the data
-- segment is copied, since the original is stored as a word and dropped.
recordInstructionUsesSymbol :: Spim -> InstructionRef -> Label -> IO ()
recordInstructionUsesSymbol s instruction l = do
  inData :: Bool <- get (dataDirective s)
  (ref, at) :: (InstructionRef, Word32) <-
    if inData
      then do
        copy :: InstructionRef <- get instruction >>= newIORef
        (,) copy <$> currentDataLocation s
      else (,) instruction <$> currentTextLocation s
  modify (labelUses l) (UseInstruction ref at :)
  fire s (EventForwardReference at (labelName l))

-- | Remember that a datum uses a label not yet defined
-- (C: @record_data_uses_symbol@).
recordDataUsesSymbol :: Spim -> Word32 -> Label -> Int32 -> Int -> IO ()
recordDataUsesSymbol s at l offset width = do
  modify (labelUses l) (UseData at offset width :)
  fire s (EventForwardReference at (labelName l))

-- | Patch every use of a label that has just been defined
-- (C: @resolve_label_uses@).
resolveLabelUses :: Spim -> Label -> IO ()
resolveLabelUses s l = do
  uses :: [LabelUse] <- get (labelUses l)
  address :: Word32 <- get (labelAddress l)
  sbot :: Word32 <- get (stackBottom s)
  forM_ uses $ \use -> do
    at :: Word32 <- case use of
      UseData at offset width -> do
        let value :: Int32
            value = fromIntegral address + offset :: Int32
        forM_ [0 .. width - 1] $ \i ->
          memoryWriteByte s (at + fromIntegral i) (fromIntegral ((fromIntegral value :: Word32) `shiftR` (8 * i)) .&. 0xff)
        pure at
      UseInstruction instruction at -> do
        resolveSubtract s l instruction at
        when (at >= dataBottom && at < sbot) $ do
          encoding :: Int32 <- get instruction >>= instructionEncodeIO
          memoryWriteWord s at encoding
        pure at
    fire s (EventForwardResolved at (labelName l) (fromIntegral address))
  set (labelUses l) []

-- | Fill in a label in an instruction assembled at the current location
-- (C: @resolve_a_label@).
resolveALabel :: Spim -> Maybe Label -> InstructionRef -> IO ()
resolveALabel s l instruction = do
  inData :: Bool <- get (dataDirective s)
  programCounter :: Word32 <- if inData then currentDataLocation s else currentTextLocation s
  resolveSubtractMaybe s l instruction programCounter

resolveSubtract :: Spim -> Label -> InstructionRef -> Word32 -> IO ()
resolveSubtract s l = resolveSubtractMaybe s (Just l)

-- | Compute an instruction's immediate field from its now-known expression
-- (C: @resolve_a_label_sub@): a branch gets a word offset, a jump a 26-bit
-- target, anything else the low 16 bits.
resolveSubtractMaybe :: Spim -> Maybe Label -> InstructionRef -> Word32 -> IO ()
resolveSubtractMaybe s _ instruction programCounter = do
  i0 :: Instruction <- get instruction
  case instructionExpression i0 of
    Nothing -> pure ()
    Just e0 -> resolveExpression s instruction i0 e0 programCounter

resolveExpression :: Spim -> InstructionRef -> Instruction -> ImmediateExpression -> Word32 -> IO ()
resolveExpression s instruction i0 e0 programCounter = do
  let e :: ImmediateExpression
      e = if immediateExpressionProgramCounterRelative e0 then e0 {immediateExpressionOffset = fromIntegral (0 - programCounter)} else e0
  set instruction i0 {instructionExpression = Just e}
  defined :: Bool <- maybe (pure True) symbolIsDefined (immediateExpressionSymbol e)
  if not defined
    then cError ("Resolving undefined symbol: " ++ maybe "" labelName (immediateExpressionSymbol e) ++ "\n")
    else do
      let op :: Int
          op = instructionOpcode i0
      (value, fieldMask) :: (Int32, Int32) <-
        if opcodeIsBranch op
          then do
            byteOffset :: Int32 <- evaluateImmediateExpression s e {immediateExpressionBits = 0}
            delayed :: Bool <- get (optionDelayedBranches s)
            let wordOffset :: Int32
                wordOffset = (byteOffset `quot` 4) - (if delayed then 1 else 0)
            when (wordOffset < -32768 || wordOffset > 32767) $
              cError (printf "Branch target is out of range (more than 128 KB away) at pc 0x%x\n" programCounter)
            pure (fromIntegral (fromIntegral wordOffset .&. (0xffff :: Word32)), 0xffff)
          else
            if opcodeIsJump op
              then do
                v :: Int32 <- evaluateImmediateExpression s e
                when ((fromIntegral v .&. 0xf0000000) /= (programCounter .&. 0xf0000000)) $
                  cError (printf "Target of jump differs in high-order 4 bits from instruction pc 0x%x\n" programCounter)
                pure ((v .&. 0x0fffffff) `shiftR` 2, -1)
              else do
                v :: Int32 <- evaluateImmediateExpression s e
                when (opcodeIsLoadStore op && v .&. 0x8000 /= 0) $ compensateLui s e programCounter
                pure (v, 0xffff)
      let high :: Int32
          high = value .&. complement fieldMask
      when (high /= 0 && high /= fromIntegral (0xffff0000 :: Word32)) $ do
        cError "Immediate value is too large for field: "
        printInstruction s programCounter
      i1 :: Instruction <- get instruction
      let i2 :: Instruction
          i2 = if opcodeIsJump op then setTarget (fromIntegral value) i1 else setImmediate value i1
      encoding :: Int32 <- instructionEncodeIO i2
      set instruction i2 {instructionEncoding = encoding}

-- | A load or store sign-extends its 16-bit offset, so when the low half
-- of the address is "negative", the @lui@ that loaded the high half (one
-- or two instructions back) must load one more.
compensateLui :: Spim -> ImmediateExpression -> Word32 -> IO ()
compensateLui s e programCounter = do
  previous :: Maybe InstructionRef <- memoryReadInstruction s (programCounter - 4)
  previousPrevious :: Maybe InstructionRef <- memoryReadInstruction s (programCounter - 8)
  okPrevious :: Bool <- isPlainLui previous
  if okPrevious
    then bump previous
    else do
      okPreviousPrevious :: Bool <- isPlainLui previousPrevious
      when okPreviousPrevious $ bump previousPrevious
  where
    isPlainLui :: Maybe InstructionRef -> IO Bool
    isPlainLui Nothing = pure False
    isPlainLui (Just ref) = do
      i :: Instruction <- get ref
      pure $ case instructionExpression i of
        Just pe -> isToken TOKEN_LUI_OPCODE (instructionOpcode i) && immediateExpressionSymbol e == immediateExpressionSymbol pe && instructionImmediate i == 0
        Nothing -> False
    bump :: Maybe InstructionRef -> IO ()
    bump Nothing = pure ()
    bump (Just ref) = modify ref (\i -> i {instructionExpression = fmap (\pe -> pe {immediateExpressionOffset = immediateExpressionOffset pe + 0x10000}) (instructionExpression i)})

-- | The value of an immediate expression (C: @eval_imm_expr@).
evaluateImmediateExpression :: Spim -> ImmediateExpression -> IO Int32
evaluateImmediateExpression _ e = do
  value :: Int32 <- case immediateExpressionSymbol e of
    Nothing -> pure (immediateExpressionOffset e)
    Just l -> do
      address :: Word32 <- get (labelAddress l)
      if address /= 0
        then pure (immediateExpressionOffset e + fromIntegral address)
        else do
          cError ("Evaluated undefined symbol: " ++ labelName l ++ "\n")
          pure 0
  pure $
    if immediateExpressionBits e > 0
      then (value `shiftR` 16) .&. 0xffff
      else if immediateExpressionBits e < 0 then value .&. 0xffff else value

-- | At the end of a file, drop its non-global labels from the table so
-- the next file cannot see them (C: @flush_local_labels@).
flushLocalLabels :: Spim -> Bool -> IO ()
flushLocalLabels s warn = do
  locals :: [Label] <- get (localLabels s)
  forM_ locals $ \l -> do
    let h :: Int
        h = hashOf (labelName l)
    bucket :: [Label] <- readArray (labelTable s) h
    case break ((== labelName l) . labelName) bucket of
      (before, entry : after) -> do
        writeArray (labelTable s) h (before ++ after)
        address :: Word32 <- get (labelAddress entry)
        isConstant :: Bool <- get (labelConstant entry)
        when (warn && address == 0 && not isConstant) $
          cError ("Warning: local symbol " ++ labelName entry ++ " was not defined\n")
      _ -> pure ()
  set (localLabels s) []

-- | A label's address, or 0 (C: @find_symbol_address@; it creates the
-- label if it is new).
findSymbolAddress :: Spim -> String -> IO Word32
findSymbolAddress s name = lookupLabel s name >>= get . labelAddress

-- | Every label, in bucket order.
allLabels :: Spim -> IO [Label]
allLabels s = concat <$> forM [0 .. tableSize - 1] (readArray (labelTable s))

-- | C: @print_symbols@.
printSymbols :: Spim -> IO ()
printSymbols s = do
  ls :: [Label] <- allLabels s
  forM_ ls $ \l -> do
    g :: Bool <- get (labelGlobal l)
    a :: Word32 <- get (labelAddress l)
    writeError (printf "%s%s at 0x%08x\n" (if g then "g\t" else "\t" :: String) (labelName l) a)

-- | The names of the undefined labels, one per line, or Nothing
-- (C: @undefined_symbol_string@).
undefinedSymbolString :: Spim -> IO (Maybe String)
undefinedSymbolString s = do
  ls :: [Label] <- allLabels s
  names :: [String] <- concat <$> forM ls (\l -> do a :: Word32 <- get (labelAddress l); pure [labelName l | a == 0])
  pure (if null names then Nothing else Just (concatMap (++ "\n") names))
