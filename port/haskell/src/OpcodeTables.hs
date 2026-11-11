-- | The lookup tables the C engine builds from opcodes.h, and the C
-- library searches it uses on them.
--
-- The C engine sorts copies of the table with @qsort@ and searches them
-- with @bsearch@ (instruction.c, scanner.c, spim-utils.c).  A few binary
-- encodings appear on two rows (for example @trunc.w.s@ and @suxc1@ are
-- both @0x4600000d@); which of the two @bsearch@ finds depends on where the
-- rows sit in the sorted array.  glibc's @qsort@ is a stable merge sort,
-- so this module sorts stably and searches with glibc's exact @bsearch@
-- loop, and decodes those encodings to the same mnemonic the C engine does.
module OpcodeTables
  ( NameValVal (..)
  , bsearchBy
  , nameTable
  , iOpcodeTable
  , aOpcodeTable
  , keywordTable
  , lookupByValue
  , opTokenName
  , opcodeTypeOfToken
  , opcodeOfToken
  , findOpType
  ) where

import Data.Array (Array, bounds, listArray, (!))
import Data.Int (Int32)
import Data.List (sortBy)
import Data.Ord (comparing)
import OpcodeTypes (OpType (..))
import Opcodes

-- | C's @name_val_val@: a string and two integers.
data NameValVal a = NameValVal
  { nvvName :: String
  , nvvValue1 :: Int
  , nvvValue2 :: a
  }

-- | glibc's @bsearch@: halve the range [l, u) until the middle element
-- compares equal.  The comparison is "key versus element".
bsearchBy :: (e -> Ordering) -> Array Int e -> Maybe e
bsearchBy cmp arr = go lo (hi + 1)
  where
    (lo, hi) = bounds arr
    go l u
      | l >= u = Nothing
      | otherwise =
          let idx = (l + u) `quot` 2
              e = arr ! idx
           in case cmp e of
                LT -> go l idx
                GT -> go (idx + 1) u
                EQ -> Just e

-- | Turn a list into a zero-based array.
toArray :: [e] -> Array Int e
toArray xs = listArray (0, length xs - 1) xs

-- | Sort on the first value, stably (glibc's merge sort keeps equal keys
-- in their original order).
sortedOnValue1 :: [NameValVal a] -> Array Int (NameValVal a)
sortedOnValue1 = toArray . sortBy (comparing nvvValue1)

-- | token -> (mnemonic, operand type)  (C: @name_tbl@).
nameTable :: Array Int (NameValVal OpType)
nameTable =
  sortedOnValue1 [NameValVal (rowName r) (opcodeToken (rowOpcode r)) (rowType r) | r <- opcodeRows]

-- | token -> binary opcode  (C: @i_opcode_tbl@).
iOpcodeTable :: Array Int (NameValVal Int32)
iOpcodeTable =
  sortedOnValue1 [NameValVal (rowName r) (opcodeToken (rowOpcode r)) (rowEncoding r) | r <- opcodeRows]

-- | binary opcode -> token  (C: @a_opcode_tbl@).
aOpcodeTable :: Array Int (NameValVal Int)
aOpcodeTable =
  sortedOnValue1
    [NameValVal (rowName r) (fromIntegral (rowEncoding r)) (opcodeToken (rowOpcode r)) | r <- opcodeRows]

-- | mnemonic -> (token, operand type), in file order, which the C
-- scanner searches by name (C: @keyword_tbl@).
keywordTable :: Array Int (NameValVal OpType)
keywordTable =
  toArray [NameValVal (rowName r) (opcodeToken (rowOpcode r)) (rowType r) | r <- opcodeRows]

-- | C's @map_int_to_name_val_val@.
lookupByValue :: Array Int (NameValVal a) -> Int -> Maybe (NameValVal a)
lookupByValue tbl k = bsearchBy (\e -> compare k (nvvValue1 e)) tbl

-- | The mnemonic of a token, or @<unknown>@ (C: @op_token_name@).
opTokenName :: Int -> String
opTokenName t = maybe "<unknown>" nvvName (lookupByValue nameTable t)

-- | The operand type of a token, if it is an opcode token.
opcodeTypeOfToken :: Int -> Maybe OpType
opcodeTypeOfToken t = nvvValue2 <$> lookupByValue nameTable t

-- | The 'Opcode' constructor of a token number, if there is one.
opcodeOfToken :: Int -> Maybe Opcode
opcodeOfToken t
  | t >= firstOpcodeToken && t - firstOpcodeToken <= fromEnum (maxBound :: Opcode) =
      Just (toEnum (t - firstOpcodeToken))
  | otherwise = Nothing

-- | The parser's linear search of the table (C: @find_op_type@).
findOpType :: Opcode -> OpType
findOpType op = rowType (opcodeRowArray ! fromEnum op)

opcodeRowArray :: Array Int OpRow
opcodeRowArray = toArray opcodeRows
