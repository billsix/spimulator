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
          Just BRANCH_COPROC_COND_INST -> Just (a .|. regs ((rt `shiftR` 2) `shiftL` 2) 16 .|. low16 i)
          Just BRANCH_RS_LABEL_INST -> Just (a .|. regs rs 21 .|. low16 i)
          Just RS_IMM_INST -> Just (a .|. regs rs 21 .|. low16 i)
          Just RT_IMM_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just RT_RS_IMM_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just BRANCH_RS_RT_LABEL_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just RT_ADDR_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just RS_INST -> Just (a .|. regs rs 21)
          Just RD_INST -> Just (a .|. regs rd 11)
          Just RT_COPREG_INST -> Just (a .|. regs rt 16 .|. regs rd 11)
          Just RS_RT_INST -> Just (a .|. regs rs 21 .|. regs rt 16)
          Just RD_RS_INST -> Just (a .|. regs rs 21 .|. regs rd 11)
          Just RD_RT_SHAMT_INST -> Just (a .|. regs rt 16 .|. regs rd 11 .|. regs (instShamt i) 6)
          Just RD_RS_RT_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just RD_RT_RS_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just FP_FT_ADDR_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. low16 i)
          Just FP_FD_FS_INST -> Just (a .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just FP_RT_COPREG_INST -> Just (a .|. regs rt 16 .|. regs (instFs i) 11)
          Just FP_CMP_FS_FT_INST ->
            Just (a .|. regs (instFt i) 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6 .|. fromIntegral (instCond i))
          Just FP_FD_FS_FT_INST -> Just (a .|. regs (instFt i) 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just MOVC_TYPE_INST -> Just (a .|. regs rs 21 .|. regs rt 16 .|. regs rd 11)
          Just FP_MOVC_TYPE_INST -> Just (a .|. regs rt 16 .|. regs (instFs i) 11 .|. regs (instFd i) 6)
          Just J_TYPE_INST -> Just (fromIntegral (fromIntegral a .|. instTarget i :: Word32))
          Just NOARG_TYPE_INST -> Just a
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
            Just BRANCH_COPROC_COND_INST -> mkI op (binRs val) (binRt val)
            Just BRANCH_RS_LABEL_INST -> mkI op (binRs val) 0
            Just RS_IMM_INST -> mkI op (binRs val) 0
            Just RT_IMM_INST -> mkI op (binRs val) (binRt val)
            Just RT_RS_IMM_INST -> mkI op (binRs val) (binRt val)
            Just BRANCH_RS_RT_LABEL_INST -> mkI op (binRs val) (binRt val)
            Just RT_ADDR_INST -> mkI op (binRs val) (binRt val)
            Just RS_INST -> mkR op (binRs val) 0 0 0
            Just RD_INST -> mkR op 0 0 (binRd val) 0
            Just RT_COPREG_INST -> mkR op 0 (binRt val) (binRd val) 0
            Just RS_RT_INST -> mkR op (binRs val) (binRt val) 0 0
            Just RD_RS_INST -> mkR op (binRs val) 0 (binRd val) 0
            Just RD_RT_SHAMT_INST -> mkR op 0 (binRt val) (binRd val) (binShamt val)
            Just RD_RS_RT_INST -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just RD_RT_RS_INST -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just FP_FT_ADDR_INST -> mkI op (binRs val) (binRt val)
            Just FP_FD_FS_INST -> mkCoR op (binRd val) 0 (binShamt val)
            Just FP_RT_COPREG_INST -> mkR op 0 (binRt val) (binRd val) 0
            Just FP_CMP_FS_FT_INST ->
              setRs (fromIntegral (val .&. 0xf)) (mkCoR op (binRd val) (binRt val) (binShamt val))
            Just FP_FD_FS_FT_INST -> mkCoR op (binRd val) (binRt val) (binShamt val)
            Just MOVC_TYPE_INST -> mkR op (binRs val) (binRt val) (binRd val) 0
            Just FP_MOVC_TYPE_INST -> mkCoR op (binRd val) (binRt val) (binShamt val)
            Just J_TYPE_INST -> setTarget (fromIntegral (val .&. 0x3ffffff)) (base op)
            Just NOARG_TYPE_INST -> mkR op 0 0 0 0
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
      [ TOK_BC1F_OPCODE,
        TOK_BC1FL_OPCODE,
        TOK_BC1T_OPCODE,
        TOK_BC1TL_OPCODE,
        TOK_BC2F_OPCODE,
        TOK_BC2FL_OPCODE,
        TOK_BC2T_OPCODE,
        TOK_BC2TL_OPCODE,
        TOK_BEQ_OPCODE,
        TOK_BEQL_OPCODE,
        TOK_BEQZ_PSEUDO_OP,
        TOK_BGE_PSEUDO_OP,
        TOK_BGEU_PSEUDO_OP,
        TOK_BGEZ_OPCODE,
        TOK_BGEZAL_OPCODE,
        TOK_BGEZALL_OPCODE,
        TOK_BGEZL_OPCODE,
        TOK_BGT_PSEUDO_OP,
        TOK_BGTU_PSEUDO_OP,
        TOK_BGTZ_OPCODE,
        TOK_BGTZL_OPCODE,
        TOK_BLE_PSEUDO_OP,
        TOK_BLEU_PSEUDO_OP,
        TOK_BLEZ_OPCODE,
        TOK_BLEZL_OPCODE,
        TOK_BLT_PSEUDO_OP,
        TOK_BLTU_PSEUDO_OP,
        TOK_BLTZ_OPCODE,
        TOK_BLTZAL_OPCODE,
        TOK_BLTZALL_OPCODE,
        TOK_BLTZL_OPCODE,
        TOK_BNE_OPCODE,
        TOK_BNEL_OPCODE,
        TOK_BNEZ_PSEUDO_OP
      ]

-- | A branch-likely, which skips its delay slot when not taken.
opcodeIsNullifiedBranch :: Opcode -> Bool
opcodeIsNullifiedBranch op =
  op
    `elem` [ TOK_BC1FL_OPCODE,
             TOK_BC1TL_OPCODE,
             TOK_BC2FL_OPCODE,
             TOK_BC2TL_OPCODE,
             TOK_BEQL_OPCODE,
             TOK_BGEZALL_OPCODE,
             TOK_BGEZL_OPCODE,
             TOK_BGTZL_OPCODE,
             TOK_BLEZL_OPCODE,
             TOK_BLTZALL_OPCODE,
             TOK_BLTZL_OPCODE,
             TOK_BNEL_OPCODE
           ]

-- | A coprocessor branch on a true condition.
opcodeIsTrueBranch :: Opcode -> Bool
opcodeIsTrueBranch op = op `elem` [TOK_BC1T_OPCODE, TOK_BC1TL_OPCODE, TOK_BC2T_OPCODE, TOK_BC2TL_OPCODE]

-- | @j@ or @jal@.
opcodeIsJump :: Int -> Bool
opcodeIsJump t = t == tok TOK_J_OPCODE || t == tok TOK_JAL_OPCODE

-- | A load or a store.
opcodeIsLoadStore :: Int -> Bool
opcodeIsLoadStore t = maybe False (`elem` loadStores) (opcodeOfToken t)
  where
    loadStores :: [Opcode]
    loadStores =
      [ TOK_LB_OPCODE,
        TOK_LBU_OPCODE,
        TOK_LH_OPCODE,
        TOK_LHU_OPCODE,
        TOK_LL_OPCODE,
        TOK_LDC1_OPCODE,
        TOK_LDC2_OPCODE,
        TOK_LW_OPCODE,
        TOK_LWC1_OPCODE,
        TOK_LWC2_OPCODE,
        TOK_LWL_OPCODE,
        TOK_LWR_OPCODE,
        TOK_SB_OPCODE,
        TOK_SC_OPCODE,
        TOK_SH_OPCODE,
        TOK_SDC1_OPCODE,
        TOK_SDC2_OPCODE,
        TOK_SW_OPCODE,
        TOK_SWC1_OPCODE,
        TOK_SWC2_OPCODE,
        TOK_SWL_OPCODE,
        TOK_SWR_OPCODE
      ]
