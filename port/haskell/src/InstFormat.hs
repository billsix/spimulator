-- | Disassembling instructions, and breakpoints (C: @format_an_inst@ and
-- friends in src/instruction.c, the breakpoint list in src/spim-utils.c,
-- and @print_mem@ in src/memory.c).
--
-- A breakpoint replaces the instruction in memory with one shared @break@
-- instruction and keeps the original aside; the disassembler shows the
-- original, marked with @*@.
module InstFormat
  ( formatAnInst
  , instToString
  , printInst
  , formatImmExpr
  , instIsBreakpoint
  , addBreakpoint
  , deleteBreakpoint
  , deleteAllBreakpoints
  , listBreakpoints
  , printMem
  , breakInstruction
  ) where

import Control.Monad (forM_, when)
import Data.Int (Int32)
import Data.IORef (newIORef)
import Data.Word (Word32)
import Instruction (opcodeIsLoadStore, tok)
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
breakInstruction :: Spim -> IO InstRef
breakInstruction s = do
  existing :: Maybe InstRef <- get (breakInst s)
  case existing of
    Just b -> pure b
    Nothing -> do
      b :: InstRef <- newIORef (setRd 1 (emptyInstruction (tok TOK_BREAK_OPCODE)))
      set (breakInst s) (Just b)
      pure b

-- | Is there a breakpoint at this address?  (Reading a bad address here
-- raises an exception, as in C.)
instIsBreakpoint :: Spim -> Word32 -> IO Bool
instIsBreakpoint s addr = do
  b :: InstRef <- breakInstruction s
  inst :: Maybe InstRef <- memReadInst s addr
  pure (inst == Just b)

-- | Put the breakpoint instruction at an address; give back what was there
-- (C: @set_breakpoint@), or Nothing if it cannot be done.
setBreakpoint :: Spim -> Word32 -> IO (Maybe InstRef)
setBreakpoint s addr = do
  b :: InstRef <- breakInstruction s
  set (exceptionOccurred s) False
  old :: Maybe InstRef <- memReadInst s addr
  if old == Just b
    then pure Nothing
    else do
      memWriteInst s addr (Just b)
      exc :: Bool <- get (exceptionOccurred s)
      pure (if exc then Nothing else old)

-- | C: @add_breakpoint@.
addBreakpoint :: Spim -> Word32 -> IO ()
addBreakpoint s addr = do
  old :: Maybe InstRef <- setBreakpoint s addr
  case old of
    Just inst -> modify (breakpoints s) ((addr, inst) :)
    Nothing -> do
      exc :: Bool <- get (exceptionOccurred s)
      if exc
        then cError (printf "Cannot put a breakpoint at address 0x%08x\n" addr)
        else cError (printf "No instruction to breakpoint at address 0x%08x\n" addr)

-- | Remove every breakpoint at an address, putting the original
-- instructions back (C: @delete_breakpoint@).
deleteBreakpoint :: Spim -> Word32 -> IO ()
deleteBreakpoint s addr = do
  bs :: [(Word32, InstRef)] <- get (breakpoints s)
  let here :: [(Word32, InstRef)]
      others :: [(Word32, InstRef)]
      (here, others) = foldr split ([], []) bs
      split :: (Word32, b) -> ([(Word32, b)], [(Word32, b)]) -> ([(Word32, b)], [(Word32, b)])
      split b@(a, _) (h, o) = if a == addr then (b : h, o) else (h, b : o)
  forM_ here $ \(_, inst) -> memWriteInst s addr (Just inst)
  set (breakpoints s) others
  when (null here) $ cError (printf "No breakpoint to delete at 0x%08x\n" addr)

deleteAllBreakpoints :: Spim -> IO ()
deleteAllBreakpoints s = set (breakpoints s) []

-- | C: @list_breakpoints@.
listBreakpoints :: Spim -> IO ()
listBreakpoints s = do
  bs :: [(Word32, InstRef)] <- get (breakpoints s)
  if null bs
    then writeErr "No breakpoints set\n"
    else forM_ bs $ \(a, _) -> writeErr (printf "Breakpoint at 0x%08x\n" a)

reg :: Int -> String
reg n = regNames !! (n `mod` 32)

