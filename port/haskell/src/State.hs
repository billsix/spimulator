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
  ( Spim (..),
    Token (..),
    TokenValue (..),
    ScanState (..),
    ScanItem (..),
    LoadDest (..),
    AssemblerEvent (..),
    newSpim,
    initialScanState,
    get,
    set,
    modify,
    kilo,
    mega,
  )
where

import Ast (Node)
import Data.Array.IO (IOArray, IOUArray, newArray)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import Data.Int (Int32)
import Data.Word (Word32, Word8)
import Opcodes (Opcode)
import Types

kilo, mega :: Int
kilo = 1024
mega = kilo * kilo

-- | Read a cell.
get :: IORef a -> IO a
get = readIORef

-- | Write a cell, evaluating the value first. Long-lived state (registers,
-- HI/LO, segment tops) is written through here, and a lazy write would keep
-- a chain of unevaluated updates alive until something reads the cell.
set :: IORef a -> a -> IO ()
set r v = writeIORef r $! v

-- | Change a cell with a function.
modify :: IORef a -> (a -> a) -> IO ()
modify = modifyIORef'

-- | A token from the scanner (C: the @TOK_*@ enum, plus single characters
-- such as @(@ and @+@, which C returns as their character codes).
data Token
  = TokenEOF
  | TokenNewline
  | TokenInteger
  | TokenIdentifier
  | TokenRegister
  | TokenFloatingPointRegister
  | TokenString
  | TokenFloatingPoint
  | -- | an instruction, pseudo-instruction or directive keyword
    TokenOpcode Opcode
  | -- | punctuation: @* / : ( ) + - > =@
    TokenChar Char
  deriving (Eq, Show)

-- | The value carried by the last token (C: @scan_value@).
data TokenValue = ValueNone | ValueInteger Int32 | ValueString String | ValueFloatingPoint Double

-- | One slot of the scanner's three-token look-ahead buffer.
data ScanItem = ScanItem Token TokenValue

-- | Everything the scanner remembers (C: the statics of scanner.c).  It is
-- one immutable value in one 'IORef', so saving and restoring the whole
-- scanner, as the REPL does, is just keeping a copy.
data ScanState = ScanState
  { -- | the input not yet read into the line buffer
    scanStateInput :: String,
    -- | the current line (C: @line_buf@)
    scanStateLine :: String,
    -- | the part of the line not yet scanned
    scanStateRest :: String,
    scanStateAtEof :: Bool,
    -- | C: @line_no@
    scanStateLineNo :: Int,
    -- | the current line, for error messages
    scanStateSnapshot :: String,
    scanStateSnapshotLineNo :: Int,
    scanStateSnapshotSaved :: Bool,
    -- | classify the next identifier as TOK_ID
    scanStateForceIdentifier :: Bool,
    -- | the look-ahead buffer, at most three
    scanStateTokens :: [ScanItem]
  }

-- | A scanner reading the given text.
initialScanState :: String -> ScanState
initialScanState input = ScanState input "" "" False 1 "" 0 False False []

-- | Where a delayed load will write: a general or a floating-point
-- register (C: a @reg_word*@ pointing into either file).
data LoadDest = DestinationGeneralPurposeRegister Int | DestinationFloatingPointRegister Int

-- | An event the assembler reports to the @-listing@ trace
-- (C: @asm_event@).  Each carries the source line it happened on.
data AssemblerEvent
  = EventLabelDefinition Word32 String Bool
  | EventTextInstruction Word32 Word32
  | EventDataByte Word32 Int32
  | EventDataHalf Word32 Int32
  | EventDataWord Word32 Int32
  | EventDataDouble Word32 Double
  | EventDataString Word32 Int Bool
  | EventForwardReference Word32 String
  | EventForwardResolved Word32 String Int32

-- | The whole machine.
data Spim = Spim
  { -- spim.c: command-line options
    optionBare :: IORef Bool,
    optionAcceptPseudo :: IORef Bool,
    optionDelayedBranches :: IORef Bool,
    optionDelayedLoads :: IORef Bool,
    optionQuiet :: IORef Bool,
    optionMappedIO :: IORef Bool,
    spimReturnValue :: IORef Int32,
    forceBreak :: IORef Bool,
    -- | text, data, data limit, stack, stack limit, ktext, kdata, kdata limit
    initialSizes :: IORef (Int, Int, Int, Int, Int, Int, Int, Int),
    -- memory.c and registers.h: the registers
    generalPurposeRegisters :: IOUArray Int Int32,
    registerMultiplyDivideHigh :: IORef Int32,
    registerMultiplyDivideLow :: IORef Int32,
    registerProgramCounter :: IORef Word32,
    -- | the 32 FP registers as raw words; a double is two of them
    floatingPointRegisters :: IOUArray Int Word32,
    -- | coprocessor registers, 4 x 32
    coprocessorRegisters :: IOUArray Int Int32,
    -- | coprocessor control registers, 4 x 32
    coprocessorControl :: IOUArray Int Int32,
    -- run.c and instruction.h: exceptions
    exceptionOccurred :: IORef Bool,
    firstBadException :: IORef Int,
    runningInDelaySlot :: IORef Bool,
    delayedLoad1 :: IORef (Maybe (LoadDest, Int32)),
    delayedLoad2 :: IORef (Maybe (LoadDest, Int32)),
    -- | the CP0 timer's next tick, in microseconds
    timerDeadline :: IORef Integer,
    -- memory.c: the five segments
    textSegment :: IORef (IOArray Int (Maybe InstructionRef)),
    textTop :: IORef Word32,
    kernelTextSegment :: IORef (IOArray Int (Maybe InstructionRef)),
    kernelTextTop :: IORef Word32,
    dataSegment :: IORef (IOUArray Int Word8),
    dataTop :: IORef Word32,
    stackSegment :: IORef (IOUArray Int Word8),
    stackBottom :: IORef Word32,
    kernelDataSegment :: IORef (IOUArray Int Word8),
    kernelDataTop :: IORef Word32,
    globalPointerMidpoint :: IORef Word32,
    dataSizeLimit :: IORef Int,
    stackSizeLimit :: IORef Int,
    kernelDataSizeLimit :: IORef Int,
    -- memory.c: memory-mapped IO
    receiverControl :: IORef Word32,
    receiverBuffer :: IORef Int,
    receiverTimer :: IORef Int,
    transmitterControl :: IORef Word32,
    transmitterBuffer :: IORef Int,
    transmitterTimer :: IORef Int,
    -- data.c
    nextDataLocation :: IORef Word32,
    nextKernelDataLocation :: IORef Word32,
    dataInKernel :: IORef Bool,
    nextGlobalPointerItemAddress :: IORef Word32,
    dataAutoAlign :: IORef Bool,
    dataStoreDepth :: IORef Int,
    -- instruction.c
    textInKernel :: IORef Bool,
    nextTextLocation :: IORef Word32,
    nextKernelTextLocation :: IORef Word32,
    cachedBreakInstruction :: IORef (Maybe InstructionRef),
    -- | Nothing: inactive; Just text: the source annotation for emitted code
    emitSource :: IORef (Maybe (Maybe String)),
    -- symbol-table.c

    -- | 8191 hash buckets, newest first
    labelTable :: IOArray Int [Label],
    localLabels :: IORef [Label],
    labelCounter :: IORef Int,
    -- spim-utils.c

    -- | newest first
    breakpoints :: IORef [(Word32, InstructionRef)],
    -- scanner.c
    scanner :: IORef ScanState,
    scannerStack :: IORef [ScanState],
    scanValue :: IORef TokenValue,
    -- parser.c and pseudo-op.c
    dataDirective :: IORef Bool,
    textDirective :: IORef Bool,
    parseErrorOccurred :: IORef Bool,
    parseErrorsSeen :: IORef Int,
    inputFileName :: IORef (Maybe String),
    nullTerm :: IORef Bool,
    autoAlign :: IORef Bool,
    thisLineLabels :: IORef [Label],
    expressionUnresolvedLabel :: IORef (Maybe Label),
    -- | the statements being built, newest first
    astScope :: IORef [Node],
    dataDirectiveLine :: IORef Int,
    printAstFlag :: IORef Bool,
    printAstOnly :: IORef Bool,
    showExpansionFlag :: IORef Bool,
    printAstJsonFlag :: IORef Bool,
    -- assembler-event.c: the -listing observer
    assemblerObserver :: IORef (Maybe (AssemblerEvent -> Int -> IO ()))
  }

-- | A fresh machine, before @initialize_world@.
newSpim :: IO Spim
newSpim = do
  let b :: a -> IO (IORef a)
      b = newIORef
  emptyText :: IOArray Int (Maybe InstructionRef) <- newArray (0, 0) Nothing
  emptyText2 :: IOArray Int (Maybe InstructionRef) <- newArray (0, 0) Nothing
  emptyData :: IOUArray Int Word8 <- newArray (0, 0) 0
  emptyData2 :: IOUArray Int Word8 <- newArray (0, 0) 0
  emptyData3 :: IOUArray Int Word8 <- newArray (0, 0) 0
  Spim
    <$> b False
    <*> b True
    <*> b False
    <*> b False
    <*> b False
    <*> b False
    <*> b 0
    <*> b False
    <*> b (256 * kilo, 256 * kilo, mega, 64 * kilo, 256 * kilo, 64 * kilo, 64 * kilo, mega)
    <*> newArray (0, 31) 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> newArray (0, 31) 0
    <*> newArray (0, 127) 0
    <*> newArray (0, 127) 0
    <*> b False
    <*> b (-1)
    <*> b False
    <*> b Nothing
    <*> b Nothing
    <*> b 0
    <*> b emptyText
    <*> b 0
    <*> b emptyText2
    <*> b 0
    <*> b emptyData
    <*> b 0
    <*> b emptyData2
    <*> b 0
    <*> b emptyData3
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 1
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b 0
    <*> b False
    <*> b 0
    <*> b True
    <*> b 0
    <*> b False
    <*> b 0
    <*> b 0
    <*> b Nothing
    <*> b Nothing
    <*> newArray (0, 8190) []
    <*> b []
    <*> b 0
    <*> b []
    <*> b (initialScanState "")
    <*> b []
    <*> b ValueNone
    <*> b False
    <*> b True
    <*> b False
    <*> b 0
    <*> b Nothing
    <*> b False
    <*> b True
    <*> b []
    <*> b Nothing
    <*> b []
    <*> b 0
    <*> b False
    <*> b False
    <*> b False
    <*> b False
    <*> b Nothing
