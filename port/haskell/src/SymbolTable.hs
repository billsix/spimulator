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
    recordInstUsesSymbol,
    recordDataUsesSymbol,
    resolveLabelUses,
    resolveALabel,
    flushLocalLabels,
    findSymbolAddress,
    printSymbols,
    undefinedSymbolString,
    allLabels,
    evalImmExpr,
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
import InstFormat (printInst)
import Instruction
import Memory
import Opcodes
import Output (cError, writeErr)
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
    ( case filter ((== name) . lblName) bucket of
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
symbolIsDefined l = (/= 0) <$> get (lblAddr l)

-- | Define a label at an address (C: @record_label@).
recordLabel :: Spim -> String -> Word32 -> Bool -> IO Label
recordLabel s name address resolve = do
  l :: Label <- lookupLabel s name
  gp :: Bool <- get (lblGp l)
  old :: Word32 <- get (lblAddr l)
  if not gp && old /= 0
    then parseError s "Label is defined for the second time" >> pure l
    else do
      unless gp $ set (lblAddr l) address
      global :: Bool <- get (lblGlobal l)
      fire s (EvLabelDef address (lblName l) global)
      when resolve $ resolveLabelUses s l
      unless global $ modify (localLabels s) (l :)
      pure l

-- | C: @make_label_global@.
makeLabelGlobal :: Spim -> String -> IO Label
makeLabelGlobal s name = do
  l :: Label <- lookupLabel s name
  set (lblGlobal l) True
  pure l

-- | Remember that an instruction uses a label not yet defined
-- (C: @record_inst_uses_symbol@).  An instruction assembled into the data
-- segment is copied, since the original is stored as a word and dropped.
recordInstUsesSymbol :: Spim -> InstRef -> Label -> IO ()
recordInstUsesSymbol s inst l = do
  inData :: Bool <- get (dataDir s)
  (ref, at) :: (InstRef, Word32) <-
    if inData
      then do
        copy :: InstRef <- get inst >>= newIORef
        (,) copy <$> currentDataPc s
      else (,) inst <$> currentTextPc s
  modify (lblUses l) (UseInst ref at :)
  fire s (EvForwardRef at (lblName l))

-- | Remember that a datum uses a label not yet defined
-- (C: @record_data_uses_symbol@).
recordDataUsesSymbol :: Spim -> Word32 -> Label -> Int32 -> Int -> IO ()
recordDataUsesSymbol s at l offset width = do
  modify (lblUses l) (UseData at offset width :)
  fire s (EvForwardRef at (lblName l))

-- | Patch every use of a label that has just been defined
-- (C: @resolve_label_uses@).
resolveLabelUses :: Spim -> Label -> IO ()
resolveLabelUses s l = do
  uses :: [LabelUse] <- get (lblUses l)
  addr :: Word32 <- get (lblAddr l)
  sbot :: Word32 <- get (stackBot s)
  forM_ uses $ \use -> do
    at :: Word32 <- case use of
      UseData at offset width -> do
        let value :: Int32
            value = fromIntegral addr + offset :: Int32
        forM_ [0 .. width - 1] $ \i ->
          memWriteByte s (at + fromIntegral i) (fromIntegral ((fromIntegral value :: Word32) `shiftR` (8 * i)) .&. 0xff)
        pure at
      UseInst inst at -> do
        resolveSub s l inst at
        when (at >= dataBot && at < sbot) $ do
          enc :: Int32 <- get inst >>= instEncodeIO
          memWriteWord s at enc
        pure at
    fire s (EvForwardResolved at (lblName l) (fromIntegral addr))
  set (lblUses l) []

-- | Fill in a label in an instruction assembled at the current location
-- (C: @resolve_a_label@).
resolveALabel :: Spim -> Maybe Label -> InstRef -> IO ()
resolveALabel s l inst = do
  inData :: Bool <- get (dataDir s)
  pc :: Word32 <- if inData then currentDataPc s else currentTextPc s
  resolveSubMaybe s l inst pc

resolveSub :: Spim -> Label -> InstRef -> Word32 -> IO ()
resolveSub s l = resolveSubMaybe s (Just l)

-- | Compute an instruction's immediate field from its now-known expression
-- (C: @resolve_a_label_sub@): a branch gets a word offset, a jump a 26-bit
-- target, anything else the low 16 bits.
resolveSubMaybe :: Spim -> Maybe Label -> InstRef -> Word32 -> IO ()
resolveSubMaybe s _ inst pc = do
  i0 :: Instruction <- get inst
  case inExpr i0 of
    Nothing -> pure ()
    Just e0 -> resolveExpr s inst i0 e0 pc

resolveExpr :: Spim -> InstRef -> Instruction -> ImmExpr -> Word32 -> IO ()
resolveExpr s inst i0 e0 pc = do
  let e :: ImmExpr
      e = if iePcRelative e0 then e0 {ieOffset = fromIntegral (0 - pc)} else e0
  set inst i0 {inExpr = Just e}
  defined :: Bool <- maybe (pure True) symbolIsDefined (ieSymbol e)
  if not defined
    then cError ("Resolving undefined symbol: " ++ maybe "" lblName (ieSymbol e) ++ "\n")
    else do
      let op :: Int
          op = inOpcode i0
      (value, fieldMask) :: (Int32, Int32) <-
        if opcodeIsBranch op
          then do
            byteOffset :: Int32 <- evalImmExpr s e {ieBits = 0}
            delayed :: Bool <- get (optDelayedBranches s)
            let wordOffset :: Int32
                wordOffset = (byteOffset `quot` 4) - (if delayed then 1 else 0)
            when (wordOffset < -32768 || wordOffset > 32767) $
              cError (printf "Branch target is out of range (more than 128 KB away) at pc 0x%x\n" pc)
            pure (fromIntegral (fromIntegral wordOffset .&. (0xffff :: Word32)), 0xffff)
          else
            if opcodeIsJump op
              then do
                v :: Int32 <- evalImmExpr s e
                when ((fromIntegral v .&. 0xf0000000) /= (pc .&. 0xf0000000)) $
                  cError (printf "Target of jump differs in high-order 4 bits from instruction pc 0x%x\n" pc)
                pure ((v .&. 0x0fffffff) `shiftR` 2, -1)
              else do
                v :: Int32 <- evalImmExpr s e
                when (opcodeIsLoadStore op && v .&. 0x8000 /= 0) $ compensateLui s e pc
                pure (v, 0xffff)
      let high :: Int32
          high = value .&. complement fieldMask
      when (high /= 0 && high /= fromIntegral (0xffff0000 :: Word32)) $ do
        cError "Immediate value is too large for field: "
        printInst s pc
      i1 :: Instruction <- get inst
      let i2 :: Instruction
          i2 = if opcodeIsJump op then setTarget (fromIntegral value) i1 else setImm value i1
      enc :: Int32 <- instEncodeIO i2
      set inst i2 {inEncoding = enc}

-- | A load or store sign-extends its 16-bit offset, so when the low half
-- of the address is "negative", the @lui@ that loaded the high half (one
-- or two instructions back) must load one more.
compensateLui :: Spim -> ImmExpr -> Word32 -> IO ()
compensateLui s e pc = do
  prev :: Maybe InstRef <- memReadInst s (pc - 4)
  prevPrev :: Maybe InstRef <- memReadInst s (pc - 8)
  okPrev :: Bool <- isPlainLui prev
  if okPrev
    then bump prev
    else do
      okPrevPrev :: Bool <- isPlainLui prevPrev
      when okPrevPrev $ bump prevPrev
  where
    isPlainLui :: Maybe InstRef -> IO Bool
    isPlainLui Nothing = pure False
    isPlainLui (Just ref) = do
      i :: Instruction <- get ref
      pure $ case inExpr i of
        Just pe -> isTok TOK_LUI_OPCODE (inOpcode i) && ieSymbol e == ieSymbol pe && instImm i == 0
        Nothing -> False
    bump :: Maybe InstRef -> IO ()
    bump Nothing = pure ()
    bump (Just ref) = modify ref (\i -> i {inExpr = fmap (\pe -> pe {ieOffset = ieOffset pe + 0x10000}) (inExpr i)})

-- | The value of an immediate expression (C: @eval_imm_expr@).
evalImmExpr :: Spim -> ImmExpr -> IO Int32
evalImmExpr _ e = do
  value :: Int32 <- case ieSymbol e of
    Nothing -> pure (ieOffset e)
    Just l -> do
      addr :: Word32 <- get (lblAddr l)
      if addr /= 0
        then pure (ieOffset e + fromIntegral addr)
        else do
          cError ("Evaluated undefined symbol: " ++ lblName l ++ "\n")
          pure 0
  pure $
    if ieBits e > 0
      then (value `shiftR` 16) .&. 0xffff
      else if ieBits e < 0 then value .&. 0xffff else value

-- | At the end of a file, drop its non-global labels from the table so
-- the next file cannot see them (C: @flush_local_labels@).
flushLocalLabels :: Spim -> Bool -> IO ()
flushLocalLabels s warn = do
  locals :: [Label] <- get (localLabels s)
  forM_ locals $ \l -> do
    let h :: Int
        h = hashOf (lblName l)
    bucket :: [Label] <- readArray (labelTable s) h
    case break ((== lblName l) . lblName) bucket of
      (before, entry : after) -> do
        writeArray (labelTable s) h (before ++ after)
        addr :: Word32 <- get (lblAddr entry)
        isConst :: Bool <- get (lblConst entry)
        when (warn && addr == 0 && not isConst) $
          cError ("Warning: local symbol " ++ lblName entry ++ " was not defined\n")
      _ -> pure ()
  set (localLabels s) []

-- | A label's address, or 0 (C: @find_symbol_address@; it creates the
-- label if it is new).
findSymbolAddress :: Spim -> String -> IO Word32
findSymbolAddress s name = lookupLabel s name >>= get . lblAddr

-- | Every label, in bucket order.
allLabels :: Spim -> IO [Label]
allLabels s = concat <$> forM [0 .. tableSize - 1] (readArray (labelTable s))

-- | C: @print_symbols@.
printSymbols :: Spim -> IO ()
printSymbols s = do
  ls :: [Label] <- allLabels s
  forM_ ls $ \l -> do
    g :: Bool <- get (lblGlobal l)
    a :: Word32 <- get (lblAddr l)
    writeErr (printf "%s%s at 0x%08x\n" (if g then "g\t" else "\t" :: String) (lblName l) a)

-- | The names of the undefined labels, one per line, or Nothing
-- (C: @undefined_symbol_string@).
undefinedSymbolString :: Spim -> IO (Maybe String)
undefinedSymbolString s = do
  ls :: [Label] <- allLabels s
  names :: [String] <- concat <$> forM ls (\l -> do a :: Word32 <- get (lblAddr l); pure [lblName l | a == 0])
  pure (if null names then Nothing else Just (concatMap (++ "\n") names))