-- | One line of disassembly (C: @format_an_inst@): address, encoding,
-- mnemonic and operands, the symbolic operand in brackets, and the source
-- line it came from, starting in column 57.
formatAnInst :: Spim -> Maybe InstRef -> Word32 -> IO String
formatAnInst s minst addr = do
  bp :: Bool <- if addr /= 0 then instIsBreakpoint s addr else pure False
  if bp
    then do
      deleteBreakpoint s addr
      inner :: Maybe InstRef <- memReadInst s addr
      line :: String <- formatAnInst s inner addr
      addBreakpoint s addr
      pure ('*' : line)
    else case minst of
      Nothing -> pure (printf "[0x%08x]\t<none>\n" addr)
      Just ref -> do
        i :: Instruction <- get ref
        let start :: String
            start = printf "[0x%08x]\t" addr
        case lookupByValue nameTable (inOpcode i) of
          Nothing -> pure (start ++ printf "<unknown instruction %d>\n" (inOpcode i))
          Just entry -> do
            operands :: Maybe (String, String) <- formatOperands i (nvvValue2 entry)
            let named :: String
                named = start ++ printf "0x%08x  %s" (fromIntegral (inEncoding i) :: Word32) (nvvName entry)
                body :: String
                body = case operands of
                  Nothing -> named
                  Just ("nop-erase", _) -> take (length named - 3) named ++ "nop"
                  Just (_, ops) -> named ++ ops
                symbolic :: String
                symbolic = case inExpr i of
                  Just e | Just _ <- ieSymbol e ->
                    " [" ++ formatImmExpr e (if opcodeIsLoadStore (inOpcode i) then instBase i else -1) ++ "]"
                  _ -> ""
                text :: String
                text = body ++ symbolic
                withSource :: String
                withSource = case inSource i of
                  Nothing -> text
                  Just src -> text ++ replicate (57 - length text) ' ' ++ "; " ++ src
            pure (withSource ++ "\n")

-- | The operand text for each operand shape.  Nothing means "no operands".
formatOperands :: Instruction -> OpType -> IO (Maybe (String, String))
formatOperands i ty = case ty of
  BRANCH_COPROC_COND_INST -> ok (printf "%d %d" (instRt i `div` 4) (branchOffset i))
  BRANCH_RS_LABEL_INST -> ok (printf " $%s %d" (reg (instRs i)) (branchOffset i))
  RS_IMM_INST -> ok (printf " $%s, %d" (reg (instRs i)) (instImm i))
  RT_IMM_INST -> ok (printf " $%s, %d" (reg (instRt i)) (instImm i))
  RT_RS_IMM_INST -> ok (printf " $%s, $%s, %d" (reg (instRt i)) (reg (instRs i)) (instImm i))
  BRANCH_RS_RT_LABEL_INST -> ok (printf " $%s, $%s, %d" (reg (instRs i)) (reg (instRt i)) (branchOffset i))
  RT_ADDR_INST -> ok (printf " $%s, %d($%s)" (reg (instRt i)) (instImm i) (reg (instBase i)))
  RS_INST -> ok (" $" ++ reg (instRs i))
  RD_INST -> ok (" $" ++ reg (instRd i))
  RT_COPREG_INST -> ok (printf " $%s, $%s" (reg (instRt i)) (reg (instRd i)))
  RS_RT_INST -> ok (printf " $%s, $%s" (reg (instRs i)) (reg (instRt i)))
  RD_RS_INST -> ok (printf " $%s, $%s" (reg (instRd i)) (reg (instRs i)))
  RD_RT_SHAMT_INST
    -- nop is sll $0, $0, 0: the C engine erases the last three letters
    -- of the mnemonic and writes "nop" in their place.
    | instRd i == 0 && instRt i == 0 && instShamt i == 0 -> pure (Just ("nop-erase", ""))
    | otherwise -> ok (printf " $%s, $%s, %d" (reg (instRd i)) (reg (instRt i)) (instShamt i))
  RD_RS_RT_INST -> ok (printf " $%s, $%s, $%s" (reg (instRd i)) (reg (instRs i)) (reg (instRt i)))
  RD_RT_RS_INST -> ok (printf " $%s, $%s, $%s" (reg (instRd i)) (reg (instRt i)) (reg (instRs i)))
  FP_FT_ADDR_INST -> ok (printf " $f%d, %d($%s)" (instFt i) (instImm i) (reg (instBase i)))
  FP_FD_FS_INST -> ok (printf " $f%d, $f%d" (instFd i) (instFs i))
  FP_RT_COPREG_INST -> ok (printf " $%s, $f%d" (reg (instRt i)) (instFs i))
  FP_CMP_FS_FT_INST
    | instFd i == 0 -> ok (printf " $f%d, $f%d" (instFs i) (instFt i))
    | otherwise -> ok (printf " %d, $f%d, $f%d" (instFd i `div` 4) (instFs i) (instFt i))
  FP_FD_FS_FT_INST -> ok (printf " $f%d, $f%d, $f%d" (instFd i) (instFs i) (instFt i))
  MOVC_TYPE_INST -> ok (printf " $%s, $%s, %d" (reg (instRd i)) (reg (instRs i)) (instRt i `div` 4))
  FP_MOVC_TYPE_INST
    | inOpcode i `elem` map tok [TOK_MOVN_S_OPCODE, TOK_MOVN_D_OPCODE, TOK_MOVZ_S_OPCODE, TOK_MOVZ_D_OPCODE] ->
        ok (printf " $f%d, $f%d, $%s" (instFd i) (instFs i) (reg (instRt i)))
    | otherwise -> ok (printf " $f%d, $f%d, %d" (instFd i) (instFs i) (instRt i `div` 4))
  J_TYPE_INST -> ok (printf " 0x%08x" (instTarget i * 4))
  NOARG_TYPE_INST -> pure Nothing
  _ -> fatalError "Unknown instruction type in print_inst\n"
  where
    ok :: Applicative f => b -> f (Maybe (String, b))
    ok text = pure (Just ("", text))

