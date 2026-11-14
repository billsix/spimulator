-- | Encoding and decoding instructions, and questions about opcodes
-- (C: the pure parts of src/instruction.c).
--
-- Everything here is a pure function: an instruction record in, a 32-bit
-- word out ('instEncode'), or the other way around ('instDecode').
module Instruction
  ( instEncode,
    instEncodeIO,
    instDecode,
    opcodeIsBranch,
    opcodeIsNullifiedBranch,
    opcodeIsTrueBranch,
    opcodeIsJump,
    opcodeIsLoadStore,
    tok,
    isTok,
    binRs,
    binRt,
    binRd,
    binShamt,
  )
where

import Data.Bits (shiftL, shiftR, (.&.), (.|.))
import Data.Int (Int32)
import Data.Word (Word32)
import OpcodeTables
import OpcodeTypes (OpType (..))
import Opcodes
import Output (fatalError)
import Types

-- | The token number of an opcode (a short name for 'opcodeToken').
tok :: Opcode -> Int
tok = opcodeToken

-- | Is the instruction's opcode token this opcode?
isTok :: Opcode -> Int -> Bool
isTok op t = t == tok op

-- | @REGS(R, O)@: a 5-bit register field placed at bit O.
regs :: Int -> Int -> Int32
regs r o = fromIntegral ((r .&. 0x1f) `shiftL` o)

low16 :: Instruction -> Int32
low16 i = fromIntegral (instImm i) .&. 0xffff

