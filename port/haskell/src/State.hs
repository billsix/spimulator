-- | All of the simulator's changeable state, in one record.
--
-- The C engine keeps its state in global variables spread over a dozen
-- files.  Haskell has no mutable globals, so the state lives here instead:
-- one 'Spim' record of mutable cells ('IORef' for a single value,
-- 'IOUArray' for a register file or a memory segment).  Every function that
-- reads or changes the machine takes the record as its first argument,
-- conventionally named @s@.
--
-- The fields are grouped by the C file whose globals they replace.
module State
  ( Spim (..)
  , Token (..)
  , TokValue (..)
  , ScanState (..)
  , ScanItem (..)
  , LoadDest (..)
  , AsmEvent (..)
  , newSpim
  , initialScanState
  , get
  , set
  , modify
  , kilo
  , mega
  ) where

import Ast (Node)
import Data.Array.IO (IOArray, IOUArray, newArray)
import Data.Int (Int32)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import Data.Word (Word32, Word8)
import Opcodes (Opcode)
import Types

kilo, mega :: Int
kilo = 1024
mega = kilo * kilo

-- | Read a cell.
get :: IORef a -> IO a
get = readIORef

-- | Write a cell.
set :: IORef a -> a -> IO ()
set = writeIORef

-- | Change a cell with a function.
modify :: IORef a -> (a -> a) -> IO ()
modify = modifyIORef'

