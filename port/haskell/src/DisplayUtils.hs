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
import Data.List (intercalate, transpose)
import Data.Word (Word32)
import GHC.Float (float2Double)
import InstFormat (formatAnInst)
import Memory
import Output (writeErr)
import Registers
import State
import Text.Printf (printf)
import Types (InstRef)

hex :: Int32 -> Word32
hex = fromIntegral

-- | The register display of @print_all_regs@ (C: @format_registers@); hex
-- shows every register in hexadecimal.
formatRegisters :: Spim -> Bool -> IO String
formatRegisters s hexFlag = do
  pc :: Word32 <- get (regPC s)
  epc :: Int32 <- getCp0 s cp0Epc
  cause :: Int32 <- getCp0 s cp0Cause
  bad :: Int32 <- getCp0 s cp0BadVAddr
  status :: Int32 <- getCp0 s cp0Status
  hi :: Int32 <- get (regHI s)
  lo :: Int32 <- get (regLO s)
  regs :: [Int32] <- mapM (getGpr s) [0 .. 31]
  fir :: Int32 <- getFcr s 0
  fcsr :: Int32 <- getFcr s 31
  doubles :: [Double] <- mapM (\k -> doubleWords s (2 * k)) [0 .. 15]
  wordPairs :: [(Word32, Word32)] <- mapM (\k -> (,) <$> fprWord s (2 * k) <*> fprWord s (2 * k + 1)) [0 .. 15]
  words' :: [Word32] <- mapM (fprWord s) [0 .. 31]
  singles :: [Float] <- mapM (fprSingle s) [0 .. 31]
  let gr :: Int -> String -> Int32 -> String
      gr n name v =
        if hexFlag
          then printf "R%-2d (%2s) = %08x" n name (hex v)
          else printf "R%-2d (%2s) = %-10d" n name v
      grFill :: String
      grFill = if hexFlag then "  " else " "
      fpVal :: Double -> String
      fpVal d = leftIn 13 (formatGAlt True 6 d)
      doubleCell :: Int -> Double -> (Word32, Word32) -> String
      doubleCell k d (w0, w1) =
        if hexFlag
          then printf "FP%-2d=%08x,%08x" (2 * k) w0 w1
          else printf "FP%-2d = %s" (2 * k) (fpVal d)
      singleCell :: Int -> Word32 -> Float -> String
      singleCell k v f =
        if hexFlag
          then printf "FP%-2d=%08x" k v
          else printf "FP%-2d = %s" k (fpVal (float2Double f))
      -- The cells in rows of four, laid out down the columns: with n rows,
      -- row k holds cells k, k + n, k + 2n and k + 3n.
      table :: String -> Int -> [String] -> String
      table fill n cells = concatMap (\row -> intercalate fill row ++ "\n") (transpose (chunksOf n cells))
  pure $
    printf " PC      = %08x   " pc
      ++ printf "EPC     = %08x  " (hex epc)
      ++ printf " Cause   = %08x  " (hex cause)
      ++ printf " BadVAddr= %08x\n" (hex bad)
      ++ printf " Status  = %08x   " (hex status)
      ++ printf "HI      = %08x  " (hex hi)
      ++ printf " LO      = %08x\n" (hex lo)
      ++ "\t\t\t\t General Registers\n"
      ++ table grFill 8 (zipWith3 gr [0 ..] regNames regs)
      ++ printf "\n FIR    = %08x   " (hex fir)
      ++ printf " FCSR    = %08x   " (hex fcsr)
      ++ "\t\t\t      Double Floating Point Registers\n"
      ++ table " " 4 (zipWith3 doubleCell [0 ..] doubles wordPairs)
      ++ "\t\t\t      Single Floating Point Registers\n"
      ++ table " " 8 (zipWith3 singleCell [0 ..] words' singles)

-- | Disassemble the instructions in [from, to) (C: @format_insts@).
formatInsts :: Spim -> Word32 -> Word32 -> IO String
formatInsts s from to = concat <$> forM (takeWhile (< to) [from, from + 4 ..]) line
  where
    line :: Word32 -> IO String
    line a = do
      inst :: Maybe InstRef <- memReadInst s a
      case inst of
        Nothing -> pure ""
        Just _ -> formatAnInst s inst a

-- | Dump the words in [from, to), four to a line, collapsing runs of four
-- or more zero words (C: @format_mem@).
formatMem :: Spim -> Word32 -> Word32 -> IO String
formatMem s from to = do
  let start :: Word32
      start = (from + 3) .&. complement 3
  (first, i0) :: (String, Word32) <- partialLine s start
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
          v :: Int32 <- memReadWord s (i + fromIntegral j * 4)
          if v /= 0 then pure j else zeroRun i (j + 1)
    -- do { print a word } while (not at a 16-byte boundary)
    wordsToLineEnd :: Word32 -> Bool -> IO (String, Word32)
    wordsToLineEnd i firstOne
      | not firstOne && i `rem` 16 == 0 = pure ("", i)
      | otherwise = do
          v :: Int32 <- memReadWord s i
          (more, i') :: (String, Word32) <- wordsToLineEnd (i + 4) False
          pure (printf "  0x%08x" (hex v) ++ more, i')

-- | The words up to the next 16-byte boundary, if not on one
-- (C: @format_partial_line@).
partialLine :: Spim -> Word32 -> IO (String, Word32)
partialLine s addr
  | addr `rem` 16 == 0 = pure ("", addr)
  | otherwise = do
      -- The words that start before the next boundary.  For a word-aligned
      -- address the last one ends on it; an unaligned one (the C engine
      -- never stops on it) ends past it.
      let count :: Word32
          count = (16 - addr `rem` 16 + 3) `quot` 4
          addrs :: [Word32]
          addrs = [addr + 4 * k | k <- [0 .. count - 1]]
      cells :: [String] <- mapM (\a -> printf "  0x%08x" . hex <$> memReadWord s a) addrs
      pure (printf "[0x%08x]\t\t      " addr ++ concat cells ++ "\n", addr + 4 * count)

-- | A list cut into pieces of n elements (the last may be shorter).
chunksOf :: forall x. Int -> [x] -> [[x]]
chunksOf _ [] = []
chunksOf n xs = piece : chunksOf n rest
  where
    piece :: [x]
    rest :: [x]
    (piece, rest) = splitAt n xs

-- | The data, stack and kernel data segments (C: @format_data_segs@).
formatDataSegs :: Spim -> IO String
formatDataSegs s = do
  dtop :: Word32 <- get (dataTop s)
  sp :: Word32 <- fromIntegral <$> getGpr s 29 :: IO Word32
  kdtop :: Word32 <- get (kDataTop s)
  d :: String <- formatMem s dataBot dtop
  st :: String <- formatMem s (sp .&. complement 3) stackTop
  k :: String <- formatMem s kDataBot kdtop
  pure ("\tDATA\n" ++ d ++ "\n\tSTACK\n" ++ st ++ "\n\tKERNEL DATA\n" ++ k)

-- | The REPL's @disasm@ (C: @print_text_listing@).
printTextListing :: Spim -> IO ()
printTextListing s = do
  ttop :: Word32 <- get (textTop s)
  text :: String <- formatInsts s textBot ttop
  if null text
    then writeErr "No program loaded.\n"
    else writeErr ("\n\tUSER TEXT SEGMENT\n" ++ text ++ "\n")
