-- | Disassembling instructions, and breakpoints (C: @format_an_inst@ and
-- friends in src/instruction.c, the breakpoint list in src/spim-utils.c,
-- and @print_mem@ in src/memory.c).
--
-- A breakpoint replaces the instruction in memory with one shared @break@
-- instruction and keeps the original aside; the disassembler shows the
-- original, marked with @*@.
module InstFormat
  ( formatAnInstruction,
    instructionToString,
    printInstruction,
    formatImmediateExpression,
    instructionIsBreakpoint,
    addBreakpoint,
    deleteBreakpoint,
    deleteAllBreakpoints,
    listBreakpoints,
    printMemory,
    breakInstruction,
  )
where

import Control.Monad (forM_, when)
import Data.IORef (newIORef)
import Data.Int (Int32)
import Data.Word (Word32)
import Instruction (opcodeIsLoadStore, token)
import Memory
import OpcodeTables
import OpcodeTypes (OpType (..))
import Opcodes
import Output
import Registers
import State
import Text.Printf (printf)
import Types

-- | The one shared breakpoint instruction, @break 1@, made on first use.
breakInstruction :: Spim -> IO InstructionRef
breakInstruction s = do
  existing :: Maybe InstructionRef <- get (cachedBreakInstruction s)
  case existing of
    Just b -> pure b
    Nothing -> do
      b :: InstructionRef <- newIORef (setDestinationRegister 1 (emptyInstruction (token TOKEN_BREAK_OPCODE)))
      set (cachedBreakInstruction s) (Just b)
      pure b

-- | Is there a breakpoint at this address?  (Reading a bad address here
-- raises an exception, as in C.)
instructionIsBreakpoint :: Spim -> Word32 -> IO Bool
instructionIsBreakpoint s address = do
  b :: InstructionRef <- breakInstruction s
  instruction :: Maybe InstructionRef <- memoryReadInstruction s address
  pure (instruction == Just b)

-- | Put the breakpoint instruction at an address; give back what was there
-- (C: @set_breakpoint@), or Nothing if it cannot be done.
setBreakpoint :: Spim -> Word32 -> IO (Maybe InstructionRef)
setBreakpoint s address = do
  b :: InstructionRef <- breakInstruction s
  set (exceptionOccurred s) False
  old :: Maybe InstructionRef <- memoryReadInstruction s address
  if old == Just b
    then pure Nothing
    else do
      memoryWriteInstruction s address (Just b)
      exception :: Bool <- get (exceptionOccurred s)
      pure (if exception then Nothing else old)

-- | C: @add_breakpoint@.
addBreakpoint :: Spim -> Word32 -> IO ()
addBreakpoint s address = do
  old :: Maybe InstructionRef <- setBreakpoint s address
  case old of
    Just instruction -> modify (breakpoints s) ((address, instruction) :)
    Nothing -> do
      exception :: Bool <- get (exceptionOccurred s)
      if exception
        then cError (printf "Cannot put a breakpoint at address 0x%08x\n" address)
        else cError (printf "No instruction to breakpoint at address 0x%08x\n" address)

-- | Remove every breakpoint at an address, putting the original
-- instructions back (C: @delete_breakpoint@).
deleteBreakpoint :: Spim -> Word32 -> IO ()
deleteBreakpoint s address = do
  bs :: [(Word32, InstructionRef)] <- get (breakpoints s)
  let here :: [(Word32, InstructionRef)]
      others :: [(Word32, InstructionRef)]
      (here, others) = foldr split ([], []) bs
      split :: (Word32, b) -> ([(Word32, b)], [(Word32, b)]) -> ([(Word32, b)], [(Word32, b)])
      split b@(a, _) (h, o) = if a == address then (b : h, o) else (h, b : o)
  forM_ here $ \(_, instruction) -> memoryWriteInstruction s address (Just instruction)
  set (breakpoints s) others
  when (null here) $ cError (printf "No breakpoint to delete at 0x%08x\n" address)

deleteAllBreakpoints :: Spim -> IO ()
deleteAllBreakpoints s = set (breakpoints s) []

-- | C: @list_breakpoints@.
listBreakpoints :: Spim -> IO ()
listBreakpoints s = do
  bs :: [(Word32, InstructionRef)] <- get (breakpoints s)
  if null bs
    then writeError "No breakpoints set\n"
    else forM_ bs $ \(a, _) -> writeError (printf "Breakpoint at 0x%08x\n" a)

registerNumber :: Int -> String
registerNumber n = case drop (n `mod` 32) registerNames of
  name : _ -> name
  [] -> show n -- not reached: registerNames names all 32 registers