-- | A symbolic immediate, as the disassembler shows it in brackets
-- (C: @format_imm_expr@).  base is the base register of a load or store,
-- or -1.
formatImmExpr :: ImmExpr -> Int -> String
formatImmExpr e base =
  maybe "" lblName (ieSymbol e) ++ offsetText ++ baseText
  where
    off :: Int32
    off = ieOffset e
    negOff :: Int32
    negOff = negate off
    offsetText :: String
    offsetText
      | iePcRelative e = printf "-0x%08x" (fromIntegral negOff :: Word32)
      | off < -10 = printf "-%d (-0x%08x)" negOff (fromIntegral negOff :: Word32)
      | off > 10 = printf "+%d (0x%08x)" off (fromIntegral off :: Word32)
      | otherwise = ""
    baseText :: String
    baseText
      | base /= -1 && ieSymbol e /= Nothing && (off > 10 || off < -10) =
          (if off /= 0 || base /= 0 then "($" ++ reg base ++ ")" else "")
      | otherwise = ""

-- | The disassembly of the instruction at an address (C: @inst_to_string@).
instToString :: Spim -> Word32 -> IO String
instToString s addr = do
  set (exceptionOccurred s) False
  inst :: Maybe InstRef <- memReadInst s addr
  exc :: Bool <- get (exceptionOccurred s)
  if exc
    then do
      cError (printf "Can't print instruction not in text segment (0x%08x)\n" addr)
      pure ""
    else formatAnInst s inst addr

-- | Print the instruction at an address on stderr (C: @print_inst@).
printInst :: Spim -> Word32 -> IO ()
printInst s addr = instToString s addr >>= writeErr

-- | Print the word at an address (C: @print_mem@).
printMem :: Spim -> Word32 -> IO ()
printMem s addr0 = do
  let addr :: Word32
      addr = addr0 - (addr0 `rem` 4)
  ttop :: Word32 <- get (textTop s)
  dtop :: Word32 <- get (dataTop s)
  sbot :: Word32 <- get (stackBot s)
  kttop :: Word32 <- get (kTextTop s)
  kdtop :: Word32 <- get (kDataTop s)
  let showWord :: String -> IO ()
      showWord seg = do
        v :: Int32 <- memReadWord s addr
        writeErr (printf "%s @ 0x%08x (%d) = 0x%08x (%d)\n" (seg :: String) addr (asInt addr) (fromIntegral v :: Word32) v)
  if textBot <= addr && addr < ttop
    then printInst s addr
    else
      if dataBot <= addr && addr < dtop
        then showWord "Data seg"
        else
          if sbot <= addr && addr < stackTop
            then showWord "Stack seg"
            else
              if kTextBot <= addr && addr < kttop
                then printInst s addr
                else
                  if kDataBot <= addr && addr < kdtop
                    then showWord "Kernel Data seg"
                    else cError (printf "Address 0x%08x (%d) to print_mem is out of bounds\n" addr (asInt addr))
  where
    asInt :: Word32 -> Int32
    asInt = fromIntegral
