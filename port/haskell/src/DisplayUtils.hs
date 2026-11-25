-- | Text views of the machine: the register display, memory dumps and the
-- disassembly listing (C: src/display-utils.c).
module DisplayUtils
  ( formatRegisters,
    formatInstructions,
    formatMemory,
    formatDataSegments,
    printTextListing,
  )
where

import CFormat (formatGAlt, leftIn)
import Control.Monad (forM)
import Data.Bits (complement, (.&.))
import Data.Int (Int32)
import Data.List (intercalate, transpose)
import Data.Word (Word32)
import GHC.Float (float2Double)
import InstFormat (formatAnInstruction)
import Memory
import Output (writeError)
import Registers
import State
import Text.Printf (printf)
import Types (InstructionRef)

hex :: Int32 -> Word32
hex = fromIntegral

-- | The register display of @print_all_regs@ (C: @format_registers@); hex
-- shows every register in hexadecimal.
formatRegisters :: Spim -> Bool -> IO String
formatRegisters s hexFlag = do
  programCounter :: Word32 <- get (registerProgramCounter s)
  exceptionProgramCounter :: Int32 <- getCoprocessor0 s coprocessor0ExceptionProgramCounter
  cause :: Int32 <- getCoprocessor0 s coprocessor0Cause
  bad :: Int32 <- getCoprocessor0 s coprocessor0BadVirtualAddress
  status :: Int32 <- getCoprocessor0 s coprocessor0Status
  hi :: Int32 <- get (registerMultiplyDivideHigh s)
  lo :: Int32 <- get (registerMultiplyDivideLow s)
  registers :: [Int32] <- mapM (getGeneralPurposeRegister s) [0 .. 31]
  floatingPointImplementationRegister :: Int32 <- getFloatingPointControlRegister s 0
  floatingPointControlStatusRegister :: Int32 <- getFloatingPointControlRegister s 31
  doubles :: [Double] <- mapM (\k -> doubleWords s (2 * k)) [0 .. 15]
  wordPairs :: [(Word32, Word32)] <- mapM (\k -> (,) <$> floatingPointRegisterWord s (2 * k) <*> floatingPointRegisterWord s (2 * k + 1)) [0 .. 15]
  words' :: [Word32] <- mapM (floatingPointRegisterWord s) [0 .. 31]
  singles :: [Float] <- mapM (floatingPointRegisterSingle s) [0 .. 31]
  let gr :: Int -> String -> Int32 -> String
      gr n name v =
        if hexFlag
          then printf "R%-2d (%2s) = %08x" n name (hex v)
          else printf "R%-2d (%2s) = %-10d" n name v
      grFill :: String
      grFill = if hexFlag then "  " else " "
      floatingPointValue :: Double -> String
      floatingPointValue d = leftIn 13 (formatGAlt True 6 d)
      doubleCell :: Int -> Double -> (Word32, Word32) -> String
      doubleCell k d (w0, w1) =
        if hexFlag
          then printf "FP%-2d=%08x,%08x" (2 * k) w0 w1
          else printf "FP%-2d = %s" (2 * k) (floatingPointValue d)
      singleCell :: Int -> Word32 -> Float -> String
      singleCell k v f =
        if hexFlag
          then printf "FP%-2d=%08x" k v
          else printf "FP%-2d = %s" k (floatingPointValue (float2Double f))
      -- The cells in rows of four, laid out down the columns: with n rows,
      -- row k holds cells k, k + n, k + 2n and k + 3n.
      table :: String -> Int -> [String] -> String
      table fill n cells = concatMap (\row -> intercalate fill row ++ "\n") (transpose (chunksOf n cells))
  pure $
    printf " PC      = %08x   " programCounter
      ++ printf "EPC     = %08x  " (hex exceptionProgramCounter)
      ++ printf " Cause   = %08x  " (hex cause)
      ++ printf " BadVAddr= %08x\n" (hex bad)
      ++ printf " Status  = %08x   " (hex status)
      ++ printf "HI      = %08x  " (hex hi)
      ++ printf " LO      = %08x\n" (hex lo)
      ++ "\t\t\t\t General Registers\n"
      ++ table grFill 8 (zipWith3 gr [0 ..] registerNames registers)
      ++ printf "\n FIR    = %08x   " (hex floatingPointImplementationRegister)
      ++ printf " FCSR    = %08x   " (hex floatingPointControlStatusRegister)
      ++ "\t\t\t      Double Floating Point Registers\n"
      ++ table " " 4 (zipWith3 doubleCell [0 ..] doubles wordPairs)
      ++ "\t\t\t      Single Floating Point Registers\n"
      ++ table " " 8 (zipWith3 singleCell [0 ..] words' singles)

-- | Disassemble the instructions in [from, to) (C: @format_insts@).
formatInstructions :: Spim -> Word32 -> Word32 -> IO String
formatInstructions s from to = concat <$> forM (takeWhile (< to) [from, from + 4 ..]) line
  where
    line :: Word32 -> IO String
    line a = do
      instruction :: Maybe InstructionRef <- memoryReadInstruction s a
      case instruction of
        Nothing -> pure ""
        Just _ -> formatAnInstruction s instruction a

-- | Dump the words in [from, to), four to a line, collapsing runs of four
-- or more zero words (C: @format_mem@).
formatMemory :: Spim -> Word32 -> Word32 -> IO String
formatMemory s from to = do
  -- Start at `from` rounded up to a word boundary.
  (first, i0) :: (String, Word32) <- partialLine s ((from + 3) .&. complement 3)
  rest :: String <- loop i0
  pure (first ++ rest)
  where
    loop :: Word32 -> IO String
    loop i
      | i >= to = pure ""
      | otherwise = do
          j :: Int <- zeroRun i 0
          if j >= 4
            then do
              let next :: Word32
                  next = i + fromIntegral j * 4
                  text :: String
                  text = printf "[0x%08x]...[0x%08x]\t0x00000000\n" i next
              (partial, i') :: (String, Word32) <- partialLine s next
              more :: String <- loop i'
              pure (text ++ partial ++ more)
            else do
              (cells, i') :: (String, Word32) <- wordsToLineEnd i True
              more :: String <- loop i'
              pure (printf "[0x%08x]\t\t      " i ++ cells ++ "\n" ++ more)
    zeroRun :: Word32 -> Int -> IO Int
    zeroRun i j
      | i + fromIntegral j * 4 >= to = pure j
      | otherwise = do
          v :: Int32 <- memoryReadWord s (i + fromIntegral j * 4)
          if v /= 0 then pure j else zeroRun i (j + 1)
    -- do { print a word } while (not at a 16-byte boundary)
    wordsToLineEnd :: Word32 -> Bool -> IO (String, Word32)
    wordsToLineEnd i firstOne
      | not firstOne && i `rem` 16 == 0 = pure ("", i)
      | otherwise = do
          v :: Int32 <- memoryReadWord s i
          (more, i') :: (String, Word32) <- wordsToLineEnd (i + 4) False
          pure (printf "  0x%08x" (hex v) ++ more, i')

-- | The words up to the next 16-byte boundary, if not on one
-- (C: @format_partial_line@).
partialLine :: Spim -> Word32 -> IO (String, Word32)
partialLine s address
  | address `rem` 16 == 0 = pure ("", address)
  | otherwise = do
      -- The words that start before the next boundary.  For a word-aligned
      -- address the last one ends on it; an unaligned one (the C engine
      -- never stops on it) ends past it.
      let count :: Word32
          count = (16 - address `rem` 16 + 3) `quot` 4
          addresses :: [Word32]
          addresses = [address + 4 * k | k <- [0 .. count - 1]]
      cells :: [String] <- mapM (\a -> printf "  0x%08x" . hex <$> memoryReadWord s a) addresses
      pure (printf "[0x%08x]\t\t      " address ++ concat cells ++ "\n", address + 4 * count)

-- | A list cut into pieces of n elements (the last may be shorter).
chunksOf :: forall x. Int -> [x] -> [[x]]
chunksOf _ [] = []
chunksOf n xs = piece : chunksOf n rest
  where
    piece :: [x]
    rest :: [x]
    (piece, rest) = splitAt n xs

-- | The data, stack and kernel data segments (C: @format_data_segs@).
formatDataSegments :: Spim -> IO String
formatDataSegments s = do
  dtop :: Word32 <- get (dataTop s)
  sp :: Word32 <- fromIntegral <$> getGeneralPurposeRegister s 29 :: IO Word32
  kdtop :: Word32 <- get (kernelDataTop s)
  d :: String <- formatMemory s dataBottom dtop
  st :: String <- formatMemory s (sp .&. complement 3) stackTop
  k :: String <- formatMemory s kernelDataBottom kdtop
  pure ("\tDATA\n" ++ d ++ "\n\tSTACK\n" ++ st ++ "\n\tKERNEL DATA\n" ++ k)

-- | The REPL's @disasm@ (C: @print_text_listing@).
printTextListing :: Spim -> IO ()
printTextListing s = do
  ttop :: Word32 <- get (textTop s)
  text :: String <- formatInstructions s textBottom ttop
  if null text
    then writeError "No program loaded.\n"
    else writeError ("\n\tUSER TEXT SEGMENT\n" ++ text ++ "\n")
