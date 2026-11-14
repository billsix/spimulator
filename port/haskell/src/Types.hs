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
  ( Instruction (..),
    InstructionRef,
    emptyInstruction,
    ImmediateExpression (..),
    Label (..),
    LabelUse (..),

    -- * The instruction's overlapping fields
    instructionSourceRegister,
    instructionTargetRegister,
    instructionDestinationRegister,
    instructionShiftAmount,
    instructionImmediate,
    instructionTarget,
    setSourceRegister,
    setTargetRegister,
    setDestinationRegister,
    setShiftAmount,
    setImmediate,
    setTarget,
    instructionFloatingPointSource,
    instructionFloatingPointTarget,
    instructionFloatingPointDestination,
    instructionBase,
    instructionCondition,
    branchOffset,
  )
where

import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.IORef (IORef)
import Data.Int (Int16, Int32)
import Data.Word (Word32, Word8)

-- | An immediate expression: @symbol + offset@, either part optional
-- (C: @imm_expr@).
data ImmediateExpression = ImmediateExpression
  { -- | offset from the symbol
    immediateExpressionOffset :: Int32,
    -- | the label, if any
    immediateExpressionSymbol :: Maybe Label,
    -- | > 0: use bits 31..16; < 0: bits 15..0; 0: all
    immediateExpressionBits :: Int16,
    -- | an offset from the instruction's own address
    immediateExpressionProgramCounterRelative :: Bool
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
  { -- | the C token number of the opcode (0: invalid)
    instructionOpcode :: Int,
    -- | the overlapping register/immediate/target fields
    instructionFields :: Word32,
    -- | the 32-bit binary encoding
    instructionEncoding :: Int32,
    -- | the symbolic immediate, if any
    instructionExpression :: Maybe ImmediateExpression,
    -- | the source line it came from, for listings
    instructionSource :: Maybe String
  }

-- | A shared, changeable instruction: the text segment holds these, and a
-- forward reference to a label patches one in place.
type InstructionRef = IORef Instruction

-- | An instruction with every field zero (C: @zmalloc@).
emptyInstruction :: Int -> Instruction
emptyInstruction op = Instruction op 0 0 Nothing Nothing

byteAt :: Int -> Word32 -> Word8
byteAt n w = fromIntegral (w `shiftR` (8 * n))

setByteAt :: Int -> Word8 -> Word32 -> Word32
setByteAt n b w =
  (w .&. complement (0xff `shiftL` (8 * n))) .|. (fromIntegral b `shiftL` (8 * n))

instructionSourceRegister, instructionTargetRegister, instructionDestinationRegister, instructionShiftAmount :: Instruction -> Int
instructionSourceRegister = fromIntegral . byteAt 0 . instructionFields
instructionTargetRegister = fromIntegral . byteAt 1 . instructionFields
instructionDestinationRegister = fromIntegral . byteAt 2 . instructionFields
instructionShiftAmount = fromIntegral . byteAt 3 . instructionFields

-- | The 16-bit immediate (bytes 2 and 3), signed.
instructionImmediate :: Instruction -> Int16
instructionImmediate i = fromIntegral (instructionFields i `shiftR` 16)

-- | The 32-bit jump target (all four bytes).
instructionTarget :: Instruction -> Word32
instructionTarget = instructionFields

-- | The C engine's setters cast to @uint8_t@ / @int16_t@, keeping the low bits.
setSourceRegister, setTargetRegister, setDestinationRegister, setShiftAmount :: Int -> Instruction -> Instruction
setSourceRegister v i = i {instructionFields = setByteAt 0 (fromIntegral v) (instructionFields i)}
setTargetRegister v i = i {instructionFields = setByteAt 1 (fromIntegral v) (instructionFields i)}
setDestinationRegister v i = i {instructionFields = setByteAt 2 (fromIntegral v) (instructionFields i)}
setShiftAmount v i = i {instructionFields = setByteAt 3 (fromIntegral v) (instructionFields i)}

setImmediate :: Int32 -> Instruction -> Instruction
setImmediate v i =
  i {instructionFields = (instructionFields i .&. 0xffff) .|. ((fromIntegral v .&. 0xffff) `shiftL` 16)}

setTarget :: Word32 -> Instruction -> Instruction
setTarget v i = i {instructionFields = v}

-- | The FP and other aliases of the same fields (instruction.h).
instructionFloatingPointSource, instructionFloatingPointTarget, instructionFloatingPointDestination, instructionBase, instructionCondition :: Instruction -> Int
instructionFloatingPointSource = instructionDestinationRegister
instructionFloatingPointTarget = instructionTargetRegister
instructionFloatingPointDestination = instructionShiftAmount
instructionBase = instructionSourceRegister
instructionCondition = instructionSourceRegister

-- | The branch displacement in bytes: the 16-bit word offset,
-- sign-extended, times four.
branchOffset :: Instruction -> Int32
branchOffset i = fromIntegral (instructionImmediate i) * 4

-- | A label in the symbol table (C: @label@).  The changeable parts are
-- references; 'lblId' gives each label an identity, like the C pointer.
data Label = Label
  { labelIdentifier :: Int,
    labelName :: String,
    -- | its address (or constant value); 0 if undefined
    labelAddress :: IORef Word32,
    -- | declared @.globl@
    labelGlobal :: IORef Bool,
    -- | lives in the small data area off @$gp@
    labelGlobalPointer :: IORef Bool,
    -- | a constant (@name = value@)
    labelConstant :: IORef Bool,
    -- | uses waiting for the label to be defined
    labelUses :: IORef [LabelUse]
  }

instance Eq Label where
  a == b = labelIdentifier a == labelIdentifier b

-- | A place that used a label before it was defined (C: @label_use@).
data LabelUse
  = -- | an instruction, and the address it was assembled at
    UseInstruction InstructionRef Word32
  | -- | a data item: its address, the constant added to the label, and its
    -- width in bytes (1 for .byte, 2 for .half, 4 for .word)
    UseData Word32 Int32 Int
