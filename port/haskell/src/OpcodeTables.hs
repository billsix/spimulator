-- | The lookup tables the C engine builds from the opcode table, and the C
-- library search it uses on them.
--
-- The C engine searches sorted tables with @bsearch@ (instruction.c,
-- scanner.c, spim-utils.c).  The name and token tables have one row per key.
-- The decode table (binary encoding -> token) does too: several rows share
-- an encoding (for example @trunc.w.s@ and @suxc1@ are both @0x4600000d@), and
-- the generated 'opcodeDecodeRows' holds the one row each encoding decodes
-- to, already sorted (include/opcodes.tsv's decode column).
module OpcodeTables
  ( NameValueValue (..),
    bsearchBy,
    nameTable,
    iOpcodeTable,
    aOpcodeTable,
    keywordTable,
    lookupByValue,
    opcodeTokenName,
    opcodeTypeOfToken,
    opcodeOfToken,
    findOpcodeType,
  )
where

import Data.Array (Array, bounds, listArray, (!))
import Data.Int (Int32)
import Data.List (sortBy)
import Data.Ord (comparing)
import OpcodeTypes (OpType (..))
import Opcodes

-- | C's @name_val_val@: a string and two integers.
data NameValueValue a = NameValueValue
  { nvvName :: String,
    nvvValue1 :: Int,
    nvvValue2 :: a
  }

-- | glibc's @bsearch@: halve the range [l, u) until the middle element
-- compares equal.  The comparison is "key versus element".
bsearchBy :: forall e. (e -> Ordering) -> Array Int e -> Maybe e
bsearchBy cmp array = go lo (hi + 1)
  where
    lo :: Int
    hi :: Int
    (lo, hi) = bounds array
    go :: Int -> Int -> Maybe e
    go l u
      | l >= u = Nothing
      | otherwise =
          let index :: Int
              index = (l + u) `quot` 2
              e :: e
              e = array ! index
           in case cmp e of
                LT -> go l index
                GT -> go (index + 1) u
                EQ -> Just e

-- | Turn a list into a zero-based array.
toArray :: [e] -> Array Int e
toArray xs = listArray (0, length xs - 1) xs

-- | Sort on the first value (the keys of these tables are unique).
sortedOnValue1 :: [NameValueValue a] -> Array Int (NameValueValue a)
sortedOnValue1 = toArray . sortBy (comparing nvvValue1)

-- | token -> (mnemonic, operand type)  (C: @name_tbl@).
nameTable :: Array Int (NameValueValue OpType)
nameTable =
  sortedOnValue1 [NameValueValue (rowName r) (opcodeToken (rowOpcode r)) (rowType r) | r <- opcodeRows]

-- | token -> binary opcode  (C: @i_opcode_tbl@).
iOpcodeTable :: Array Int (NameValueValue Int32)
iOpcodeTable =
  sortedOnValue1 [NameValueValue (rowName r) (opcodeToken (rowOpcode r)) (rowEncoding r) | r <- opcodeRows]

-- | binary opcode -> token  (C: @a_opcode_tbl@).
aOpcodeTable :: Array Int (NameValueValue Int)
aOpcodeTable =
  toArray
    [NameValueValue (rowName r) (fromIntegral (rowEncoding r)) (opcodeToken (rowOpcode r)) | r <- opcodeDecodeRows]

-- | mnemonic -> (token, operand type), in file order, which the C
-- scanner searches by name (C: @keyword_tbl@).
keywordTable :: Array Int (NameValueValue OpType)
keywordTable =
  toArray [NameValueValue (rowName r) (opcodeToken (rowOpcode r)) (rowType r) | r <- opcodeRows]

-- | C's @map_int_to_name_val_val@.
lookupByValue :: Array Int (NameValueValue a) -> Int -> Maybe (NameValueValue a)
lookupByValue table k = bsearchBy (\e -> compare k (nvvValue1 e)) table

-- | The mnemonic of a token, or @<unknown>@ (C: @op_token_name@).
opcodeTokenName :: Int -> String
opcodeTokenName t = maybe "<unknown>" nvvName (lookupByValue nameTable t)

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
findOpcodeType :: Opcode -> OpType
findOpcodeType op = rowType (opcodeRowArray ! fromEnum op)

opcodeRowArray :: Array Int OpRow
opcodeRowArray = toArray opcodeRows