-- | The binary encoding, or Nothing for a row with no encoding rule (the C
-- engine stops with "Unknown instruction type in inst_encoding").
instEncode :: Instruction -> Maybe Int32
instEncode i = case lookupByValue iOpcodeTable (inOpcode i) of
  Nothing -> Just 0
  Just entry ->
    let a :: Int32
        a = nvvValue2 entry
        rs :: Int
        rs = instRs i
        rt :: Int
        rt = instRt i
        rd :: Int
        rd = instRd i
     in case opcodeTypeOfToken (inOpcode i) of
          Just BRANCH_COPROCESSOR_CONDITION_INSTRUCTION -> Just (a .|. regs ((rt `shiftR` 2) `shiftL` 2) 16 .|. low16 i)
          Just BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION -> Just (a .|. regs rs 21 .|. low16 i)
          Just SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> Just (a .|. regs rs 21 .|. low16 i)
          Just TARGET_REGISTER_IMMEDIATE_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just TARGET_REGISTER_ADDRESS_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just SOURCE_REGISTER_INSTRUCTION -> Just (a .|. regs rs 21)
          Just DESTINATION_REGISTER_INSTRUCTION -> Just (a .|. regs rd 11)
          Just TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> Just (a .|. regs rt 16 .|. regs rd 11)
          Just SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16)
          Just DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rd 11)
          Just DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION -> Just (a .|. regs rt 16 .|. regs rd 11 .|. regs (instShamt i) 6)
          Just DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION -> Just (a .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> Just (a .|. regs rt 16 .|. regs (instFs i) 11)
          Just FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION ->
            Just (a .|. regs (instFt i) 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6 .|. fromIntegral (instCond i))
          Just FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION -> Just (a .|. regs (instFt i) 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just MOVE_CONDITIONAL_TYPE_INSTRUCTION -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION -> Just (a .|. regs rt 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just J_TYPE_INSTRUCTION -> Just (fromIntegral (fromIntegral a .|. instTarget i :: Word32))
          Just NO_ARGUMENT_TYPE_INSTRUCTION -> Just a
          _ -> Nothing

-- | 'instEncode', stopping the engine as C does when there is no rule.
instEncodeIO :: Instruction -> IO Int32
instEncodeIO i = maybe (fatalError "Unknown instruction type in inst_encoding\n") pure (instEncode i)

-- | The fields of a binary instruction (C: @BIN_RS@ and friends).
binRs, binRt, binRd, binShamt :: Int32 -> Int
binRs v = fromIntegral ((v `shiftR` 21) .&. 0x1f)
binRt v = fromIntegral ((v `shiftR` 16) .&. 0x1f)
binRd v = fromIntegral ((v `shiftR` 11) .&. 0x1f)
binShamt v = fromIntegral ((v `shiftR` 6) .&. 0x1f)

-- | Turn a 32-bit word back into an instruction (C: @inst_decode@).  A word
-- that is no instruction decodes to opcode 0, which the disassembler shows
-- as @<unknown instruction 0>@.
instDecode :: Int32 -> Instruction
instDecode val =
  case lookupByValue aOpcodeTable (fromIntegral aOpcode) of
    Nothing -> invalid
    Just entry ->
      let op :: Int
          op = nvvValue2 entry
       in case opcodeTypeOfToken op of
            Just BRANCH_COPROCESSOR_CONDITION_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION -> mkI op (binRs val) 0
            Just SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> mkI op (binRs val) 0
            Just TARGET_REGISTER_IMMEDIATE_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just TARGET_REGISTER_ADDRESS_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just SOURCE_REGISTER_INSTRUCTION -> mkR op (binRs val) 0 0 0
            Just DESTINATION_REGISTER_INSTRUCTION -> mkR op 0 0 (binRd val) 0
            Just TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> mkR op 0 (binRt val) (binRd val) 0
            Just SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> mkR op (binRs val) (binRt val) 0 0
            Just DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION -> mkR op (binRs val) 0 (binRd val) 0
            Just DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION -> mkR op 0 (binRt val) (binRd val) (binShamt val)
            Just DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION -> mkI op (binRs val) (binRt val)
            Just FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION -> mkCoR op (binRd val) 0 (binShamt val)
            Just FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> mkR op 0 (binRt val) (binRd val) 0
            Just FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION ->
              setRs (fromIntegral (val .&. 0xf)) (mkCoR op (binRd val) (binRt val) (binShamt val))
            Just FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION -> mkCoR op (binRd val) (binRt val) (binShamt val)
            Just MOVE_CONDITIONAL_TYPE_INSTRUCTION -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION -> mkCoR op (binRd val) (binRt val) (binShamt val)
            Just J_TYPE_INSTRUCTION -> setTarget (fromIntegral (val .&. 0x3ffffff)) (base op)
            Just NO_ARGUMENT_TYPE_INSTRUCTION -> mkR op 0 0 0 0
            _ -> invalid
  where
    top :: Word32
    top = fromIntegral val .&. 0xfc000000 :: Word32
    v :: Word32
    v = fromIntegral val :: Word32
    aOpcode :: Int32
    aOpcode = fromIntegral (group top)
    group :: Word32 -> Word32
    group t
      | t == 0 || t == 0x70000000 = t .|. (v .&. 0x3f) -- SPECIAL, SPECIAL2
      | t == 0x04000000 = t .|. (v .&. 0x001f0000) -- REGIMM
      | t == 0x40000000 = t .|. (v .&. 0x03e00000) .|. (v .&. 0x1f) -- COP0
      | t == 0x44000000 =
          -- COP1: bc1f/bc1t keep bit 16, the rest the function field
          t .|. (v .&. 0x03e00000) .|. (if v .&. 0xff000000 == 0x45000000 then v .&. 0x00010000 else v .&. 0x3f)
      | t == 0x48000000 = t .|. (v .&. 0x03e00000) -- COP2: the rs field
      | t == 0x4c000000 = t .|. (v .&. 0x3f) -- COP1X: the funct field
      | otherwise = t
    base :: Int -> Instruction
    base op = (emptyInstruction op) {inEncoding = val}
    invalid :: Instruction
    invalid = base 0
    mkR :: Int -> Int -> Int -> Int -> Int -> Instruction
    mkR op rs rt rd sh = setShamt sh (setRd rd (setRt rt (setRs rs (base op))))
    mkCoR :: Int -> Int -> Int -> Int -> Instruction
    mkCoR op fs ft fd = setShamt fd (setRt ft (setRd fs (base op)))
    mkI :: Int -> Int -> Int -> Instruction
    mkI op rs rt = setImm (val .&. 0xffff) (setRt rt (setRs rs (base op)))

-- | A conditional branch, including the branch pseudo-instructions
-- (C: @opcode_is_branch@).
opcodeIsBranch :: Int -> Bool
opcodeIsBranch t = maybe False (`elem` branches) (opcodeOfToken t)
  where
    branches :: [Opcode]
    branches =
      [ TOKEN_BC1F_OPCODE,
        TOKEN_BC1FL_OPCODE,
        TOKEN_BC1T_OPCODE,
        TOKEN_BC1TL_OPCODE,
        TOKEN_BC2F_OPCODE,
        TOKEN_BC2FL_OPCODE,
        TOKEN_BC2T_OPCODE,
        TOKEN_BC2TL_OPCODE,
        TOKEN_BEQ_OPCODE,
        TOKEN_BEQL_OPCODE,
        TOKEN_BEQZ_PSEUDO_OP,
        TOKEN_BGE_PSEUDO_OP,
        TOKEN_BGEU_PSEUDO_OP,
        TOKEN_BGEZ_OPCODE,
        TOKEN_BGEZAL_OPCODE,
        TOKEN_BGEZALL_OPCODE,
        TOKEN_BGEZL_OPCODE,
        TOKEN_BGT_PSEUDO_OP,
        TOKEN_BGTU_PSEUDO_OP,
        TOKEN_BGTZ_OPCODE,
        TOKEN_BGTZL_OPCODE,
        TOKEN_BLE_PSEUDO_OP,
        TOKEN_BLEU_PSEUDO_OP,
        TOKEN_BLEZ_OPCODE,
        TOKEN_BLEZL_OPCODE,
        TOKEN_BLT_PSEUDO_OP,
        TOKEN_BLTU_PSEUDO_OP,
        TOKEN_BLTZ_OPCODE,
        TOKEN_BLTZAL_OPCODE,
        TOKEN_BLTZALL_OPCODE,
        TOKEN_BLTZL_OPCODE,
        TOKEN_BNE_OPCODE,
        TOKEN_BNEL_OPCODE,
        TOKEN_BNEZ_PSEUDO_OP
      ]

-- | A branch-likely, which skips its delay slot when not taken.
opcodeIsNullifiedBranch :: Opcode -> Bool
opcodeIsNullifiedBranch op =
  op
    `elem` [ TOKEN_BC1FL_OPCODE,
             TOKEN_BC1TL_OPCODE,
             TOKEN_BC2FL_OPCODE,
             TOKEN_BC2TL_OPCODE,
             TOKEN_BEQL_OPCODE,
             TOKEN_BGEZALL_OPCODE,
             TOKEN_BGEZL_OPCODE,
             TOKEN_BGTZL_OPCODE,
             TOKEN_BLEZL_OPCODE,
             TOKEN_BLTZALL_OPCODE,
             TOKEN_BLTZL_OPCODE,
             TOKEN_BNEL_OPCODE
           ]

-- | A coprocessor branch on a true condition.
opcodeIsTrueBranch :: Opcode -> Bool
opcodeIsTrueBranch op = op `elem` [TOKEN_BC1T_OPCODE, TOKEN_BC1TL_OPCODE, TOKEN_BC2T_OPCODE, TOKEN_BC2TL_OPCODE]

-- | @j@ or @jal@.
opcodeIsJump :: Int -> Bool
opcodeIsJump t = t == tok TOKEN_J_OPCODE || t == tok TOKEN_JAL_OPCODE

-- | A load or a store.
opcodeIsLoadStore :: Int -> Bool
opcodeIsLoadStore t = maybe False (`elem` loadStores) (opcodeOfToken t)
  where
    loadStores :: [Opcode]
    loadStores =
      [ TOKEN_LB_OPCODE,
        TOKEN_LBU_OPCODE,
        TOKEN_LH_OPCODE,
        TOKEN_LHU_OPCODE,
        TOKEN_LL_OPCODE,
        TOKEN_LDC1_OPCODE,
        TOKEN_LDC2_OPCODE,
        TOKEN_LW_OPCODE,
        TOKEN_LWC1_OPCODE,
        TOKEN_LWC2_OPCODE,
        TOKEN_LWL_OPCODE,
        TOKEN_LWR_OPCODE,
        TOKEN_SB_OPCODE,
        TOKEN_SC_OPCODE,
        TOKEN_SH_OPCODE,
        TOKEN_SDC1_OPCODE,
        TOKEN_SDC2_OPCODE,
        TOKEN_SW_OPCODE,
        TOKEN_SWC1_OPCODE,
        TOKEN_SWC2_OPCODE,
        TOKEN_SWL_OPCODE,
        TOKEN_SWR_OPCODE
      ]
