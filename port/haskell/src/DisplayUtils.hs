-- | Text views of the machine: the register display, memory dumps and the
-- disassembly listing (C: src/display-utils.c).
module DisplayUtils
  ( formatRegisters
  , formatInsts
  , formatMem
  , formatDataSegs
  , printTextListing
  ) where

import CFormat (formatGAlt, leftIn)
import Control.Monad (forM)
import Data.Bits (complement, (.&.))
import Data.Int (Int32)
import Data.Word (Word32)
import GHC.Float (float2Double)
import InstFormat (formatAnInst)
import Memory
import Output (writeErr)
import Registers
import State
import Text.Printf (printf)

hex :: Int32 -> Word32
hex = fromIntegral

-- | The register display of @print_all_regs@ (C: @format_registers@); hex
-- shows every register in hexadecimal.
formatRegisters :: Spim -> Bool -> IO String
formatRegisters s hexFlag = do
  pc <- get (regPC s)
  epc <- getCp0 s cp0Epc
  cause <- getCp0 s cp0Cause
  bad <- getCp0 s cp0BadVAddr
  status <- getCp0 s cp0Status
  hi <- get (regHI s)
  lo <- get (regLO s)
  regs <- mapM (getGpr s) [0 .. 31]
  fir <- getFcr s 0
  fcsr <- getFcr s 31
  doubles <- mapM (\k -> doubleWords s (2 * k)) [0 .. 15]
  words' <- mapM (fprWord s) [0 .. 31]
  singles <- mapM (fprSingle s) [0 .. 31]
  let gr n =
        if hexFlag
          then printf "R%-2d (%2s) = %08x" n (regNames !! n) (hex (regs !! n))
          else printf "R%-2d (%2s) = %-10d" n (regNames !! n) (regs !! n)
      grFill = if hexFlag then "  " else " "
      generalRow k = concatMap (\c -> gr (k + 8 * c) ++ (if c < 3 then grFill else "\n")) [0 .. 3 :: Int]
      fpVal :: Double -> String
      fpVal d = leftIn 13 (formatGAlt True 6 d)
      doubleCell k =
        if hexFlag
          then printf "FP%-2d=%08x,%08x" (2 * k) (words' !! (2 * k)) (words' !! (2 * k + 1))
          else printf "FP%-2d = %s" (2 * k) (fpVal (doubles !! k))
      doubleRow k = concatMap (\c -> doubleCell (k + 4 * c) ++ (if c < 3 then " " else "\n")) [0 .. 3 :: Int]
      singleCell k =
        if hexFlag
          then printf "FP%-2d=%08x" k (words' !! k)
          else printf "FP%-2d = %s" k (fpVal (float2Double (singles !! k)))
      singleRow k = concatMap (\c -> singleCell (k + 8 * c) ++ (if c < 3 then " " else "\n")) [0 .. 3 :: Int]
  pure $
    printf " PC      = %08x   " pc
      ++ printf "EPC     = %08x  " (hex epc)
      ++ printf " Cause   = %08x  " (hex cause)
      ++ printf " BadVAddr= %08x\n" (hex bad)
      ++ printf " Status  = %08x   " (hex status)
      ++ printf "HI      = %08x  " (hex hi)
      ++ printf " LO      = %08x\n" (hex lo)
      ++ "\t\t\t\t General Registers\n"
      ++ concatMap generalRow [0 .. 7]
      ++ printf "\n FIR    = %08x   " (hex fir)
      ++ printf " FCSR    = %08x   " (hex fcsr)
      ++ "\t\t\t      Double Floating Point Registers\n"
      ++ concatMap doubleRow [0 .. 3]
      ++ "\t\t\t      Single Floating Point Registers\n"
      ++ concatMap singleRow [0 .. 7]

-- | Disassemble the instructions in [from, to) (C: @format_insts@).
formatInsts :: Spim -> Word32 -> Word32 -> IO String
formatInsts s from to = concat <$> forM (takeWhile (< to) [from, from + 4 ..]) line
  where
    line a = do
      inst <- memReadInst s a
      case inst of
        Nothing -> pure ""
        Just _ -> formatAnInst s inst a

-- | Dump the words in [from, to), four to a line, collapsing runs of four
-- or more zero words (C: @format_mem@).
formatMem :: Spim -> Word32 -> Word32 -> IO String
formatMem s from to = do
  let start = (from + 3) .&. complement 3
  (first, i0) <- partialLine s start
  rest <- loop i0
  pure (first ++ rest)
  where
    loop i
      | i >= to = pure ""
      | otherwise = do
          j <- zeroRun i 0
          if j >= 4
            then do
              let next = i + fromIntegral j * 4
                  text = printf "[0x%08x]...[0x%08x]\t0x00000000\n" i next
              (partial, i') <- partialLine s next
              more <- loop i'
              pure (text ++ partial ++ more)
            else do
              (cells, i') <- wordsToLineEnd i True
              more <- loop i'
              pure (printf "[0x%08x]\t\t      " i ++ cells ++ "\n" ++ more)
    zeroRun :: Word32 -> Int -> IO Int
    zeroRun i j
      | i + fromIntegral j * 4 >= to = pure j
      | otherwise = do
          v <- memReadWord s (i + fromIntegral j * 4)
          if v /= 0 then pure j else zeroRun i (j + 1)
    -- do { print a word } while (not at a 16-byte boundary)
    wordsToLineEnd i firstOne
      | not firstOne && i `rem` 16 == 0 = pure ("", i)
      | otherwise = do
          v <- memReadWord s i
          (more, i') <- wordsToLineEnd (i + 4) False
          pure (printf "  0x%08x" (hex v) ++ more, i')

-- | The words up to the next 16-byte boundary, if not on one
-- (C: @format_partial_line@).
partialLine :: Spim -> Word32 -> IO (String, Word32)
partialLine s addr
  | addr `rem` 16 == 0 = pure ("", addr)
  | otherwise = do
      let addrs = takeWhile (\a -> a `rem` 16 /= 0) [addr, addr + 4 ..]
      cells <- mapM (\a -> printf "  0x%08x" . hex <$> memReadWord s a) addrs
      pure (printf "[0x%08x]\t\t      " addr ++ concat cells ++ "\n", last addrs + 4)

-- | The data, stack and kernel data segments (C: @format_data_segs@).
formatDataSegs :: Spim -> IO String
formatDataSegs s = do
  dtop <- get (dataTop s)
  sp <- fromIntegral <$> getGpr s 29 :: IO Word32
  kdtop <- get (kDataTop s)
  d <- formatMem s dataBot dtop
  st <- formatMem s (sp .&. complement 3) stackTop
  k <- formatMem s kDataBot kdtop
  pure ("\tDATA\n" ++ d ++ "\n\tSTACK\n" ++ st ++ "\n\tKERNEL DATA\n" ++ k)

-- | The REPL's @disasm@ (C: @print_text_listing@).
printTextListing :: Spim -> IO ()
printTextListing s = do
  ttop <- get (textTop s)
  text <- formatInsts s textBot ttop
  if null text
    then writeErr "No program loaded.\n"
    else writeErr ("\n\tUSER TEXT SEGMENT\n" ++ text ++ "\n")