-- | One line of disassembly (C: @format_an_inst@): address, encoding,
-- mnemonic and operands, the symbolic operand in brackets, and the source
-- line it came from, starting in column 57.
formatAnInstruction :: Spim -> Maybe InstructionRef -> Word32 -> IO String
formatAnInstruction s minst address = do
  bp :: Bool <- if address /= 0 then instructionIsBreakpoint s address else pure False
  if bp
    then do
      deleteBreakpoint s address
      inner :: Maybe InstructionRef <- memoryReadInstruction s address
      line :: String <- formatAnInstruction s inner address
      addBreakpoint s address
      pure ('*' : line)
    else case minst of
      Nothing -> pure (printf "[0x%08x]\t<none>\n" address)
      Just ref -> do
        i :: Instruction <- get ref
        let start :: String
            start = printf "[0x%08x]\t" address
        case lookupByValue nameTable (instructionOpcode i) of
          Nothing -> pure (start ++ printf "<unknown instruction %d>\n" (instructionOpcode i))
          Just entry -> do
            operands :: Maybe (String, String) <- formatOperands i (nvvValue2 entry)
            let named :: String
                named = start ++ printf "0x%08x  %s" (fromIntegral (instructionEncoding i) :: Word32) (nvvName entry)
                body :: String
                body = case operands of
                  Nothing -> named
                  Just ("nop-erase", _) -> take (length named - 3) named ++ "nop"
                  Just (_, ops) -> named ++ ops
                symbolic :: String
                symbolic = case instructionExpression i of
                  Just e
                    | Just _ <- immediateExpressionSymbol e ->
                        " [" ++ formatImmediateExpression e (if opcodeIsLoadStore (instructionOpcode i) then instructionBase i else -1) ++ "]"
                  _ -> ""
                text :: String
                text = body ++ symbolic
                withSource :: String
                withSource = case instructionSource i of
                  Nothing -> text
                  Just source -> text ++ replicate (57 - length text) ' ' ++ "; " ++ source
            pure (withSource ++ "\n")

-- | The operand text for each operand shape.  Nothing means "no operands".
formatOperands :: Instruction -> OpType -> IO (Maybe (String, String))
formatOperands i ty = case ty of
  BRANCH_COPROCESSOR_CONDITION_INSTRUCTION -> ok (printf "%d %d" (instructionTargetRegister i `div` 4) (branchOffset i))
  BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION -> ok (printf " $%s %d" (registerNumber (instructionSourceRegister i)) (branchOffset i))
  SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> ok (printf " $%s, %d" (registerNumber (instructionSourceRegister i)) (instructionImmediate i))
  TARGET_REGISTER_IMMEDIATE_INSTRUCTION -> ok (printf " $%s, %d" (registerNumber (instructionTargetRegister i)) (instructionImmediate i))
  TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION -> ok (printf " $%s, $%s, %d" (registerNumber (instructionTargetRegister i)) (registerNumber (instructionSourceRegister i)) (instructionImmediate i))
  BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION -> ok (printf " $%s, $%s, %d" (registerNumber (instructionSourceRegister i)) (registerNumber (instructionTargetRegister i)) (branchOffset i))
  TARGET_REGISTER_ADDRESS_INSTRUCTION -> ok (printf " $%s, %d($%s)" (registerNumber (instructionTargetRegister i)) (instructionImmediate i) (registerNumber (instructionBase i)))
  SOURCE_REGISTER_INSTRUCTION -> ok (" $" ++ registerNumber (instructionSourceRegister i))
  DESTINATION_REGISTER_INSTRUCTION -> ok (" $" ++ registerNumber (instructionDestinationRegister i))
  TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> ok (printf " $%s, $%s" (registerNumber (instructionTargetRegister i)) (registerNumber (instructionDestinationRegister i)))
  SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> ok (printf " $%s, $%s" (registerNumber (instructionSourceRegister i)) (registerNumber (instructionTargetRegister i)))
  DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION -> ok (printf " $%s, $%s" (registerNumber (instructionDestinationRegister i)) (registerNumber (instructionSourceRegister i)))
  DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION
    -- nop is sll $0, $0, 0: the C engine erases the last three letters
    -- of the mnemonic and writes "nop" in their place.
    | instructionDestinationRegister i == 0 && instructionTargetRegister i == 0 && instructionShiftAmount i == 0 -> pure (Just ("nop-erase", ""))
    | otherwise -> ok (printf " $%s, $%s, %d" (registerNumber (instructionDestinationRegister i)) (registerNumber (instructionTargetRegister i)) (instructionShiftAmount i))
  DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION -> ok (printf " $%s, $%s, $%s" (registerNumber (instructionDestinationRegister i)) (registerNumber (instructionSourceRegister i)) (registerNumber (instructionTargetRegister i)))
  DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION -> ok (printf " $%s, $%s, $%s" (registerNumber (instructionDestinationRegister i)) (registerNumber (instructionTargetRegister i)) (registerNumber (instructionSourceRegister i)))
  FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION -> ok (printf " $f%d, %d($%s)" (instructionFloatingPointTarget i) (instructionImmediate i) (registerNumber (instructionBase i)))
  FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION -> ok (printf " $f%d, $f%d" (instructionFloatingPointDestination i) (instructionFloatingPointSource i))
  FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION -> ok (printf " $%s, $f%d" (registerNumber (instructionTargetRegister i)) (instructionFloatingPointSource i))
  FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION
    | instructionFloatingPointDestination i == 0 -> ok (printf " $f%d, $f%d" (instructionFloatingPointSource i) (instructionFloatingPointTarget i))
    | otherwise -> ok (printf " %d, $f%d, $f%d" (instructionFloatingPointDestination i `div` 4) (instructionFloatingPointSource i) (instructionFloatingPointTarget i))
  FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION -> ok (printf " $f%d, $f%d, $f%d" (instructionFloatingPointDestination i) (instructionFloatingPointSource i) (instructionFloatingPointTarget i))
  MOVE_CONDITIONAL_TYPE_INSTRUCTION -> ok (printf " $%s, $%s, %d" (registerNumber (instructionDestinationRegister i)) (registerNumber (instructionSourceRegister i)) (instructionTargetRegister i `div` 4))
  FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION
    | instructionOpcode i `elem` map token [TOKEN_MOVN_S_OPCODE, TOKEN_MOVN_D_OPCODE, TOKEN_MOVZ_S_OPCODE, TOKEN_MOVZ_D_OPCODE] ->
        ok (printf " $f%d, $f%d, $%s" (instructionFloatingPointDestination i) (instructionFloatingPointSource i) (registerNumber (instructionTargetRegister i)))
    | otherwise -> ok (printf " $f%d, $f%d, %d" (instructionFloatingPointDestination i) (instructionFloatingPointSource i) (instructionTargetRegister i `div` 4))
  J_TYPE_INSTRUCTION -> ok (printf " 0x%08x" (instructionTarget i * 4))
  NO_ARGUMENT_TYPE_INSTRUCTION -> pure Nothing
  _ -> fatalError "Unknown instruction type in print_inst\n"
  where
    ok :: (Applicative f) => b -> f (Maybe (String, b))
    ok text = pure (Just ("", text))

