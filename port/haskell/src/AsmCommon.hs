-- | Small pieces of assembler state that many modules need: where the next
-- instruction and datum go, parse-error reporting, the current source
-- line, and the @-listing@ events.
--
-- In C these are scattered over instruction.c, data.c, pseudo-op.c,
-- scanner.c and assembler-event.c; collecting them here lets the larger
-- modules depend on them without depending on each other.
module AsmCommon
  ( currentTextPc
  , currentDataPc
  , parseError
  , parseWarn
  , erroneousLine
  , sourceLine
  , lineNo
  , setLineNo
  , fire
  , fixCurrentLabelAddress
  ) where

import Data.Word (Word32)
import Output (cError)
import State
import Types

-- | Where the next instruction goes (C: @current_text_pc@).
currentTextPc :: Spim -> IO Word32
currentTextPc s = do
  k <- get (textInKernel s)
  get (if k then nextKTextPc s else nextTextPc s)

-- | Where the next datum goes (C: @current_data_pc@).
currentDataPc :: Spim -> IO Word32
currentDataPc s = do
  k <- get (dataInKernel s)
  get (if k then nextKDataPc s else nextDataPc s)

-- | The scanner's line number (C: @line_no@).
lineNo :: Spim -> IO Int
lineNo s = ssLineNo <$> get (scanner s)

setLineNo :: Spim -> Int -> IO ()
setLineNo s n = modify (scanner s) (\st -> st {ssLineNo = n})

-- | The line being parsed, for an error message (C: @erroneous_line@).
erroneousLine :: Spim -> IO String
erroneousLine s = do
  st <- get (scanner s)
  pure ("\t  " ++ ssSnapshot st ++ "\n")

-- | The line being parsed as "NNN: text", if a token of it has been read
-- (C: @source_line@).
sourceLine :: Spim -> IO (Maybe String)
sourceLine s = do
  st <- get (scanner s)
  pure $
    if ssSnapshotSaved st
      then Just (show (ssSnapshotLineNo st) ++ ": " ++ ssSnapshot st)
      else Nothing

-- | A warning with the line it is about (C: @parse_warn@).
parseWarn :: Spim -> String -> IO ()
parseWarn s msg = do
  n <- lineNo s
  file <- get (inputFileName s)
  line <- erroneousLine s
  cError ("spim: (parser) " ++ msg ++ " on line " ++ show n ++ " of file " ++ maybe "(input)" id file ++ "\n" ++ line)

-- | An error found while assembling (C: @parse_error@, pseudo-op.c).
parseError :: Spim -> String -> IO ()
parseError s msg = do
  set (parseErrorOccurred s) True
  modify (parseErrorsSeen s) (+ 1)
  parseWarn s msg

-- | Report an event to the @-listing@ observer, if there is one.
fire :: Spim -> AsmEvent -> IO ()
fire s ev = do
  obs <- get (asmObserver s)
  case obs of
    Nothing -> pure ()
    Just f -> lineNo s >>= f ev

-- | Move the labels defined on this line to a new address, when alignment
-- moved the location counter past them (C: @fix_current_label_address@).
fixCurrentLabelAddress :: Spim -> Word32 -> IO ()
fixCurrentLabelAddress s addr = do
  ls <- get (thisLineLabels s)
  mapM_ (\l -> set (lblAddr l) addr) ls