-- | A token from the scanner (C: the @TOK_*@ enum, plus single characters
-- such as @(@ and @+@, which C returns as their character codes).
data Token
  = TokEOF
  | TokNL
  | TokInt
  | TokId
  | TokReg
  | TokFpReg
  | TokStr
  | TokFp
  | TokOp Opcode -- ^ an instruction, pseudo-instruction or directive keyword
  | TokChar Char -- ^ punctuation: @* / : ( ) + - > =@
  deriving (Eq, Show)

-- | The value carried by the last token (C: @scan_value@).
data TokValue = VNone | VInt Int32 | VStr String | VFp Double

-- | One slot of the scanner's three-token look-ahead buffer.
data ScanItem = ScanItem Token TokValue

-- | Everything the scanner remembers (C: the statics of scanner.c).  It is
-- one immutable value in one 'IORef', so saving and restoring the whole
-- scanner, as the REPL does, is just keeping a copy.
data ScanState = ScanState
  { ssInput :: String -- ^ the input not yet read into the line buffer
  , ssLine :: String -- ^ the current line (C: @line_buf@)
  , ssRest :: String -- ^ the part of the line not yet scanned
  , ssAtEof :: Bool
  , ssLineNo :: Int -- ^ C: @line_no@
  , ssSnapshot :: String -- ^ the current line, for error messages
  , ssSnapshotLineNo :: Int
  , ssSnapshotSaved :: Bool
  , ssForceId :: Bool -- ^ classify the next identifier as TOK_ID
  , ssTokens :: [ScanItem] -- ^ the look-ahead buffer, at most three
  }

-- | A scanner reading the given text.
initialScanState :: String -> ScanState
initialScanState input = ScanState input "" "" False 1 "" 0 False False []

-- | Where a delayed load will write: a general or a floating-point
-- register (C: a @reg_word*@ pointing into either file).
data LoadDest = DestGpr Int | DestFpr Int

-- | An event the assembler reports to the @-listing@ trace
-- (C: @asm_event@).  Each carries the source line it happened on.
data AsmEvent
  = EvLabelDef Word32 String Bool
  | EvTextInst Word32 Word32
  | EvDataByte Word32 Int32
  | EvDataHalf Word32 Int32
  | EvDataWord Word32 Int32
  | EvDataDouble Word32 Double
  | EvDataString Word32 Int Bool
  | EvForwardRef Word32 String
  | EvForwardResolved Word32 String Int32

-- | The whole machine.
data Spim = Spim
  { -- spim.c: command-line options
    optBare :: IORef Bool
  , optAcceptPseudo :: IORef Bool
  , optDelayedBranches :: IORef Bool
  , optDelayedLoads :: IORef Bool
  , optQuiet :: IORef Bool
  , optMappedIo :: IORef Bool
  , spimReturnValue :: IORef Int32
  , forceBreak :: IORef Bool
  , initialSizes :: IORef (Int, Int, Int, Int, Int, Int, Int, Int)
  -- ^ text, data, data limit, stack, stack limit, ktext, kdata, kdata limit
  , -- memory.c and registers.h: the registers
    gpr :: IOUArray Int Int32
  , regHI :: IORef Int32
  , regLO :: IORef Int32
  , regPC :: IORef Word32
  , fpRegs :: IOUArray Int Word32
  -- ^ the 32 FP registers as raw words; a double is two of them
  , cpRegs :: IOUArray Int Int32 -- ^ coprocessor registers, 4 x 32
  , cpControl :: IOUArray Int Int32 -- ^ coprocessor control registers, 4 x 32
  , -- run.c and instruction.h: exceptions
    exceptionOccurred :: IORef Bool
  , firstBadException :: IORef Int
  , runningInDelaySlot :: IORef Bool
  , delayedLoad1 :: IORef (Maybe (LoadDest, Int32))
  , delayedLoad2 :: IORef (Maybe (LoadDest, Int32))
  , timerDeadline :: IORef Integer -- ^ the CP0 timer's next tick, in microseconds
  , -- memory.c: the five segments
    textSeg :: IORef (IOArray Int (Maybe InstRef))
  , textTop :: IORef Word32
  , kTextSeg :: IORef (IOArray Int (Maybe InstRef))
  , kTextTop :: IORef Word32
  , dataSeg :: IORef (IOUArray Int Word8)
  , dataTop :: IORef Word32
  , stackSeg :: IORef (IOUArray Int Word8)
  , stackBot :: IORef Word32
  , kDataSeg :: IORef (IOUArray Int Word8)
  , kDataTop :: IORef Word32
  , gpMidpoint :: IORef Word32
  , dataSizeLimit :: IORef Int
  , stackSizeLimit :: IORef Int
  , kDataSizeLimit :: IORef Int
  , -- memory.c: memory-mapped IO
    recvControl :: IORef Word32
  , recvBuffer :: IORef Int
  , recvTimer :: IORef Int
  , transControl :: IORef Word32
  , transBuffer :: IORef Int
  , transTimer :: IORef Int
  , -- data.c
    nextDataPc :: IORef Word32
  , nextKDataPc :: IORef Word32
  , dataInKernel :: IORef Bool
  , nextGpItemAddr :: IORef Word32
  , dataAutoAlign :: IORef Bool
  , dataStoreDepth :: IORef Int
  , -- instruction.c
    textInKernel :: IORef Bool
  , nextTextPc :: IORef Word32
  , nextKTextPc :: IORef Word32
  , breakInst :: IORef (Maybe InstRef)
  , emitSource :: IORef (Maybe (Maybe String))
  -- ^ Nothing: inactive; Just text: the source annotation for emitted code
  , -- symbol-table.c
    labelTable :: IOArray Int [Label] -- ^ 8191 hash buckets, newest first
  , localLabels :: IORef [Label]
  , labelCounter :: IORef Int
  , -- spim-utils.c
    breakpoints :: IORef [(Word32, InstRef)] -- ^ newest first
  , -- scanner.c
    scanner :: IORef ScanState
  , scannerStack :: IORef [ScanState]
  , scanValue :: IORef TokValue
  , -- parser.c and pseudo-op.c
    dataDir :: IORef Bool
  , textDir :: IORef Bool
  , parseErrorOccurred :: IORef Bool
  , parseErrorsSeen :: IORef Int
  , inputFileName :: IORef (Maybe String)
  , nullTerm :: IORef Bool
  , autoAlign :: IORef Bool
  , thisLineLabels :: IORef [Label]
  , exprUnresolvedLabel :: IORef (Maybe Label)
  , astScope :: IORef [Node] -- ^ the statements being built, newest first
  , dataDirectiveLine :: IORef Int
  , printAstFlag :: IORef Bool
  , printAstOnly :: IORef Bool
  , showExpansionFlag :: IORef Bool
  , printAstJsonFlag :: IORef Bool
  , -- assembler-event.c: the -listing observer
    asmObserver :: IORef (Maybe (AsmEvent -> Int -> IO ()))
  }

-- | A fresh machine, before @initialize_world@.
newSpim :: IO Spim
newSpim = do
  let b :: a -> IO (IORef a)
      b = newIORef
  emptyText :: IOArray Int (Maybe InstRef) <- newArray (0, 0) Nothing
  emptyText2 :: IOArray Int (Maybe InstRef) <- newArray (0, 0) Nothing
  emptyData :: IOUArray Int Word8 <- newArray (0, 0) 0
  emptyData2 :: IOUArray Int Word8 <- newArray (0, 0) 0
  emptyData3 :: IOUArray Int Word8 <- newArray (0, 0) 0
  Spim
    <$> b False <*> b True <*> b False <*> b False <*> b False <*> b False
    <*> b 0 <*> b False
    <*> b (256 * kilo, 256 * kilo, mega, 64 * kilo, 256 * kilo, 64 * kilo, 64 * kilo, mega)
    <*> newArray (0, 31) 0 <*> b 0 <*> b 0 <*> b 0
    <*> newArray (0, 31) 0 <*> newArray (0, 127) 0 <*> newArray (0, 127) 0
    <*> b False <*> b (-1) <*> b False <*> b Nothing <*> b Nothing <*> b 0
    <*> b emptyText <*> b 0 <*> b emptyText2 <*> b 0
    <*> b emptyData <*> b 0 <*> b emptyData2 <*> b 0 <*> b emptyData3 <*> b 0
    <*> b 0 <*> b 0 <*> b 0 <*> b 0
    <*> b 0 <*> b 0 <*> b 0 <*> b 1 <*> b 0 <*> b 0
    <*> b 0 <*> b 0 <*> b False <*> b 0 <*> b True <*> b 0
    <*> b False <*> b 0 <*> b 0 <*> b Nothing <*> b Nothing
    <*> newArray (0, 8190) [] <*> b [] <*> b 0
    <*> b []
    <*> b (initialScanState "") <*> b [] <*> b VNone
    <*> b False <*> b True <*> b False <*> b 0 <*> b Nothing <*> b False <*> b True
    <*> b [] <*> b Nothing <*> b [] <*> b 0
    <*> b False <*> b False <*> b False <*> b False
    <*> b Nothing