-- | A symbolic immediate, as the disassembler shows it in brackets
-- (C: @format_imm_expr@).  base is the base register of a load or store,
-- or -1.
formatImmediateExpression :: ImmediateExpression -> Int -> String
formatImmediateExpression e base =
  maybe "" labelName (immediateExpressionSymbol e) ++ offsetText ++ baseText
  where
    offset :: Int32
    offset = immediateExpressionOffset e
    negOffset :: Int32
    negOffset = negate offset
    offsetText :: String
    offsetText
      | immediateExpressionProgramCounterRelative e = printf "-0x%08x" (fromIntegral negOffset :: Word32)
      | offset < -10 = printf "-%d (-0x%08x)" negOffset (fromIntegral negOffset :: Word32)
      | offset > 10 = printf "+%d (0x%08x)" offset (fromIntegral offset :: Word32)
      | otherwise = ""
    baseText :: String
    baseText
      | base /= -1 && immediateExpressionSymbol e /= Nothing && (offset > 10 || offset < -10) =
          (if offset /= 0 || base /= 0 then "($" ++ registerNumber base ++ ")" else "")
      | otherwise = ""

-- | The disassembly of the instruction at an address (C: @inst_to_string@).
instructionToString :: Spim -> Word32 -> IO String
instructionToString s address = do
  -- A side-effect-free read: showing an instruction must not raise, or
  -- clear, the exception the program's own step raised.
  peeked :: Maybe (Maybe InstructionRef) <- memoryPeekInstruction s address
  case peeked of
    Nothing -> do
      cError (printf "Can't print instruction not in text segment (0x%08x)\n" address)
      pure ""
    Just instruction -> formatAnInstruction s instruction address

-- | Print the instruction at an address on stderr (C: @print_inst@).
printInstruction :: Spim -> Word32 -> IO ()
printInstruction s address = instructionToString s address >>= writeError

-- | Print the word at an address (C: @print_mem@).
printMemory :: Spim -> Word32 -> IO ()
printMemory s address0 = do
  let address :: Word32
      address = address0 - (address0 `rem` 4)
  ttop :: Word32 <- get (textTop s)
  dtop :: Word32 <- get (dataTop s)
  sbot :: Word32 <- get (stackBottom s)
  kttop :: Word32 <- get (kernelTextTop s)
  kdtop :: Word32 <- get (kernelDataTop s)
  let showWord :: String -> IO ()
      showWord segment = do
        v :: Int32 <- memoryReadWord s address
        writeError (printf "%s @ 0x%08x (%d) = 0x%08x (%d)\n" (segment :: String) address (asInteger address) (fromIntegral v :: Word32) v)
  if textBottom <= address && address < ttop
    then printInstruction s address
    else
      if dataBottom <= address && address < dtop
        then showWord "Data seg"
        else
          if sbot <= address && address < stackTop
            then showWord "Stack seg"
            else
              if kernelTextBottom <= address && address < kttop
                then printInstruction s address
                else
                  if kernelDataBottom <= address && address < kdtop
                    then showWord "Kernel Data seg"
                    else cError (printf "Address 0x%08x (%d) to print_mem is out of bounds\n" address (asInteger address))
  where
    asInteger :: Word32 -> Int32
    asInteger = fromIntegral
