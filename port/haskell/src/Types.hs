-- | The data types the rest of the engine shares: instructions, immediate
-- expressions, symbol-table labels and their pending uses.
--
-- In C these live in include/instruction.h and include/symbol-table.h.
-- They point at each other (an instruction's immediate names a label; a
-- label keeps a list of the instructions waiting for it), and the C engine
-- changes them in place when a forward reference is resolved.  Here the
-- parts that change are 'IORef's, and everything else is plain immutable
-- data.
module Types
  ( Instruction (..)
  , InstRef
  , emptyInstruction
  , ImmExpr (..)
  , Label (..)
  , LabelUse (..)
    -- * The instruction's overlapping fields
  , instRs
  , instRt
  , instRd
  , instShamt
  , instImm
  , instTarget
  , setRs
  , setRt
  , setRd
  , setShamt
  , setImm
  , setTarget
  , instFs
  , instFt
  , instFd
  , instBase
  , instCond
  , branchOffset
  ) where

import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int16, Int32)
import Data.IORef (IORef)
import Data.Word (Word32, Word8)

-- | An immediate expression: @symbol + offset@, either part optional
-- (C: @imm_expr@).
data ImmExpr = ImmExpr
  { ieOffset :: Int32 -- ^ offset from the symbol
  , ieSymbol :: Maybe Label -- ^ the label, if any
  , ieBits :: Int16 -- ^ > 0: use bits 31..16; < 0: bits 15..0; 0: all
  , iePcRelative :: Bool -- ^ an offset from the instruction's own address
  }

-- | One assembled instruction (C: @mips_instruction@).
--
-- The C struct keeps the register fields in overlapping unions, laid out
-- like this on a little-endian machine:
--
-- >   byte:     0      1      2       3
-- >            rs     rt     rd     shamt      (R-type)
-- >            rs     rt     |--- imm ---|     (I-type)
-- >            |--------- target --------|     (J-type)
--
-- so writing @target@ overwrites the registers, and @imm@ and @rd@/@shamt@
-- share their bytes.  The engine depends on this (for example, an FP
-- instruction keeps FD in the shamt byte), so 'inFields' is that one 32-bit
-- word and the accessors below read and write its bytes.
data Instruction = Instruction
  { inOpcode :: Int -- ^ the C token number of the opcode (0: invalid)
  , inFields :: Word32 -- ^ the overlapping register/immediate/target fields
  , inEncoding :: Int32 -- ^ the 32-bit binary encoding
  , inExpr :: Maybe ImmExpr -- ^ the symbolic immediate, if any
  , inSource :: Maybe String -- ^ the source line it came from, for listings
  }

-- | A shared, changeable instruction: the text segment holds these, and a
-- forward reference to a label patches one in place.
type InstRef = IORef Instruction

-- | An instruction with every field zero (C: @zmalloc@).
emptyInstruction :: Int -> Instruction
emptyInstruction op = Instruction op 0 0 Nothing Nothing

byteAt :: Int -> Word32 -> Word8
byteAt n w = fromIntegral (w `shiftR` (8 * n))

setByteAt :: Int -> Word8 -> Word32 -> Word32
setByteAt n b w =
  (w .&. complement (0xff `shiftL` (8 * n))) .|. (fromIntegral b `shiftL` (8 * n))

instRs, instRt, instRd, instShamt :: Instruction -> Int
instRs = fromIntegral . byteAt 0 . inFields
instRt = fromIntegral . byteAt 1 . inFields
instRd = fromIntegral . byteAt 2 . inFields
instShamt = fromIntegral . byteAt 3 . inFields

-- | The 16-bit immediate (bytes 2 and 3), signed.
instImm :: Instruction -> Int16
instImm i = fromIntegral (inFields i `shiftR` 16)

-- | The 32-bit jump target (all four bytes).
instTarget :: Instruction -> Word32
instTarget = inFields

-- | The C engine's setters cast to @uint8_t@ / @int16_t@, keeping the low bits.
setRs, setRt, setRd, setShamt :: Int -> Instruction -> Instruction
setRs v i = i {inFields = setByteAt 0 (fromIntegral v) (inFields i)}
setRt v i = i {inFields = setByteAt 1 (fromIntegral v) (inFields i)}
setRd v i = i {inFields = setByteAt 2 (fromIntegral v) (inFields i)}
setShamt v i = i {inFields = setByteAt 3 (fromIntegral v) (inFields i)}

setImm :: Int32 -> Instruction -> Instruction
setImm v i =
  i {inFields = (inFields i .&. 0xffff) .|. ((fromIntegral v .&. 0xffff) `shiftL` 16)}

setTarget :: Word32 -> Instruction -> Instruction
setTarget v i = i {inFields = v}

-- | The FP and other aliases of the same fields (instruction.h).
instFs, instFt, instFd, instBase, instCond :: Instruction -> Int
instFs = instRd
instFt = instRt
instFd = instShamt
instBase = instRs
instCond = instRs

-- | The branch displacement in bytes: the 16-bit word offset,
-- sign-extended, times four.
branchOffset :: Instruction -> Int32
branchOffset i = fromIntegral (instImm i) * 4

-- | A label in the symbol table (C: @label@).  The changeable parts are
-- references; 'lblId' gives each label an identity, like the C pointer.
data Label = Label
  { lblId :: Int
  , lblName :: String
  , lblAddr :: IORef Word32 -- ^ its address (or constant value); 0 if undefined
  , lblGlobal :: IORef Bool -- ^ declared @.globl@
  , lblGp :: IORef Bool -- ^ lives in the small data area off @$gp@
  , lblConst :: IORef Bool -- ^ a constant (@name = value@)
  , lblUses :: IORef [LabelUse] -- ^ uses waiting for the label to be defined
  }

instance Eq Label where
  a == b = lblId a == lblId b

-- | A place that used a label before it was defined (C: @label_use@).
data LabelUse
  = -- | an instruction, and the address it was assembled at
    UseInst InstRef Word32
  | -- | a data item: its address, the constant added to the label, and its
    -- width in bytes (1 for .byte, 2 for .half, 4 for .word)
    UseData Word32 Int32 Int
